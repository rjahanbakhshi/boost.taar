//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Streaming responses and protocol upgrades over a real connection.

#include "e2e.hpp"
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
namespace taar = boost::taar;
using namespace std::chrono_literals;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;
using socket_type = taar::rebind_executor<net::ip::tcp::socket>;
using parser_type = http::response_parser<http::string_body>;

taar::awaitable<socket_type> connect(unsigned short port)
{
    socket_type socket {co_await net::this_coro::executor};
    co_await socket.async_connect({net::ip::address_v4::loopback(), port});
    co_return socket;
}

taar::awaitable<void> send_get(socket_type& socket, std::string target)
{
    http::request<http::empty_body> req {http::verb::get, target, 11};
    req.set(http::field::host, "localhost");
    co_await http::async_write(socket, req);
}

/// Reads until the parser is done or the connection fails. Returns the error.
taar::awaitable<boost::system::error_code> read_to_end(
    socket_type& socket,
    beast::flat_buffer& buffer,
    parser_type& parser)
{
    while (!parser.is_done())
    {
        auto [ec, n] = co_await http::async_read_some(socket, buffer, parser);
        if (ec) co_return ec;
    }
    co_return boost::system::error_code {};
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_chunked_response_arrives_in_chunks)
{
    net::io_context io;
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/three",
        rest([]() -> taar::chunked_response<std::string>
        {
            co_yield taar::set_header(http::field::content_type, "text/plain");
            co_yield std::string {"one"};
            co_yield std::string {"two"};
            co_yield std::string {"three"};
        }));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        co_await send_get(socket, "/three");

        beast::flat_buffer buffer;
        parser_type parser;
        int chunks = 0;
        // The terminating zero-length chunk also reports a header; count data chunks only.
        auto count_chunk = [&](std::uint64_t size, std::string_view, boost::system::error_code&) { if (size > 0) ++chunks; };
        parser.on_chunk_header(count_chunk);
        co_await http::async_read_header(socket, buffer, parser);
        BOOST_TEST(parser.get().result() == http::status::ok);
        BOOST_TEST(parser.get().chunked());
        BOOST_TEST(parser.get()[http::field::content_type] == "text/plain");

        auto ec = co_await read_to_end(socket, buffer, parser);
        BOOST_TEST(!ec);
        BOOST_TEST(parser.is_done());
        BOOST_TEST(chunks == 3);
        BOOST_TEST(parser.get().body() == "onetwothree");

        // The connection is still usable afterwards.
        co_await send_get(socket, "/three");
        parser_type again;
        auto ec2 = co_await read_to_end(socket, buffer, again);
        BOOST_TEST(!ec2);
        BOOST_TEST(again.get().body() == "onetwothree");
    });
}

struct frame_probe
{
    static inline std::atomic<int> alive {0};
    frame_probe() { ++alive; }
    ~frame_probe() { --alive; }
};

BOOST_AUTO_TEST_CASE(test_e2e_streaming_client_disconnect_stops_infinite_generator)
{
    net::io_context io;
    taar::session::http session;
    frame_probe::alive = 0;

    session.register_request_handler(
        method == http::verb::get && target == "/ticks",
        rest([]() -> taar::chunked_response<std::string>
        {
            frame_probe const probe;
            net::steady_timer timer {co_await net::this_coro::executor};
            for (;;)
            {
                timer.expires_after(10ms);
                co_await timer.async_wait(taar::use_awaitable);
                co_yield std::string {"tick\n"};
            }
        }));
    session.register_request_handler(
        method == http::verb::get && target == "/ping",
        rest([] { return std::string {"pong"}; }));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        co_await send_get(socket, "/ticks");

        beast::flat_buffer buffer;
        parser_type parser;
        co_await http::async_read_header(socket, buffer, parser);
        while (parser.get().body().size() < 10)
        {
            co_await http::async_read_some(socket, buffer, parser);
        }
        BOOST_TEST(frame_probe::alive == 1);

        boost::system::error_code ignored;
        socket.shutdown(net::ip::tcp::socket::shutdown_both, ignored);
        socket.close(ignored);

        // Bounded wait for the server to notice and destroy the generator frame.
        net::steady_timer timer {co_await net::this_coro::executor};
        auto const deadline = std::chrono::steady_clock::now() + 2s;
        while (frame_probe::alive != 0 && std::chrono::steady_clock::now() < deadline)
        {
            timer.expires_after(10ms);
            co_await timer.async_wait(taar::use_awaitable);
        }
        BOOST_TEST(frame_probe::alive == 0);

        auto other = co_await e2e::send_all(port, e2e::make_request(http::verb::get, "/ping"));
        BOOST_TEST(other[0].body() == "pong");
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_generator_throwing_mid_stream_truncates_response)
{
    net::io_context io;
    taar::session::http session;
    std::atomic<int> generator_errors {0};
    session.set_hard_error_handler([&](taar::failure const& f)
    {
        // Transport errors (peer EOF) also arrive here.
        if (f.as<std::runtime_error>()) ++generator_errors;
    });

    session.register_request_handler(
        method == http::verb::get && target == "/boom",
        rest([]() -> taar::chunked_response<std::string>
        {
            co_yield std::string {"a"};
            co_yield std::string {"b"};
            throw std::runtime_error {"mid-stream"};
        }));
    session.register_request_handler(
        method == http::verb::get && target == "/ping",
        rest([] { return std::string {"pong"}; }));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        co_await send_get(socket, "/boom");

        beast::flat_buffer buffer;
        parser_type parser;
        co_await http::async_read_header(socket, buffer, parser);
        BOOST_TEST(parser.get().result() == http::status::ok);
        auto ec = co_await read_to_end(socket, buffer, parser);
        // No terminating chunk: the client sees a truncated body and EOF.
        BOOST_TEST((ec == http::error::partial_message));
        BOOST_TEST(!parser.is_done());
        BOOST_TEST(parser.get().body() == "ab");

        auto other = co_await e2e::send_all(port, e2e::make_request(http::verb::get, "/ping"));
        BOOST_TEST(other[0].body() == "pong");
    });
    // Once headers are on the wire no error response is possible; the
    // exception still reaches the hard error handler for logging.
    BOOST_TEST(generator_errors == 1);
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_mid_stream_throw_bypasses_custom_soft_error_handler)
{
    net::io_context io;
    taar::session::http session;
    std::atomic<int> soft_calls {0};
    std::atomic<int> hard_calls {0};

    // A soft handler that writes a full response must not run once chunks
    // are on the wire: its bytes would corrupt the chunked framing.
    session.set_soft_error_handler([&](taar::failure const&)
    {
        ++soft_calls;
        http::response<http::string_body> res {http::status::internal_server_error, 11};
        res.body() = "{\"error\":\"late\"}";
        res.prepare_payload();
        return res;
    });
    session.set_hard_error_handler([&](taar::failure const& f)
    {
        if (f.as<std::runtime_error>()) ++hard_calls;
    });

    session.register_request_handler(
        method == http::verb::get && target == "/boom",
        rest([]() -> taar::chunked_response<std::string>
        {
            co_yield std::string {"a"};
            co_yield std::string {"b"};
            throw std::runtime_error {"mid-stream"};
        }));
    session.register_request_handler(
        method == http::verb::get && target == "/early",
        rest([]() -> taar::chunked_response<std::string>
        {
            throw std::runtime_error {"before first chunk"};
            co_yield std::string {"never"};
        }));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        co_await send_get(socket, "/boom");

        beast::flat_buffer buffer;
        parser_type parser;
        co_await http::async_read_header(socket, buffer, parser);
        BOOST_TEST(parser.get().result() == http::status::ok);
        auto ec = co_await read_to_end(socket, buffer, parser);
        BOOST_TEST((ec == http::error::partial_message));   // clean truncation, no trailing response
        BOOST_TEST(parser.get().body() == "ab");
        BOOST_TEST(soft_calls == 0);

        // Before the first chunk nothing is on the wire: the soft handler applies.
        auto early = co_await e2e::send_all(port, e2e::make_request(http::verb::get, "/early"));
        BOOST_TEST(early[0].result() == http::status::internal_server_error);
        BOOST_TEST(early[0].body() == "{\"error\":\"late\"}");
        BOOST_TEST(soft_calls == 1);
    });
    BOOST_TEST(hard_calls == 1);
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_websocket_echo_upgrade)
{
    net::io_context io;
    taar::session::http session;
    using awaitable_token = net::use_awaitable_t<net::any_io_executor>;

    session.register_raw_handler(
        method == http::verb::get && target == "/echo",
        [](
            taar::matcher::context const&,
            http::request<http::empty_body>&& request,
            beast::tcp_stream&& stream,
            beast::flat_buffer&& buffer,
            taar::cancellation_signals&) -> taar::awaitable<void>
        {
            websocket::stream<beast::tcp_stream> ws {std::move(stream)};
            co_await ws.async_accept(request, awaitable_token {});
            try
            {
                for (;;)
                {
                    auto sz = co_await ws.async_read(buffer, awaitable_token {});
                    ws.text(ws.got_text());
                    co_await ws.async_write(buffer.data(), awaitable_token {});
                    buffer.consume(sz);
                }
            }
            catch (boost::system::system_error const&)
            {
                // closed by the peer
            }
        });

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        websocket::stream<socket_type> ws {co_await net::this_coro::executor};
        co_await ws.next_layer().async_connect({net::ip::address_v4::loopback(), port}, awaitable_token {});
        co_await ws.async_handshake("localhost", "/echo", awaitable_token {});

        for (auto msg : {"hello", "world"})
        {
            co_await ws.async_write(net::buffer(std::string {msg}), awaitable_token {});
            beast::flat_buffer buffer;
            co_await ws.async_read(buffer, awaitable_token {});
            BOOST_TEST(ws.got_text());
            BOOST_TEST(beast::buffers_to_string(buffer.data()) == msg);
        }
        co_await ws.async_close(websocket::close_code::normal, awaitable_token {});
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_under_with_session_sets_cookie)
{
    struct visitor { int streams = 0; };
    net::io_context io;
    taar::session::http session;
    taar::context_store<std::string, visitor> store {std::chrono::minutes {5}};
    session.register_request_handler(
        method == http::verb::get && target == "/mine",
        taar::handler::with_session(store, taar::handler::session_key::cookie {"sid"},
            taar::handler::session_policy::create_if_missing,
            rest([](visitor& v) -> taar::chunked_response<std::string>
            {
                ++v.streams;
                co_yield taar::set_header(http::field::content_type, "text/plain");
                co_yield std::string {"stream "};
                co_yield std::to_string(v.streams);
            })));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        co_await send_get(socket, "/mine");
        beast::flat_buffer buffer;
        parser_type first;
        auto ec = co_await read_to_end(socket, buffer, first);
        BOOST_TEST(!ec);
        BOOST_TEST(first.get().result() == http::status::ok);
        BOOST_TEST(first.get().chunked());
        BOOST_TEST(first.get()[http::field::content_type] == "text/plain");
        std::string const cookie {first.get()[http::field::set_cookie]};
        BOOST_TEST(cookie.starts_with("sid="));
        BOOST_TEST(first.get().body() == "stream 1");

        // Same connection, key presented: same entry, no new cookie.
        http::request<http::empty_body> req {http::verb::get, "/mine", 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::cookie, cookie.substr(0, cookie.find(';')));
        co_await http::async_write(socket, req);
        parser_type second;
        ec = co_await read_to_end(socket, buffer, second);
        BOOST_TEST(!ec);
        BOOST_TEST(second.get().body() == "stream 2");
        BOOST_TEST(second.get().count(http::field::set_cookie) == 0u);
        BOOST_TEST(store.size() == 1u);
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_shutdown_ends_open_stream_without_hard_error)
{
    net::io_context io;
    taar::session::http session;
    std::atomic<int> hard_errors {0};
    std::string hard_error_text;
    session.set_hard_error_handler([&](taar::failure const& f)
    {
        ++hard_errors;
        hard_error_text = f.chain();
    });
    session.register_request_handler(
        method == http::verb::get && target == "/forever",
        rest([]() -> taar::chunked_response<std::string>
        {
            co_yield taar::set_header(http::field::content_type, "text/event-stream");
            for (int i = 0;; ++i)
            {
                co_yield "data: " + std::to_string(i) + "\n\n";
                net::steady_timer timer {co_await net::this_coro::executor};
                timer.expires_after(10ms);
                co_await timer.async_wait(taar::use_awaitable);
            }
        }));

    taar::cancellation_signals signals;
    std::atomic<unsigned short> port {0};
    net::co_spawn(io,
        taar::server::tcp("127.0.0.1", "0", session, signals, [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);
    std::exception_ptr client_error;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        try
        {
            net::steady_timer timer {co_await net::this_coro::executor};
            while (port == 0) { timer.expires_after(1ms); co_await timer.async_wait(taar::use_awaitable); }
            auto socket = co_await connect(port);
            co_await send_get(socket, "/forever");
            beast::flat_buffer buffer;
            parser_type parser;
            parser.body_limit(boost::none);
            co_await http::async_read_header(socket, buffer, parser);
            while (parser.get().body().find("data: 3\n\n") == std::string::npos)
            {
                co_await http::async_read_some(socket, buffer, parser);
            }
            signals.emit();                 // while the stream is live
            boost::system::error_code ec;
            while (!ec && !parser.is_done())
            {
                auto [rec, n] = co_await http::async_read_some(socket, buffer, parser, net::as_tuple(taar::use_awaitable));
                ec = rec;
            }
            BOOST_TEST(ec);                 // the server dropped the connection
        }
        catch (...)
        {
            client_error = std::current_exception();
            signals.emit();
        }
    }, net::detached);
    io.run();
    if (client_error) std::rethrow_exception(client_error);
    BOOST_TEST(hard_errors == 0, "hard error: " << hard_error_text);
}

} // namespace
