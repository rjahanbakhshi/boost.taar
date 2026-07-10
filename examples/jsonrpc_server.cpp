//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// A JSON-RPC 2.0 calculator on one HTTP route. Methods are plain functions;
// named params are bound by rpc_arg, batches and notifications are handled
// by the endpoint, and a domain exception is mapped to a custom error code.
// The endpoint is wrapped in with_session so each client (identified by an
// X-Session-Id header) keeps its own calculation history.
//
//   ./jsonrpc_server 8080
//   curl -s localhost:8080/rpc -d '{"jsonrpc":"2.0","id":1,"method":"add","params":{"a":2,"b":3}}'
//   curl -s localhost:8080/rpc -d '[{"jsonrpc":"2.0","id":1,"method":"divide","params":{"a":1,"b":0}},
//                                   {"jsonrpc":"2.0","method":"log","params":{"text":"hi"}}]'
//   curl -si localhost:8080/rpc -d '{"jsonrpc":"2.0","id":2,"method":"history"}'      # note X-Session-Id
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/jsonrpc/arg.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;

struct calc_session
{
    std::vector<std::string> history;
};

struct division_by_zero : std::domain_error
{
    using domain_error::domain_error;
};

int add(calc_session& s, int a, int b)
{
    s.history.push_back(std::to_string(a) + " + " + std::to_string(b));
    return a + b;
}

double divide(calc_session& s, double a, double b)
{
    if (b == 0) throw division_by_zero {"division by zero"};
    s.history.push_back(std::to_string(a) + " / " + std::to_string(b));
    return a / b;
}

std::string concat(std::string a, std::optional<std::string> b)
{
    return a + b.value_or("");
}

std::vector<std::string> history(calc_session const& s) { return s.history; }

void log_message(std::string text) { std::clog << "log: " << text << '\n'; }

} // namespace

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::with_session;
    using taar::handler::session_policy;
    using jsonrpc::rpc_arg;

    char const* port = argc > 1 ? argv[1] : "8080";

    jsonrpc::endpoint<calc_session> rpc;
    rpc.method("add", "Add two integers", &add, rpc_arg("a"), rpc_arg("b"));
    rpc.method("divide", "Divide a by b", &divide, rpc_arg("a"), rpc_arg("b"));
    rpc.method("concat", &concat, rpc_arg("a"), rpc_arg("b", "optional suffix"));
    rpc.method("history", "Calculations made in this session", &history);
    rpc.method("log", "Fire-and-forget: send as a notification", &log_message, rpc_arg("text"));

    // Domain exceptions become JSON-RPC errors; everything else keeps the default mapping.
    rpc.set_error_mapper([](std::exception_ptr e) -> jsonrpc::error_info
    {
        try { std::rethrow_exception(e); }
        catch (division_by_zero const& x) { return {-32001, x.what(), std::nullopt}; }
        catch (...) { return jsonrpc::default_error_mapper(e); }
    });

    taar::context_store<std::string, calc_session> sessions {std::chrono::minutes {10}};
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::post && target == "/rpc",
        with_session(sessions, taar::handler::session_key::header {"X-Session-Id"},
            session_policy::create_if_missing, rpc.handler()));

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", port, session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on " << ep << '\n'; }),
        net::bind_cancellation_slot(signals.slot(), taar::ignore_and_rethrow));

    io.run();
}
