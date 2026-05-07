//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// WebSocket echo server. Demonstrates register_raw_handler: the matched
// handler takes ownership of the parsed HTTP request, the underlying
// tcp_stream, and the flat_buffer, then completes the WebSocket upgrade
// and echoes incoming text frames back to the client.
//
//   ./websocket_server 8082
//
//   wscat -c ws://127.0.0.1:8082/echo
//   > hello
//   < hello
//

#include <boost/taar/session/http.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/error.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: websocket_server <port>\n";
        return EXIT_FAILURE;
    }

    namespace net = boost::asio;
    namespace beast = boost::beast;
    namespace http = boost::beast::http;
    namespace websocket = boost::beast::websocket;
    namespace taar = boost::taar;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::rebind_executor;
    using awaitable_token = net::use_awaitable_t<net::io_context::executor_type>;

    net::io_context io_context;

    net::signal_set os_signals(io_context, SIGINT, SIGTERM);
    os_signals.async_wait([&](auto, auto) { io_context.stop(); });

    taar::session::http http_session;

    // Plain HTTP route — proves /api/* requests still flow through the
    // normal request-handler dispatch.
    http_session.register_request_handler(
        method == http::verb::get && target == "/api/version",
        taar::handler::rest([] { return std::string{"1.0"}; }));

    // WebSocket upgrade route via register_raw_handler. Once the matcher
    // fires, the session's per-connection coroutine ends and ownership of
    // the stream + buffer passes to this handler — taar will not touch
    // the socket again.
    http_session.register_raw_handler(
        method == http::verb::get && target == "/echo",
        [](
            taar::matcher::context const&,
            http::request<http::empty_body>&& request,
            rebind_executor<beast::tcp_stream>&& stream,
            beast::flat_buffer&& buffer,
            taar::cancellation_signals&) -> taar::awaitable<void>
        {
            try
            {
                using ws_stream_t = websocket::stream<rebind_executor<beast::tcp_stream>>;
                ws_stream_t ws{std::move(stream)};

                // Suggest the server identity in the upgrade response.
                ws.set_option(websocket::stream_base::decorator(
                    [](websocket::response_type& res)
                    {
                        res.set(http::field::server,
                                "boost.taar websocket-example");
                    }));

                co_await ws.async_accept(request, awaitable_token{});

                // Reuse the buffer we inherited (cheap; usually empty for a
                // fresh upgrade since the client waits for 101 before any
                // payload).
                while (true)
                {
                    auto sz = co_await ws.async_read(buffer, awaitable_token{});
                    ws.text(ws.got_text());
                    co_await ws.async_write(buffer.data(), awaitable_token{});
                    buffer.consume(sz);
                }
            }
            catch (boost::system::system_error const& e)
            {
                auto const& ec = e.code();
                if (ec != websocket::error::closed
                    && ec != net::error::eof
                    && ec != net::error::connection_reset)
                {
                    std::cerr << "websocket error: " << e.what() << '\n';
                }
            }
            co_return;
        });

    taar::cancellation_signals cancellation_signals;
    net::co_spawn(
        io_context,
        taar::server::tcp(
            "0.0.0.0",
            argv[1],
            http_session,
            cancellation_signals,
            [](net::ip::tcp::endpoint const& endpoint)
            {
                std::clog << "WebSocket server listening on port "
                          << endpoint.port()
                          << " (try ws://127.0.0.1:" << endpoint.port()
                          << "/echo)\n";
            }),
        net::bind_cancellation_slot(cancellation_signals.slot(),
                                    taar::ignore_and_rethrow));

    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back([&] { io_context.run(); });
    }

    return EXIT_SUCCESS;
}
