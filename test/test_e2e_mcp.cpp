//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/session/http.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/json/parse.hpp>
#include <boost/test/unit_test.hpp>
#include <string>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace mcp = boost::taar::mcp;

using taar::matcher::target;
using mcp::mcp_arg;

struct conn_stats { int requests = 0; };
struct calc_session { int last = 0; };

int add(conn_stats& c, calc_session& s, int a, int b) { ++c.requests; s.last = a + b; return s.last; }
int last(calc_session const& s) { return s.last; }

BOOST_AUTO_TEST_CASE(test_e2e_mcp_stateful_session_across_connections)
{
    net::io_context io;
    taar::session::basic_http<conn_stats> session;

    mcp::endpoint<calc_session, conn_stats> endpoint {{.name = "calc-server", .version = "1.0.0"}};
    endpoint.tool("add", "Add two integers", &add, mcp_arg("a"), mcp_arg("b"));
    endpoint.tool("last", "Last result", &last);
    session.register_request_handler(target == "/mcp", endpoint.handler());

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto init = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})");
        auto a = co_await e2e::send_all(port, {init});
        BOOST_TEST(a[0].result() == http::status::ok);
        std::string sid {a[0]["Mcp-Session-Id"]};
        BOOST_TEST(sid.size() == 32u);
        BOOST_TEST(json::parse(a[0].body()).at("result").at("serverInfo").at("name").as_string() == "calc-server");

        auto initialized = e2e::make_request(http::verb::post, "/mcp", R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        initialized.set("Mcp-Session-Id", sid);
        auto call = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add","arguments":{"a":20,"b":22}}})");
        call.set("Mcp-Session-Id", sid);
        call.set("MCP-Protocol-Version", "2025-11-25");
        auto b = co_await e2e::send_all(port, {initialized, call});
        BOOST_TEST(b[0].result() == http::status::accepted);
        BOOST_TEST(json::parse(b[1].body()).at("result").at("structuredContent").at("result").as_int64() == 42);

        // Another connection, same session: the session state is shared.
        auto read = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"last"}})");
        read.set("Mcp-Session-Id", sid);
        auto c = co_await e2e::send_all(port, {read});
        BOOST_TEST(json::parse(c[0].body()).at("result").at("structuredContent").at("result").as_int64() == 42);

        // No session header -> 400; DELETE -> 200 then 404.
        auto bare = e2e::make_request(http::verb::post, "/mcp", R"({"jsonrpc":"2.0","id":4,"method":"ping"})");
        auto del = e2e::make_request(http::verb::delete_, "/mcp");
        del.set("Mcp-Session-Id", sid);
        auto after = e2e::make_request(http::verb::post, "/mcp", R"({"jsonrpc":"2.0","id":5,"method":"ping"})");
        after.set("Mcp-Session-Id", sid);
        auto d = co_await e2e::send_all(port, {bare, del, after});
        BOOST_TEST(d[0].result() == http::status::bad_request);
        BOOST_TEST(d[1].result() == http::status::ok);
        BOOST_TEST(d[2].result() == http::status::not_found);
        BOOST_TEST(endpoint.session_count() == 0u);
    });
}

/// Reads from an open SSE response until @a needle shows up in the body.
taar::awaitable<void> read_until(
    taar::rebind_executor<net::ip::tcp::socket>& socket,
    boost::beast::flat_buffer& buffer,
    http::response_parser<http::string_body>& parser,
    std::string_view needle)
{
    while (parser.get().body().find(needle) == std::string::npos && !parser.is_done())
    {
        auto [ec, n] = co_await http::async_read_some(socket, buffer, parser);
        if (ec) throw boost::system::system_error {ec};
    }
}

BOOST_AUTO_TEST_CASE(test_e2e_mcp_event_stream)
{
    using taar::matcher::method;

    net::io_context io;
    taar::session::http session;

    mcp::endpoint<calc_session> endpoint {{.name = "calc-server", .version = "1.0.0"}};
    endpoint.tool("fire", "Notify every client", [&]
    {
        endpoint.broadcast("notifications/resources/updated", json::object {{"uri", "calc://history/last"}});
    });
    session.register_request_handler(method == http::verb::get && target == "/mcp", endpoint.stream_handler());
    session.register_request_handler(target == "/mcp", endpoint.handler());

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto init = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})");
        auto a = co_await e2e::send_all(port, {init});
        std::string sid {a[0]["Mcp-Session-Id"]};

        // Open the stream on its own connection.
        taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
        co_await socket.async_connect({net::ip::address_v4::loopback(), port});
        http::request<http::empty_body> get {http::verb::get, "/mcp", 11};
        get.set(http::field::host, "localhost");
        get.set(http::field::accept, "text/event-stream");
        get.set("Mcp-Session-Id", sid);
        co_await http::async_write(socket, get);

        boost::beast::flat_buffer buffer;
        http::response_parser<http::string_body> parser;
        parser.body_limit(boost::none);
        co_await http::async_read_header(socket, buffer, parser);
        BOOST_TEST(parser.get().result() == http::status::ok);
        BOOST_TEST(parser.get()[http::field::content_type] == "text/event-stream");
        BOOST_TEST(parser.get().chunked());
        co_await read_until(socket, buffer, parser, ": connected\n\n");

        // A tool call on another connection pushes a notification into the stream.
        auto fire = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"fire"}})");
        fire.set("Mcp-Session-Id", sid);
        auto b = co_await e2e::send_all(port, {fire});
        BOOST_TEST(b[0].result() == http::status::ok);
        co_await read_until(socket, buffer, parser, "event: message\ndata: ");
        BOOST_TEST(parser.get().body().find("notifications/resources/updated") != std::string::npos);

        // Deleting the session ends the stream cleanly (final chunk, parser done).
        auto del = e2e::make_request(http::verb::delete_, "/mcp");
        del.set("Mcp-Session-Id", sid);
        auto c = co_await e2e::send_all(port, {del});
        BOOST_TEST(c[0].result() == http::status::ok);
        while (!parser.is_done())
        {
            auto [ec, n] = co_await http::async_read_some(socket, buffer, parser);
            if (ec) throw boost::system::system_error {ec};
        }
        BOOST_TEST(parser.is_done());
    });
}

} // namespace
