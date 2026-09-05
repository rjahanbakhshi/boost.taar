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
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace mcp = boost::taar::mcp;
namespace jsonrpc = boost::taar::jsonrpc;

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

// The JSON payload of an "id: N\nevent: message\ndata: ...\n\n" frame.
std::string frame_data(std::string const& frame)
{
    auto const start = frame.find("data: ") + 6;
    return frame.substr(start, frame.size() - start - 2);
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
    BOOST_TEST(queued->starts_with("id: 1\nevent: message\ndata: "));
    BOOST_TEST(frame_data(*queued) == "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/tools/list_changed\"}");

    // Pushed while the stream is waiting.
    auto progress = r.next([&]
    {
        BOOST_TEST(e.notify(sid, "notifications/progress", json::object {{"progress", 1}}));
    });
    BOOST_REQUIRE(progress.has_value());
    auto const msg = json::parse(frame_data(*progress));
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

BOOST_AUTO_TEST_CASE(test_mcp_progress_notifications_reach_the_stream)
{
    endpoint e {{.name = "s", .version = "1"}};
    e.tool("export", "Report progress", [](jsonrpc::request_scope const& scope, int items)
    {
        for (int i = 1; i <= items; ++i) scope.report_progress(i, items, "step " + std::to_string(i));
        return items;
    }, mcp::mcp_arg("items"));
    auto sid = initialize(e);

    stream_reader r {open_stream(e, sid)};
    BOOST_REQUIRE(r.next().has_value());
    auto first = r.next([&]
    {
        auto res = json::parse(post(e, http::verb::post,
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export","arguments":{"items":2},"_meta":{"progressToken":"tok-1"}}})", sid).body());
        BOOST_TEST(res.at("result").at("structuredContent").at("result").as_int64() == 2);
    });
    BOOST_REQUIRE(first.has_value());
    auto msg = json::parse(frame_data(*first));
    BOOST_TEST(msg.at("method").as_string() == "notifications/progress");
    BOOST_TEST(msg.at("params").at("progressToken").as_string() == "tok-1");
    BOOST_TEST(msg.at("params").at("progress").to_number<double>() == 1.0);
    BOOST_TEST(msg.at("params").at("total").to_number<double>() == 2.0);
    BOOST_TEST(msg.at("params").at("message").as_string() == "step 1");
    auto second = r.next();
    BOOST_REQUIRE(second.has_value());
    BOOST_TEST(second->find("\"progress\":2") != std::string::npos);

    // Without a progressToken the reports are dropped silently.
    auto quiet = json::parse(post(e, http::verb::post,
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"export","arguments":{"items":1}}})", sid).body());
    BOOST_TEST(quiet.at("result").at("structuredContent").at("result").as_int64() == 1);
}

BOOST_AUTO_TEST_CASE(test_mcp_cancelled_notification_interrupts_a_tool_call)
{
    using namespace std::chrono_literals;
    endpoint e {{.name = "s", .version = "1"}};
    e.tool("slow", "Wait", [](jsonrpc::request_scope const&, int ms) -> taar::awaitable<std::string>
    {
        net::steady_timer timer {co_await net::this_coro::executor};
        timer.expires_after(std::chrono::milliseconds {ms});
        co_await timer.async_wait(taar::use_awaitable);
        co_return "done";
    }, mcp::mcp_arg("ms"));
    auto sid = initialize(e);

    // Start the slow call and, 50 ms later, post notifications/cancelled on
    // the same session. The notification bypasses the session lock.
    http::request<http::string_body> req {http::verb::post, "/mcp", 11};
    req.set(http::field::content_type, "application/json");
    req.set("Mcp-Session-Id", sid);
    req.body() = R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"slow","arguments":{"ms":2000}}})";
    req.prepare_payload();
    http::request<http::string_body> cancel {http::verb::post, "/mcp", 11};
    cancel.set(http::field::content_type, "application/json");
    cancel.set("Mcp-Session-Id", sid);
    cancel.body() = R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":9,"reason":"user"}})";
    cancel.prepare_payload();

    taar::matcher::context mctx;
    auto handler = e.handler();
    net::io_context io;
    std::optional<http::response<http::string_body>> slow_res;
    std::optional<http::response<http::string_body>> cancel_res;
    auto const started = std::chrono::steady_clock::now();
    net::co_spawn(io, [&]() -> taar::awaitable<void> { slow_res = co_await handler(taar::handler::contexts<> {}, req, mctx); }, net::detached);
    net::steady_timer t {io, 50ms};
    t.async_wait([&](auto)
    {
        net::co_spawn(io, [&]() -> taar::awaitable<void> { cancel_res = co_await handler(taar::handler::contexts<> {}, cancel, mctx); }, net::detached);
    });
    io.run();
    BOOST_TEST((std::chrono::steady_clock::now() - started) < 1s);
    BOOST_REQUIRE(cancel_res.has_value());
    BOOST_TEST(cancel_res->result() == http::status::accepted);
    BOOST_REQUIRE(slow_res.has_value());
    BOOST_TEST(slow_res->result() == http::status::accepted);     // cancelled: no response envelope
    BOOST_TEST(slow_res->body().empty());
}

BOOST_AUTO_TEST_CASE(test_mcp_resource_subscriptions)
{
    endpoint e {{.name = "s", .version = "1"}};
    e.resource("notes://all", "text/plain", "All notes", [] { return std::string {"x"}; });
    auto a = initialize(e);
    auto b = initialize(e);

    auto ok = json::parse(post(e, http::verb::post, R"({"jsonrpc":"2.0","id":1,"method":"resources/subscribe","params":{"uri":"notes://all"}})", a).body());
    BOOST_TEST(ok.at("result").as_object().empty());
    auto bad = json::parse(post(e, http::verb::post, R"({"jsonrpc":"2.0","id":2,"method":"resources/subscribe","params":{}})", a).body());
    BOOST_TEST(bad.at("error").at("code").as_int64() == -32602);

    BOOST_TEST(e.resource_updated("notes://all") == 1u);       // only session a
    BOOST_TEST(e.resource_updated("notes://other") == 0u);

    stream_reader ra {open_stream(e, a)};
    BOOST_REQUIRE(ra.next().has_value());
    auto msg = ra.next();
    BOOST_REQUIRE(msg.has_value());
    BOOST_TEST(msg->find("notifications/resources/updated") != std::string::npos);
    BOOST_TEST(msg->find("notes://all") != std::string::npos);

    post(e, http::verb::post, R"({"jsonrpc":"2.0","id":3,"method":"resources/unsubscribe","params":{"uri":"notes://all"}})", a);
    BOOST_TEST(e.resource_updated("notes://all") == 0u);
    BOOST_TEST(e.resources_changed() == 2u);                    // list_changed reaches every session
    (void)b;
}

BOOST_AUTO_TEST_CASE(test_mcp_stream_resumes_from_last_event_id)
{
    endpoint e {{.name = "s", .version = "1", .stream_history = 10}};
    auto sid = initialize(e);
    auto last_id = [](std::string const& frame)
    {
        auto const pos = frame.find("id: ");
        return std::stoull(frame.substr(pos + 4, frame.find('\n', pos) - pos - 4));
    };

    e.notify(sid, "notifications/message", json::object {{"n", 1}});
    e.notify(sid, "notifications/message", json::object {{"n", 2}});
    std::uint64_t seen = 0;
    {
        stream_reader r {open_stream(e, sid)};
        BOOST_REQUIRE(r.next().has_value());                       // ": connected"
        auto first = r.next();
        BOOST_REQUIRE(first.has_value());
        BOOST_TEST(first->starts_with("id: 1\nevent: message\n"));
        seen = last_id(*first);                                     // the client saw event 1 only
    }                                                               // ... then the connection dropped

    // Events 2 (never delivered) and 3, 4 (pushed while disconnected) all
    // arrive once, in order, when the client resumes after event 1.
    e.notify(sid, "notifications/message", json::object {{"n", 3}});
    e.notify(sid, "notifications/message", json::object {{"n", 4}});
    http::request<http::empty_body> req {http::verb::get, "/mcp", 11};
    req.set("Mcp-Session-Id", sid);
    req.set("Last-Event-ID", std::to_string(seen));
    taar::matcher::context mctx;
    stream_reader r {e.stream_handler()(req, mctx)};
    BOOST_REQUIRE(r.next().has_value());
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < 3; ++i)
    {
        auto frame = r.next();
        BOOST_REQUIRE(frame.has_value());
        ids.push_back(last_id(*frame));
        BOOST_TEST(frame->find("\"n\":" + std::to_string(i + 2)) != std::string::npos);
    }
    BOOST_TEST((ids == std::vector<std::uint64_t> {2, 3, 4}));
    // Nothing duplicated: a fresh push is the only thing left to read.
    auto next = r.next([&] { e.notify(sid, "notifications/message", json::object {{"n", 5}}); });
    BOOST_REQUIRE(next.has_value());
    BOOST_TEST(last_id(*next) == 5u);
}

BOOST_AUTO_TEST_CASE(test_mcp_server_to_client_requests)
{
    endpoint e {{.name = "s", .version = "1"}};
    json::object const name_schema {
        {"type", "object"}, {"properties", json::object {{"name", json::object {{"type", "string"}}}}}};
    e.tool("ask", "Elicit a name", [&](mcp::session_info const& me) -> taar::awaitable<std::string>
    {
        BOOST_TEST(me.client.name == "agent");
        BOOST_TEST(me.capabilities.elicitation);
        BOOST_TEST(me.capabilities.roots);
        BOOST_TEST(me.capabilities.roots_list_changed);
        BOOST_TEST(!me.capabilities.sampling);
        auto reply = co_await e.elicit(me.id, "Your name?", name_schema);
        co_return std::string {reply.at("action").as_string()} + ":" + std::string {reply.at("content").at("name").as_string()};
    });
    e.tool("roots", "List roots", [&](mcp::session_info const& me) -> taar::awaitable<json::value>
    {
        co_return co_await e.list_roots(me.id);
    });
    e.tool("sample", "Sampling that the client refuses", [&](mcp::session_info const& me) -> taar::awaitable<std::string>
    {
        try
        {
            co_await e.create_message(me.id, json::object {{"maxTokens", 10}});
        }
        catch (mcp::client_error const& err)
        {
            co_return std::to_string(err.code()) + " " + err.what() + " " + json::serialize(err.data());
        }
        co_return "no error";
    });
    e.tool("impatient", "Gives up quickly", [&](mcp::session_info const& me) -> taar::awaitable<std::string>
    {
        try
        {
            co_await e.list_roots(me.id, std::chrono::milliseconds {20});
        }
        catch (boost::system::system_error const& err)
        {
            co_return err.code() == boost::system::errc::timed_out ? "timed out" : std::string {"other: "} + err.what();
        }
        co_return "answered";
    });
    int roots_changed = 0;
    e.on_client_notification("notifications/roots/list_changed", [&](auto const&, json::value const& params)
    {
        ++roots_changed;
        BOOST_TEST(params.is_null());
    });

    auto init = post(e, http::verb::post,
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25",)"
        R"("capabilities":{"elicitation":{},"roots":{"listChanged":true}},"clientInfo":{"name":"agent","version":"0.1"}}})");
    std::string const sid {init["Mcp-Session-Id"]};

    // Runs one tools/call and a scripted client on a shared io_context: the
    // client reads the server's request off the stream and answers it with
    // a POST (or does not), then the tool's reply comes back.
    auto exchange = [&](std::string tool, auto responder) -> json::value
    {
        auto handler = e.handler();
        auto gen = open_stream(e, sid);
        net::io_context io;
        gen.set_executor(io.get_executor());
        taar::matcher::context mctx;
        std::optional<json::value> tool_reply;
        http::request<http::string_body> call {http::verb::post, "/mcp", 11};
        call.set(http::field::content_type, "application/json");
        call.set("Mcp-Session-Id", sid);
        call.body() = R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":")" + tool + R"("}})";
        call.prepare_payload();
        net::co_spawn(io, [&]() -> taar::awaitable<void>
        {
            auto res = co_await handler(taar::handler::contexts<> {}, call, mctx);
            tool_reply = json::parse(res.body());
        }, net::detached);
        net::co_spawn(io, [&]() -> taar::awaitable<void>
        {
            auto [ec0, hello] = co_await gen.next();
            BOOST_REQUIRE(hello.has_value());
            auto [ec, frame] = co_await gen.next();
            BOOST_REQUIRE(frame.has_value());
            auto request = json::parse(frame_data(*frame));
            BOOST_TEST(request.at("jsonrpc").as_string() == "2.0");
            auto answer = responder(request);
            if (answer.is_null()) co_return;
            answer.as_object()["jsonrpc"] = "2.0";
            answer.as_object()["id"] = request.at("id");
            http::request<http::string_body> res {http::verb::post, "/mcp", 11};
            res.set(http::field::content_type, "application/json");
            res.set("Mcp-Session-Id", sid);
            res.body() = json::serialize(answer);
            res.prepare_payload();
            auto ack = co_await handler(taar::handler::contexts<> {}, res, mctx);
            BOOST_TEST(ack.result_int() == 202);
        }, net::detached);
        io.run();
        BOOST_REQUIRE(tool_reply.has_value());
        return tool_reply->at("result");
    };
    auto text_of = [](json::value const& result)
    {
        return std::string {result.at("content").as_array()[0].at("text").as_string()};
    };

    auto asked = exchange("ask", [&](json::value const& request) -> json::value
    {
        BOOST_TEST(request.at("method").as_string() == "elicitation/create");
        BOOST_TEST(request.at("params").at("message").as_string() == "Your name?");
        BOOST_TEST(request.at("params").at("requestedSchema") == name_schema);
        return json::object {{"result", json::object {{"action", "accept"}, {"content", json::object {{"name", "Reza"}}}}}};
    });
    BOOST_TEST(text_of(asked) == "accept:Reza");
    BOOST_TEST(asked.at("isError").as_bool() == false);

    auto roots = exchange("roots", [&](json::value const& request) -> json::value
    {
        BOOST_TEST(request.at("method").as_string() == "roots/list");
        BOOST_TEST(!request.as_object().contains("params"));
        return json::object {{"result", json::object {{"roots", json::array {json::object {{"uri", "file:///home"}, {"name", "home"}}}}}}};
    });
    BOOST_TEST(roots.at("structuredContent").at("roots").as_array()[0].at("uri").as_string() == "file:///home");

    auto refused = exchange("sample", [&](json::value const& request) -> json::value
    {
        BOOST_TEST(request.at("method").as_string() == "sampling/createMessage");
        BOOST_TEST(request.at("params").at("maxTokens").as_int64() == 10);
        return json::object {{"error", json::object {{"code", -1}, {"message", "User rejected"}, {"data", json::object {{"why", "no"}}}}}};
    });
    BOOST_TEST(text_of(refused) == "-1 User rejected {\"why\":\"no\"}");

    auto late = exchange("impatient", [&](json::value const&) -> json::value { return nullptr; });
    BOOST_TEST(text_of(late) == "timed out");

    // Request ids are distinct across calls; nothing is left pending.
    // An unknown session is rejected up front.
    net::io_context io;
    bool threw = false;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        try { co_await e.list_roots("nope"); }
        catch (std::out_of_range const&) { threw = true; }
    }, net::detached);
    io.run();
    BOOST_TEST(threw);

    // A client notification reaches its hook.
    auto ack = post(e, http::verb::post, R"({"jsonrpc":"2.0","method":"notifications/roots/list_changed"})", sid);
    BOOST_TEST(ack.result_int() == 202);
    BOOST_TEST(roots_changed == 1);

    // A stray response (no request waiting) is still acknowledged.
    auto stray = post(e, http::verb::post, R"({"jsonrpc":"2.0","id":999,"result":{}})", sid);
    BOOST_TEST(stray.result_int() == 202);
}

BOOST_AUTO_TEST_CASE(test_mcp_stream_queue_is_bounded_without_a_stream)
{
    endpoint e {{.name = "s", .version = "1", .stream_history = 5}};
    auto sid = initialize(e);
    for (int n = 1; n <= 8; ++n) e.notify(sid, "notifications/message", json::object {{"n", n}});

    // Only the newest five are still queued when the client finally connects.
    stream_reader r {open_stream(e, sid)};
    BOOST_REQUIRE(r.next().has_value());
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < 5; ++i)
    {
        auto frame = r.next();
        BOOST_REQUIRE(frame.has_value());
        auto const pos = frame->find("id: ") + 4;
        ids.push_back(std::stoull(frame->substr(pos, frame->find('\n', pos) - pos)));
    }
    BOOST_TEST((ids == std::vector<std::uint64_t> {4, 5, 6, 7, 8}));
    auto fresh = r.next([&] { e.notify(sid, "notifications/message", json::object {{"n", 9}}); });
    BOOST_REQUIRE(fresh.has_value());
    BOOST_TEST(fresh->starts_with("id: 9\n"));
}

BOOST_AUTO_TEST_CASE(test_mcp_stream_keepalive_comments)
{
    using namespace std::chrono_literals;
    endpoint e {{.name = "s", .version = "1", .stream_keepalive = 30ms}};
    auto sid = initialize(e);
    stream_reader r {open_stream(e, sid)};
    BOOST_REQUIRE(r.next().has_value());                       // ": connected"
    auto idle = r.next();                                       // nothing pushed: a keepalive arrives
    BOOST_REQUIRE(idle.has_value());
    BOOST_TEST(*idle == ": keepalive\n\n");
    auto event = r.next([&] { e.notify(sid, "notifications/message", json::object {{"n", 1}}); });
    BOOST_REQUIRE(event.has_value());
    BOOST_TEST(event->starts_with("id: 1\n"));
}

BOOST_AUTO_TEST_CASE(test_mcp_request_default_timeout_bounds_the_wait)
{
    using namespace std::chrono_literals;
    endpoint e {{.name = "s", .version = "1", .request_timeout = 40ms}};
    auto sid = initialize(e);                                    // no stream opened, ever
    e.tool("ask", "Elicit", [&](mcp::session_info const& me) -> taar::awaitable<std::string>
    {
        try
        {
            co_await e.elicit(me.id, "?", json::object {{"type", "object"}});
        }
        catch (boost::system::system_error const& err)
        {
            co_return err.code() == boost::system::errc::timed_out ? "timed out" : "other";
        }
        co_return "answered";
    });
    auto res = post(e, http::verb::post, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ask"}})", sid);
    BOOST_TEST(json::parse(res.body()).at("result").at("content").as_array()[0].at("text").as_string() == "timed out");
}

} // namespace
