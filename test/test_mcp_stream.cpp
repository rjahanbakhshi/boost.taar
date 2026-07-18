//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/parse.hpp>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace mcp = boost::taar::mcp;

struct chat_session { int messages = 0; };
using endpoint = mcp::endpoint<chat_session>;

http::response<http::string_body> post(endpoint& e, http::verb verb, std::string body, std::string sid = {})
{
    http::request<http::string_body> req {verb, "/mcp", 11};
    req.set(http::field::content_type, "application/json");
    if (!sid.empty()) req.set("Mcp-Session-Id", sid);
    req.body() = std::move(body);
    req.prepare_payload();

    taar::matcher::context mctx;
    auto handler = e.handler();
    net::io_context io;
    std::optional<http::response<http::string_body>> out;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        out = co_await handler(taar::handler::contexts<> {}, req, mctx);
    }, net::detached);
    io.run();
    return *out;
}

std::string initialize(endpoint& e)
{
    auto res = post(e, http::verb::post, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})");
    return std::string {res["Mcp-Session-Id"]};
}

/// Drives a chunked_response one value at a time on its own io_context.
struct stream_reader
{
    net::io_context io;
    taar::chunked_response<std::string> gen;

    explicit stream_reader(taar::chunked_response<std::string> g) : gen {std::move(g)}
    {
        gen.set_executor(io.get_executor());
    }

    /// Returns the next chunk, or nullopt at end of stream. @a while_waiting
    /// runs once the generator is suspended waiting for a message.
    template <typename F = void (*)()>
    std::optional<std::string> next(F while_waiting = [] {})
    {
        std::optional<std::string> got;
        net::co_spawn(io, [&]() -> taar::awaitable<void>
        {
            auto [ec, v] = co_await gen.next();
            (void)ec;
            got = std::move(v);
        }, net::detached);
        net::post(io, [&] { while_waiting(); });
        io.restart();
        io.run();
        return got;
    }
};

taar::chunked_response<std::string> open_stream(endpoint& e, std::string sid)
{
    http::request<http::empty_body> req {http::verb::get, "/mcp", 11};
    req.set(http::field::accept, "text/event-stream");
    if (!sid.empty()) req.set("Mcp-Session-Id", sid);
    taar::matcher::context mctx;
    return e.stream_handler()(req, mctx);
}

std::string header_value(taar::chunked_response<std::string> const& gen, http::field f)
{
    for (auto const& h : gen.headers())
    {
        if (h.field == f) return h.value;
    }
    return {};
}

BOOST_AUTO_TEST_CASE(test_mcp_stream_rejects_missing_or_unknown_session)
{
    endpoint e {{.name = "s", .version = "1"}};
    initialize(e);

    stream_reader missing {open_stream(e, "")};
    auto m = missing.next();
    BOOST_REQUIRE(m.has_value());
    BOOST_TEST(missing.gen.status() == http::status::bad_request);
    BOOST_TEST(*m == "Missing Mcp-Session-Id");

    stream_reader unknown {open_stream(e, "nope")};
    auto u = unknown.next();
    BOOST_REQUIRE(u.has_value());
    BOOST_TEST(unknown.gen.status() == http::status::not_found);
}

BOOST_AUTO_TEST_CASE(test_mcp_stream_delivers_notifications)
{
    endpoint e {{.name = "s", .version = "1"}};
    auto sid = initialize(e);

    BOOST_TEST(e.notify(sid, "notifications/tools/list_changed"));   // queued before the stream opens
    BOOST_TEST(!e.notify("unknown", "notifications/tools/list_changed"));

    stream_reader r {open_stream(e, sid)};
    auto first = r.next();
    BOOST_REQUIRE(first.has_value());
    BOOST_TEST(r.gen.status() == http::status::ok);
    BOOST_TEST(header_value(r.gen, http::field::content_type) == "text/event-stream");
    BOOST_TEST(header_value(r.gen, http::field::cache_control) == "no-cache");
    BOOST_TEST(*first == ": connected\n\n");

    auto queued = r.next();
    BOOST_REQUIRE(queued.has_value());
    BOOST_TEST(*queued == "event: message\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/tools/list_changed\"}\n\n");

    // Pushed while the stream is waiting.
    auto progress = r.next([&]
    {
        BOOST_TEST(e.notify(sid, "notifications/progress", json::object {{"progress", 1}}));
    });
    BOOST_REQUIRE(progress.has_value());
    auto const data = progress->substr(std::string {"event: message\ndata: "}.size());
    auto const msg = json::parse(data.substr(0, data.size() - 2));
    BOOST_TEST(msg.at("method").as_string() == "notifications/progress");
    BOOST_TEST(msg.at("params").at("progress").as_int64() == 1);
}

BOOST_AUTO_TEST_CASE(test_mcp_stream_one_per_session_and_reattach)
{
    endpoint e {{.name = "s", .version = "1"}};
    auto sid = initialize(e);

    auto first = std::make_optional<stream_reader>(open_stream(e, sid));
    BOOST_REQUIRE(first->next().has_value());

    stream_reader second {open_stream(e, sid)};
    BOOST_REQUIRE(second.next().has_value());
    BOOST_TEST(second.gen.status() == http::status::conflict);

    first.reset();      // client went away
    stream_reader third {open_stream(e, sid)};
    BOOST_REQUIRE(third.next().has_value());
    BOOST_TEST(third.gen.status() == http::status::ok);
}

BOOST_AUTO_TEST_CASE(test_mcp_stream_ends_when_session_deleted)
{
    endpoint e {{.name = "s", .version = "1"}};
    auto sid = initialize(e);

    stream_reader r {open_stream(e, sid)};
    BOOST_REQUIRE(r.next().has_value());

    auto end = r.next([&]
    {
        auto del = post(e, http::verb::delete_, "", sid);
        BOOST_TEST(del.result() == http::status::ok);
    });
    BOOST_TEST(!end.has_value());      // end of stream
}

BOOST_AUTO_TEST_CASE(test_mcp_broadcast_reaches_every_session)
{
    endpoint e {{.name = "s", .version = "1"}};
    auto a = initialize(e);
    auto b = initialize(e);
    BOOST_TEST(e.broadcast("notifications/tools/list_changed") == 2u);

    for (auto const& sid : {a, b})
    {
        stream_reader r {open_stream(e, sid)};
        r.next();
        auto m = r.next();
        BOOST_REQUIRE(m.has_value());
        BOOST_TEST(m->find("notifications/tools/list_changed") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(test_mcp_session_info_lets_a_tool_address_its_own_stream)
{
    endpoint e {{.name = "s", .version = "1"}};
    e.tool("whoami", "Session id", [](mcp::session_info const& s) { return s.id; });
    e.tool("ping_me", "Notify the caller", [&](chat_session& c, mcp::session_info const& s)
    {
        ++c.messages;
        return e.notify(s.id, "notifications/message", json::object {{"level", "info"}, {"data", c.messages}});
    });
    auto sid = initialize(e);

    auto who = json::parse(post(e, http::verb::post, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"whoami"}})", sid).body());
    BOOST_TEST(who.at("result").at("structuredContent").at("result").as_string() == sid);

    auto schema = json::parse(post(e, http::verb::post, R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})", sid).body());
    BOOST_TEST(schema.at("result").at("tools").as_array()[0].at("inputSchema").at("properties").as_object().empty());   // contexts are not parameters

    stream_reader r {open_stream(e, sid)};
    BOOST_REQUIRE(r.next().has_value());
    auto msg = r.next([&]
    {
        auto res = json::parse(post(e, http::verb::post, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ping_me"}})", sid).body());
        BOOST_TEST(res.at("result").at("structuredContent").at("result").as_bool());
    });
    BOOST_REQUIRE(msg.has_value());
    BOOST_TEST(msg->find("notifications/message") != std::string::npos);
    BOOST_TEST(msg->find("\"data\":1") != std::string::npos);
}

} // namespace
