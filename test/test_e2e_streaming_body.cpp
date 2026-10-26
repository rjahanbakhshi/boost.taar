//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/core/body_reader.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/write.hpp>
#include <boost/json/parse.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <cstdint>
#include <string>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;
using taar::handler::query_arg;
using taar::handler::stream_body_arg;
using taar::handler::with_default;

std::atomic<std::size_t> peak_piece {0};

taar::session::http make_session()
{
    taar::session::http session;
    session.set_request_body_limit(1024);     // streaming bypasses this

    // Count bytes and a cheap checksum without buffering the body.
    session.register_request_handler(
        method == http::verb::post && target == "/upload",
        rest([](taar::body_reader& body, std::string name) -> taar::awaitable<json::value>
        {
            std::uint64_t total = 0;
            std::uint64_t sum = 0;
            while (!body.done())
            {
                auto piece = co_await body.read_some(4096);
                if (piece.size() > peak_piece) peak_piece = piece.size();
                total += piece.size();
                for (unsigned char c : piece) sum += c;
            }
            co_return json::value {
                {"name", name}, {"bytes", total}, {"sum", sum},
                {"declared", body.content_length() ? json::value {*body.content_length()} : json::value {nullptr}}};
        },
        stream_body_arg {},
        with_default(query_arg("name"), std::string {"anonymous"})));

    // Reads only the first piece, then answers: the rest is not drained.
    session.register_request_handler(
        method == http::verb::post && target == "/peek",
        rest([](taar::body_reader& body) -> taar::awaitable<std::string>
        {
            auto first = co_await body.read_some(16);
            co_return first;
        },
        stream_body_arg {}));

    // Raw handler form with a plain streaming_request.
    session.register_request_handler(
        method == http::verb::put && target == "/raw",
        [](taar::streaming_request const& request, taar::matcher::context const&) -> taar::awaitable<http::response<http::string_body>>
        {
            auto all = co_await request.body.read_all(1 << 20);
            http::response<http::string_body> res {http::status::ok, request.version()};
            res.body() = std::to_string(all.size()) + ":" + std::string {request[http::field::content_type]};
            res.prepare_payload();
            co_return res;
        });

    session.register_request_handler(
        method == http::verb::post && target == "/limited",
        rest([](taar::body_reader& body) -> taar::awaitable<std::string>
        {
            co_return co_await body.read_all(100);       // throws body_limit beyond 100 bytes
        },
        stream_body_arg {}));

    session.register_request_handler(method == http::verb::get && target == "/ping", rest([] { return "pong"; }));
    return session;
}

std::string pattern(std::size_t n)
{
    std::string s(n, '\0');
    for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<char>('a' + i % 26);
    return s;
}

std::uint64_t checksum(std::string const& s)
{
    std::uint64_t sum = 0;
    for (unsigned char c : s) sum += c;
    return sum;
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_body_large_upload_beyond_body_limit)
{
    net::io_context io;
    auto session = make_session();
    auto const payload = pattern(5 * 1024 * 1024);

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto req = e2e::make_request(http::verb::post, "/upload?name=big", payload, "application/octet-stream");
        auto ping = e2e::make_request(http::verb::get, "/ping");
        auto res = co_await e2e::send_all(port, req, ping);
        BOOST_TEST(res[0].result() == http::status::ok);
        auto body = json::parse(res[0].body());
        BOOST_TEST(body.at("name").as_string() == "big");
        BOOST_TEST(body.at("bytes").to_number<std::uint64_t>() == payload.size());
        BOOST_TEST(body.at("sum").to_number<std::uint64_t>() == checksum(payload));
        BOOST_TEST(body.at("declared").to_number<std::uint64_t>() == payload.size());
        BOOST_TEST(peak_piece <= 4096u);                 // bounded memory per read
        BOOST_TEST(res[1].body() == "pong");               // connection reusable after a consumed stream
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_body_chunked_upload_and_raw_handler)
{
    net::io_context io;
    auto session = make_session();

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
        co_await socket.async_connect({net::ip::address_v4::loopback(), port});

        // Chunked transfer-encoding: no Content-Length is known up front.
        std::string raw =
            "POST /upload HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
            "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
        co_await net::async_write(socket, net::buffer(raw));
        beast::flat_buffer buffer;
        e2e::response res;
        co_await http::async_read(socket, buffer, res);
        auto body = json::parse(res.body());
        BOOST_TEST(body.at("bytes").to_number<std::uint64_t>() == 11u);
        BOOST_TEST(body.at("declared").is_null());

        auto put = e2e::make_request(http::verb::put, "/raw", pattern(3000), "text/plain");
        co_await http::async_write(socket, put);
        e2e::response raw_res;
        co_await http::async_read(socket, buffer, raw_res);
        BOOST_TEST(raw_res.body() == "3000:text/plain");
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_body_unread_remainder_closes_connection)
{
    net::io_context io;
    auto session = make_session();

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
        co_await socket.async_connect({net::ip::address_v4::loopback(), port});
        auto req = e2e::make_request(http::verb::post, "/peek", pattern(100000), "application/octet-stream");
        co_await http::async_write(socket, req);
        beast::flat_buffer buffer;
        e2e::response res;
        co_await http::async_read(socket, buffer, res);
        BOOST_TEST(res.body() == "abcdefghijklmnop");
        BOOST_TEST(!res.keep_alive());                   // the body was not consumed
        char c;
        auto [ec, n] = co_await socket.async_read_some(net::buffer(&c, 1));
        BOOST_TEST((ec == net::error::eof || ec == net::error::connection_reset));
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_streaming_body_limit_in_handler_is_a_soft_error)
{
    net::io_context io;
    auto session = make_session();

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto ok = co_await e2e::send_all(port,
            e2e::make_request(http::verb::post, "/limited", pattern(50), "text/plain"),
            e2e::make_request(http::verb::get, "/ping"));
        BOOST_TEST(ok[0].body() == pattern(50));
        BOOST_TEST(ok[1].body() == "pong");                // fully consumed: connection reusable

        // The handler's own limit trips mid-body: soft error, then the connection closes.
        auto res = co_await e2e::send_all(port,
            e2e::make_request(http::verb::post, "/limited", pattern(500), "text/plain"));
        BOOST_TEST(res[0].result() == http::status::payload_too_large);
        BOOST_TEST(res[0].body().find("request body exceeds the read limit of 100 bytes") != std::string::npos);
        BOOST_TEST(!res[0].keep_alive());
    });
}

BOOST_AUTO_TEST_CASE(test_openapi_documents_stream_body_as_binary)
{
    taar::session::http session;
    taar::openapi::generator gen {{.title = "t", .version = "1"}};
    session.register_request_handler(
        method == http::verb::post && target == "/upload",
        rest([](taar::body_reader&) { return 0; }, stream_body_arg {taar::handler::description {"Raw file bytes"}}),
        gen.describe("Upload"));
    auto doc = gen.document();
    auto const& rb = doc.at("paths").at("/upload").at("post").at("requestBody");
    BOOST_TEST(rb.at("description").as_string() == "Raw file bytes");
    auto const& schema = rb.at("content").at("application/octet-stream").at("schema");
    BOOST_TEST(schema.at("type").as_string() == "string");
    BOOST_TEST(schema.at("format").as_string() == "binary");
}

} // namespace
