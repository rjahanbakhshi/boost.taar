//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// Static file server. handler::htdocs maps the greedy {*path} capture onto a
// document root, infers content types, and serves index.html for directories.
//
//   ./htdocs_server 8080 ./doc
//   curl localhost:8080/main.qbk
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/handler/htdocs.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::method;
    using taar::matcher::target;

    if (argc != 3)
    {
        std::cerr << "Usage: htdocs_server <port> <document-root>\n";
        return 1;
    }

    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/{*path}",
        taar::handler::htdocs {argv[2]});

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", argv[1], session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on " << ep << '\n'; }),
        taar::ignore_and_rethrow);

    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&] { io.run(); });
}
