//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/json/value.hpp>
#include <boost/web/handler/htdocs.hpp>
#include <boost/web/session/http.hpp>
#include <boost/web/server/tcp.hpp>
#include "boost/web/matcher/method.hpp"
#include "boost/web/matcher/target.hpp"
#include <boost/web/core/cancellation_signals.hpp>
#include <boost/web/core/ignore_and_rethrow.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/io_context.hpp>
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
    namespace web = boost::web;
    using net::co_spawn;
    using net::bind_cancellation_slot;
    using net::detached;
    using web::matcher::method;
    using web::matcher::target;

    net::io_context io_context;

    net::signal_set os_signals(io_context, SIGINT, SIGTERM);
    os_signals.async_wait([&](auto, auto) { io_context.stop(); });

    web::session::http http_session;

    http_session.register_request_handler(
        method == http::verb::get && target == "/{*}",
        web::handler::htdocs {argv[2]}
    );

    web::cancellation_signals cancellation_signals;
    co_spawn(
        io_context,
        web::server::tcp(
            "0.0.0.0",
            argv[1],
            http_session,
            cancellation_signals,
            [](const net::ip::tcp::endpoint& endpoint)
            {
                std::clog << "HTTP server is listening on port " << endpoint.port() << '\n';
            }),
        bind_cancellation_slot(cancellation_signals.slot(), web::ignore_and_rethrow));

    io_context.run();

    return EXIT_SUCCESS;
}

