//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

// Edge cases of routing and argument extraction seen from the wire.

#include "e2e.hpp"
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/header.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/asio/write.hpp>
#include <boost/test/unit_test.hpp>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace taar = boost::taar;

using taar::matcher::header;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::path_arg;
using taar::handler::query_arg;
using taar::handler::rest;
using taar::handler::with_default;

taar::session::http make_session()
{
    taar::session::http session;

    session.register_request_handler(
        method == http::verb::get && target == "/users/{id}",
        rest([](int id) { return id; }, path_arg("id")));

    session.register_request_handler(
        method == http::verb::get && target == "/files/{*path}",
        rest([](std::string path) { return "[" + path + "]"; }, path_arg("path")));

    session.register_request_handler(
        method == http::verb::get && target == "/u/{uid}/p/{pid}",
        rest([](std::string uid, std::string pid) { return uid + "-" + pid; }, path_arg("uid"), path_arg("pid")));

    session.register_request_handler(
        method == http::verb::get && target == "/names/{name}",
        rest([](std::string name) { return "[" + name + "]"; }, path_arg("name")));

    session.register_request_handler(
        method == http::verb::get && target == "/a",
        rest([] { return std::string {"a"}; }));

    session.register_request_handler(
        (target == "/x" || target == "/y") && !(method == http::verb::delete_),
        rest([] { return std::string {"xy"}; }));

    session.register_request_handler(
        method == http::verb::get && target == "/hdr" && header("X-Token") == "t",
        rest([] { return std::string {"ok"}; }));

    session.register_request_handler(
        method == http::verb::get && target == "/page",
        rest([](int page) { return page; }, with_default(query_arg("page"), 1)));

    session.register_request_handler(
        method == http::verb::get && target == "/tags",
        rest([](std::string tag) { return tag; }, query_arg("tag")));

    session.register_request_handler(
        method == http::verb::get && target == "/q",
        rest([](std::string q) { return "[" + q + "]"; }, query_arg("q")));

    session.register_request_handler(
        method == http::verb::get && target == "/qi",
        rest([](int n) { return n; }, query_arg("n")));

    session.register_request_handler(
        method == http::verb::get && target == "/flag",
        rest([](bool b) { return std::string {b ? "T" : "F"}; }, query_arg("b")));

    return session;
}

e2e::request get(std::string target)
{
    return e2e::make_request(http::verb::get, std::move(target));
}

BOOST_AUTO_TEST_CASE(test_routing_single_segment_param_does_not_cross_slash)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {get("/users/42"), get("/users/42/posts"), get("/users")});
        BOOST_TEST(r[0].result() == http::status::ok);
        BOOST_TEST(r[0].body() == "42");
        BOOST_TEST(r[1].result() == http::status::not_found);
        BOOST_TEST(r[2].result() == http::status::not_found);
    });
}

BOOST_AUTO_TEST_CASE(test_routing_greedy_param_captures_remainder_and_matches_empty_remainder)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {get("/files/a/b/c.txt"), get("/files/"), get("/files")});
        BOOST_TEST(r[0].body() == "[a/b/c.txt]");
        // Both spellings of an empty remainder match and capture "".
        BOOST_TEST(r[1].result() == http::status::ok);
        BOOST_TEST(r[1].body() == "[]");
        BOOST_TEST(r[2].result() == http::status::ok);
        BOOST_TEST(r[2].body() == "[]");
    });
}

BOOST_AUTO_TEST_CASE(test_routing_multiple_params_keep_their_positions)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {get("/u/1/p/2")});
        BOOST_TEST(r[0].body() == "1-2");
    });
}

BOOST_AUTO_TEST_CASE(test_routing_percent_encoded_segment_is_decoded_and_encoded_slash_does_not_split)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {get("/names/a%20b"), get("/names/a%2Fb")});
        BOOST_TEST(r[0].result() == http::status::ok);
        BOOST_TEST(r[0].body() == "[a b]");
        BOOST_TEST(r[1].result() == http::status::ok);
        BOOST_TEST(r[1].body() == "[a/b]");
    });
}

BOOST_AUTO_TEST_CASE(test_routing_trailing_slash_does_not_match_and_query_is_ignored)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {get("/a"), get("/a/"), get("/a?x=1")});
        BOOST_TEST(r[0].body() == "a");
        BOOST_TEST(r[1].result() == http::status::not_found);
        BOOST_TEST(r[2].result() == http::status::ok);
    });
}

BOOST_AUTO_TEST_CASE(test_routing_or_and_not_composition)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {
            get("/x"), get("/y"),
            e2e::make_request(http::verb::post, "/x"),
            e2e::make_request(http::verb::delete_, "/x"),
            get("/z")});
        BOOST_TEST(r[0].body() == "xy");
        BOOST_TEST(r[1].body() == "xy");
        BOOST_TEST(r[2].body() == "xy");
        BOOST_TEST(r[3].result() == http::status::not_found);
        BOOST_TEST(r[4].result() == http::status::not_found);
    });
}

BOOST_AUTO_TEST_CASE(test_routing_header_matcher_name_is_case_insensitive)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto lower = get("/hdr");
        lower.set("x-token", "t");
        auto wrong = get("/hdr");
        wrong.set("X-Token", "u");
        auto r = co_await e2e::send_all(port, {lower, wrong, get("/hdr")});
        BOOST_TEST(r[0].body() == "ok");
        BOOST_TEST(r[1].result() == http::status::not_found);
        BOOST_TEST(r[2].result() == http::status::not_found);
    });
}

BOOST_AUTO_TEST_CASE(test_routing_head_on_get_only_route_is_served_header_only)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        // Header-only responses must be read raw: a generic client parser
        // would wait for the announced Content-Length.
        taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
        co_await socket.async_connect({net::ip::address_v4::loopback(), port});
        std::string const req = "HEAD /a HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
        co_await net::async_write(socket, net::buffer(req));
        std::string out;
        char buf[1024];
        for (;;)
        {
            auto [ec, n] = co_await socket.async_read_some(net::buffer(buf));
            out.append(buf, n);
            if (ec) break;
        }
        BOOST_TEST(out.starts_with("HTTP/1.1 200 OK\r\n"));
        BOOST_TEST(out.find("Content-Length: 1\r\n") != std::string::npos);   // "a"
        BOOST_TEST(out.ends_with("\r\n\r\n"));
    });
}

BOOST_AUTO_TEST_CASE(test_rest_arg_integer_overflow_is_bad_request)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {get("/users/99999999999999999999"), get("/users/-1")});
        BOOST_TEST(r[0].result() == http::status::bad_request);
        BOOST_TEST(r[1].body() == "-1");
    });
}

BOOST_AUTO_TEST_CASE(test_rest_arg_with_default_does_not_mask_invalid_value)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {get("/page"), get("/page?page=5"), get("/page?page=abc")});
        BOOST_TEST(r[0].body() == "1");
        BOOST_TEST(r[1].body() == "5");
        BOOST_TEST(r[2].result() == http::status::bad_request);
    });
}

BOOST_AUTO_TEST_CASE(test_rest_arg_repeated_query_parameter_is_ambiguous)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, {get("/tags?tag=a"), get("/tags?tag=a&tag=b")});
        BOOST_TEST(r[0].body() == "a");
        BOOST_TEST(r[1].result() == http::status::bad_request);
    });
}

BOOST_AUTO_TEST_CASE(test_rest_arg_empty_query_value)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        std::vector<e2e::request> reqs {get("/q?q="), get("/q?q"), get("/qi?n="), get("/qi")};
        auto r = co_await e2e::send_all(port, std::move(reqs));
        BOOST_TEST(r[0].body() == "[]");
        BOOST_TEST(r[1].body() == "[]");
        BOOST_TEST(r[2].result() == http::status::bad_request);
        BOOST_TEST(r[3].result() == http::status::bad_request);
    });
}

BOOST_AUTO_TEST_CASE(test_rest_arg_bool_spellings)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        std::vector<e2e::request> reqs;
        for (auto v : {"true", "TRUE", "yes", "1", "false", "No", "0", "on", "2", ""})
        {
            reqs.push_back(get(std::string {"/flag?b="} + v));
        }
        auto r = co_await e2e::send_all(port, std::move(reqs));
        for (std::size_t i = 0; i < 4; ++i) BOOST_TEST(r[i].body() == "T");
        for (std::size_t i = 4; i < 7; ++i) BOOST_TEST(r[i].body() == "F");
        for (std::size_t i = 7; i < 10; ++i) BOOST_TEST(r[i].result() == http::status::bad_request);
    });
}

BOOST_AUTO_TEST_CASE(test_routing_path_arg_from_any_target_alternative)
{
    // A placeholder of the second alternative is a legitimate path_arg.
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && (target == "/items" || target == "/items/{id}"),
        rest([](std::string id) { return id.empty() ? std::string {"all"} : id; },
            with_default(path_arg("id"), std::string {})));
    net::io_context io;
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port, {
            e2e::make_request(http::verb::get, "/items"),
            e2e::make_request(http::verb::get, "/items/7")});
        BOOST_TEST(res[0].body() == "all");
        BOOST_TEST(res[1].body() == "7");
    });
}

} // namespace
