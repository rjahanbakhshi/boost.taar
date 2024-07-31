//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/json/value.hpp>
#include <boost/taar/handler/htdocs.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/server/tcp.hpp>
#include "boost/taar/matcher/method.hpp"
#include "boost/taar/matcher/target.hpp"
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/io_context.hpp>
#include <thread>
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: htdocs_server port /example/document/root\n";
        return EXIT_FAILURE;
    }

    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using net::co_spawn;
    using net::bind_cancellation_slot;
    using net::detached;
    using taar::matcher::method;
    using taar::matcher::target;

    net::io_context io_context;

    net::signal_set os_signals(io_context, SIGINT, SIGTERM);
    os_signals.async_wait([&](auto, auto) { io_context.stop(); });

    taar::session::http http_session;

    http_session.register_request_handler(
        method == http::verb::get && target == "/{*}",
        taar::handler::htdocs {argv[2]}
    );

    taar::cancellation_signals cancellation_signals;
    co_spawn(
        io_context,
        taar::server::tcp(
            "0.0.0.0",
            argv[1],
            http_session,
            cancellation_signals,
            [](const net::ip::tcp::endpoint& endpoint)
            {
                std::clog << "HTTP server is listening on port " << endpoint.port() << '\n';
            }),
        bind_cancellation_slot(cancellation_signals.slot(), taar::ignore_and_rethrow));

    std::vector<std::jthread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&]{io_context.run();});
    }

    return EXIT_SUCCESS;
}

