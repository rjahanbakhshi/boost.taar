//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_SESSION_HTTP_HPP
#define BOOST_TAAR_SESSION_HTTP_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include "boost/taar/core/invoke_response.hpp"
#include <boost/taar/core/member_function_of.hpp>
#include <boost/taar/core/callable_traits.hpp>
#include <boost/taar/core/specialization_of.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/url/url_view.hpp>
#include <concepts>
#include <exception>
#include <vector>
#include <functional>
#include <utility>

namespace boost::taar::session {

class http
{
private:
    using matcher_type = std::function<
        bool(
            const boost::beast::http::request_header<>&,
            matcher::context&,
            const boost::urls::url_view&)>;

    using request_handler_wrapper_type = std::function<
        awaitable<bool>(
            const matcher::context&,
            rebind_executor<boost::beast::tcp_stream>&,
            boost::beast::flat_buffer&,
            boost::beast::http::request_parser<boost::beast::http::buffer_body>&,
            cancellation_signals&)>;

    using soft_error_handler_type = std::function<
        awaitable<bool>(
            std::exception_ptr,
            rebind_executor<boost::beast::tcp_stream>&,
            boost::beast::http::request_header<>&,
            cancellation_signals&)>;

    using hard_error_handler_type = std::function<void(std::exception_ptr)>;

    static void default_soft_error_handler(std::exception_ptr eptr)
    {
        std::rethrow_exception(eptr);
    }

public:
    http()
    {
        set_soft_error_handler(&default_soft_error_handler);
    }
    awaitable<void> operator()(
        rebind_executor<boost::asio::ip::tcp::socket> socket,
        cancellation_signals& signals) const
    {
        namespace net = boost::asio;
        namespace http = boost::beast::http;
        namespace this_coro = net::this_coro;
        using boost::beast::tcp_stream;
        using boost::beast::flat_buffer;

        rebind_executor<tcp_stream> stream {std::move(socket)};

        try
        {
            for (auto cs = co_await this_coro::cancellation_state;
                cs.cancelled() == net::cancellation_type::none;
                cs = co_await this_coro::cancellation_state)
            {
                // Read and parse the header and use the target to find the handler.
                flat_buffer buffer;
                http::request_parser<http::buffer_body> header_parser;

                auto [header_ec, header_sz] = co_await http::async_read_header(
                    stream,
                    buffer,
                    header_parser);

                if (header_ec)
                {
                    // Error reading the http header. Session will be closed.
                    break;
                }

                auto& req_header = header_parser.get();
                matcher::context context;
                boost::urls::url_view parsed_target;

                if (needs_parsed_target_)
                {
                    parsed_target = boost::urls::url_view(req_header.target());
                }

                auto iter = std::ranges::find_if(matcher_handlers_,
                    [&](auto const& matcher) -> bool
                    {
                        context.path_args.clear();
                        return matcher.first(req_header, context, parsed_target);
                    });

                if (iter != matcher_handlers_.cend())
                {
                    auto const& handler = iter->second;
                    if (!co_await handler(context, stream, buffer, header_parser, signals))
                    {
                        // Handler instructed to close the session.
                        break;
                    }

                    // Done with this request. Proceeding with the next one.
                    continue;
                }

                // No handler found for this request. Reply with not found error.
                // But before sending the error response, it is necessary to read
                // the full buffer.
                auto keep_alive = req_header.keep_alive();
                auto version = req_header.version();
                if (req_header.payload_size().has_value() &&
                    req_header.payload_size().value() > 0)
                {
                    http::request_parser<http::string_body> body_parser {std::move(header_parser)};
                    auto [req_ec, req_sz] = co_await async_read(stream, buffer, body_parser);
                    if (req_ec)
                    {
                        // Error reading the full request. The session will be closed.
                        break;
                    }
                    auto const& request = body_parser.get();
                }

                // Stock not-found response.
                http::response<http::empty_body> response {http::status::not_found, version};
                response.keep_alive(keep_alive);
                response.prepare_payload();
                auto [resp_ec, resp_sz] = co_await http::async_write(stream, response);
                if (resp_ec)
                {
                    // Error in writing the response. The session will be closed.
                    break;
                }

                if (!keep_alive)
                {
                    // Keep alive is not requested.
                    break;
                }
            }
        }
        catch (...)
        {
            // Unrecoverable error.
            hard_error_handler_(std::current_exception());
        }

        // Send a TCP shutdown
        boost::system::error_code ec;
        stream.socket().shutdown(net::ip::tcp::socket::shutdown_send, ec); // NOLINT
    }

    template <typename MatcherType, typename RequestHandler>
    // TODO: need a concept to make sure MatcherType is compatible
    // TODO: need a concept to make sure RequestHandler is compatible
    auto register_request_handler(
        MatcherType&& matcher,
        RequestHandler request_handler)
    {
        namespace http = boost::beast::http;

        matcher::operand operand {std::forward<MatcherType&&>(matcher)};
        needs_parsed_target_ |= decltype(operand)::with_parsed_target;

        matcher_handlers_.emplace_back(
            [this, operand = std::move(operand)](
                const http::request_header<>& request,
                matcher::context& context,
                const boost::urls::url_view& parsed_target)
            {
                return operand(request, context, parsed_target);
            },
            [this, request_handler = std::move(request_handler)](
                const matcher::context& context,
                rebind_executor<boost::beast::tcp_stream>& stream,
                boost::beast::flat_buffer& buffer,
                boost::beast::http::request_parser<boost::beast::http::buffer_body>& header_parser,
                cancellation_signals& signals) mutable
            -> awaitable<bool>
            {
                using request_type = std::remove_cvref_t<callable_arg_type<RequestHandler, 0>>;
                using body_type = request_type::body_type;

                http::request_parser<body_type> body_parser {std::move(header_parser)};
                auto [req_ec, req_sz] = co_await async_read(stream, buffer, body_parser);

                std::exception_ptr ex;
                try
                {
                    auto response = request_handler(
                        body_parser.get(),
                        context);

                    const bool keep_alive = response.keep_alive();
                    co_await boost::beast::async_write(stream, std::move(response));
                    co_return keep_alive;
                }
                catch (...)
                {
                    ex = std::current_exception();
                }

                // An exception is thrown within the request handler.
                co_return co_await soft_error_handler_(
                    ex,
                    stream,
                    body_parser.get(),
                    signals);
            }
        );
    }

    template <typename MatcherType, typename ObjectType, typename ResultType, typename... ArgsType>
    auto register_request_handler(
        MatcherType&& matcher,
        ResultType(ObjectType::*memfn)(ArgsType...),
        ObjectType* object)
    {
        return register_request_handler(
            std::forward<MatcherType>(matcher),
            [memfn, object](ArgsType&&... args)
            {
                return (object->*memfn)(std::forward<ArgsType>(args)...);
            }
        );
    }

    template <typename MatcherType, typename ObjectType, typename ResultType, typename... ArgsType>
    auto register_request_handler(
        MatcherType&& matcher,
        ResultType(ObjectType::*memfn)(ArgsType...) const,
        ObjectType const* object)
    {
        return register_request_handler(
            std::forward<MatcherType>(matcher),
            [memfn, object](ArgsType&&... args)
            {
                return (object->*memfn)(std::forward<ArgsType>(args)...);
            }
        );
    }

    template <typename Handler> requires
        // TODO: need a concept to make sure Handler is compatible
        std::constructible_from<callable_arg_type<Handler, 0>, std::exception_ptr>
    void set_soft_error_handler(Handler handler)
    {
        namespace http = boost::beast::http;

        soft_error_handler_ =
            [handler = std::move(handler)](
                std::exception_ptr ex,
                rebind_executor<boost::beast::tcp_stream>& stream,
                boost::beast::http::request_header<>& request_header,
                cancellation_signals& signals) mutable
            -> awaitable<bool>
            {
                auto rg = invoke_response(
                    request_header,
                    http::status::internal_server_error,
                    handler,
                    ex);

                const bool keep_alive = rg.keep_alive();
                co_await boost::beast::async_write(stream, std::move(rg));
                co_return keep_alive;
            };
    }

    void set_hard_error_handler(hard_error_handler_type handler)
    {
        hard_error_handler_ = std::move(handler);
    }

private:
    std::vector<std::pair<matcher_type, request_handler_wrapper_type>> matcher_handlers_;
    soft_error_handler_type soft_error_handler_;
    hard_error_handler_type hard_error_handler_ = [](std::exception_ptr){};
    bool needs_parsed_target_ = false;
};

} // namespace boost::taar::session

#endif // BOOST_TAAR_SESSION_HTTP_HPP
