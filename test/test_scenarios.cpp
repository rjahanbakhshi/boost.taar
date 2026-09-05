//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// Scenario tests: several features working together the way an
// application would combine them, end to end over TCP where it matters.

#include "e2e.hpp"
#include <boost/taar/core/async_channel.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/cors.hpp>
#include <boost/taar/handler/constrained.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/jsonrpc/arg.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/mcp/content.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/describe/class.hpp>
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
namespace mcp = boost::taar::mcp;
namespace openapi = boost::taar::openapi;
using namespace std::chrono_literals;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::constrained;
using taar::handler::header_arg;
using taar::handler::json_body_arg;
using taar::handler::path_arg;
using taar::handler::query_arg;
using taar::handler::query_list_arg;
using taar::handler::rest;
using taar::handler::session_policy;
using taar::handler::string_body_arg;
using taar::handler::with_default;
using taar::handler::with_session;
using jsonrpc::rpc_arg;
using mcp::mcp_arg;
using mcp::uri_arg;
namespace session_key = taar::handler::session_key;

using socket_type = taar::rebind_executor<net::ip::tcp::socket>;

// ---- a small domain shared by every surface ---------------------------------

struct task
{
    int id;
    std::string title;
    std::vector<std::string> tags;
    bool done;
};
BOOST_DESCRIBE_STRUCT(task, (), (id, title, tags, done))

struct task_input
{
    std::string title;
    std::vector<std::string> tags;
};
BOOST_DESCRIBE_STRUCT(task_input, (), (title, tags))

/// The board is shared by REST, JSON-RPC and MCP handlers running on several
/// threads, so it has its own lock; per-client state lives in the stores.
struct board
{
    std::mutex mutex;
    std::vector<task> tasks;
    int next_id = 1;

    task add(task_input in)
    {
        std::lock_guard<std::mutex> const g {mutex};
        task t {next_id++, std::move(in.title), std::move(in.tags), false};
        tasks.push_back(t);
        return t;
    }

    std::vector<task> list(std::vector<std::string> const& tags, int page, int per_page)
    {
        std::lock_guard<std::mutex> const g {mutex};
        std::vector<task> out;
        for (auto const& t : tasks)
        {
            bool const match = tags.empty() || std::ranges::any_of(tags, [&](auto const& tag)
            {
                return std::ranges::find(t.tags, tag) != t.tags.end();
            });
            if (match) out.push_back(t);
        }
        auto const start = static_cast<std::size_t>((page - 1) * per_page);
        if (start >= out.size()) return {};
        out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(start));
        if (out.size() > static_cast<std::size_t>(per_page)) out.resize(static_cast<std::size_t>(per_page));
        return out;
    }

    std::optional<task> get(int id)
    {
        std::lock_guard<std::mutex> const g {mutex};
        for (auto const& t : tasks) if (t.id == id) return t;
        return std::nullopt;
    }

    bool remove(int id)
    {
        std::lock_guard<std::mutex> const g {mutex};
        return std::erase_if(tasks, [&](auto const& t) { return t.id == id; }) > 0;
    }

    std::size_t size()
    {
        std::lock_guard<std::mutex> const g {mutex};
        return tasks.size();
    }
};

struct user_session { int requests = 0; };
struct agent_session { int calls = 0; };

/// Everything one server needs; kept together so the scenario can inspect it.
struct taskboard
{
    board tasks;
    taar::context_store<std::string, user_session> users {10min};
    taar::session::http session;
    jsonrpc::endpoint<> rpc;
    mcp::endpoint<agent_session> agent {{.name = "taskboard", .version = "1"}};
    openapi::generator openapi_doc {{.title = "Task board", .version = "1"}};
    taar::cors cors;

    taskboard()
    {
        session_key::cookie const sid {"sid"};

        session.set_response_filter(cors.response_filter());
        session.register_request_handler(method == http::verb::options, cors.preflight_handler());
        openapi_doc.add_security_scheme("bearer", openapi::security_scheme::bearer("opaque"));

        session.register_request_handler(
            method == http::verb::post && target == "/tasks",
            with_session(users, sid, session_policy::create_if_missing,
                rest([this](user_session& u, task_input in) { ++u.requests; return tasks.add(std::move(in)); },
                    json_body_arg(taar::handler::description {"The task to create"}))),
            openapi_doc.describe({.summary = "Create a task", .tags = {"tasks"}}));
        session.register_request_handler(
            method == http::verb::get && target == "/tasks",
            rest([this](std::vector<std::string> tags, int page, int per_page) { return tasks.list(tags, page, per_page); },
                with_default(query_list_arg("tag", "Only tasks carrying one of these tags"), std::vector<std::string> {}),
                constrained(with_default(query_arg("page", "1-based page"), 1), {.minimum = 1}),
                constrained(with_default(query_arg("per_page"), 10), {.minimum = 1, .maximum = 50})),
            openapi_doc.describe({.summary = "List tasks", .tags = {"tasks"}}));
        session.register_request_handler(
            method == http::verb::delete_ && target == "/tasks/{id}",
            rest([this](std::string const& auth, int id)
            {
                if (auth != "Bearer admin") throw std::invalid_argument {"not an admin"};
                return tasks.remove(id);
            }, header_arg(http::field::authorization), constrained(path_arg("id"), {.minimum = 1})),
            openapi_doc.describe({.summary = "Delete a task", .tags = {"tasks"}}));
        session.register_request_handler(
            method == http::verb::get && target == "/openapi.json", openapi_doc.handler());

        rpc.method("tasks.add", [this](task_input in) { return tasks.add(std::move(in)); }, rpc_arg("task"));
        rpc.method("tasks.list", [this](std::vector<std::string> tags) { return tasks.list(tags, 1, 50); },
            with_default(rpc_arg("tags"), std::vector<std::string> {}));
        session.register_request_handler(method == http::verb::post && target == "/rpc", rpc.handler());

        agent.tool(mcp::tool_spec {.name = "tasks/add", .description = "Create a task",
                .annotations = mcp::tool_annotations {.destructive_hint = false, .idempotent_hint = false}},
            [this](agent_session& s, task_input in) { ++s.calls; return tasks.add(std::move(in)); },
            mcp_arg("task", "The task"));
        agent.tool("tasks/count", "How many tasks exist", [this] { return tasks.size(); });
        agent.resource("tasks://{id}{?format}", "application/json", "One task",
            [this](int id, std::optional<std::string> format) -> json::value
            {
                auto t = tasks.get(id);
                if (!t) throw std::out_of_range {"no task " + std::to_string(id)};
                if (format.value_or("full") == "brief") return json::value {t->title};
                return json::value_from(*t);
            }, uri_arg("id"), uri_arg("format"));
        agent.prompt("tasks/triage", "Triage the board", [](std::string focus) { return "Triage tasks focusing on " + focus; },
            mcp_arg("focus", "What to look at"));
        agent.complete_prompt("tasks/triage", "focus", [](std::string_view typed)
        {
            std::vector<std::string> out;
            for (std::string_view s : {"overdue", "untagged", "done"}) if (s.starts_with(typed)) out.emplace_back(s);
            return out;
        });
        session.register_request_handler(method == http::verb::get && target == "/mcp", agent.stream_handler());
        session.register_request_handler(target == "/mcp", agent.handler());
    }
};

// ---- raw client helpers ------------------------------------------------------

taar::awaitable<socket_type> connect(unsigned short port)
{
    socket_type socket {co_await net::this_coro::executor};
    co_await socket.async_connect({net::ip::address_v4::loopback(), port});
    co_return socket;
}

/// One request/response on an open connection; @a head reads a header-only reply.
taar::awaitable<e2e::response> exchange(socket_type& socket, beast::flat_buffer& buffer, e2e::request req, bool head = false)
{
    if (!req.has_content_length() && !req.chunked()) req.prepare_payload();
    co_await http::async_write(socket, req);
    http::response_parser<http::string_body> parser;
    parser.skip(head);
    co_await http::async_read(socket, buffer, parser);
    co_return parser.release();
}

json::value body_of(e2e::response const& r) { return json::parse(r.body()); }

// ---- scenarios ---------------------------------------------------------------

/// A board served over all three surfaces to many concurrent clients on a
/// multi-threaded io_context, documented by OpenAPI, shut down from the
/// main thread.
BOOST_AUTO_TEST_CASE(test_scenario_taskboard_all_surfaces_multithreaded)
{
    constexpr int threads = 4;
    constexpr int clients = 12;

    net::io_context io;
    taskboard app;
    taar::cancellation_signals signals;
    std::atomic<unsigned short> port {0};
    net::co_spawn(io,
        taar::server::tcp("127.0.0.1", "0", app.session, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);

    std::atomic<int> finished {0};
    std::vector<std::string> errors(clients);
    for (int c = 0; c < clients; ++c)
    {
        net::co_spawn(io, [&, c]() -> taar::awaitable<void>
        {
            try
            {
                net::steady_timer timer {co_await net::this_coro::executor};
                while (port == 0) { timer.expires_after(1ms); co_await timer.async_wait(taar::use_awaitable); }
                auto socket = co_await connect(port);
                beast::flat_buffer buffer;
                std::string const tag = "t" + std::to_string(c % 3);

                // REST: create (mints a cookie), then list by tag with the cookie.
                auto created = co_await exchange(socket, buffer, e2e::make_request(http::verb::post, "/tasks",
                    json::serialize(json::object {{"title", "task-" + std::to_string(c)}, {"tags", json::array {tag}}})));
                if (created.result() != http::status::ok) throw std::runtime_error {"create " + std::to_string(created.result_int())};
                int const id = static_cast<int>(body_of(created).at("id").as_int64());
                std::string const cookie {created[http::field::set_cookie]};
                if (!cookie.starts_with("sid=")) throw std::runtime_error {"no cookie"};
                auto list = e2e::make_request(http::verb::get, "/tasks?tag=" + tag + "&per_page=50");
                list.set(http::field::cookie, cookie.substr(0, cookie.find(';')));
                auto listed = co_await exchange(socket, buffer, list);
                bool found = false;
                for (auto const& t : body_of(listed).as_array()) found = found || t.at("id").as_int64() == id;
                if (!found) throw std::runtime_error {"own task not listed"};

                // JSON-RPC sees the same board.
                auto rpc = co_await exchange(socket, buffer, e2e::make_request(http::verb::post, "/rpc",
                    R"({"jsonrpc":"2.0","id":1,"method":"tasks.list","params":{"tags":[")" + tag + R"("]}})"));
                found = false;
                for (auto const& t : body_of(rpc).at("result").as_array()) found = found || t.at("id").as_int64() == id;
                if (!found) throw std::runtime_error {"rpc did not list own task"};

                // MCP: its own session, a counting tool and a templated resource.
                auto init = co_await exchange(socket, buffer, e2e::make_request(http::verb::post, "/mcp",
                    R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})"));
                std::string const sid {init["Mcp-Session-Id"]};
                auto count = e2e::make_request(http::verb::post, "/mcp", R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"tasks/count"}})");
                count.set("Mcp-Session-Id", sid);
                auto counted = co_await exchange(socket, buffer, count);
                if (body_of(counted).at("result").at("structuredContent").at("result").as_int64() < 1) throw std::runtime_error {"count"};
                auto read = e2e::make_request(http::verb::post, "/mcp",
                    R"({"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"tasks://)" + std::to_string(id) + R"(?format=brief"}})");
                read.set("Mcp-Session-Id", sid);
                auto contents = body_of(co_await exchange(socket, buffer, read)).at("result").at("contents").as_array()[0];
                if (contents.at("text").as_string() != "task-" + std::to_string(c)) throw std::runtime_error {"resource text"};

                // Error paths on the same connection: constraint, 405 with Allow, HEAD of the document.
                auto bad = co_await exchange(socket, buffer, e2e::make_request(http::verb::get, "/tasks?page=0"));
                if (bad.result() != http::status::bad_request || bad.body().find("page must be >= 1") == std::string::npos)
                    throw std::runtime_error {"constraint"};
                auto put = co_await exchange(socket, buffer, e2e::make_request(http::verb::put, "/tasks", "{}"));
                if (put.result() != http::status::method_not_allowed || std::string {put[http::field::allow]}.find("POST") == std::string::npos)
                    throw std::runtime_error {"405"};
                auto head = co_await exchange(socket, buffer, e2e::make_request(http::verb::head, "/openapi.json"), true);
                if (head.result() != http::status::ok || std::stoul(std::string {head[http::field::content_length]}) < 100)
                    throw std::runtime_error {"head"};
            }
            catch (std::exception const& e)
            {
                errors[c] = e.what();
            }
            ++finished;
        }, net::detached);
    }

    std::vector<std::jthread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back([&] { io.run(); });
    while (finished < clients) std::this_thread::sleep_for(1ms);

    // The document is consistent with what the clients used.
    std::optional<json::value> document;
    std::atomic<bool> done {false};
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);
        beast::flat_buffer buffer;
        document = body_of(co_await exchange(socket, buffer, e2e::make_request(http::verb::get, "/openapi.json")));
        done = true;
    }, net::detached);
    while (!done) std::this_thread::sleep_for(1ms);

    signals.emit();     // from a thread that is not running the io_context
    pool.clear();

    for (int c = 0; c < clients; ++c) BOOST_TEST(errors[c] == "", "client " << c << ": " << errors[c]);
    BOOST_TEST(app.tasks.size() == static_cast<std::size_t>(clients));
    BOOST_TEST(app.users.size() == static_cast<std::size_t>(clients));
    BOOST_TEST(app.agent.session_count() == static_cast<std::size_t>(clients));

    BOOST_REQUIRE(document.has_value());
    auto const& doc = document->as_object();
    auto const& paths = doc.at("paths").as_object();
    BOOST_TEST(paths.size() == 2u);                          // /tasks and /tasks/{id}; /openapi.json, /rpc and /mcp undescribed
    auto const& list_params = paths.at("/tasks").at("get").at("parameters").as_array();
    BOOST_REQUIRE(list_params.size() == 3u);
    BOOST_TEST(list_params[0].at("name").as_string() == "tag");
    BOOST_TEST(list_params[0].at("schema").at("type").as_string() == "array");
    BOOST_TEST(!list_params[0].as_object().contains("explode"));
    BOOST_TEST(list_params[1].at("schema").at("default").as_int64() == 1);
    BOOST_TEST(list_params[1].at("schema").at("minimum").as_int64() == 1);
    BOOST_TEST(list_params[2].at("schema").at("maximum").as_int64() == 50);
    auto const& created_schema = paths.at("/tasks").at("post").at("requestBody").at("content").at("application/json").at("schema");
    BOOST_TEST(std::string {created_schema.at("$ref").as_string()}.ends_with("task_input"));
    BOOST_TEST(doc.at("components").at("schemas").as_object().size() == 2u);
    auto const& del = paths.at("/tasks/{id}").at("delete");
    BOOST_TEST(del.at("security").as_array()[0].as_object().contains("bearer"));   // inferred from header_arg(authorization)
    BOOST_TEST(del.at("parameters").as_array().size() == 1u);
    BOOST_TEST(del.at("parameters").as_array()[0].at("schema").at("minimum").as_int64() == 1);
}

/// A tool awaiting the client (elicitation) is cancelled by the client; the
/// late answer is still acknowledged and the session keeps working.
BOOST_AUTO_TEST_CASE(test_scenario_mcp_elicitation_cancelled_mid_flight)
{
    struct chat { int asked = 0; };
    mcp::endpoint<chat> e {{.name = "s", .version = "1"}};
    std::string outcome;
    e.tool("ask", "Elicit", [&](chat& c, mcp::session_info const& me) -> taar::awaitable<std::string>
    {
        ++c.asked;
        try
        {
            auto reply = co_await e.elicit(me.id, "Proceed?", json::object {{"type", "object"}});
            outcome = "answered";
            co_return std::string {reply.at("action").as_string()};
        }
        catch (boost::system::system_error const& err)
        {
            outcome = err.code() == boost::system::errc::operation_canceled ? "cancelled" : std::string {"other: "} + err.what();
            throw;
        }
    });
    e.tool("ping2", "Plain", [] { return 2; });

    taar::matcher::context mctx;
    auto handler = e.handler();
    auto post_body = [&](std::string body, std::string sid = {})
    {
        http::request<http::string_body> req {http::verb::post, "/mcp", 11};
        req.set(http::field::content_type, "application/json");
        if (!sid.empty()) req.set("Mcp-Session-Id", sid);
        req.body() = std::move(body);
        req.prepare_payload();
        return req;
    };
    auto run_one = [&](http::request<http::string_body> req)
    {
        net::io_context io;
        std::optional<http::response<http::string_body>> out;
        net::co_spawn(io, [&]() -> taar::awaitable<void> { out = co_await handler(taar::handler::contexts<> {}, req, mctx); }, net::detached);
        io.run();
        return *out;
    };
    auto init = run_one(post_body(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{"elicitation":{}}}})"));
    std::string const sid {init["Mcp-Session-Id"]};

    net::io_context io;
    http::request<http::empty_body> get {http::verb::get, "/mcp", 11};
    get.set("Mcp-Session-Id", sid);
    auto stream = e.stream_handler()(get, mctx);
    stream.set_executor(io.get_executor());

    std::optional<http::response<http::string_body>> call_res, cancel_res, late_res;
    json::value server_request;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        call_res = co_await handler(taar::handler::contexts<> {},
            post_body(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"ask"}})", sid), mctx);
    }, net::detached);
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        auto [ec0, hello] = co_await stream.next();
        auto [ec1, frame] = co_await stream.next();
        BOOST_REQUIRE(frame.has_value());
        auto const pos = frame->find("data: ") + 6;
        server_request = json::parse(frame->substr(pos, frame->size() - pos - 2));
        BOOST_TEST(server_request.at("method").as_string() == "elicitation/create");
        // The user gives up: the client cancels the tool call.
        cancel_res = co_await handler(taar::handler::contexts<> {},
            post_body(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":7}})", sid), mctx);
        // ... and the UI still sends its answer afterwards. Cancellation is
        // delivered through the executor, so give it a turn first.
        net::steady_timer settle {co_await net::this_coro::executor};
        settle.expires_after(20ms);
        co_await settle.async_wait(taar::use_awaitable);
        late_res = co_await handler(taar::handler::contexts<> {},
            post_body(R"({"jsonrpc":"2.0","id":)" + json::serialize(server_request.at("id")) + R"(,"result":{"action":"accept","content":{}}})", sid), mctx);
    }, net::detached);
    io.run();

    BOOST_TEST(outcome == "cancelled");
    BOOST_REQUIRE(call_res.has_value());
    BOOST_TEST(call_res->result() == http::status::accepted);       // cancelled calls produce no envelope
    BOOST_TEST(call_res->body().empty());
    BOOST_TEST(cancel_res->result() == http::status::accepted);
    BOOST_TEST(late_res->result() == http::status::accepted);

    auto after = run_one(post_body(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"ping2"}})", sid));
    BOOST_TEST(body_of(after).at("result").at("structuredContent").at("result").as_int64() == 2);
    BOOST_TEST(e.session_count() == 1u);
}

/// Resumption with a small history: what an earlier stream took and the
/// client missed is replayed if still remembered, what was never taken
/// follows from the queue, and nothing is duplicated.
BOOST_AUTO_TEST_CASE(test_scenario_stream_resumption_after_history_overflow)
{
    auto scenario = [](std::size_t history, int expected_frames) -> std::vector<std::uint64_t>
    {
        mcp::endpoint<user_session> e {{.name = "s", .version = "1", .stream_history = history}};
        taar::matcher::context mctx;
        auto handler = e.handler();
        http::request<http::string_body> init {http::verb::post, "/mcp", 11};
        init.set(http::field::content_type, "application/json");
        init.body() = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})";
        init.prepare_payload();
        std::string sid;
        {
            net::io_context io;
            net::co_spawn(io, [&]() -> taar::awaitable<void>
            {
                auto res = co_await handler(taar::handler::contexts<> {}, init, mctx);
                sid = std::string {res["Mcp-Session-Id"]};
            }, net::detached);
            io.run();
        }
        for (int n = 1; n <= 6; ++n) e.notify(sid, "notifications/message", json::object {{"n", n}});

        auto id_of = [](std::string const& frame)
        {
            auto const pos = frame.find("id: ") + 4;
            return std::stoull(frame.substr(pos, frame.find('\n', pos) - pos));
        };
        auto open = [&](std::string last_event_id)
        {
            http::request<http::empty_body> get {http::verb::get, "/mcp", 11};
            get.set("Mcp-Session-Id", sid);
            if (!last_event_id.empty()) get.set("Last-Event-ID", last_event_id);
            return e.stream_handler()(get, mctx);
        };
        auto take = [&](taar::chunked_response<std::string>& gen, int frames)
        {
            std::vector<std::uint64_t> ids;
            net::io_context io;
            gen.set_executor(io.get_executor());
            net::co_spawn(io, [&]() -> taar::awaitable<void>
            {
                auto [ec0, hello] = co_await gen.next();
                for (int i = 0; i < frames; ++i)
                {
                    auto [ec, frame] = co_await gen.next();
                    if (!frame) break;
                    ids.push_back(id_of(*frame));
                }
            }, net::detached);
            io.run();
            return ids;
        };

        {
            auto first = open("");
            auto got = take(first, 2);              // the client received events 1 and 2, then the connection dropped
            BOOST_TEST((got == std::vector<std::uint64_t> {1, 2}));
        }
        for (int n = 7; n <= 8; ++n) e.notify(sid, "notifications/message", json::object {{"n", n}});

        auto resumed = open("1");                   // the client acknowledges only event 1
        return take(resumed, expected_frames);
    };

    // History of 3: event 2 fell out before the reconnect and is gone; 3..8 arrive once, in order.
    BOOST_TEST((scenario(3, 6) == std::vector<std::uint64_t> {3, 4, 5, 6, 7, 8}));
    // History of 10: event 2 is replayed first, then 3..8.
    BOOST_TEST((scenario(10, 7) == std::vector<std::uint64_t> {2, 3, 4, 5, 6, 7, 8}));
}

/// A per-user event stream under with_session stays open while the same
/// user's other requests, on another connection, are served and feed it.
BOOST_AUTO_TEST_CASE(test_scenario_with_session_stream_fed_by_concurrent_requests)
{
    struct inbox_session
    {
        std::shared_ptr<taar::async_channel<std::string>> inbox = std::make_shared<taar::async_channel<std::string>>();
        int posted = 0;
    };
    net::io_context io;
    taar::context_store<std::string, inbox_session> store {5min};
    taar::session::http session;
    session_key::cookie const sid {"sid"};
    session.register_request_handler(
        method == http::verb::get && target == "/whoami",
        with_session(store, sid, session_policy::create_if_missing, rest([](inbox_session const& s) { return s.posted; })));
    session.register_request_handler(
        method == http::verb::get && target == "/events",
        with_session(store, sid, session_policy::require,
            rest([](inbox_session const& s) -> taar::chunked_response<std::string>
            {
                auto inbox = s.inbox;                       // the stream outlives the lock; the channel is thread-safe
                co_yield taar::set_header(http::field::content_type, "text/event-stream");
                co_yield std::string {": connected\n\n"};
                for (;;)
                {
                    auto msg = co_await inbox->async_receive(taar::use_awaitable);
                    if (!msg) break;
                    co_yield "data: " + *msg + "\n\n";
                }
            })));
    session.register_request_handler(
        method == http::verb::post && target == "/events",
        with_session(store, sid, session_policy::require,
            rest([](inbox_session& s, std::string text) { ++s.posted; s.inbox->push(std::move(text)); return s.posted; },
                string_body_arg(taar::handler::all_content_types))));
    session.register_request_handler(
        method == http::verb::post && target == "/events/close",
        with_session(store, sid, session_policy::require,
            rest([](inbox_session& s) { s.inbox->close(); return "closed"; })));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto a = co_await connect(port);
        beast::flat_buffer abuf;
        auto who = co_await exchange(a, abuf, e2e::make_request(http::verb::get, "/whoami"));
        std::string const cookie {who[http::field::set_cookie]};
        auto const pair = cookie.substr(0, cookie.find(';'));

        // Connection A: the stream.
        auto events = e2e::make_request(http::verb::get, "/events");
        events.set(http::field::cookie, pair);
        co_await http::async_write(a, events);
        http::response_parser<http::string_body> parser;
        parser.body_limit(boost::none);
        co_await http::async_read_header(a, abuf, parser);
        BOOST_TEST(parser.get().result() == http::status::ok);
        BOOST_TEST(parser.get()[http::field::content_type] == "text/event-stream");
        auto read_until = [&](std::string_view needle) -> taar::awaitable<void>
        {
            while (parser.get().body().find(needle) == std::string::npos && !parser.is_done())
            {
                co_await http::async_read_some(a, abuf, parser);
            }
        };
        co_await read_until(": connected\n\n");

        // Connection B, same user: posts are served while the stream is open.
        auto b = co_await connect(port);
        beast::flat_buffer bbuf;
        for (int i = 1; i <= 3; ++i)
        {
            auto post = e2e::make_request(http::verb::post, "/events", "hello " + std::to_string(i), "text/plain");
            post.set(http::field::cookie, pair);
            auto res = co_await exchange(b, bbuf, post);
            BOOST_TEST(res.result() == http::status::ok);
            BOOST_TEST(res.body() == std::to_string(i));
            co_await read_until("data: hello " + std::to_string(i) + "\n\n");
        }
        auto close = e2e::make_request(http::verb::post, "/events/close");
        close.set(http::field::cookie, pair);
        BOOST_TEST((co_await exchange(b, bbuf, close)).body() == "closed");
        while (!parser.is_done()) co_await http::async_read_some(a, abuf, parser);
        BOOST_TEST(parser.get().body().find("data: hello 3") != std::string::npos);

        // Connection A is still usable after its stream ended.
        auto again = e2e::make_request(http::verb::get, "/whoami");
        again.set(http::field::cookie, pair);
        BOOST_TEST((co_await exchange(a, abuf, again)).body() == "3");
        BOOST_TEST(store.size() == 1u);
    });
}

/// Pipelined requests with mixed outcomes on one keep-alive connection are
/// answered in order and leave the connection open.
BOOST_AUTO_TEST_CASE(test_scenario_pipelined_mixed_outcomes)
{
    struct visits { int n = 0; };
    net::io_context io;
    taar::context_store<std::string, visits> store {5min};
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/items/{id}",
        rest([](int id) { return id; }, constrained(path_arg("id"), {.minimum = 1})));
    session.register_request_handler(
        method == http::verb::get && target == "/me",
        with_session(store, session_key::cookie {"sid"}, session_policy::create_if_missing, rest([](visits& v) { return ++v.n; })));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto socket = co_await connect(port);

        // Everything goes out at once.
        std::string wire;
        auto append = [&](e2e::request req)
        {
            req.prepare_payload();
            std::ostringstream os;
            os << req;
            wire += os.str();
        };
        append(e2e::make_request(http::verb::get, "/me"));
        append(e2e::make_request(http::verb::get, "/items/0"));
        append(e2e::make_request(http::verb::get, "/nope"));
        append(e2e::make_request(http::verb::put, "/items/5", "{}"));
        append(e2e::make_request(http::verb::head, "/items/5"));
        append(e2e::make_request(http::verb::get, "/items/5"));
        co_await net::async_write(socket, net::buffer(wire), taar::use_awaitable);

        beast::flat_buffer buffer;
        auto read = [&](bool head = false) -> taar::awaitable<e2e::response>
        {
            http::response_parser<http::string_body> parser;
            parser.skip(head);
            co_await http::async_read(socket, buffer, parser);
            co_return parser.release();
        };
        auto me = co_await read();
        BOOST_TEST(me.body() == "1");
        std::string const cookie {me[http::field::set_cookie]};
        BOOST_TEST(cookie.starts_with("sid="));
        auto bad = co_await read();
        BOOST_TEST(bad.result() == http::status::bad_request);
        BOOST_TEST(bad.body().find("id must be >= 1") != std::string::npos);
        BOOST_TEST(bad.keep_alive());
        auto missing = co_await read();
        BOOST_TEST(missing.result() == http::status::not_found);
        auto put = co_await read();
        BOOST_TEST(put.result() == http::status::method_not_allowed);
        BOOST_TEST(put[http::field::allow] == "GET, HEAD");
        auto head = co_await read(true);
        BOOST_TEST(head.result() == http::status::ok);
        BOOST_TEST(head[http::field::content_length] == "1");
        auto five = co_await read();
        BOOST_TEST(five.body() == "5");

        // The same connection, now presenting the cookie.
        auto again = e2e::make_request(http::verb::get, "/me");
        again.set(http::field::cookie, cookie.substr(0, cookie.find(';')));
        BOOST_TEST((co_await exchange(socket, buffer, again)).body() == "2");
        BOOST_TEST(store.size() == 1u);
    });
}

} // namespace
