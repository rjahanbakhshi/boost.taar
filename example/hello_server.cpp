//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//
// The smallest complete server: two routes, graceful shutdown on Ctrl-C.
//
//   ./hello_server 8080
//   curl localhost:8080/hello
//   curl 'localhost:8080/hello/Ada?times=2'
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
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
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::rest;
    using taar::handler::path_arg;
    using taar::handler::query_arg;
    using taar::handler::with_default;

    char const* port = argc > 1 ? argv[1] : "8080";

    taar::session::http session;

    session.register_request_handler(
        method == http::verb::get && target == "/hello",
        rest([] { return "Hello, world!"; }));

    session.register_request_handler(
        method == http::verb::get && target == "/hello/{name}",
        rest([](std::string name, int times)
        {
            std::string greeting;
            for (int i = 0; i < times; ++i) greeting += "Hello, " + name + "! ";
            return boost::json::value {{"greeting", greeting}, {"times", times}};
        },
        path_arg("name"),
        with_default(query_arg("times"), 1)));

    net::io_context io;
    taar::cancellation_signals signals;

    // Ctrl-C emits the cancellation signals: the acceptor stops, in-flight
    // requests finish, idle connections close, and io.run() returns.
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", port, session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on " << ep << '\n'; }),
        taar::ignore_and_rethrow);

    io.run();
}
