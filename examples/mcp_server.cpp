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
    server.tool("notes/clear", "Delete all notes", &clear_notes);
    server.tool("time/now", "Current UTC time", &now);
    server.tool("notes/announce", "Tell every connected client that the notes changed", [&]
    {
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
        net::bind_cancellation_slot(signals.slot(), taar::ignore_and_rethrow));

    io.run();
}
