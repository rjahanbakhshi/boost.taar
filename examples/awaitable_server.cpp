//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/handler/htdocs.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/core/response_builder.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json/value.hpp>
#include <exception>
#include <iostream>
#include <cstdlib>
#include <thread>

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: awaitable_server port\n";
        return EXIT_FAILURE;
    }

    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using net::co_spawn;
    using net::bind_cancellation_slot;
    using net::detached;
    using taar::response_builder;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::awaitable;

    net::io_context io_context;

    net::signal_set os_signals(io_context, SIGINT, SIGTERM);
    os_signals.async_wait([&](auto, auto) { io_context.stop(); });

    taar::session::http http_session;

    http_session.set_soft_error_handler(
        [](std::exception_ptr eptr) -> awaitable<response_builder>
        {
            try
            {
                std::rethrow_exception(eptr);
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << '\n';
                co_return
                    response_builder(boost::json::value{{"soft_error", e.what()}})
                        .set_status(http::status::internal_server_error);
            }
            catch (...)
            {
                std::cerr << "Unknown error!\n";
                co_return
                    response_builder(boost::json::value{{"soft_error", "Unknown error!"}})
                        .set_status(http::status::internal_server_error);
            }
        }
    );

    http_session.register_request_handler(
        method == http::verb::get && target == "/special/{*}",
        [](
            const http::request<http::empty_body>& request,
            const taar::matcher::context& context) -> awaitable<http::message_generator>
        {
            http::response<http::string_body> res {
                boost::beast::http::status::ok,
                request.version()};
            res.set(boost::beast::http::field::content_type, "text/html");
            res.keep_alive(request.keep_alive());
            res.body() = "Special path is: " + context.path_args.at("*");
            res.prepare_payload();
            co_return res;
        }
    );

    http_session.register_request_handler(
        method == http::verb::get && target == "/api/sum/{a}/{b}",
        taar::handler::rest([](int a, int b) -> awaitable<boost::json::value>
        {
            co_return boost::json::value {
                {"a", a},
                {"b", b},
                {"result", a + b}
            };
        },
        taar::handler::path_arg("a"),
        taar::handler::path_arg("b")
    ));

    http_session.register_request_handler(
        method == http::verb::post && target == "/api/concat/{a}",
        taar::handler::rest([](std::string a, const std::string& b)
            -> awaitable<std::string>
        {
            co_return std::string{a} + std::string{b};
        },
        taar::handler::path_arg("a"),
        taar::handler::string_body_arg(taar::handler::all_content_types)
    ));

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

