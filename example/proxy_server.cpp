//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//
// Asynchronous handlers. The handler is a coroutine: it fetches an upstream
// URL with Beast while the connection stays unblocked, then returns the
// upstream body. The io_context runs on several threads.
//
//   ./proxy_server 8080
//   curl 'localhost:8080/fetch?url=http://example.com/'
//   curl 'localhost:8080/delay?ms=500'
//

#include <boost/taar/core/awaitable.hpp>
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
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json/value.hpp>
#include <boost/url/parse.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace taar = boost::taar;

// GET an http:// URL and return the upstream response unchanged.
taar::awaitable<http::response<http::string_body>> fetch(std::string url_text)
{
    auto url = boost::urls::parse_uri(url_text);
    if (!url || url->scheme() != "http") throw std::invalid_argument {"only http:// URLs are supported"};

    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::resolver resolver {executor};
    beast::tcp_stream stream {executor};
    stream.expires_after(std::chrono::seconds {10});

    auto const endpoints = co_await resolver.async_resolve(
        url->host(), url->has_port() ? url->port() : "80", taar::use_awaitable);
    co_await stream.async_connect(endpoints, taar::use_awaitable);

    std::string target = url->encoded_path().empty() ? "/" : std::string {url->encoded_path()};
    if (url->has_query()) target += "?" + std::string {url->encoded_query()};

    http::request<http::empty_body> req {http::verb::get, target, 11};
    req.set(http::field::host, url->host());
    req.set(http::field::user_agent, "boost.taar proxy_server");
    co_await http::async_write(stream, req, taar::use_awaitable);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(stream, buffer, res, taar::use_awaitable);

    res.keep_alive(true);
    res.erase(http::field::transfer_encoding);
    res.prepare_payload();
    co_return res;
}

} // namespace

int main(int argc, char* argv[])
{
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::rest;
    using taar::handler::query_arg;
    using taar::handler::with_default;

    char const* port = argc > 1 ? argv[1] : "8080";

    taar::session::http session;

    session.register_request_handler(
        method == http::verb::get && target == "/fetch",
        rest(&fetch, query_arg("url")));

    session.register_request_handler(
        method == http::verb::get && target == "/delay",
        rest([](int ms) -> taar::awaitable<boost::json::value>
        {
            net::steady_timer timer {co_await net::this_coro::executor};
            timer.expires_after(std::chrono::milliseconds {ms});
            co_await timer.async_wait(taar::use_awaitable);
            co_return boost::json::value {{"slept_ms", ms}};
        },
        with_default(query_arg("ms"), 100)));

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", port, session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on " << ep << '\n'; }),
        taar::ignore_and_rethrow);

    std::vector<std::thread> threads;
    for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) threads.emplace_back([&] { io.run(); });
    for (auto& t : threads) t.join();
}
