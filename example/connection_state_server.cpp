//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//
// Per-connection state. basic_http<conn_state> creates one conn_state when a
// connection is accepted and destroys it when the connection closes; every
// handler that declares a leading `conn_state&` parameter receives it.
// Here it is used for request counting and a simple per-connection quota.
//
//   ./connection_state_server 8080
//   curl -v localhost:8080/stats localhost:8080/stats     # one keep-alive connection: 1, 2
//   for i in $(seq 7); do curl -s -o /dev/null -w '%{http_code}\n' localhost:8080/limited; done
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/core/response_builder.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/json/value.hpp>
#include <atomic>
#include <chrono>
#include <iostream>

namespace {

std::atomic<int> live_connections {0};

struct conn_state
{
    int requests = 0;
    std::chrono::steady_clock::time_point connected = std::chrono::steady_clock::now();

    conn_state() { ++live_connections; }
    ~conn_state() { --live_connections; }
};

} // namespace

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::rest;

    char const* port = argc > 1 ? argv[1] : "8080";

    taar::session::basic_http<conn_state> session;

    session.register_request_handler(
        method == http::verb::get && target == "/stats",
        rest([](conn_state& c)
        {
            ++c.requests;
            auto const up = std::chrono::steady_clock::now() - c.connected;
            return boost::json::value {
                {"requests_on_this_connection", c.requests},
                {"connection_age_ms", std::chrono::duration_cast<std::chrono::milliseconds>(up).count()},
                {"live_connections", live_connections.load()}};
        }));

    session.register_request_handler(
        method == http::verb::get && target == "/limited",
        rest([](conn_state& c)
        {
            if (++c.requests > 5)
            {
                return taar::response_builder(boost::json::value {{"error", "quota exceeded on this connection"}})
                    .set_status(http::status::too_many_requests);
            }
            return taar::response_builder(boost::json::value {{"remaining", 5 - c.requests}});
        }));

    // Handlers without a context parameter work unchanged.
    session.register_request_handler(
        method == http::verb::get && target == "/ping",
        rest([] { return "pong"; }));

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", port, session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on " << ep << '\n'; }),
        taar::ignore_and_rethrow);

    io.run();
}
