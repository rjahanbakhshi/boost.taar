//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

// Cancellation reaches nested work on every path the library offers:
// server shutdown, client disconnect, JSON-RPC endpoint::cancel and MCP
// notifications/cancelled. Each test checks promptness (the work would
// take seconds, it must stop well under one), cleanup (RAII gauges in the
// innermost frames return to zero), the outcome on the wire, and that
// other requests keep being served.
//
// The elicitation case (a tool suspended in elicit() when its call is
// cancelled) is covered by test_scenario_mcp_elicitation_cancelled_mid_flight
// in test_scenarios.cpp.

#include "e2e.hpp"
#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/jsonrpc/arg.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/jsonrpc/request_scope.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
namespace mcp = boost::taar::mcp;
using namespace std::chrono_literals;
using namespace boost::asio::experimental::awaitable_operators;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;
using clock_type = std::chrono::steady_clock;

/// What the interrupted work would take on its own.
constexpr auto natural = 5s;
/// How soon it must stop once cancelled (generous for a loaded machine).
constexpr auto prompt = 1s;

/// Counts live frames: up in the constructor, down in the destructor.
struct gauge
{
    std::atomic<int>& live;
    explicit gauge(std::atomic<int>& l) : live {l} { ++live; }
    gauge(gauge const&) = delete;
    gauge& operator=(gauge const&) = delete;
    ~gauge() { --live; }
};

// Three levels of co_await down to a timer, each with its own gauge.
taar::awaitable<void> wait_level3(std::atomic<int>& live, clock_type::duration d)
{
    gauge const g {live};
    net::steady_timer timer {co_await net::this_coro::executor};
    timer.expires_after(d);
    co_await timer.async_wait(taar::use_awaitable);
}

taar::awaitable<void> wait_level2(std::atomic<int>& live, clock_type::duration d)
{
    gauge const g {live};
    co_await wait_level3(live, d);
}

taar::awaitable<void> nested_wait(std::atomic<int>& live, clock_type::duration d)
{
    gauge const g {live};
    co_await wait_level2(live, d);
}

/// Polls @a done every few milliseconds until it holds or @a limit passes.
taar::awaitable<bool> eventually(std::function<bool()> done, clock_type::duration limit = 2s)
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

/// Runs @a session behind a server; @a client gets the port and the signals.
template <typename Session, typename Client>
void run_server(net::io_context& io, Session& session, Client client)
{
    taar::cancellation_signals signals;
    std::atomic<unsigned short> port {0};
    std::exception_ptr client_error;
    net::co_spawn(io,
        taar::server::tcp("127.0.0.1", "0", session, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);
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

using stream_type = beast::tcp_stream;
using parser_type = http::response_parser<http::string_body>;

/// One client connection reading a response piece by piece; every
/// operation is bounded so that a stream that never ends fails the test
/// instead of hanging it.
struct streaming_client
{
    stream_type stream;
    beast::flat_buffer buffer;
    parser_type parser;

    explicit streaming_client(net::any_io_executor ex) : stream {ex}
    {
        parser.body_limit(boost::none);
    }

    taar::awaitable<void> open(unsigned short port, http::verb verb, std::string path, std::string body = {})
    {
        stream.expires_after(3s);
        co_await stream.async_connect(net::ip::tcp::endpoint {net::ip::address_v4::loopback(), port}, taar::use_awaitable);
        http::request<http::string_body> req {verb, path, 11};
        req.set(http::field::host, "localhost");
        if (!body.empty())
        {
            req.set(http::field::content_type, "application/json");
            req.body() = std::move(body);
        }
        req.prepare_payload();
        stream.expires_after(3s);
        co_await http::async_write(stream, req, taar::use_awaitable);
    }

    /// Reads until the body holds @a n bytes; false on error.
    taar::awaitable<bool> read_at_least(std::size_t n)
    {
        stream.expires_after(3s);
        if (!parser.is_header_done())
        {
            auto [ec, sz] = co_await http::async_read_header(stream, buffer, parser, net::as_tuple(taar::use_awaitable));
            if (ec) co_return false;
        }
        while (parser.get().body().size() < n && !parser.is_done())
        {
            auto [ec, sz] = co_await http::async_read_some(stream, buffer, parser, net::as_tuple(taar::use_awaitable));
            if (ec) co_return false;
        }
        co_return parser.get().body().size() >= n;
    }

    /// Reads to the end of the response or of the connection; returns the error (none if the response completed).
    taar::awaitable<boost::system::error_code> read_to_end(clock_type::duration limit = 3s)
    {
        stream.expires_after(limit);
        boost::system::error_code ec;
        if (!parser.is_header_done())
        {
            auto [hec, sz] = co_await http::async_read_header(stream, buffer, parser, net::as_tuple(taar::use_awaitable));
            if (hec) co_return hec;
        }
        while (!parser.is_done())
        {
            auto [rec, sz] = co_await http::async_read_some(stream, buffer, parser, net::as_tuple(taar::use_awaitable));
            if (rec) co_return rec;
        }
        co_return ec;
    }
};

// ---- A. server shutdown -----------------------------------------------------

struct shutdown_gauges
{
    std::atomic<int> timer {0};
    std::atomic<int> helper {0};
    std::atomic<int> splice {0};
    std::atomic<int> generator {0};
    std::atomic<int> either {0};
    std::atomic<int> poll {0};
    std::atomic<int> plain {0};
    std::atomic<int> poll_steps {-1};
};

taar::chunked_response<std::string> spliced_inner(std::atomic<int>& live)
{
    gauge const g {live};
    co_yield std::string {"inner;"};
    co_await nested_wait(live, natural);          // suspended here when cancelled
    co_yield std::string {"inner-late;"};
}

void register_shutdown_routes(taar::session::http& session, shutdown_gauges& g)
{
    // A timer between chunks.
    session.register_request_handler(method == http::verb::get && target == "/chunk-timer",
        rest([&g]() -> taar::chunked_response<std::string>
        {
            gauge const frame {g.timer};
            net::steady_timer timer {co_await net::this_coro::executor};
            for (;;)
            {
                co_yield std::string {"tick;"};
                timer.expires_after(natural);
                co_await timer.async_wait(taar::use_awaitable);
            }
        }));
    // A three-level helper between chunks.
    session.register_request_handler(method == http::verb::get && target == "/chunk-helper",
        rest([&g]() -> taar::chunked_response<std::string>
        {
            gauge const frame {g.helper};
            for (;;)
            {
                co_yield std::string {"tick;"};
                co_await nested_wait(g.helper, natural);
            }
        }));
    // A spliced inner stream is the one suspended; the outer would wait again after it.
    session.register_request_handler(method == http::verb::get && target == "/chunk-splice",
        rest([&g]() -> taar::chunked_response<std::string>
        {
            gauge const frame {g.splice};
            co_yield std::string {"outer;"};
            co_yield spliced_inner(g.splice);
            co_await nested_wait(g.splice, natural);
            co_yield std::string {"outer-late;"};
        }));
    // The same shape as an async_generator.
    session.register_request_handler(method == http::verb::get && target == "/generator",
        rest([&g]() -> taar::async_generator<std::string>
        {
            gauge const frame {g.generator};
            for (;;)
            {
                co_yield std::string {"tick;"};
                co_await nested_wait(g.generator, natural);
            }
        }));
    // Two concurrent nested waits, whichever finishes first.
    session.register_request_handler(method == http::verb::get && target == "/chunk-either",
        rest([&g]() -> taar::chunked_response<std::string>
        {
            gauge const frame {g.either};
            for (;;)
            {
                co_yield std::string {"tick;"};
                co_await (nested_wait(g.either, natural) || nested_wait(g.either, natural));
            }
        }));
    // Explicit checks between short steps; a step reports, never throws.
    session.register_request_handler(method == http::verb::get && target == "/poll",
        rest([&g]() -> taar::chunked_response<std::string>
        {
            gauge const frame {g.poll};
            co_yield std::string {"start;"};
            net::steady_timer timer {co_await net::this_coro::executor};
            int steps = 0;
            for (; steps < 500; ++steps)      // 500 x 10 ms = the natural duration
            {
                auto const cs = co_await net::this_coro::cancellation_state;
                if (cs.cancelled() != net::cancellation_type::none) break;
                timer.expires_after(10ms);
                co_await timer.async_wait(net::as_tuple(taar::use_awaitable));
            }
            g.poll_steps = steps;
        }));
    // A plain handler is shielded: it finishes and its answer closes the connection.
    session.register_request_handler(method == http::verb::get && target == "/plain",
        rest([&g]() -> taar::awaitable<std::string>
        {
            co_await nested_wait(g.plain, 300ms);
            co_return std::string {"done"};
        }));
}

BOOST_AUTO_TEST_CASE(test_cancellation_shutdown_stops_nested_stream_work_and_spares_plain_handlers)
{
    net::io_context io;
    taar::session::http session;
    std::atomic<int> hard_errors {0};
    std::string hard_error_text;
    session.set_hard_error_handler([&](taar::failure const& f) { ++hard_errors; hard_error_text = f.chain(); });
    shutdown_gauges g;
    register_shutdown_routes(session, g);

    run_server(io, session, [&](unsigned short port, taar::cancellation_signals& signals) -> taar::awaitable<void>
    {
        auto ex = co_await net::this_coro::executor;
        std::vector<std::string> const streams {"/chunk-timer", "/chunk-helper", "/chunk-splice", "/generator", "/chunk-either", "/poll"};
        std::vector<std::unique_ptr<streaming_client>> clients;
        for (auto const& path : streams)
        {
            clients.push_back(std::make_unique<streaming_client>(ex));
            co_await clients.back()->open(port, http::verb::get, path);
            BOOST_TEST_CONTEXT(path)
            {
                BOOST_TEST(co_await clients.back()->read_at_least(1));
            }
        }
        // The splice is suspended in its inner stream, under its nested helper.
        BOOST_TEST(co_await eventually([&] { return g.splice == 5; }));
        BOOST_TEST(g.splice.load() == 5);
        BOOST_TEST(co_await eventually([&] { return g.either == 7; }));
        streaming_client plain {ex};
        co_await plain.open(port, http::verb::get, "/plain");
        BOOST_TEST(co_await eventually([&] { return g.plain == 3; }));

        auto const emitted = clock_type::now();
        signals.emit();

        for (std::size_t i = 0; i < clients.size(); ++i)
        {
            BOOST_TEST_CONTEXT(streams[i])
            {
                auto const ec = co_await clients[i]->read_to_end();
                BOOST_TEST(ec);                       // the connection ended mid-stream
                BOOST_TEST((ec != beast::error::timeout));
            }
        }
        BOOST_TEST((clock_type::now() - emitted < prompt));
        BOOST_TEST(co_await eventually([&]
        {
            return g.timer == 0 && g.helper == 0 && g.splice == 0 && g.generator == 0 && g.either == 0 && g.poll == 0;
        }, prompt));
        BOOST_TEST(g.timer.load() == 0);
        BOOST_TEST(g.helper.load() == 0);
        BOOST_TEST(g.splice.load() == 0);
        BOOST_TEST(g.generator.load() == 0);
        BOOST_TEST(g.either.load() == 0);
        BOOST_TEST(g.poll.load() == 0);
        BOOST_TEST(g.poll_steps.load() >= 0);
        BOOST_TEST(g.poll_steps.load() < 500);

        // The shielded plain handler ran to completion and announced the close.
        auto const ec = co_await plain.read_to_end();
        BOOST_TEST(!ec);
        BOOST_TEST(plain.parser.get().result() == http::status::ok);
        BOOST_TEST(plain.parser.get().body() == "done");
        BOOST_TEST(!plain.parser.get().keep_alive());
        BOOST_TEST(g.plain.load() == 0);
    });
    BOOST_TEST(hard_errors.load() == 0, "hard error: " << hard_error_text);
}

BOOST_AUTO_TEST_CASE(test_cancellation_shutdown_from_foreign_thread_multithreaded)
{
    constexpr int threads = 4;
    net::io_context io;
    taar::session::http session;
    std::atomic<int> hard_errors {0};
    std::mutex hard_error_mutex;
    std::string hard_error_text;
    session.set_hard_error_handler([&](taar::failure const& f)
    {
        std::lock_guard<std::mutex> const lock {hard_error_mutex};
        ++hard_errors;
        hard_error_text = f.chain();
    });
    std::atomic<int> live {0};
    session.register_request_handler(method == http::verb::get && target == "/stream",
        rest([&live]() -> taar::chunked_response<std::string>
        {
            gauge const frame {live};
            for (;;)
            {
                co_yield std::string {"tick;"};
                co_await (nested_wait(live, natural) && nested_wait(live, natural));
            }
        }));

    taar::cancellation_signals signals;
    std::atomic<unsigned short> port {0};
    net::co_spawn(io,
        taar::server::tcp("127.0.0.1", "0", session, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);

    constexpr int streams = 4;
    std::atomic<int> streaming {0};
    std::atomic<int> ended {0};
    std::atomic<int> failures {0};
    for (int i = 0; i < streams; ++i)
    {
        net::co_spawn(io, [&]() -> taar::awaitable<void>
        {
            auto ex = co_await net::this_coro::executor;
            net::steady_timer timer {ex};
            while (port == 0)
            {
                timer.expires_after(1ms);
                co_await timer.async_wait(taar::use_awaitable);
            }
            streaming_client client {ex};
            co_await client.open(port, http::verb::get, "/stream");
            if (!co_await client.read_at_least(1)) ++failures;
            ++streaming;
            auto const ec = co_await client.read_to_end();
            if (!ec || ec == beast::error::timeout) ++failures;
            ++ended;
        }, net::detached);
    }

    std::vector<std::jthread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back([&] { io.run(); });

    auto const deadline = clock_type::now() + 3s;
    while (streaming < streams && clock_type::now() < deadline) std::this_thread::sleep_for(1ms);
    // Every stream is suspended in both branches of its nested wait.
    while (live < streams * 7 && clock_type::now() < deadline) std::this_thread::sleep_for(1ms);
    BOOST_TEST(live.load() == streams * 7);

    // Emitted from a thread that does not run the io_context.
    auto const emitted = clock_type::now();
    signals.emit();
    pool.clear();
    auto const elapsed = clock_type::now() - emitted;

    BOOST_TEST(io.stopped());
    BOOST_TEST((elapsed < prompt));
    BOOST_TEST(ended.load() == streams);
    BOOST_TEST(failures.load() == 0);
    BOOST_TEST(live.load() == 0);
    BOOST_TEST(hard_errors.load() == 0, "hard error: " << hard_error_text);
}

// ---- B. client disconnect mid-stream -------------------------------------------

BOOST_AUTO_TEST_CASE(test_cancellation_client_disconnect_stops_nested_stream_work)
{
    // 250 chunks, each behind a nested 20 ms wait: five seconds of streaming.
    // The server notices the disconnect at its next write.
    constexpr int chunks = 250;
    constexpr auto step = 20ms;

    net::io_context io;
    taar::session::http session;
    std::atomic<int> hard_errors {0};
    std::string hard_error_text;
    session.set_hard_error_handler([&](taar::failure const& f) { ++hard_errors; hard_error_text = f.chain(); });

    std::atomic<int> timer_live {0};
    std::atomic<int> helper_live {0};
    std::atomic<int> splice_live {0};
    std::atomic<int> generator_live {0};

    session.register_request_handler(method == http::verb::get && target == "/chunk-timer",
        rest([&]() -> taar::chunked_response<std::string>
        {
            gauge const frame {timer_live};
            net::steady_timer timer {co_await net::this_coro::executor};
            for (int i = 0; i < chunks; ++i)
            {
                co_yield std::string {"tick;"};
                timer.expires_after(step);
                co_await timer.async_wait(taar::use_awaitable);
            }
        }));
    session.register_request_handler(method == http::verb::get && target == "/chunk-helper",
        rest([&]() -> taar::chunked_response<std::string>
        {
            gauge const frame {helper_live};
            for (int i = 0; i < chunks; ++i)
            {
                co_yield std::string {"tick;"};
                co_await nested_wait(helper_live, step);
            }
        }));
    session.register_request_handler(method == http::verb::get && target == "/chunk-splice",
        rest([&]() -> taar::chunked_response<std::string>
        {
            gauge const frame {splice_live};
            co_yield std::string {"outer;"};
            co_yield [step](std::atomic<int>& live) -> taar::chunked_response<std::string>
            {
                gauge const inner {live};
                for (int i = 0; i < chunks; ++i)
                {
                    co_yield std::string {"inner;"};
                    co_await nested_wait(live, step);
                }
            }(splice_live);
            co_yield std::string {"outer-end;"};
        }));
    session.register_request_handler(method == http::verb::get && target == "/generator",
        rest([&]() -> taar::async_generator<std::string>
        {
            gauge const frame {generator_live};
            for (int i = 0; i < chunks; ++i)
            {
                co_yield std::string {"tick;"};
                co_await nested_wait(generator_live, step);
            }
        }));
    session.register_request_handler(method == http::verb::get && target == "/ping",
        rest([] { return std::string {"pong"}; }));

    struct route { std::string path; std::atomic<int>* live; };
    std::vector<route> const routes {
        {"/chunk-timer", &timer_live}, {"/chunk-helper", &helper_live},
        {"/chunk-splice", &splice_live}, {"/generator", &generator_live}};

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto ex = co_await net::this_coro::executor;
        for (auto const& r : routes)
        {
            BOOST_TEST_CONTEXT(r.path)
            {
                streaming_client client {ex};
                co_await client.open(port, http::verb::get, r.path);
                BOOST_TEST(co_await client.read_at_least(30));
                BOOST_TEST(r.live->load() > 0);

                auto const closed = clock_type::now();
                boost::system::error_code ignored;
                client.stream.socket().shutdown(net::ip::tcp::socket::shutdown_both, ignored);
                client.stream.close();

                BOOST_TEST(co_await eventually([&] { return r.live->load() == 0; }, prompt));
                BOOST_TEST(r.live->load() == 0);
                BOOST_TEST((clock_type::now() - closed < prompt));
            }
        }
        // Other connections are served as before.
        auto other = co_await e2e::send_all(port, e2e::make_request(http::verb::get, "/ping"));
        BOOST_TEST(other[0].body() == "pong");
    });
    BOOST_TEST(hard_errors.load() == 0, "hard error: " << hard_error_text);
}

// ---- C. JSON-RPC endpoint::cancel --------------------------------------------------

struct rpc_outcome
{
    json::value body;
    clock_type::duration elapsed {};
};

/// Sends @a call on one connection and, once @a ready holds, asks the
/// server to cancel request @a id on another.
taar::awaitable<rpc_outcome> call_then_cancel(
    unsigned short port, std::string call, int id, std::function<bool()> ready)
{
    auto ex = co_await net::this_coro::executor;
    streaming_client slow {ex};
    co_await slow.open(port, http::verb::post, "/rpc", std::move(call));
    auto const started = clock_type::now();
    BOOST_TEST(co_await eventually(std::move(ready)));

    auto cancelled = co_await e2e::send_all(port, e2e::make_request(http::verb::post, "/rpc",
        R"({"jsonrpc":"2.0","id":100,"method":"cancel","params":{"id":)" + std::to_string(id) + "}}"));
    BOOST_TEST(json::parse(cancelled[0].body()).at("result").as_int64() == 1);

    auto const ec = co_await slow.read_to_end();
    BOOST_TEST(!ec);
    rpc_outcome out;
    out.elapsed = clock_type::now() - started;
    out.body = json::parse(slow.parser.get().body());
    co_return out;
}

BOOST_AUTO_TEST_CASE(test_cancellation_jsonrpc_cancel_interrupts_nested_and_concurrent_work)
{
    net::io_context io;
    taar::session::http session;
    std::atomic<int> nested_live {0};
    std::atomic<int> both_live {0};
    std::atomic<int> either_live {0};

    jsonrpc::endpoint<> rpc;
    rpc.method("nested", [&](jsonrpc::request_scope const&) -> taar::awaitable<std::string>
    {
        co_await nested_wait(nested_live, natural);
        co_return "done";
    });
    rpc.method("both", [&](jsonrpc::request_scope const&) -> taar::awaitable<std::string>
    {
        co_await (nested_wait(both_live, natural) && nested_wait(both_live, natural));
        co_return "done";
    });
    rpc.method("either", [&](jsonrpc::request_scope const&) -> taar::awaitable<std::string>
    {
        co_await (nested_wait(either_live, natural) || nested_wait(either_live, natural));
        co_return "done";
    });
    rpc.method("cancel", [&](int id) { return rpc.cancel(json::value {id}); }, jsonrpc::rpc_arg("id"));
    rpc.method("ping", [] { return std::string {"pong"}; });
    session.register_request_handler(method == http::verb::post && target == "/rpc", rpc.handler());

    struct shape { std::string name; std::atomic<int>* live; int suspended; };
    std::vector<shape> const shapes {{"nested", &nested_live, 3}, {"both", &both_live, 6}, {"either", &either_live, 6}};

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        int id = 1;
        for (auto const& s : shapes)
        {
            BOOST_TEST_CONTEXT(s.name)
            {
                auto const out = co_await call_then_cancel(port,
                    R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"method":")" + s.name + R"("})",
                    id, [&] { return s.live->load() == s.suspended; });
                BOOST_TEST((out.elapsed < prompt));
                BOOST_TEST(out.body.at("id").as_int64() == id);
                BOOST_TEST(out.body.at("error").at("code").as_int64() == jsonrpc::request_cancelled_code);
                BOOST_TEST(co_await eventually([&] { return s.live->load() == 0; }, prompt));
                BOOST_TEST(s.live->load() == 0);
            }
            ++id;
        }
        auto pong = co_await e2e::send_all(port, e2e::make_request(http::verb::post, "/rpc",
            R"({"jsonrpc":"2.0","id":50,"method":"ping"})"));
        BOOST_TEST(json::parse(pong[0].body()).at("result").as_string() == "pong");
    });
}

BOOST_AUTO_TEST_CASE(test_cancellation_jsonrpc_cancel_stops_explicit_check_loops)
{
    net::io_context io;
    taar::session::http session;
    std::atomic<int> state_steps {-1};
    std::atomic<int> scope_steps {-1};
    std::atomic<bool> state_running {false};
    std::atomic<bool> scope_running {false};

    jsonrpc::endpoint<> rpc;
    // Polls the coroutine's cancellation state between short steps.
    rpc.method("poll_state", [&](jsonrpc::request_scope const&) -> taar::awaitable<int>
    {
        co_await net::this_coro::throw_if_cancelled(false);
        net::steady_timer timer {co_await net::this_coro::executor};
        int steps = 0;
        for (; steps < 500; ++steps)
        {
            auto const cs = co_await net::this_coro::cancellation_state;
            if (cs.cancelled() != net::cancellation_type::none) break;
            state_running = true;
            timer.expires_after(10ms);
            co_await timer.async_wait(net::as_tuple(taar::use_awaitable));
        }
        state_steps = steps;
        co_return steps;
    });
    // Polls request_scope::cancelled() between steps that only yield.
    rpc.method("poll_scope", [&](jsonrpc::request_scope const& scope) -> taar::awaitable<int>
    {
        co_await net::this_coro::throw_if_cancelled(false);
        auto ex = co_await net::this_coro::executor;
        net::steady_timer timer {ex};
        int steps = 0;
        for (; steps < 500; ++steps)
        {
            if (scope.cancelled()) break;
            scope_running = true;
            co_await net::post(ex, net::as_tuple(taar::use_awaitable));
            timer.expires_after(10ms);
            co_await timer.async_wait(net::as_tuple(taar::use_awaitable));
        }
        scope_steps = steps;
        co_return steps;
    });
    rpc.method("cancel", [&](int id) { return rpc.cancel(json::value {id}); }, jsonrpc::rpc_arg("id"));
    session.register_request_handler(method == http::verb::post && target == "/rpc", rpc.handler());

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto a = co_await call_then_cancel(port, R"({"jsonrpc":"2.0","id":1,"method":"poll_state"})", 1,
            [&] { return state_running.load(); });
        BOOST_TEST((a.elapsed < prompt));
        BOOST_TEST(a.body.at("error").at("code").as_int64() == jsonrpc::request_cancelled_code);
        BOOST_TEST(state_steps.load() >= 1);
        BOOST_TEST(state_steps.load() < 500);

        auto b = co_await call_then_cancel(port, R"({"jsonrpc":"2.0","id":2,"method":"poll_scope"})", 2,
            [&] { return scope_running.load(); });
        BOOST_TEST((b.elapsed < prompt));
        BOOST_TEST(b.body.at("error").at("code").as_int64() == jsonrpc::request_cancelled_code);
        BOOST_TEST(scope_steps.load() >= 1);
        BOOST_TEST(scope_steps.load() < 500);
    });
}

// ---- D. MCP notifications/cancelled ----------------------------------------------

struct agent { int calls = 0; };

BOOST_AUTO_TEST_CASE(test_cancellation_mcp_notification_over_http)
{
    net::io_context io;
    taar::session::http session;
    std::atomic<int> live {0};
    mcp::endpoint<agent> endpoint {{.name = "s", .version = "1"}};
    endpoint.tool("nested", "Nested wait", [&](agent& a, jsonrpc::request_scope const&) -> taar::awaitable<std::string>
    {
        ++a.calls;
        co_await nested_wait(live, natural);
        co_return "done";
    });
    endpoint.tool("calls", "Calls so far", [](agent const& a) { return a.calls; });
    session.register_request_handler(target == "/mcp", endpoint.handler());

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto ex = co_await net::this_coro::executor;
        auto init = co_await e2e::send_all(port, e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})"));
        std::string const sid {init[0]["Mcp-Session-Id"]};
        BOOST_TEST(!sid.empty());

        streaming_client slow {ex};
        slow.stream.expires_after(3s);
        co_await slow.stream.async_connect(net::ip::tcp::endpoint {net::ip::address_v4::loopback(), port}, taar::use_awaitable);
        auto call = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"nested"}})");
        call.set("Mcp-Session-Id", sid);
        co_await http::async_write(slow.stream, call, taar::use_awaitable);
        auto const started = clock_type::now();
        BOOST_TEST(co_await eventually([&] { return live == 3; }));

        auto cancel = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":9,"reason":"user"}})");
        cancel.set("Mcp-Session-Id", sid);
        auto ack = co_await e2e::send_all(port, cancel);
        BOOST_TEST(ack[0].result() == http::status::accepted);

        BOOST_TEST(!co_await slow.read_to_end());
        BOOST_TEST((clock_type::now() - started < prompt));
        BOOST_TEST(slow.parser.get().result() == http::status::accepted);   // no response envelope
        BOOST_TEST(slow.parser.get().body().empty());
        BOOST_TEST(co_await eventually([&] { return live == 0; }, prompt));
        BOOST_TEST(live.load() == 0);

        // The session goes on.
        auto next = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"calls"}})");
        next.set("Mcp-Session-Id", sid);
        auto r = co_await e2e::send_all(port, next);
        BOOST_TEST(json::parse(r[0].body()).at("result").at("structuredContent").at("result").as_int64() == 1);
    });
}

#if defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)

} // namespace

#include <boost/asio/posix/stream_descriptor.hpp>
#include <unistd.h>

namespace {

BOOST_AUTO_TEST_CASE(test_cancellation_mcp_notification_over_stdio)
{
    net::io_context io;
    std::atomic<int> live {0};
    mcp::endpoint<agent> server {{.name = "s", .version = "1"}};
    server.tool("nested", "Nested wait", [&](jsonrpc::request_scope const&) -> taar::awaitable<std::string>
    {
        co_await nested_wait(live, natural);
        co_return "done";
    });

    std::signal(SIGPIPE, SIG_IGN);
    int in_pipe[2] {};
    int out_pipe[2] {};
    BOOST_REQUIRE(::pipe(in_pipe) == 0);
    BOOST_REQUIRE(::pipe(out_pipe) == 0);
    bool finished = false;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        co_await server.serve_stdio(in_pipe[0], out_pipe[1], server.default_incoming());
        finished = true;
    }, net::detached);

    std::exception_ptr client_error;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        auto ex = co_await net::this_coro::executor;
        net::posix::stream_descriptor in {ex, in_pipe[1]};
        net::posix::stream_descriptor out {ex, out_pipe[0]};
        try
        {
            std::string pending;
            auto send = [&](std::string line) -> taar::awaitable<void>
            {
                line += '\n';
                co_await net::async_write(in, net::buffer(line), taar::use_awaitable);
            };
            auto receive = [&]() -> taar::awaitable<json::value>
            {
                auto n = co_await net::async_read_until(out, net::dynamic_buffer(pending), '\n', taar::use_awaitable);
                auto line = pending.substr(0, n);
                pending.erase(0, n);
                co_return json::parse(line);
            };

            co_await send(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
            co_await receive();
            co_await send(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"nested"}})");
            auto const started = clock_type::now();
            BOOST_TEST(co_await eventually([&] { return live == 3; }));
            co_await send(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":2}})");
            BOOST_TEST(co_await eventually([&] { return live == 0; }, prompt));
            BOOST_TEST((clock_type::now() - started < prompt));
            // No reply for the cancelled request: the next line answers the ping.
            co_await send(R"({"jsonrpc":"2.0","id":3,"method":"ping"})");
            auto pong = co_await receive();
            BOOST_TEST(pong.at("id").as_int64() == 3);
        }
        catch (...)
        {
            client_error = std::current_exception();
        }
        in.close();
    }, net::detached);

    io.run();
    if (client_error) std::rethrow_exception(client_error);
    BOOST_TEST(finished);
    BOOST_TEST(live.load() == 0);
}

#endif

// ---- streaming request body on shutdown ------------------------------------------

BOOST_AUTO_TEST_CASE(test_cancellation_streaming_body_read_is_shielded_on_shutdown)
{
    // A handler reading a streamed body is a plain handler: shutdown does not
    // interrupt its reads, it finishes once the body is in, and its answer
    // closes the connection.
    net::io_context io;
    taar::session::http session;
    std::atomic<std::uint64_t> received {0};
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
        auto ex = co_await net::this_coro::executor;
        streaming_client client {ex};
        client.stream.expires_after(3s);
        co_await client.stream.async_connect(net::ip::tcp::endpoint {net::ip::address_v4::loopback(), port}, taar::use_awaitable);
        std::string const head =
            "POST /upload HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Type: application/octet-stream\r\nContent-Length: 2000\r\n\r\n";
        co_await net::async_write(client.stream, net::buffer(head + std::string(1000, 'a')), taar::use_awaitable);
        BOOST_TEST(co_await eventually([&] { return received == 1000; }));

        signals.emit();
        net::steady_timer pause {ex, 100ms};
        co_await pause.async_wait(taar::use_awaitable);
        BOOST_TEST(received.load() == 1000u);       // still waiting for the rest, not aborted

        co_await net::async_write(client.stream, net::buffer(std::string(1000, 'b')), taar::use_awaitable);
        BOOST_TEST(!co_await client.read_to_end());
        BOOST_TEST(client.parser.get().result() == http::status::ok);
        BOOST_TEST(client.parser.get().body() == "2000");
        BOOST_TEST(!client.parser.get().keep_alive());
    });
}

} // namespace
