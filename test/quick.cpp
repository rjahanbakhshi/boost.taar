//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// A consumer program for the CMake and b2 integration tests: serves one
// REST route on a loopback port, requests it once, checks the answer and
// shuts down. It links Boost::taar alone, so it fails to build or link if
// the target's usage requirements are incomplete.

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <iostream>
#include <string>

int main()
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::method;
    using taar::matcher::target;

    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/add/{a}/{b}",
        taar::handler::rest(
            [](int a, int b) { return boost::json::value {{"sum", a + b}}; },
            taar::handler::path_arg("a"),
            taar::handler::path_arg("b")));

    net::io_context io;
    taar::cancellation_signals signals;
    bool ok = false;

    auto client = [&](net::ip::tcp::endpoint ep) -> net::awaitable<void>
    {
        net::ip::tcp::socket socket {io};
        co_await socket.async_connect(ep, net::use_awaitable);

        http::request<http::string_body> req {http::verb::get, "/add/2/3", 11};
        req.set(http::field::host, "localhost");
        co_await http::async_write(socket, req, net::use_awaitable);

        boost::beast::flat_buffer buffer;
        http::response<http::string_body> res;
        co_await http::async_read(socket, buffer, res, net::use_awaitable);

        ok = res.result() == http::status::ok &&
            boost::json::parse(res.body()).at("sum").to_number<int>() == 5;
        if (!ok)
            std::cerr << "unexpected response:\n" << res << '\n';
        signals.emit();
    };

    net::co_spawn(
        io,
        taar::server::tcp("127.0.0.1", "0", session, signals,
            [&](net::ip::tcp::endpoint const& ep)
            {
                net::co_spawn(io, client(ep), taar::ignore_and_rethrow);
            }),
        taar::ignore_and_rethrow);

    io.run();
    return ok ? 0 : 1;
}
