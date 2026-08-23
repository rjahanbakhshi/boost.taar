//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

// Transport behaviour, error escalation, connection lifecycle, shutdown and
// multi-threaded operation over real connections.

#include "e2e.hpp"
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <memory>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <thread>
#include <vector>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
using namespace std::chrono_literals;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::json_body_arg;
using taar::handler::path_arg;
using taar::handler::rest;
using socket_type = taar::rebind_executor<net::ip::tcp::socket>;

struct division_by_zero : std::domain_error { using domain_error::domain_error; };

taar::awaitable<socket_type> connect(unsigned short port)
{
    socket_type socket {co_await net::this_coro::executor};
    co_await socket.async_connect({net::ip::address_v4::loopback(), port});
    co_return socket;
}

taar::awaitable<e2e::response> read_response(socket_type& socket, beast::flat_buffer& buffer)
{
    e2e::response res;
    auto [ec, n] = co_await http::async_read(socket, buffer, res);
    if (ec) throw boost::system::system_error {ec};
    co_return res;
}

/// Reads raw bytes until the peer closes. Returns everything received.
taar::awaitable<std::string> read_until_eof(socket_type& socket)
{
    std::string out;
    for (;;)
    {
        char buf[1024];
        auto [ec, n] = co_await socket.async_read_some(net::buffer(buf));
        out.append(buf, n);
        if (ec) co_return out;
    }
}

/// Reads one response, giving up after @a limit. nullopt on timeout or EOF.
taar::awaitable<std::optional<e2e::response>> read_response_within(
    socket_type& socket,
    beast::flat_buffer& buffer,
    std::chrono::milliseconds limit)
{
    using namespace net::experimental::awaitable_operators;
    net::steady_timer timer {co_await net::this_coro::executor};
    timer.expires_after(limit);
    e2e::response res;
    auto outcome = co_await (
        http::async_read(socket, buffer, res, net::as_tuple(taar::use_awaitable)) ||
        timer.async_wait(net::as_tuple(taar::use_awaitable)));
    if (outcome.index() == 1) co_return std::nullopt;
    auto [ec, n] = std::get<0>(outcome);
    if (ec) co_return std::nullopt;
    co_return res;
}

/// True if the next read observes EOF within @a limit.
taar::awaitable<bool> peer_closed_within(socket_type& socket, std::chrono::milliseconds limit)
{
    using namespace net::experimental::awaitable_operators;
    net::steady_timer timer {co_await net::this_coro::executor};
    timer.expires_after(limit);
    char buf[1];
    auto outcome = co_await (
        socket.async_read_some(net::buffer(buf), net::as_tuple(taar::use_awaitable)) ||
        timer.async_wait(net::as_tuple(taar::use_awaitable)));
    if (outcome.index() == 1) co_return false;
    auto [ec, n] = std::get<0>(outcome);
    co_return ec == net::error::eof && n == 0;
}

taar::session::http make_session()
{
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/echo/{x}",
        rest([](std::string x) { return x; }, path_arg("x")));
    session.register_request_handler(
        method == http::verb::get && target == "/divide/{b}",
        rest([](int b) { if (b == 0) throw division_by_zero {"division by zero"}; return 10 / b; }, path_arg("b")));
    session.register_request_handler(
        method == http::verb::get && target == "/crash",
        rest([]() -> int { throw std::runtime_error {"unexpected"}; }));
    return session;
}

// ---- keep-alive, close, pipelining, malformed input ---------------------------

BOOST_AUTO_TEST_CASE(test_e2e_keep_alive_then_connection_close_is_honoured)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        beast::flat_buffer buffer;
        for (auto x : {"1", "2", "3"})
        {
            auto req = e2e::make_request(http::verb::get, std::string {"/echo/"} + x);
            co_await http::async_write(socket, req);
            auto res = co_await read_response(socket, buffer);
            BOOST_TEST(res.body() == x);
            BOOST_TEST(res.keep_alive());
        }

        auto last = e2e::make_request(http::verb::get, "/echo/bye");
        last.keep_alive(false);
        co_await http::async_write(socket, last);
        auto res = co_await read_response(socket, buffer);
        BOOST_TEST(res.body() == "bye");
        // Connection: close must be reflected in the response and end the connection.
        BOOST_TEST(!res.keep_alive());
        BOOST_TEST(co_await peer_closed_within(socket, 500ms));
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_pipelined_requests_answered_in_order)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        std::string wire =
            "GET /echo/first HTTP/1.1\r\nHost: localhost\r\n\r\n"
            "GET /echo/second HTTP/1.1\r\nHost: localhost\r\n\r\n";
        co_await net::async_write(socket, net::buffer(wire));

        beast::flat_buffer buffer;
        auto a = co_await read_response(socket, buffer);
        BOOST_TEST(a.body() == "first");
        // The second request was already buffered by the server with the first.
        auto b = co_await read_response_within(socket, buffer, 500ms);
        BOOST_REQUIRE(b.has_value());
        BOOST_TEST(b->body() == "second");
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_malformed_request_line_is_400_then_close)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        co_await net::async_write(socket, net::buffer(std::string {"GARBAGE\r\n\r\n"}));
        auto raw = co_await read_until_eof(socket);
        BOOST_TEST(raw.starts_with("HTTP/1.1 400"));
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_http10_closes_unless_keep_alive_requested)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        {
            auto socket = co_await connect(port);
            http::request<http::empty_body> req {http::verb::get, "/echo/old", 10};
            co_await http::async_write(socket, req);
            beast::flat_buffer buffer;
            auto res = co_await read_response(socket, buffer);
            BOOST_TEST(res.body() == "old");
            // An HTTP/1.0 request gets an HTTP/1.0 response and the connection closes.
            BOOST_TEST(res.version() == 10u);
            BOOST_TEST(co_await peer_closed_within(socket, 500ms));
        }
        {
            auto socket = co_await connect(port);
            http::request<http::empty_body> req {http::verb::get, "/echo/ka", 10};
            req.keep_alive(true);
            co_await http::async_write(socket, req);
            beast::flat_buffer buffer;
            auto res = co_await read_response(socket, buffer);
            BOOST_TEST(res.body() == "ka");
            BOOST_TEST(res.keep_alive());
            co_await http::async_write(socket, req);
            auto again = co_await read_response(socket, buffer);
            BOOST_TEST(again.body() == "ka");
        }
    });
}

// ---- soft and hard errors ----------------------------------------------------------

BOOST_AUTO_TEST_CASE(test_e2e_custom_soft_error_handler_keeps_connection)
{
    net::io_context io;
    auto session = make_session();
    session.set_soft_error_handler([](std::exception_ptr ep)
    {
        try
        {
            std::rethrow_exception(ep);
        }
        catch (division_by_zero const& e)
        {
            http::response<http::string_body> res {http::status::unprocessable_entity, 11};
            res.set(http::field::content_type, "application/json");
            res.body() = json::serialize(json::object {{"error", e.what()}});
            res.keep_alive(true);
            res.prepare_payload();
            return res;
        }
    });

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {
            e2e::make_request(http::verb::get, "/divide/0"),
            e2e::make_request(http::verb::get, "/divide/5")});
        BOOST_TEST(r[0].result() == http::status::unprocessable_entity);
        BOOST_TEST(json::parse(r[0].body()).at("error").as_string() == "division by zero");
        BOOST_TEST(r[1].body() == "2");
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_unknown_exception_escalates_to_hard_error_and_server_survives)
{
    net::io_context io;
    auto session = make_session();
    std::atomic<int> runtime_errors {0};
    session.set_hard_error_handler([&](std::exception_ptr ep)
    {
        try { std::rethrow_exception(ep); }
        catch (std::runtime_error const&) { ++runtime_errors; }
        catch (...) {}
    });

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        auto req = e2e::make_request(http::verb::get, "/crash");
        co_await http::async_write(socket, req);
        auto raw = co_await read_until_eof(socket);
        BOOST_TEST(raw.empty());                 // nothing written; connection dropped
        BOOST_TEST(runtime_errors == 1);

        auto r = co_await e2e::send_all(port, {e2e::make_request(http::verb::get, "/echo/alive")});
        BOOST_TEST(r[0].body() == "alive");
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_soft_error_handler_that_throws_becomes_hard_error)
{
    net::io_context io;
    auto session = make_session();
    std::atomic<int> logic_errors {0};
    session.set_soft_error_handler([](std::exception_ptr) -> http::response<http::string_body>
    {
        throw std::logic_error {"handler failed"};
    });
    session.set_hard_error_handler([&](std::exception_ptr ep)
    {
        try { std::rethrow_exception(ep); }
        catch (std::logic_error const&) { ++logic_errors; }
        catch (...) {}
    });

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        auto req = e2e::make_request(http::verb::get, "/divide/0");
        co_await http::async_write(socket, req);
        auto raw = co_await read_until_eof(socket);
        BOOST_TEST(raw.empty());
        BOOST_TEST(logic_errors == 1);

        auto r = co_await e2e::send_all(port, {e2e::make_request(http::verb::get, "/echo/alive")});
        BOOST_TEST(r[0].body() == "alive");
    });
}

// ---- connection context lifecycle ----------------------------------------------

struct lifecycle_ctx
{
    static inline std::atomic<int> constructed {0};
    static inline std::atomic<int> destroyed {0};
    int requests = 0;
    lifecycle_ctx() { ++constructed; }
    ~lifecycle_ctx() { ++destroyed; }
};

BOOST_AUTO_TEST_CASE(test_e2e_connection_context_constructed_and_destroyed_once_per_connection)
{
    net::io_context io;
    lifecycle_ctx::constructed = 0;
    lifecycle_ctx::destroyed = 0;
    taar::session::basic_http<lifecycle_ctx> session;
    session.register_request_handler(
        method == http::verb::get && target == "/count",
        rest([](lifecycle_ctx& c) { return ++c.requests; }));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto a = co_await connect(port);
        auto b = co_await connect(port);
        beast::flat_buffer ba;
        beast::flat_buffer bb;
        auto req = e2e::make_request(http::verb::get, "/count");
        for (int i = 1; i <= 2; ++i)
        {
            co_await http::async_write(a, req);
            co_await http::async_write(b, req);
            BOOST_TEST((co_await read_response(a, ba)).body() == std::to_string(i));
            BOOST_TEST((co_await read_response(b, bb)).body() == std::to_string(i));
        }
        BOOST_TEST(lifecycle_ctx::constructed == 2);
        BOOST_TEST(lifecycle_ctx::destroyed == 0);

        boost::system::error_code ignored;
        a.close(ignored);
        b.close(ignored);

        net::steady_timer timer {co_await net::this_coro::executor};
        auto const deadline = std::chrono::steady_clock::now() + 2s;
        while (lifecycle_ctx::destroyed != 2 && std::chrono::steady_clock::now() < deadline)
        {
            timer.expires_after(10ms);
            co_await timer.async_wait(taar::use_awaitable);
        }
        BOOST_TEST(lifecycle_ctx::destroyed == 2);
        BOOST_TEST(lifecycle_ctx::constructed == 2);
    });
}

// ---- graceful shutdown ------------------------------------------------------------

BOOST_AUTO_TEST_CASE(test_e2e_graceful_shutdown_completes_in_flight_request_and_stops_listening)
{
    net::io_context io;
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/slow",
        rest([]() -> taar::awaitable<std::string>
        {
            net::steady_timer timer {co_await net::this_coro::executor};
            timer.expires_after(200ms);
            co_await timer.async_wait(taar::use_awaitable);
            co_return std::string {"done"};
        }));

    taar::cancellation_signals signals;
    unsigned short port = 0;
    net::co_spawn(
        io,
        taar::server::tcp("127.0.0.1", "0", session, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);

    std::exception_ptr client_error;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        try
        {
            net::steady_timer timer {co_await net::this_coro::executor};
            while (port == 0)
            {
                timer.expires_after(1ms);
                co_await timer.async_wait(taar::use_awaitable);
            }

            auto socket = co_await connect(port);
            auto req = e2e::make_request(http::verb::get, "/slow");
            co_await http::async_write(socket, req);

            timer.expires_after(50ms);
            co_await timer.async_wait(taar::use_awaitable);
            signals.emit();                      // shutdown while /slow is in flight

            // The in-flight request should still complete with its full response.
            beast::flat_buffer buffer;
            auto res = co_await read_response_within(socket, buffer, 2000ms);
            BOOST_TEST(res.has_value());
            if (res)
            {
                BOOST_TEST(res->result() == http::status::ok);
                BOOST_TEST(res->body() == "done");
            }
            BOOST_TEST(co_await peer_closed_within(socket, 500ms));

            socket_type refused {co_await net::this_coro::executor};
            auto [ec] = co_await refused.async_connect({net::ip::address_v4::loopback(), port});
            BOOST_TEST(ec == net::error::connection_refused);
        }
        catch (...)
        {
            client_error = std::current_exception();
            signals.emit();
        }
    }, net::detached);

    io.run();
    if (client_error) std::rethrow_exception(client_error);
}

// ---- multi-threaded stress ------------------------------------------------------

struct conn_counter { int requests = 0; };

BOOST_AUTO_TEST_CASE(test_e2e_multithreaded_concurrent_clients)
{
    constexpr int threads = 4;
    constexpr int clients = 50;
    constexpr int rounds = 3;

    net::io_context io;
    taar::session::basic_http<conn_counter> session;
    session.register_request_handler(
        method == http::verb::get && target == "/echo/{x}",
        rest([](std::string x) { return x; }, path_arg("x")));
    session.register_request_handler(
        method == http::verb::post && target == "/json",
        rest([](json::value v) { return v; }, json_body_arg()));
    session.register_request_handler(
        method == http::verb::get && target == "/count",
        rest([](conn_counter& c) { return ++c.requests; }));

    taar::cancellation_signals signals;
    std::atomic<unsigned short> port {0};
    net::co_spawn(
        io,
        taar::server::tcp("127.0.0.1", "0", session, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);

    std::atomic<int> finished {0};
    std::atomic<int> failures {0};
    std::vector<std::string> errors(clients);

    for (int c = 0; c < clients; ++c)
    {
        net::co_spawn(io, [&, c]() -> taar::awaitable<void>
        {
            try
            {
                net::steady_timer timer {co_await net::this_coro::executor};
                while (port == 0)
                {
                    timer.expires_after(1ms);
                    co_await timer.async_wait(taar::use_awaitable);
                }
                auto socket = co_await connect(port);
                beast::flat_buffer buffer;
                for (int r = 1; r <= rounds; ++r)
                {
                    auto const tag = std::to_string(c) + "-" + std::to_string(r);
                    auto echo = e2e::make_request(http::verb::get, "/echo/" + tag);
                    co_await http::async_write(socket, echo);
                    if ((co_await read_response(socket, buffer)).body() != tag) throw std::runtime_error {"echo " + tag};

                    auto body = json::serialize(json::object {{"client", c}, {"round", r}});
                    auto post = e2e::make_request(http::verb::post, "/json", body);
                    co_await http::async_write(socket, post);
                    auto jr = co_await read_response(socket, buffer);
                    if (json::parse(jr.body()) != json::parse(body)) throw std::runtime_error {"json " + tag};

                    auto count = e2e::make_request(http::verb::get, "/count");
                    co_await http::async_write(socket, count);
                    if ((co_await read_response(socket, buffer)).body() != std::to_string(r)) throw std::runtime_error {"count " + tag};
                }
            }
            catch (std::exception const& e)
            {
                errors[c] = e.what();
                ++failures;
            }
            if (++finished == clients) signals.emit();
        }, net::detached);
    }

    std::vector<std::jthread> pool;
    for (int i = 0; i < threads; ++i)
    {
        pool.emplace_back([&] { io.run(); });
    }
    pool.clear();

    BOOST_TEST(finished == clients);
    BOOST_TEST(failures == 0);
    for (auto const& e : errors) BOOST_TEST(e.empty(), e);
}

BOOST_AUTO_TEST_CASE(test_e2e_multithreaded_shutdown_from_foreign_thread)
{
    constexpr int threads = 4;
    constexpr int clients = 16;

    net::io_context io;
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/ping",
        rest([] { return std::string {"pong"}; }));

    taar::cancellation_signals signals;
    std::atomic<unsigned short> port {0};
    net::co_spawn(
        io,
        taar::server::tcp("127.0.0.1", "0", session, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);

    // Clients keep their connections open and idle after one request.
    std::atomic<int> served {0};
    std::vector<std::unique_ptr<socket_type>> sockets(clients);
    for (int c = 0; c < clients; ++c)
    {
        net::co_spawn(io, [&, c]() -> taar::awaitable<void>
        {
            net::steady_timer timer {co_await net::this_coro::executor};
            while (port == 0)
            {
                timer.expires_after(1ms);
                co_await timer.async_wait(taar::use_awaitable);
            }
            sockets[c] = std::make_unique<socket_type>(co_await connect(port));
            beast::flat_buffer buffer;
            co_await http::async_write(*sockets[c], e2e::make_request(http::verb::get, "/ping"));
            if ((co_await read_response(*sockets[c], buffer)).body() == "pong") ++served;
        }, net::detached);
    }

    std::vector<std::jthread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back([&] { io.run(); });

    // The main thread is not running the io_context: emission must still be safe.
    while (served < clients) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(5ms);
    signals.emit();
    pool.clear();

    BOOST_TEST(served == clients);
    BOOST_TEST(io.stopped());
}

} // namespace
