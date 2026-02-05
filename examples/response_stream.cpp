//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url_view.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace taar = boost::taar;
using tcp = net::ip::tcp;
using taar::awaitable;
using taar::chunked_response;

// Awaitable that connects to a host and returns a tcp_stream
// Note: tcp_stream is not default-constructible, demonstrating the fix
awaitable<beast::tcp_stream> connect_to_host(
    std::string const& host,
    std::string const& port)
{
    auto executor = co_await net::this_coro::executor;
    tcp::resolver resolver{executor};
    beast::tcp_stream stream{executor};

    auto const results = co_await resolver.async_resolve(
        host,
        port,
        net::use_awaitable_t<net::io_context::executor_type>{});

    stream.expires_after(std::chrono::seconds(30));
    co_await stream.async_connect(
        results,
        net::use_awaitable_t<net::io_context::executor_type>{});

    co_return stream;
}

// Chunked response generator that yields HTTP response as chunks
chunked_response<std::string> fetch_url_chunked(
    std::string const& host,
    std::string const& port,
    std::string const& target)
{
    // Connect to the host - this co_await returns a non-default-constructible type
    auto stream = co_await connect_to_host(host, port);

    // Set up the HTTP GET request
    http::request<http::empty_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "boost.taar/response_stream");

    // Send the request
    co_await http::async_write(
        stream,
        req,
        net::use_awaitable_t<net::io_context::executor_type>{});

    // Receive the response header
    beast::flat_buffer buffer;
    http::response_parser<http::string_body> parser;
    parser.body_limit((std::numeric_limits<std::uint64_t>::max)());

    co_await http::async_read_header(
        stream,
        buffer,
        parser,
        net::use_awaitable_t<net::io_context::executor_type>{});

    // Yield the response status and headers
    auto const& response = parser.get();
    std::ostringstream header_stream;
    header_stream << "HTTP/" << response.version() / 10 << "." << response.version() % 10
                  << " " << response.result_int() << " " << response.reason() << "\n";

    for (auto const& field : response)
    {
        header_stream << field.name_string() << ": " << field.value() << "\n";
    }
    header_stream << "\n";

    co_yield header_stream.str();

    // Read and yield the body in chunks
    while (!parser.is_done())
    {
        co_await http::async_read_some(
            stream,
            buffer,
            parser,
            net::use_awaitable_t<net::io_context::executor_type>{});

        auto const& body = parser.get().body();
        if (!body.empty())
        {
            co_yield body;
            // Clear the body for next chunk (parser accumulates)
            const_cast<std::string&>(body).clear();
        }
    }

    // Gracefully close the socket
    beast::error_code ec;
    std::ignore = stream.socket().shutdown(tcp::socket::shutdown_both, ec);
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: response_stream <url>\n";
        std::cerr << "Example: response_stream http://example.com/\n";
        return EXIT_FAILURE;
    }

    // Parse the URL
    auto url_result = boost::urls::parse_uri(argv[1]);
    if (!url_result)
    {
        std::cerr << "Invalid URL: " << url_result.error().message() << "\n";
        return EXIT_FAILURE;
    }

    auto const& url = *url_result;
    std::string host = url.host();
    std::string port = url.has_port() ? url.port() : "80";
    std::string target = url.path();
    if (target.empty())
        target = "/";
    if (url.has_query())
        target += "?" + std::string{url.query()};

    std::cout << "Fetching: " << host << ":" << port << target << "\n\n";

    net::io_context ioc;

    net::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = fetch_url_chunked(host, port, target);
            gen.set_executor(ioc.get_executor());

            while (true)
            {
                auto [ec, chunk] = co_await gen.next();
                if (!chunk)
                    break;
                std::cout << *chunk;
            }

            gen.rethrow_if_exception();
        },
        [](std::exception_ptr ep)
        {
            if (ep)
            {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch (std::exception const& e)
                {
                    std::cerr << "Error: " << e.what() << "\n";
                }
            }
        });

    ioc.run();

    return EXIT_SUCCESS;
}
