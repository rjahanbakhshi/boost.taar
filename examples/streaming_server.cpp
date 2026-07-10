//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// Streaming responses with chunked transfer encoding: server-sent events
// from a timer, a JSON stream from an async_generator, and large files
// streamed from disk with a header/footer generator spliced in.
//
//   ./streaming_server 8080 ./
//   curl -N localhost:8080/events          # one event per second, ten events
//   curl -N localhost:8080/numbers
//   curl localhost:8080/download/README.md
//   curl localhost:8080/wrapped/README.md
//

#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/http.hpp>
#include <boost/json/value.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace http = boost::beast::http;
namespace taar = boost::taar;

std::string content_type_for(std::filesystem::path const& p)
{
    auto const ext = p.extension().string();
    if (ext == ".html") return "text/html";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".md" || ext == ".txt") return "text/plain";
    return "application/octet-stream";
}

// Server-sent events: text/event-stream with one "tick" event per second.
taar::chunked_response<std::string> clock_events()
{
    co_yield taar::set_header(http::field::content_type, "text/event-stream");
    co_yield taar::set_header(http::field::cache_control, "no-cache");

    boost::asio::steady_timer timer {co_await boost::asio::this_coro::executor};
    for (int i = 1; i <= 10; ++i)
    {
        timer.expires_after(std::chrono::seconds {1});
        co_await timer.async_wait(taar::use_awaitable);
        co_yield "id: " + std::to_string(i) + "\nevent: tick\ndata: {\"n\":" + std::to_string(i) + "}\n\n";
    }
}

// A JSON stream: each chunk is one serialised value (application/json).
taar::async_generator<boost::json::value> squares()
{
    for (int i = 1; i <= 5; ++i)
    {
        co_yield boost::json::value {{"n", i}, {"square", i * i}};
    }
}

taar::chunked_response<std::string> banner(std::string text)
{
    co_yield "=== " + std::move(text) + " ===\n";
}

taar::chunked_response<std::string> file_chunks(std::filesystem::path path)
{
    std::ifstream file {path, std::ios::binary};
    if (!file) throw std::runtime_error {"cannot open " + path.string()};
    char buffer[16 * 1024];
    while (file.read(buffer, sizeof buffer) || file.gcount() > 0)
    {
        co_yield std::string {buffer, static_cast<std::size_t>(file.gcount())};
    }
}

} // namespace

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::matcher::context;

    if (argc != 3)
    {
        std::cerr << "Usage: streaming_server <port> <document-root>\n";
        return 1;
    }
    std::filesystem::path const root = argv[2];

    taar::session::http session;

    session.register_request_handler(
        method == http::verb::get && target == "/events",
        [](http::request<http::empty_body> const&, context const&) { return clock_events(); });

    session.register_request_handler(
        method == http::verb::get && target == "/numbers",
        [](http::request<http::empty_body> const&, context const&) { return squares(); });

    session.register_request_handler(
        method == http::verb::get && target == "/download/{*path}",
        [root](http::request<http::empty_body> const&, context const& ctx) -> taar::chunked_response<std::string>
        {
            auto const path = root / ctx.path_args.at("path");
            co_yield taar::set_header(http::field::content_type, content_type_for(path));
            co_yield file_chunks(path);         // splice another generator's chunks
        });

    session.register_request_handler(
        method == http::verb::get && target == "/wrapped/{*path}",
        [root](http::request<http::empty_body> const&, context const& ctx) -> taar::chunked_response<std::string>
        {
            auto const path = root / ctx.path_args.at("path");
            co_yield taar::set_header(http::field::content_type, "text/plain");
            co_yield banner("begin " + path.filename().string());
            co_yield file_chunks(path);
            co_yield banner("end " + path.filename().string());
        });

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", argv[1], session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on " << ep << '\n'; }),
        net::bind_cancellation_slot(signals.slot(), taar::ignore_and_rethrow));

    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&] { io.run(); });
}
