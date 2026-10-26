//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/mcp/endpoint.hpp>
#include <boost/test/unit_test.hpp>

#if defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <csignal>
#include <functional>
#include <optional>
#include <string_view>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace net = boost::asio;
namespace json = boost::json;
namespace taar = boost::taar;
namespace mcp = boost::taar::mcp;
using mcp::mcp_arg;

struct notes { std::vector<std::string> items; };
using token = net::use_awaitable_t<net::any_io_executor>;

/// A stdio server wired to two pipes, driven from a test coroutine.
struct stdio_harness
{
    net::io_context io;
    mcp::endpoint<notes> server {{.name = "notes", .version = "1"}};
    int in_pipe[2] {};
    int out_pipe[2] {};
    bool finished = false;
    std::function<void()> close_input;      ///< Ends the server's stdin from inside a client script.

    stdio_harness()
    {
        BOOST_REQUIRE(::pipe(in_pipe) == 0);
        BOOST_REQUIRE(::pipe(out_pipe) == 0);
        server.tool("notes/add", "Add", [](notes& n, std::string text) { n.items.push_back(text); return n.items.size(); }, mcp_arg("text"));
        server.tool("notes/announce", "Broadcast", [this] { return server.broadcast("notifications/resources/updated", json::object {{"uri", "notes://all"}}); });
        server.tool("slow", "Wait", [](int ms) -> taar::awaitable<std::string>
        {
            net::steady_timer timer {co_await net::this_coro::executor};
            timer.expires_after(std::chrono::milliseconds {ms});
            co_await timer.async_wait(token {});
            co_return "done";
        }, mcp_arg("ms"));
        std::signal(SIGPIPE, SIG_IGN);
        // The server owns the read end of stdin and the write end of stdout.
        net::co_spawn(io, [this]() -> taar::awaitable<void>
        {
            co_await server.serve_stdio(in_pipe[0], out_pipe[1], server.default_incoming());
            finished = true;
        }, net::detached);
    }

    template <typename Client>
    void run(Client client)
    {
        net::co_spawn(io, [&]() -> taar::awaitable<void>
        {
            net::posix::stream_descriptor in {co_await net::this_coro::executor, in_pipe[1]};
            net::posix::stream_descriptor out {co_await net::this_coro::executor, out_pipe[0]};
            close_input = [&in] { if (in.is_open()) in.close(); };
            net::streambuf buffer;
            auto send = [&](std::string line, bool newline = true) -> taar::awaitable<void>
            {
                if (newline) line += '\n';
                co_await net::async_write(in, net::buffer(line), token {});
            };
            auto receive = [&]() -> taar::awaitable<json::value>
            {
                auto n = co_await net::async_read_until(out, buffer, '\n', token {});
                std::string line {net::buffers_begin(buffer.data()), net::buffers_begin(buffer.data()) + static_cast<std::ptrdiff_t>(n)};
                buffer.consume(n);
                co_return json::parse(line);
            };
            co_await client(send, receive);
            close_input();  // EOF for the server
        }, net::detached);
        io.run();
    }
};

BOOST_AUTO_TEST_CASE(test_mcp_stdio_lifecycle_tools_and_notifications)
{
    stdio_harness h;
    h.run([&](auto& send, auto& receive) -> taar::awaitable<void>
    {
        co_await send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","clientInfo":{"name":"t","version":"1"}}})");
        auto init = co_await receive();
        BOOST_TEST(init.at("id").as_int64() == 1);
        BOOST_TEST(init.at("result").at("serverInfo").at("name").as_string() == "notes");
        BOOST_TEST(h.server.session_count() == 1u);

        co_await send(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        co_await send(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"notes/add","arguments":{"text":"milk"}}})");
        auto added = co_await receive();
        BOOST_TEST(added.at("id").as_int64() == 2);
        BOOST_TEST(added.at("result").at("structuredContent").at("result").as_int64() == 1);

        // A broadcast reaches the stdio client as a notification line.
        co_await send(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"notes/announce"}})");
        json::value a = co_await receive();
        json::value b = co_await receive();
        auto const& notification = a.as_object().contains("method") ? a : b;
        auto const& reply = a.as_object().contains("method") ? b : a;
        BOOST_TEST(notification.at("method").as_string() == "notifications/resources/updated");
        BOOST_TEST(!notification.as_object().contains("id"));
        BOOST_TEST(reply.at("result").at("structuredContent").at("result").as_int64() == 1);

        co_await send("{not json");
        auto bad = co_await receive();
        BOOST_TEST(bad.at("error").at("code").as_int64() == -32700);

        co_await send(R"([{"jsonrpc":"2.0","id":4,"method":"ping"}])");
        auto batch = co_await receive();
        BOOST_TEST(batch.at("error").at("code").as_int64() == -32600);

        co_await send(R"({"jsonrpc":"2.0","id":9,"result":{}})");       // client response: ignored
        co_await send(R"({"jsonrpc":"2.0","id":5,"method":"ping"})");
        auto pong = co_await receive();
        BOOST_TEST(pong.at("id").as_int64() == 5);
    });
    BOOST_TEST(h.finished);
    BOOST_TEST(h.server.session_count() == 0u);    // EOF ended the implicit session
}

BOOST_AUTO_TEST_CASE(test_mcp_stdio_requires_initialize_first)
{
    stdio_harness h;
    h.run([&](auto& send, auto& receive) -> taar::awaitable<void>
    {
        co_await send(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
        auto err = co_await receive();
        BOOST_TEST(err.at("error").at("code").as_int64() == -32600);
        BOOST_TEST(err.at("error").at("message").as_string() == "Server not initialized");
        co_await send(R"({"jsonrpc":"2.0","id":2,"method":"initialize"})");
        auto init = co_await receive();
        BOOST_TEST(init.at("result").at("protocolVersion").as_string() == "2025-11-25");
        co_await send(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
        auto tools = co_await receive();
        BOOST_TEST(tools.at("result").at("tools").as_array().size() == 3u);
    });
    BOOST_TEST(h.finished);
}

BOOST_AUTO_TEST_CASE(test_mcp_stdio_cancelled_notification_interrupts_a_tool_call)
{
    using namespace std::chrono_literals;
    stdio_harness h;
    h.run([&](auto& send, auto& receive) -> taar::awaitable<void>
    {
        co_await send(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
        co_await receive();
        auto const started = std::chrono::steady_clock::now();
        co_await send(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"slow","arguments":{"ms":2000}}})");
        net::steady_timer pause {co_await net::this_coro::executor, 50ms};
        co_await pause.async_wait(token {});
        co_await send(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":2}})");
        // The cancelled request yields no reply; the next request is answered promptly.
        co_await send(R"({"jsonrpc":"2.0","id":3,"method":"ping"})");
        auto pong = co_await receive();
        BOOST_TEST(pong.at("id").as_int64() == 3);
        BOOST_TEST((std::chrono::steady_clock::now() - started) < 1s);
    });
    BOOST_TEST(h.finished);
}

BOOST_AUTO_TEST_CASE(test_mcp_stdio_server_to_client_request)
{
    stdio_harness h;
    h.server.tool("confirm", "Ask before acting", [&](mcp::session_info const& me) -> taar::awaitable<std::string>
    {
        auto reply = co_await h.server.elicit(me.id, "Sure?",
            json::object {{"type", "object"}, {"properties", json::object {{"ok", json::object {{"type", "boolean"}}}}}});
        co_return reply.at("content").at("ok").as_bool() ? "did it" : "skipped";
    });
    h.run([&](auto& send, auto& receive) -> taar::awaitable<void>
    {
        co_await send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{"elicitation":{}},"clientInfo":{"name":"t","version":"1"}}})");
        (void)co_await receive();
        co_await send(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"confirm"}})");
        auto request = co_await receive();
        BOOST_TEST(request.at("method").as_string() == "elicitation/create");
        BOOST_TEST(request.at("params").at("message").as_string() == "Sure?");
        BOOST_TEST(!request.as_object().contains("result"));
        co_await send(R"({"jsonrpc":"2.0","id":)" + json::serialize(request.at("id")) +
            R"(,"result":{"action":"accept","content":{"ok":true}}})");
        auto reply = co_await receive();
        BOOST_TEST(reply.at("id").as_int64() == 2);
        BOOST_TEST(reply.at("result").at("content").as_array()[0].at("text").as_string() == "did it");
    });
    BOOST_TEST(h.finished);
}

BOOST_AUTO_TEST_CASE(test_mcp_stdio_full_session_scenario)
{
    using namespace std::chrono_literals;
    stdio_harness h;
    h.server.tool("confirm", "Ask before acting", [&](mcp::session_info const& me) -> taar::awaitable<std::string>
    {
        auto reply = co_await h.server.elicit(me.id, "Sure?",
            json::object {{"type", "object"}, {"properties", json::object {{"ok", json::object {{"type", "boolean"}}}}}});
        co_return reply.at("content").at("ok").as_bool() ? "did it" : "skipped";
    });
    h.server.tool("logged", "Logs at info", [&](mcp::session_info const& me)
    {
        return h.server.log(me.id, mcp::log_level::info, "hello", "test");
    });
    h.server.resource("notes://{index}{?format}", "text/plain", "One note",
        [](notes const& n, int index, std::optional<std::string> format)
        {
            return (format.value_or("plain") == "upper" ? "NOTE " : "note ") + std::to_string(index) + ": " + n.items.at(index);
        }, mcp::uri_arg("index"), mcp::uri_arg("format"));
    h.server.prompt("summarize", "Summarize", [](std::string style) { return "Summarize " + style; }, mcp_arg("style"));
    h.server.complete_prompt("summarize", "style", [](std::string_view v)
    {
        std::vector<std::string> out;
        for (std::string_view s : {"brief", "bullets"}) if (s.starts_with(v)) out.emplace_back(s);
        return out;
    });

    h.run([&](auto& send, auto& receive) -> taar::awaitable<void>
    {
        co_await send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{"elicitation":{}},"clientInfo":{"name":"t","version":"1"}}})");
        auto init = co_await receive();
        auto const& caps = init.at("result").at("capabilities").as_object();
        BOOST_TEST(caps.contains("logging"));
        BOOST_TEST(caps.contains("completions"));
        co_await send(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");

        // tools/list carries the derived output schema of notes/add (size_t -> integer under result).
        co_await send(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
        auto tools = co_await receive();
        bool saw_schema = false;
        for (auto const& t : tools.at("result").at("tools").as_array())
        {
            if (t.at("name").as_string() == "notes/add")
            {
                saw_schema = t.at("outputSchema").at("properties").at("result").at("type").as_string() == "integer";
            }
        }
        BOOST_TEST(saw_schema);

        co_await send(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"notes/add","arguments":{"text":"milk"}}})");
        auto const add_result = co_await receive();
        BOOST_TEST(add_result.at("result").at("structuredContent").at("result").as_int64() == 1);

        // Logging honours the level the client sets.
        co_await send(R"({"jsonrpc":"2.0","id":4,"method":"logging/setLevel","params":{"level":"warning"}})");
        (void)co_await receive();
        co_await send(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"logged"}})");
        auto const muted = co_await receive();
        BOOST_TEST(muted.at("result").at("structuredContent").at("result").as_bool() == false);
        co_await send(R"({"jsonrpc":"2.0","id":6,"method":"logging/setLevel","params":{"level":"debug"}})");
        (void)co_await receive();
        co_await send(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"logged"}})");
        // The result and the notifications/message line both arrive, in either order.
        bool got_result = false, got_log = false;
        for (int i = 0; i < 2; ++i)
        {
            auto line = co_await receive();
            if (line.as_object().contains("id")) got_result = line.at("result").at("structuredContent").at("result").as_bool();
            else
            {
                got_log = line.at("method").as_string() == "notifications/message";
                BOOST_TEST(line.at("params").at("level").as_string() == "info");
                BOOST_TEST(line.at("params").at("logger").as_string() == "test");
            }
        }
        BOOST_TEST(got_result);
        BOOST_TEST(got_log);

        // Server-to-client request answered over stdin.
        co_await send(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"confirm"}})");
        auto request = co_await receive();
        BOOST_TEST(request.at("method").as_string() == "elicitation/create");
        co_await send(R"({"jsonrpc":"2.0","id":)" + json::serialize(request.at("id")) + R"(,"result":{"action":"accept","content":{"ok":true}}})");
        auto const confirmed = co_await receive();
        BOOST_TEST(confirmed.at("result").at("content").as_array()[0].at("text").as_string() == "did it");

        // Completion and a templated resource with an optional query operator.
        co_await send(R"({"jsonrpc":"2.0","id":9,"method":"completion/complete","params":{"ref":{"type":"ref/prompt","name":"summarize"},"argument":{"name":"style","value":"b"}}})");
        auto const completion = co_await receive();
        BOOST_TEST(completion.at("result").at("completion").at("values").as_array().size() == 2u);
        co_await send(R"({"jsonrpc":"2.0","id":10,"method":"resources/read","params":{"uri":"notes://0?format=upper"}})");
        auto const resource = co_await receive();
        BOOST_TEST(resource.at("result").at("contents").as_array()[0].at("text").as_string() == "NOTE 0: milk");

        // A slow call cancelled mid-flight produces no response; the session goes on.
        co_await send(R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"slow","arguments":{"ms":5000}}})");
        co_await send(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":11}})");
        co_await send(R"({"jsonrpc":"2.0","id":12,"method":"ping"})");
        auto pong = co_await receive();
        BOOST_TEST(pong.at("id").as_int64() == 12);
    });
    BOOST_TEST(h.finished);
    BOOST_TEST(h.server.session_count() == 0u);
}

BOOST_AUTO_TEST_CASE(test_mcp_stdio_eof_waits_for_running_calls)
{
    stdio_harness h;
    h.run([&](auto& send, auto& receive) -> taar::awaitable<void>
    {
        co_await send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})");
        (void)co_await receive();
        co_await send(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"slow","arguments":{"ms":150}}})");
        h.close_input();                    // stdin ends while the call is still running
        auto reply = co_await receive();    // ... and its answer is still written
        BOOST_TEST(reply.at("id").as_int64() == 2);
        BOOST_TEST(reply.at("result").at("content").as_array()[0].at("text").as_string() == "done");
    });
    BOOST_TEST(h.finished);
    BOOST_TEST(h.server.session_count() == 0u);
}

BOOST_AUTO_TEST_CASE(test_mcp_stdio_final_line_without_newline)
{
    stdio_harness h;
    h.run([&](auto& send, auto& receive) -> taar::awaitable<void>
    {
        co_await send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})");
        (void)co_await receive();
        co_await send(R"({"jsonrpc":"2.0","id":9,"method":"ping"})", false);   // no trailing newline
        h.close_input();
        auto reply = co_await receive();
        BOOST_TEST(reply.at("id").as_int64() == 9);
    });
    BOOST_TEST(h.finished);
}

} // namespace

#endif // BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR
