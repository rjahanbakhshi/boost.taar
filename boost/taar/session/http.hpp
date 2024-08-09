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

#include <boost/asio/async_result.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/taar/core/member_function_of.hpp>
#include <boost/taar/core/callable_traits.hpp>
#include <boost/taar/core/specialization_of.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/error.hpp>
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
#include <exception>
#include <type_traits>
#include <vector>
#include <functional>
#include <utility>

namespace boost::taar::session {
namespace detail {

template <typename HandlerType>
concept soft_error_handler = requires(HandlerType handler)
{
    requires std::is_invocable_v<HandlerType, std::exception_ptr>;
    response_from_invoke(std::move(handler), std::declval<std::exception_ptr>());
};

template <typename T>
concept hard_error_handler = requires
{
    requires std::is_invocable_v<T, std::exception_ptr>;
};

template <typename StreamType>
auto async_write(
    StreamType& stream,
    ::boost::beast::http::message_generator&& generator)
{
    return ::boost::beast::async_write(
        stream,
        std::move(generator));
}

template <
    typename StreamType,
    bool isRequest,
    typename Body,
    typename Fields>
auto async_write(
    StreamType& stream,
    ::boost::beast::http::message<isRequest, Body, Fields>&& message)
{
    return ::boost::beast::http::async_write(
        stream,
        std::move(message));
}

} // detail

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

    using wrapped_soft_error_handler_type = std::function<
        awaitable<bool>(
            std::exception_ptr,
            rebind_executor<boost::beast::tcp_stream>&,
            boost::beast::http::request_header<>&,
            cancellation_signals&)>;

    using hard_error_handler_type = std::function<void(std::exception_ptr)>;

public:
    http()
        : wrapped_soft_error_handler_ {
            [](
                std::exception_ptr eptr,
                rebind_executor<boost::beast::tcp_stream>&,
                boost::beast::http::request_header<>&,
                cancellation_signals&) -> awaitable<bool>
            {
                std::rethrow_exception(eptr);
            }
        }
    {}

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
                    auto [req_ec, req_sz] = co_await async_read(
                        stream,
                        buffer,
                        body_parser);

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
                auto [resp_ec, resp_sz] = co_await http::async_write(
                    stream,
                    response);

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
    auto register_request_handler(
        MatcherType&&,
        RequestHandler)
    {
        static_assert(
            matcher::is_matcher<std::decay_t<MatcherType>>,
            "Incompatible matcher type");

        static_assert(
            std::is_copy_constructible_v<std::decay_t<RequestHandler>>,
            "Http handler target must be copy-constructible");
    }

    template <typename MatcherType, typename RequestHandler> requires(
        matcher::is_matcher<std::decay_t<MatcherType>> &&
        std::is_copy_constructible_v<std::decay_t<RequestHandler>>)
    auto register_request_handler(
        MatcherType&& matcher,
        RequestHandler request_handler)
    {
        namespace http = boost::beast::http;

        matcher::operand operand {std::forward<MatcherType>(matcher)};
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
                std::string error_msg;
                try
                {
                    auto response = co_await response_from_invoke(
                        std::move(request_handler),
                        body_parser.get(),
                        context);

                    bool keep_alive = response.keep_alive();
                    co_await detail::async_write(stream, std::move(response));
                    co_return keep_alive;
                }
                catch(const boost::system::system_error& e)
                {
                    if (e.code().category() == error_category())
                    {
                        error_msg = e.what();
                    }
                    ex = std::current_exception();
                }
                catch (...)
                {
                    ex = std::current_exception();
                }

                if (!error_msg.empty())
                {
                    auto response = response_from(error_msg);
                    response.result(boost::beast::http::status::bad_request);
                    bool keep_alive = response.keep_alive();
                    co_await detail::async_write(stream, std::move(response));
                    co_return keep_alive;
                }

                // An exception is thrown within the request handler.
                co_return co_await wrapped_soft_error_handler_(
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

    template <detail::soft_error_handler HandlerType>
    void set_soft_error_handler(HandlerType handler)
    {
        wrapped_soft_error_handler_ =
            [handler = std::move(handler)](
                std::exception_ptr eptr,
                rebind_executor<boost::beast::tcp_stream>& stream,
                boost::beast::http::request_header<>& request_header,
                cancellation_signals& signals) mutable
            -> awaitable<bool>
            {
                auto response = co_await response_from_invoke(std::move(handler), eptr);
                bool keep_alive = response.keep_alive();
                co_await detail::async_write(stream, std::move(response));
                co_return keep_alive;
            };
    }

    template <detail::hard_error_handler HandlerType>
    void set_hard_error_handler(HandlerType handler)
    {
        hard_error_handler_ = std::move(handler);
    }

private:
    std::vector<std::pair<matcher_type, request_handler_wrapper_type>> matcher_handlers_;
    wrapped_soft_error_handler_type wrapped_soft_error_handler_;
    hard_error_handler_type hard_error_handler_ = [](std::exception_ptr){};
    bool needs_parsed_target_ = false;
};

} // namespace boost::taar::session

#endif // BOOST_TAAR_SESSION_HTTP_HPP
