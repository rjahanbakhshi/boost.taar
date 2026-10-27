//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Connection timeouts of the session: idle, header, body and send. The
// server side uses short timeouts; every client operation is bounded by
// a generous expiry so that a missing timeout fails the test instead of
// hanging it, and the assertions are on outcomes, not durations.

#include <boost/taar/core/body_reader.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/http_date.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/test/unit_test.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <string>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace taar = boost::taar;
using namespace std::chrono_literals;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;
using clock_type = std::chrono::steady_clock;

/// Short server-side timeout used by the tests.
constexpr auto short_timeout = 150ms;
/// Upper bound for any single client operation (generous for loaded CI).
constexpr auto client_bound = 15s;

/// Polls @a done every few milliseconds until it holds or @a limit passes.
taar::awaitable<bool> eventually(std::function<bool()> done, clock_type::duration limit = client_bound)
{
    net::steady_timer timer {co_await net::this_coro::executor};
    auto const deadline = clock_type::now() + limit;
    while (!done())
    {
        if (clock_type::now() >= deadline) co_return false;
        timer.expires_after(5ms);
        co_await timer.async_wait(taar::use_awaitable);
    }
    co_return true;
}

taar::awaitable<void> pause(clock_type::duration d)
{
    net::steady_timer timer {co_await net::this_coro::executor, d};
    co_await timer.async_wait(taar::use_awaitable);
}

/** Runs @a handler (a session or a session wrapper) behind a server on
    an ephemeral port; @a client gets the port and the signals. */
template <typename Handler, typename Client>
void run_server(net::io_context& io, Handler& handler, Client client)
{
    taar::cancellation_signals signals;
    std::atomic<unsigned short> port {0};
    std::exception_ptr client_error;
    net::co_spawn(io,
        taar::server::tcp("127.0.0.1", "0", handler, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        try
        {
            co_await eventually([&] { return port != 0; });
            co_await client(port.load(), signals);
        }
        catch (...)
        {
            client_error = std::current_exception();
        }
        signals.emit();
    }, net::detached);
    io.run();
    if (client_error) std::rethrow_exception(client_error);
}

/// A raw client connection; every operation is bounded by client_bound.
struct client
{
    beast::tcp_stream stream;
    beast::flat_buffer buffer;

    explicit client(net::any_io_executor ex) : stream {ex} {}

    taar::awaitable<void> connect(unsigned short port)
    {
        stream.expires_after(client_bound);
        co_await stream.async_connect(net::ip::tcp::endpoint {net::ip::address_v4::loopback(), port}, taar::use_awaitable);
    }

    taar::awaitable<void> write(std::string const& bytes)
    {
        stream.expires_after(client_bound);
        co_await net::async_write(stream, net::buffer(bytes), taar::use_awaitable);
    }

    /// Reads one response; throws on error.
    taar::awaitable<http::response<http::string_body>> read_response()
    {
        http::response_parser<http::string_body> parser;
        parser.body_limit(boost::none);
        stream.expires_after(client_bound);
        co_await http::async_read(stream, buffer, parser, taar::use_awaitable);
        co_return parser.release();
    }

    /// True if the server closes the connection without sending anything more.
    taar::awaitable<bool> closed_by_peer()
    {
        if (buffer.size() != 0) co_return false;
        std::array<char, 256> sink {};
        stream.expires_after(client_bound);
        auto [ec, n] = co_await stream.async_read_some(net::buffer(sink), net::as_tuple(taar::use_awaitable));
        co_return n == 0 && (ec == net::error::eof || ec == net::error::connection_reset);
    }
};

std::string get_request(std::string const& path)
{
    return "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}

/// A session with every timeout short, counting hard errors.
taar::session::http make_session(std::atomic<int>& hard_errors)
{
    taar::session::http session;
    session.set_idle_timeout(short_timeout);
    session.set_header_timeout(short_timeout);
    session.set_body_timeout(short_timeout);
    session.set_send_timeout(short_timeout);
    session.set_hard_error_handler([&](taar::failure const&) { ++hard_errors; });
    session.register_request_handler(method == http::verb::get && target == "/ping",
        rest([] { return std::string {"pong"}; }));
    session.register_request_handler(method == http::verb::post && target == "/echo",
        rest([](std::string body) { return body; }, taar::handler::string_body_arg {}));
    return session;
}

} // namespace

BOOST_AUTO_TEST_CASE(test_timeout_idle_connection_closes_silently)
{
    net::io_context io;
    std::atomic<int> hard_errors {0};
    auto session = make_session(hard_errors);
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        auto ex = co_await net::this_coro::executor;

        // Before the first request.
        client a {ex};
        co_await a.connect(port);
        BOOST_TEST(co_await a.closed_by_peer());

        // After a keep-alive request.
        client b {ex};
        co_await b.connect(port);
        co_await b.write(get_request("/ping"));
        auto res = co_await b.read_response();
        BOOST_TEST(res.body() == "pong");
        BOOST_TEST(res.keep_alive());
        BOOST_TEST(co_await b.closed_by_peer());
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_header_timeout_starts_at_the_first_byte)
{
    // A connection idle well past the header timeout is still served: the
    // header timeout only runs once a request has begun.
    net::io_context io;
    std::atomic<int> hard_errors {0};
    auto session = make_session(hard_errors);
    session.set_idle_timeout(30s);
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await pause(short_timeout * 4);
        co_await c.write(get_request("/ping"));
        auto res = co_await c.read_response();
        BOOST_TEST(res.body() == "pong");
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_partial_header_is_408_then_close)
{
    net::io_context io;
    std::atomic<int> hard_errors {0};
    auto session = make_session(hard_errors);
    session.set_idle_timeout(30s);
    session.set_response_filter([](http::request_header<> const&, http::response_header<>& res)
    {
        res.set(http::field::server, "taar-test");
    });
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await c.write("GET /ping HTTP/1.1\r\nHost: localhost\r\n");     // no blank line
        auto res = co_await c.read_response();
        BOOST_TEST(res.result() == http::status::request_timeout);
        BOOST_TEST(!res.keep_alive());
        BOOST_TEST(res[http::field::server] == "taar-test");
        BOOST_TEST(taar::parse_http_date(res[http::field::date]).has_value());
        BOOST_TEST(co_await c.closed_by_peer());
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_stalled_buffered_body_is_408_then_close)
{
    net::io_context io;
    std::atomic<int> hard_errors {0};
    auto session = make_session(hard_errors);
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await c.write(
            "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\n"
            "Content-Length: 100\r\n\r\n0123456789");
        auto res = co_await c.read_response();
        BOOST_TEST(res.result() == http::status::request_timeout);
        BOOST_TEST(!res.keep_alive());
        BOOST_TEST(co_await c.closed_by_peer());

        // A body for no route at all (404 path) times out the same way.
        client d {co_await net::this_coro::executor};
        co_await d.connect(port);
        co_await d.write(
            "POST /nowhere HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\n0123456789");
        auto missing = co_await d.read_response();
        BOOST_TEST(missing.result() == http::status::request_timeout);
        BOOST_TEST(co_await d.closed_by_peer());
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_stalled_streamed_body_is_408_then_close)
{
    net::io_context io;
    std::atomic<int> hard_errors {0};
    std::atomic<std::uint64_t> received {0};
    auto session = make_session(hard_errors);
    session.register_request_handler(method == http::verb::post && target == "/upload",
        rest([&](taar::body_reader& body) -> taar::awaitable<std::string>
        {
            while (!body.done())
            {
                received += (co_await body.read_some(256)).size();
            }
            co_return std::to_string(received.load());
        }, taar::handler::stream_body_arg {}));
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await c.write(
            "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\n"
            "Content-Length: 2000\r\n\r\n" + std::string(1000, 'a'));
        auto res = co_await c.read_response();
        BOOST_TEST(res.result() == http::status::request_timeout);
        BOOST_TEST(!res.keep_alive());
        BOOST_TEST(received.load() == 1000u);
        BOOST_TEST(co_await c.closed_by_peer());
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_stalled_reader_of_a_large_response_is_closed)
{
    // The client asks for a response far larger than the socket buffers
    // and does not read: the write blocks, the send timeout closes the
    // connection, and the client later sees a truncated response.
    constexpr std::size_t big = 64 * 1024 * 1024;
    net::io_context io;
    std::atomic<int> hard_errors {0};
    std::atomic<int> finished {0};
    auto session = make_session(hard_errors);
    session.register_request_handler(method == http::verb::get && target == "/big",
        rest([] { return std::string(big, 'x'); }));
    auto handler = [&](net::ip::tcp::socket socket, taar::cancellation_signals& signals) -> taar::awaitable<void>
    {
        co_await session(std::move(socket), signals);
        ++finished;
    };
    run_server(io, handler, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await c.write(get_request("/big"));
        // The session ends on its own while nobody reads.
        BOOST_TEST(co_await eventually([&] { return finished == 1; }));

        std::size_t total = 0;
        std::array<char, 64 * 1024> sink {};
        for (;;)
        {
            c.stream.expires_after(client_bound);
            auto [ec, n] = co_await c.stream.async_read_some(net::buffer(sink), net::as_tuple(taar::use_awaitable));
            total += n;
            if (ec) break;
        }
        BOOST_TEST(total < big);
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_gaps_between_stream_chunks_do_not_count)
{
    net::io_context io;
    std::atomic<int> hard_errors {0};
    auto session = make_session(hard_errors);
    session.register_request_handler(method == http::verb::get && target == "/events",
        rest([]() -> taar::chunked_response<std::string>
        {
            co_yield std::string {"a"};
            co_await pause(short_timeout * 4);
            co_yield std::string {"b"};
            co_await pause(short_timeout * 4);
            co_yield std::string {"c"};
        }));
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await c.write(get_request("/events"));
        auto res = co_await c.read_response();
        BOOST_TEST(res.result() == http::status::ok);
        BOOST_TEST(res.body() == "abc");
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_slow_handler_trips_nothing)
{
    net::io_context io;
    std::atomic<int> hard_errors {0};
    auto session = make_session(hard_errors);
    session.set_idle_timeout(30s);
    session.register_request_handler(method == http::verb::post && target == "/slow",
        rest([](std::string body) -> taar::awaitable<std::string>
        {
            co_await pause(short_timeout * 4);
            co_return "done " + body;
        }, taar::handler::string_body_arg {}));
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await c.write(
            "POST /slow HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\n"
            "Content-Length: 2\r\n\r\nhi");
        auto res = co_await c.read_response();
        BOOST_TEST(res.result() == http::status::ok);
        BOOST_TEST(res.body() == "done hi");
        BOOST_TEST(res.keep_alive());
        // The connection is still usable.
        co_await c.write(get_request("/ping"));
        auto again = co_await c.read_response();
        BOOST_TEST(again.body() == "pong");
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_shutdown_with_a_stalled_upload_completes)
{
    // A streaming body read is shielded from shutdown; the body timeout
    // bounds it, so a client that stalls mid-upload no longer holds the
    // shutdown: the handler's read times out and the answer is 408.
    net::io_context io;
    std::atomic<int> hard_errors {0};
    std::atomic<std::uint64_t> received {0};
    taar::session::http session;
    session.set_body_timeout(300ms);
    session.set_hard_error_handler([&](taar::failure const&) { ++hard_errors; });
    session.register_request_handler(method == http::verb::post && target == "/upload",
        rest([&](taar::body_reader& body) -> taar::awaitable<std::string>
        {
            while (!body.done())
            {
                received += (co_await body.read_some(256)).size();
            }
            co_return std::to_string(received.load());
        }, taar::handler::stream_body_arg {}));
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals& signals) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await c.write(
            "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\n"
            "Content-Length: 2000\r\n\r\n" + std::string(1000, 'a'));
        BOOST_TEST(co_await eventually([&] { return received == 1000; }));
        signals.emit();
        auto res = co_await c.read_response();
        BOOST_TEST(res.result() == http::status::request_timeout);
        BOOST_TEST(!res.keep_alive());
        BOOST_TEST(co_await c.closed_by_peer());
    });
    BOOST_TEST(hard_errors.load() == 0);
}

BOOST_AUTO_TEST_CASE(test_timeout_zero_disables)
{
    net::io_context io;
    std::atomic<int> hard_errors {0};
    auto session = make_session(hard_errors);
    session.set_idle_timeout(0s);
    session.set_header_timeout(0s);
    run_server(io, session, [&](unsigned short port, taar::cancellation_signals&) -> taar::awaitable<void>
    {
        client c {co_await net::this_coro::executor};
        co_await c.connect(port);
        co_await pause(short_timeout * 3);
        co_await c.write("GET /ping HTTP/1.1\r\n");
        co_await pause(short_timeout * 3);
        co_await c.write("Host: localhost\r\n\r\n");
        auto res = co_await c.read_response();
        BOOST_TEST(res.body() == "pong");
    });
    BOOST_TEST(hard_errors.load() == 0);
}
