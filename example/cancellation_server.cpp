//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//
// The cancellation model end to end. Every route awaits nested helpers
// whose RAII guards log when a frame unwinds before finishing, so the
// server's output shows what was interrupted and that it was cleaned up.
//
//   ./cancellation_server 8080
//
// A stream stops when its client goes away (noticed at the next write) or
// when the server shuts down:
//   curl -N localhost:8080/countdown                 # Ctrl-C after a few events
//
// A JSON-RPC job checks its request_scope between steps; a second request
// cancels it by id, and the job's answer is a -32800 error:
//   curl -s localhost:8080/rpc -d '{"jsonrpc":"2.0","id":7,"method":"job.run","params":{"steps":30}}' &
//   curl -s localhost:8080/rpc -d '{"jsonrpc":"2.0","id":1,"method":"job.cancel","params":{"id":7}}'
//
// An MCP tool doing nested work with progress, cancelled by
// notifications/cancelled (the cancelled call is answered 202, no body):
//   curl -si localhost:8080/mcp -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}}'
//   curl -s localhost:8080/mcp -H 'Mcp-Session-Id: <sid>' \
//     -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"work","arguments":{"steps":30},"_meta":{"progressToken":"p"}}}' &
//   curl -s localhost:8080/mcp -H 'Mcp-Session-Id: <sid>' \
//     -d '{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":2}}'
//
// On SIGINT, a plain handler in flight finishes (it is shielded) and its
// answer closes the connection, while open streams stop at once:
//   curl -si localhost:8080/slow & curl -N localhost:8080/countdown & sleep 1; kill -INT <server pid>
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/handler/rest.hpp>
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
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
namespace mcp = boost::taar::mcp;
using namespace std::chrono_literals;

std::mutex log_mutex;

void log(std::string const& line)
{
    std::lock_guard<std::mutex> const lock {log_mutex};
    std::clog << line << '\n';
}

/// Logs if the frame that owns it unwinds before marking itself finished.
struct unwind_logger
{
    std::string what;
    bool finished = false;
    ~unwind_logger()
    {
        if (!finished) log("  interrupted, cleaned up: " + what);
    }
};

/// Per-MCP-session state (the endpoint is stateful so that a client can
/// address its own calls by id in notifications/cancelled).
struct agent_session
{
};

/// The innermost level: a timer, interrupted by cancellation.
taar::awaitable<void> wait_a_while(std::string const& who, std::chrono::milliseconds d)
{
    unwind_logger guard {who + " / timer"};
    net::steady_timer timer {co_await net::this_coro::executor};
    timer.expires_after(d);
    co_await timer.async_wait(taar::use_awaitable);
    guard.finished = true;
}

/// A helper awaiting another helper: cancellation reaches the timer two levels down.
taar::awaitable<void> one_step(std::string const& who, std::chrono::milliseconds d)
{
    unwind_logger guard {who + " / step"};
    co_await wait_a_while(who, d);
    guard.finished = true;
}

} // namespace

int main(int argc, char* argv[])
{
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::rest;
    using jsonrpc::rpc_arg;
    using mcp::mcp_arg;

    char const* port = argc > 1 ? argv[1] : "8080";

    taar::session::http session;
    session.set_hard_error_handler([](taar::failure const& f) { log("hard error: " + f.chain()); });

    // ---- streaming: stops on disconnect and on shutdown --------------------------
    session.register_request_handler(
        method == http::verb::get && target == "/countdown",
        rest([]() -> taar::chunked_response<std::string>
        {
            unwind_logger guard {"countdown stream"};
            co_yield taar::set_header(http::field::content_type, "text/event-stream");
            for (int i = 10; i > 0; --i)
            {
                co_yield "data: " + std::to_string(i) + "\n\n";
                co_await one_step("countdown", 1s);
            }
            co_yield std::string {"data: liftoff\n\n"};
            guard.finished = true;
        }));

    // ---- plain: shielded from shutdown, finishes and closes ---------------------
    session.register_request_handler(
        method == http::verb::get && target == "/slow",
        rest([]() -> taar::awaitable<std::string>
        {
            co_await one_step("slow plain handler", 3s);
            log("slow plain handler answered");
            co_return std::string {"finished\n"};
        }));

    // ---- JSON-RPC: request_scope and endpoint::cancel ----------------------------
    jsonrpc::endpoint<> rpc;
    rpc.method("job.run", [](jsonrpc::request_scope const& scope, int steps) -> taar::awaitable<int>
    {
        unwind_logger guard {"job " + json::serialize(scope.id())};
        int done = 0;
        for (; done < steps && !scope.cancelled(); ++done)
        {
            co_await one_step("job", 500ms);
            // A no-op over plain JSON-RPC (no progress token, no stream); an
            // MCP client that asked for progress receives it.
            scope.report_progress(done + 1, steps);
        }
        // Stopped at a check: not an unwind, but the endpoint answers -32800.
        if (scope.cancelled()) log("  job " + json::serialize(scope.id()) + " stopped after " + std::to_string(done) + " steps");
        guard.finished = true;
        co_return done;
    }, rpc_arg("steps"));
    rpc.method("job.cancel", [&rpc](json::value id) { return rpc.cancel(id); }, rpc_arg("id"));
    session.register_request_handler(method == http::verb::post && target == "/rpc", rpc.handler());

    // ---- MCP: notifications/cancelled interrupts a tool ---------------------------
    mcp::endpoint<agent_session> agent {{.name = "cancellation-demo", .version = "1.0.0"}};
    agent.tool("work", "Nested work with progress; cancellable",
        [](jsonrpc::request_scope const& scope, int steps) -> taar::awaitable<std::string>
        {
            unwind_logger guard {"tool call " + json::serialize(scope.id())};
            for (int i = 0; i < steps; ++i)
            {
                co_await one_step("tool", 500ms);
                scope.report_progress(i + 1, steps, "step " + std::to_string(i + 1));
            }
            guard.finished = true;
            co_return "all " + std::to_string(steps) + " steps done";
        }, mcp_arg("steps"));
    session.register_request_handler(method == http::verb::get && target == "/mcp", agent.stream_handler());
    session.register_request_handler(target == "/mcp", agent.handler());

    // ---- run on every core, stop on SIGINT/SIGTERM ------------------------------
    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto)
    {
        log("shutting down: streams stop, plain handlers finish");
        signals.emit();
    });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", port, session, signals,
            [](net::ip::tcp::endpoint const& endpoint) { log("cancellation demo on port " + std::to_string(endpoint.port())); }),
        taar::ignore_and_rethrow);

    std::vector<std::thread> threads;
    for (unsigned i = 1; i < std::max(2u, std::thread::hardware_concurrency()); ++i) threads.emplace_back([&] { io.run(); });
    io.run();
    for (auto& t : threads) t.join();
    return 0;
}
