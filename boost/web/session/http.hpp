//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_SESSION_HTTP_HPP
#define BOOST_WEB_SESSION_HTTP_HPP

#include "boost/web/core/callable_traits.hpp"
#include <boost/web/matcher/context.hpp>
#include <boost/web/matcher/operand.hpp>
#include <boost/web/core/cancellation_signals.hpp>
#include <boost/web/core/awaitable.hpp>
#include <boost/web/core/rebind_executor.hpp>
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
#include <vector>
#include <functional>
#include <utility>

namespace boost::web::session {

class http
{
public:
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

        for (auto cs = co_await this_coro::cancellation_state;
             cs.cancelled() == net::cancellation_type::none;
             cs = co_await this_coro::cancellation_state)
        {
            try
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

                auto& header = header_parser.get();
                matcher::context context;
                boost::urls::url_view parsed_target;

                if (needs_parsed_target_)
                {
                    parsed_target = boost::urls::url_view(header.target());
                    // TODO: check parsed result.
                }

                auto iter = std::ranges::find_if(matchers_,
                    [&](auto const& matcher) -> bool
                    {
                        context.path_args.clear();
                        return matcher.first(header, context, parsed_target);
                    });

                if (iter != matchers_.cend())
                {
                    auto const& handler = iter->second;
                    if (!co_await handler(context, stream, buffer, header_parser, signals))
                    {
                        // Handler instructed to close the session.
                        break;
                    }
                }

                // No handler found for this request. Reply with not found error.
                // But before sending the error response, it is necessary to read
                // the full buffer.
                auto keep_alive = header.keep_alive();
                auto version = header.version();
                if (header.payload_size().has_value() && header.payload_size().value() > 0)
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
            catch (...)
            {
                // Session will be closed.
                break;
            }
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

        matchers_.emplace_back(
            [operand = std::move(operand)](
                const http::request_header<>& request,
                matcher::context& context,
                const boost::urls::url_view& parsed_target)
            {
                return operand(request, context, parsed_target);
            },
            [request_handler = std::move(request_handler)](
                const matcher::context& context,
                rebind_executor<boost::beast::tcp_stream>& stream,
                boost::beast::flat_buffer& buffer,
                boost::beast::http::request_parser<boost::beast::http::buffer_body>& header_parser,
                cancellation_signals& signals)
            -> awaitable<bool>
            {
                using request_type = std::remove_cvref_t<callable_arg_type<RequestHandler, 0>>;
                using body_type = request_type::body_type;

                http::request_parser<body_type> body_parser {std::move(header_parser)};
                auto [req_ec, req_sz] = co_await async_read(stream, buffer, body_parser);

                auto response = request_handler(
                    std::move(body_parser.get()),
                    context);

                const bool keep_alive = response.keep_alive();
                co_await boost::beast::async_write(stream, std::move(response));
                co_return keep_alive;
            }
        );
    }

private:
    using matcher_type = std::function<
        bool(
            const boost::beast::http::request_header<>&,
            matcher::context&,
            const boost::urls::url_view&)>;

    using handler_type = std::function<
        awaitable<bool>(
            const matcher::context&,
            rebind_executor<boost::beast::tcp_stream>&,
            boost::beast::flat_buffer&,
            boost::beast::http::request_parser<boost::beast::http::buffer_body>&,
            cancellation_signals&)>;

private:
    std::vector<std::pair<matcher_type, handler_type>> matchers_;
    bool needs_parsed_target_ = false;
};

} // namespace boost::web::session

#endif // BOOST_WEB_SESSION_HTTP_HPP
