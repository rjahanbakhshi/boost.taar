//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "boost/taar/session/fuzz_http.hpp"
#include <boost/asio/detached.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/_experimental/test/stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
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
#include <csignal>
#include <exception>
#include <iostream>
#include <string_view>
#include <thread>
#include <cstdlib>

__AFL_FUZZ_INIT();

int main(int argc, char* argv[])
{
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

    taar::session::fuzz_http fuzz_http_session;

    fuzz_http_session.set_soft_error_handler(
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

    fuzz_http_session.register_request_handler(
        method == http::verb::get && target == "/api/sum/{a}/{b}",
        taar::handler::rest([](int a, int b) -> awaitable<boost::json::value>
        {
            std::cout << "Calculating sum of " << a << " and " << b << "...\n";

            co_return boost::json::value {
                {"a", a},
                {"b", b},
                {"result", a + b}
            };
        },
        taar::handler::path_arg("a"),
        taar::handler::path_arg("b")
    ));

    fuzz_http_session.register_request_handler(
        method == http::verb::get && target == "/crash",
        taar::handler::rest([](std::string_view disabled)
            -> awaitable<void>
        {
            if (disabled != "true")
            {
                std::raise(SIGSEGV);
            }
            co_return;
        },
        boost::taar::handler::query_arg("disabled")
    ));

    fuzz_http_session.register_request_handler(
        method == http::verb::get && target == "/hang",
        taar::handler::rest([](std::string_view disabled)
            -> awaitable<void>
        {
            if (disabled != "true")
            {
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
            co_return;
        },
        boost::taar::handler::query_arg("disabled")
    ));

    __AFL_INIT();

    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(1000))
    {
        size_t len = __AFL_FUZZ_TESTCASE_LEN;

        boost::beast::test::stream stream { io_context, {reinterpret_cast<const char*>(buf), len} };

        taar::cancellation_signals cancellation_signals;
        co_spawn(
            io_context,
            fuzz_http_session(
                std::move(stream),
                cancellation_signals),
            boost::asio::detached);

        io_context.poll();
    }

    return EXIT_SUCCESS;
}

