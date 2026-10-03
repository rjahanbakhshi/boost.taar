//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// One task board, every surface: a REST API with cookie sessions,
// validated arguments and an OpenAPI document; the same operations as
// JSON-RPC methods; an MCP server with annotated tools, a templated
// resource, a prompt with argument completion, elicitation and logging; a
// per-user event stream; CORS; and a multi-threaded io_context that shuts
// down cleanly on SIGINT.
//
//   ./taskboard_server 8080
//   curl -si localhost:8080/tasks -d '{"title":"write docs","tags":["docs"]}'      # Set-Cookie: sid=...
//   curl -s 'localhost:8080/tasks?tag=docs&page=1' -b 'sid=...'
//   curl -s localhost:8080/tasks/1 -X DELETE -H 'Authorization: Bearer admin'
//   curl -s localhost:8080/openapi.json | jq .components
//   curl -s localhost:8080/rpc -d '{"jsonrpc":"2.0","id":1,"method":"tasks.list","params":{"tags":["docs"]}}'
//   curl -N localhost:8080/events -b 'sid=...'                                      # per-user stream
//   curl -si localhost:8080/mcp -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}}'
//

#include <boost/taar/core/async_channel.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/cors.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
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
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/describe/class.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
namespace mcp = boost::taar::mcp;
namespace openapi = boost::taar::openapi;
using namespace std::chrono_literals;

// ---- domain ----------------------------------------------------------------

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

/// The board is shared by handlers on every thread, so it locks itself.
/// Per-user and per-agent state live in context stores instead.
class board
{
public:
    task add(task_input in)
    {
        std::lock_guard<std::mutex> const g {mutex_};
        task t {next_id_++, std::move(in.title), std::move(in.tags), false};
        tasks_.push_back(t);
        return t;
    }

    std::vector<task> list(std::vector<std::string> const& tags, int page, int per_page) const
    {
        std::lock_guard<std::mutex> const g {mutex_};
        std::vector<task> out;
        for (auto const& t : tasks_)
        {
            if (tags.empty() || std::ranges::any_of(tags, [&](auto const& tag) { return std::ranges::find(t.tags, tag) != t.tags.end(); }))
            {
                out.push_back(t);
            }
        }
        auto const start = static_cast<std::size_t>((page - 1) * per_page);
        if (start >= out.size()) return {};
        out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(start));
        if (out.size() > static_cast<std::size_t>(per_page)) out.resize(static_cast<std::size_t>(per_page));
        return out;
    }

    std::optional<task> get(int id) const
    {
        std::lock_guard<std::mutex> const g {mutex_};
        for (auto const& t : tasks_) if (t.id == id) return t;
        return std::nullopt;
    }

    bool complete(int id)
    {
        std::lock_guard<std::mutex> const g {mutex_};
        for (auto& t : tasks_) if (t.id == id) { t.done = true; return true; }
        return false;
    }

    bool remove(int id)
    {
        std::lock_guard<std::mutex> const g {mutex_};
        return std::erase_if(tasks_, [&](auto const& t) { return t.id == id; }) > 0;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> const g {mutex_};
        return tasks_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<task> tasks_;
    int next_id_ = 1;
};

/// Per-user state: a mailbox the user's event stream drains.
struct user_session
{
    std::shared_ptr<taar::async_channel<std::string>> events = std::make_shared<taar::async_channel<std::string>>();
    int created = 0;
};

/// Per-MCP-session state.
struct agent_session
{
    int calls = 0;
};

struct not_authorized : std::runtime_error { using runtime_error::runtime_error; };

} // namespace

int main(int argc, char* argv[])
{
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::constrained;
    using taar::handler::description;
    using taar::handler::header_arg;
    using taar::handler::json_body_arg;
    using taar::handler::path_arg;
    using taar::handler::query_arg;
    using taar::handler::query_list_arg;
    using taar::handler::rest;
    using taar::handler::session_policy;
    using taar::handler::with_default;
    using taar::handler::with_session;
    using jsonrpc::rpc_arg;
    using mcp::mcp_arg;
    using mcp::uri_arg;
    namespace session_key = taar::handler::session_key;

    char const* port = argc > 1 ? argv[1] : "8080";

    board tasks;
    taar::context_store<std::string, user_session> users {30min};
    session_key::cookie const sid {"sid"};

    taar::session::http session;
    taar::cors cors;
    session.set_response_filter(cors.response_filter());
    session.register_request_handler(method == http::verb::options, cors.preflight_handler());

    // A domain error becomes a 403 on REST (JSON-RPC maps it below, MCP
    // reports it as a tool error); argument problems stay 400 with their
    // message, anything else is a 500 without details.
    session.set_soft_error_handler([](taar::failure const& f)
    {
        auto reply = [](http::status status, std::string text)
        {
            http::response<http::string_body> res {status, 11};
            res.set(http::field::content_type, "text/plain");
            res.body() = std::move(text);
            res.prepare_payload();
            return res;
        };
        if (auto const* e = f.as<not_authorized>()) return reply(http::status::forbidden, e->what());
        if (auto const* e = f.as<std::out_of_range>()) return reply(http::status::not_found, e->what());
        auto const status = taar::http_status_for(f);
        if (status != http::status::internal_server_error) return reply(status, f.text());
        return reply(http::status::internal_server_error, "internal error");
    });

    // ---- REST + OpenAPI -----------------------------------------------------
    openapi::generator doc {{.title = "Task board", .version = "1.0.0", .description = "Tasks over REST, JSON-RPC and MCP"}};
    doc.add_security_scheme("bearer", openapi::security_scheme::bearer("opaque", "Admin token"));

    session.register_request_handler(
        method == http::verb::post && target == "/tasks",
        with_session(users, sid, session_policy::create_if_missing,
            rest([&](user_session& u, task_input in)
            {
                auto t = tasks.add(std::move(in));
                ++u.created;
                u.events->push("created " + std::to_string(t.id));
                return t;
            }, json_body_arg(description {"The task to create"}))),
        doc.describe({.summary = "Create a task", .tags = {"tasks"}}));

    session.register_request_handler(
        method == http::verb::get && target == "/tasks",
        rest([&](std::vector<std::string> tags, int page, int per_page) { return tasks.list(tags, page, per_page); },
            with_default(query_list_arg("tag", "Only tasks with one of these tags"), std::vector<std::string> {}),
            constrained(with_default(query_arg("page", "1-based page"), 1), {.minimum = 1}),
            constrained(with_default(query_arg("per_page", "Page size"), 20), {.minimum = 1, .maximum = 100})),
        doc.describe({.summary = "List tasks", .tags = {"tasks"}}));

    session.register_request_handler(
        method == http::verb::get && target == "/tasks/{id}",
        rest([&](int id) -> task
        {
            auto t = tasks.get(id);
            if (!t) throw std::out_of_range {"no task " + std::to_string(id)};
            return *t;
        }, constrained(path_arg("id", "Task id"), {.minimum = 1})),
        doc.describe({.summary = "Fetch a task", .tags = {"tasks"}, .responses = {{"200", "The task"}, {"404", "No such task"}}}));

    // Authorization is read by header_arg, so the generator documents the
    // operation under the bearer scheme instead of listing a header parameter.
    session.register_request_handler(
        method == http::verb::delete_ && target == "/tasks/{id}",
        rest([&](std::string const& auth, int id)
        {
            if (auth != "Bearer admin") throw not_authorized {"admin token required"};
            return tasks.remove(id);
        }, header_arg(http::field::authorization), constrained(path_arg("id"), {.minimum = 1})),
        doc.describe({.summary = "Delete a task", .tags = {"admin"}}));

    // Per-user stream: the entry is locked only until the stream is produced,
    // so the user's other requests keep flowing while it is open.
    session.register_request_handler(
        method == http::verb::get && target == "/events",
        with_session(users, sid, session_policy::require,
            rest([](user_session const& u) -> taar::chunked_response<std::string>
            {
                auto events = u.events;
                co_yield taar::set_header(http::field::content_type, "text/event-stream");
                co_yield std::string {": connected\n\n"};
                for (;;)
                {
                    auto msg = co_await events->async_receive(taar::use_awaitable);
                    if (!msg) break;
                    co_yield "data: " + *msg + "\n\n";
                }
            })));

    session.register_request_handler(method == http::verb::get && target == "/openapi.json", doc.handler());

    // ---- JSON-RPC -------------------------------------------------------------
    jsonrpc::endpoint<> rpc;
    rpc.method("tasks.add", [&](task_input in) { return tasks.add(std::move(in)); }, rpc_arg("task", "The task"));
    rpc.method("tasks.list", [&](std::vector<std::string> tags, int page) { return tasks.list(tags, page, 50); },
        with_default(rpc_arg("tags"), std::vector<std::string> {}),
        constrained(with_default(rpc_arg("page"), 1), {.minimum = 1}));
    rpc.method("tasks.complete", [&](int id) { return tasks.complete(id); }, rpc_arg("id"));
    rpc.set_error_mapper([](taar::failure const& f)
    {
        if (auto const* e = f.as<not_authorized>()) return jsonrpc::error_info {-32001, e->what(), std::nullopt};
        return jsonrpc::default_error_mapper(f);
    });
    session.register_request_handler(method == http::verb::post && target == "/rpc", rpc.handler());

    // ---- MCP ------------------------------------------------------------------
    mcp::endpoint<agent_session> agent {{
        .name = "taskboard", .version = "1.0.0",
        .instructions = "Manage the user's tasks. Ask before deleting."}};

    agent.tool(mcp::tool_spec {.name = "tasks/add", .description = "Create a task", .title = "Add task",
            .annotations = mcp::tool_annotations {.destructive_hint = false, .idempotent_hint = false}},
        [&](agent_session& s, task_input in) { ++s.calls; return tasks.add(std::move(in)); },
        mcp_arg("task", "Title and tags"));
    agent.tool(mcp::tool_spec {.name = "tasks/list", .description = "List tasks, optionally by tag",
            .annotations = mcp::tool_annotations {.read_only_hint = true}},
        [&](std::vector<std::string> tags) { return tasks.list(tags, 1, 50); },
        with_default(mcp_arg("tags", "Tags to match"), std::vector<std::string> {}));
    agent.tool(mcp::tool_spec {.name = "tasks/remove", .description = "Delete a task after the user confirms",
            .annotations = mcp::tool_annotations {.destructive_hint = true}},
        [&](mcp::session_info const& me, int id) -> taar::awaitable<std::string>
        {
            if (!me.capabilities.elicitation) co_return "this client cannot confirm deletions";
            auto reply = co_await agent.elicit(me.id, "Delete task " + std::to_string(id) + "?",
                {{"type", "object"}, {"properties", {{"confirm", {{"type", "boolean"}}}}}, {"required", {"confirm"}}},
                2min);
            auto const* content = reply.as_object().if_contains("content");
            if (reply.at("action").as_string() != "accept" || !content || !content->at("confirm").as_bool()) co_return "kept";
            bool const removed = tasks.remove(id);
            agent.log(me.id, mcp::log_level::info, json::object {{"removed", id}}, "taskboard");
            co_return removed ? "deleted" : "no such task";
        },
        constrained(mcp_arg("id", "Task id"), {.minimum = 1}));

    agent.resource("tasks://{id}{?format}", "application/json", "One task, full or brief",
        [&](int id, std::optional<std::string> format) -> json::value
        {
            auto t = tasks.get(id);
            if (!t) throw std::out_of_range {"no task " + std::to_string(id)};
            if (format.value_or("full") == "brief") return json::value {t->title};
            return json::value_from(*t);
        }, uri_arg("id"), uri_arg("format"));

    agent.prompt("tasks/triage", "Ask the model to triage the board", [](std::string focus)
    {
        return "Review the tasks and suggest what to do next, focusing on " + focus + ".";
    }, mcp_arg("focus", "overdue, untagged or done"));
    agent.complete_prompt("tasks/triage", "focus", [](std::string_view typed)
    {
        std::vector<std::string> out;
        for (std::string_view s : {"overdue", "untagged", "done"}) if (s.starts_with(typed)) out.emplace_back(s);
        return out;
    });

    session.register_request_handler(method == http::verb::get && target == "/mcp", agent.stream_handler());
    session.register_request_handler(target == "/mcp", agent.handler());

    // ---- run on every core, stop on SIGINT/SIGTERM ------------------------------
    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", port, session, signals,
            [](net::ip::tcp::endpoint const& endpoint) { std::clog << "taskboard on port " << endpoint.port() << '\n'; }),
        taar::ignore_and_rethrow);

    std::vector<std::jthread> threads;
    for (unsigned i = 1; i < std::max(2u, std::thread::hardware_concurrency()); ++i) threads.emplace_back([&] { io.run(); });
    io.run();
    return 0;
}
