//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include "e2e.hpp"
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/handler/htdocs.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;
using taar::handler::path_arg;

struct temp_docroot
{
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "taar-head-test";
    temp_docroot()
    {
        std::filesystem::create_directories(dir);
        std::ofstream {dir / "index.html"} << "<h1>hello</h1>";
    }
    ~temp_docroot() { std::filesystem::remove_all(dir); }
};

taar::session::http make_session()
{
    taar::session::http session;
    session.register_request_handler(method == http::verb::get && target == "/items/{id}",
        rest([](int id) { return "item " + std::to_string(id); }, path_arg("id")));
    session.register_request_handler(method == http::verb::delete_ && target == "/items/{id}",
        rest([](int) { return "deleted"; }, path_arg("id")));
    session.register_request_handler((method == http::verb::get || method == http::verb::post) && target == "/both",
        rest([] { return "both"; }));
    session.register_request_handler(method == http::verb::head && target == "/explicit",
        rest([] { return std::string {"this body is never sent"}; }));
    session.register_request_handler(method == http::verb::get && target == "/stream",
        rest([]() -> taar::chunked_response<std::string> { co_yield std::string {"a"}; co_yield std::string {"b"}; }));
    // Opaque matcher: contributes nothing to Allow.
    session.register_request_handler(
        [](http::request_header<> const& req, taar::matcher::context&) { return req.target() == "/opaque" && req.method() == http::verb::put; },
        rest([] { return "opaque"; }));
    return session;
}

/// Writes @a req and reads the raw bytes until EOF or a short pause, to inspect head-only responses.
taar::awaitable<std::string> raw_exchange(unsigned short port, std::string request)
{
    taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
    co_await socket.async_connect({net::ip::address_v4::loopback(), port});
    co_await net::async_write(socket, net::buffer(request));
    std::string out;
    char buf[4096];
    for (;;)
    {
        auto [ec, n] = co_await socket.async_read_some(net::buffer(buf));
        out.append(buf, n);
        if (ec) break;
    }
    co_return out;
}

BOOST_AUTO_TEST_CASE(test_method_not_allowed_lists_the_known_methods)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port,
            e2e::make_request(http::verb::put, "/items/7", "{}", "application/json"),   // body is drained
            e2e::make_request(http::verb::delete_, "/both"),
            e2e::make_request(http::verb::post, "/explicit"),
            e2e::make_request(http::verb::put, "/opaque"),
            e2e::make_request(http::verb::post, "/nowhere"),
            e2e::make_request(http::verb::get, "/items/7"));
        BOOST_TEST(res[0].result() == http::status::method_not_allowed);
        BOOST_TEST(res[0][http::field::allow] == "GET, DELETE, HEAD");
        BOOST_TEST(res[0].body().empty());
        BOOST_TEST(res[1].result() == http::status::method_not_allowed);
        BOOST_TEST(res[1][http::field::allow] == "GET, POST, HEAD");
        BOOST_TEST(res[2].result() == http::status::method_not_allowed);
        BOOST_TEST(res[2][http::field::allow] == "HEAD");
        BOOST_TEST(res[3].body() == "opaque");                                   // opaque matcher still routes
        BOOST_TEST(res[4].result() == http::status::not_found);                  // unknown path stays 404
        BOOST_TEST(res[5].body() == "item 7");                                   // connection reused throughout
    });
}

BOOST_AUTO_TEST_CASE(test_head_is_served_from_get_without_a_body)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto raw = co_await raw_exchange(port, "HEAD /items/42 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        BOOST_TEST(raw.starts_with("HTTP/1.1 200 OK\r\n"));
        BOOST_TEST(raw.find("Content-Length: 7\r\n") != std::string::npos);     // "item 42"
        BOOST_TEST(raw.ends_with("\r\n\r\n"));                                     // header only
        BOOST_TEST(raw.find("item 42") == std::string::npos);

        // An explicit HEAD route also has its body stripped.
        auto explicit_head = co_await raw_exchange(port, "HEAD /explicit HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        BOOST_TEST(explicit_head.starts_with("HTTP/1.1 200 OK\r\n"));
        BOOST_TEST(explicit_head.find("never sent") == std::string::npos);
        BOOST_TEST(explicit_head.find("Content-Length: 23\r\n") != std::string::npos);

        // A streaming GET route answers HEAD with the chunked header and no chunks.
        auto stream = co_await raw_exchange(port, "HEAD /stream HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        BOOST_TEST(stream.starts_with("HTTP/1.1 200 OK\r\n"));
        BOOST_TEST(stream.find("Transfer-Encoding: chunked\r\n") != std::string::npos);
        BOOST_TEST(stream.ends_with("\r\n\r\n"));
        BOOST_TEST(stream.find("\r\n1\r\na") == std::string::npos);

        // HEAD keeps the connection alive like GET would. A generic client
        // parser cannot know the response has no body, so read the header
        // bytes by hand before reusing the socket.
        taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
        co_await socket.async_connect({net::ip::address_v4::loopback(), port});
        std::string const head_req = "HEAD /items/1 HTTP/1.1\r\nHost: x\r\n\r\n";
        co_await net::async_write(socket, net::buffer(head_req));
        std::string header;
        char c;
        while (!header.ends_with("\r\n\r\n"))
        {
            co_await net::async_read(socket, net::buffer(&c, 1));
            header += c;
        }
        BOOST_TEST(header.starts_with("HTTP/1.1 200 OK\r\n"));
        BOOST_TEST(header.find("Content-Length: 6\r\n") != std::string::npos);
        auto get = e2e::make_request(http::verb::get, "/items/1");
        co_await http::async_write(socket, get);
        beast::flat_buffer buffer;
        e2e::response res;
        co_await http::async_read(socket, buffer, res);
        BOOST_TEST(res.body() == "item 1");
    });
}

BOOST_AUTO_TEST_CASE(test_head_on_htdocs_get_route_is_answered_by_the_handler)
{
    // htdocs resolves request.target() against its root and handles HEAD
    // itself; the GET route is reused, the handler sees HEAD.
    net::io_context io;
    temp_docroot root;
    taar::session::http session;
    session.register_request_handler(method == http::verb::get && target == "/{*path}",
        taar::handler::htdocs {root.dir.string()});
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto file = co_await raw_exchange(port, "HEAD /index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        BOOST_TEST(file.starts_with("HTTP/1.1 200 OK\r\n"));
        BOOST_TEST(file.find("<h1>") == std::string::npos);
        BOOST_TEST(file.find("Content-Length: 14\r\n") != std::string::npos);
        auto put = co_await e2e::send_all(port, e2e::make_request(http::verb::put, "/index.html"));
        BOOST_TEST(put[0].result() == http::status::method_not_allowed);
        BOOST_TEST(put[0][http::field::allow] == "GET, HEAD");
    });
}

BOOST_AUTO_TEST_CASE(test_head_errors_are_header_only)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        // Path argument conversion fails: the 400 from the soft error handler has no body either.
        auto bad = co_await raw_exchange(port, "HEAD /items/x HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        BOOST_TEST(bad.starts_with("HTTP/1.1 400 Bad Request\r\n"));
        BOOST_TEST(bad.ends_with("\r\n\r\n"));
        BOOST_TEST(bad.find("Content-Length: ") != std::string::npos);
        auto missing = co_await raw_exchange(port, "HEAD /nowhere HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        BOOST_TEST(missing.starts_with("HTTP/1.1 404 Not Found\r\n"));
    });
}

BOOST_AUTO_TEST_CASE(test_method_fallbacks_can_be_disabled)
{
    net::io_context io;
    auto session = make_session();
    session.set_method_not_allowed(false);
    session.set_head_from_get(false);
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port,
            e2e::make_request(http::verb::put, "/items/7"),
            e2e::make_request(http::verb::head, "/items/7"));
        BOOST_TEST(res[0].result() == http::status::not_found);
        BOOST_TEST((res[0].find(http::field::allow) == res[0].end()));
        BOOST_TEST(res[1].result() == http::status::not_found);
        // An explicit HEAD route is unaffected (header-only, so read it raw).
        auto explicit_head = co_await raw_exchange(port, "HEAD /explicit HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        BOOST_TEST(explicit_head.starts_with("HTTP/1.1 200 OK\r\n"));
    });
}

} // namespace
