//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// An MCP (Model Context Protocol) server over Streamable HTTP with
// per-session state: tools, resources, a URI template, server-to-client
// notifications over the GET event stream, CORS for browser clients, and
// Origin validation. `mcp::endpoint<>` (no Session type) would be the
// stateless variant; mcp_stdio_server.cpp serves the same tools over stdio.
//
//   ./mcp_server 8080
//   curl -si localhost:8080/mcp -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"curl","version":"1"}}}'
//   SID=<Mcp-Session-Id from the response>
//   curl -s localhost:8080/mcp -H "Mcp-Session-Id: $SID" -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
//   curl -s localhost:8080/mcp -H "Mcp-Session-Id: $SID" -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"notes/add","arguments":{"text":"buy milk"}}}'
//   curl -s localhost:8080/mcp -H "Mcp-Session-Id: $SID" -d '{"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"notes://0"}}'
//   curl -N localhost:8080/mcp -H "Mcp-Session-Id: $SID" -H 'Accept: text/event-stream'
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/cors.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/mcp/content.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace taar = boost::taar;
namespace mcp = boost::taar::mcp;
namespace json = boost::json;

struct notes_session
{
    std::string client;
    std::vector<std::string> notes;
};

std::string add_note(notes_session& s, std::string text)
{
    s.notes.push_back(std::move(text));
    return "note " + std::to_string(s.notes.size() - 1) + " added";
}

std::string summarize_prompt(std::string style)
{
    return "Summarize my notes in a " + style + " style, one line each.";
}

json::array list_notes(notes_session const& s)
{
    json::array out;
    for (auto const& n : s.notes) out.emplace_back(n);
    return out;
}

void clear_notes(notes_session& s) { s.notes.clear(); }

std::string note_at(notes_session const& s, std::size_t index)
{
    if (index >= s.notes.size()) throw std::out_of_range {"no note " + std::to_string(index)};
    return s.notes[index];
}

std::string all_notes(notes_session const& s)
{
    std::string out;
    for (auto const& n : s.notes) out += n + "\n";
    return out;
}

// A typed result: a text item plus the notes embedded as a resource, so a
// client can show the summary and keep the file. Returning mcp::image or
// mcp::audio works the same way for binary content.
mcp::tool_result export_notes(notes_session const& s)
{
    mcp::tool_result r;
    r.add(mcp::text {std::to_string(s.notes.size()) + " notes exported"});
    r.add(mcp::embedded_resource {.uri = "notes://all", .mime_type = "text/plain", .text = all_notes(s)});
    return r;
}

// Binary resource contents: the notes as a blob (base64 on the wire).
mcp::blob notes_archive(notes_session const& s)
{
    return mcp::blob::from_bytes(all_notes(s), "application/octet-stream");
}

std::string now()
{
    return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

} // namespace

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    using taar::matcher::method;
    using taar::matcher::target;
    using mcp::mcp_arg;
    using mcp::uri_arg;

    char const* port = argc > 1 ? argv[1] : "8080";

    mcp::endpoint<notes_session> server {{
        .name = "notes-server",
        .version = "1.0.0",
        .instructions = "Keep short notes for the user. Notes live for the session.",
        .session_idle_timeout = std::chrono::minutes {30}}};

    server.on_initialize([](notes_session& s, mcp::initialize_params const& p)
    {
        s.client = p.client.name + " " + p.client.version;
        std::clog << "session for " << s.client << " (protocol " << p.protocol_version << ")\n";
    });

    server.tool("notes/add", "Append a note", &add_note, mcp_arg("text", "The note text"));
    server.tool("notes/list", "All notes in this session", &list_notes);
    // Annotations tell the client what a tool does to the world.
    server.tool(mcp::tool_spec {
            .name = "notes/clear", .description = "Delete all notes", .title = "Clear notes",
            .annotations = mcp::tool_annotations {.destructive_hint = true, .idempotent_hint = true}},
        &clear_notes);
    server.tool("time/now", "Current UTC time", &now);
    server.tool("notes/announce", "Tell every connected client that the notes changed", [&]
    {
        // Log entries respect each session's logging/setLevel.
        server.log_all(mcp::log_level::info, "notes announced", "notes");
        return server.broadcast("notifications/resources/updated", json::object {{"uri", "notes://all"}});
    });
    // session_info identifies the calling session, so a tool can notify its own client.
    server.tool("notes/remind", "Push a reminder to the caller's event stream",
        [&](mcp::session_info const& me, std::string text)
        {
            return server.notify(me.id, "notifications/message", json::object {{"level", "info"}, {"data", text}});
        },
        mcp_arg("text", "Reminder text"));

    server.resource("notes://all", "text/plain", "Every note, one per line", &all_notes);
    server.resource("notes://archive", "application/octet-stream", "The notes as bytes", &notes_archive);
    server.tool("notes/export", "The notes as text plus an embedded resource", &export_notes);
    // A server-to-client request: ask the user to confirm through the
    // client's elicitation UI before clearing. The reply is routed back to
    // this coroutine while it still holds the session.
    server.tool("notes/clear_confirmed", "Clear the notes after the user confirms",
        [&](notes_session& s, mcp::session_info const& me) -> taar::awaitable<std::string>
        {
            if (!me.capabilities.elicitation) co_return "this client cannot confirm; use notes/clear";
            auto reply = co_await server.elicit(me.id, "Delete all " + std::to_string(s.notes.size()) + " notes?",
                {{"type", "object"}, {"properties", {{"confirm", {{"type", "boolean"}}}}}, {"required", {"confirm"}}},
                std::chrono::minutes {2});
            auto const* content = reply.as_object().if_contains("content");
            if (reply.at("action").as_string() == "accept" && content && content->at("confirm").as_bool())
            {
                s.notes.clear();
                co_return "cleared";
            }
            co_return "kept";
        });
    server.prompt("notes/summarize", "A prompt asking the model to summarize the notes", &summarize_prompt,
        mcp_arg("style", "e.g. terse, friendly"));
    // Argument completion for the prompt (completion/complete).
    server.complete_prompt("notes/summarize", "style", [](std::string_view typed)
    {
        std::vector<std::string> out;
        for (std::string_view s : {"terse", "friendly", "formal", "bullet points"})
        {
            if (s.starts_with(typed)) out.emplace_back(s);
        }
        return out;
    });
    // Clients that subscribed to notes://all learn about changes.
    server.tool("notes/touch", "Signal that the notes changed", [&] { return server.resource_updated("notes://all"); });
    server.resource("notes://{index}", "text/plain", "A single note by index", &note_at, uri_arg("index", "zero-based"));

    // Browsers may only talk to this server from the local UI.
    server.set_origin_validator([](std::string_view origin)
    {
        return origin.starts_with("http://localhost") || origin.starts_with("http://127.0.0.1");
    });

    // Browser clients need CORS: preflight on OPTIONS, headers on every response.
    taar::cors cors;
    cors.allowed_origins = {"http://localhost:3000", "http://127.0.0.1:3000"};

    taar::session::http session;
    session.set_response_filter(cors.response_filter());
    session.register_request_handler(method == http::verb::options, cors.preflight_handler());
    session.register_request_handler(method == http::verb::get && target == "/mcp", server.stream_handler());
    session.register_request_handler(target == "/mcp", server.handler());

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", port, session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "MCP endpoint at http://" << ep << "/mcp\n"; }),
        taar::ignore_and_rethrow);

    io.run();
}
