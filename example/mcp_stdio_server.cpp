//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//
// The same MCP endpoint as mcp_server.cpp, served over stdio: the transport
// most desktop MCP clients launch locally. Only protocol messages may go to
// stdout, so diagnostics use stderr.
//
//   ./mcp_stdio_server
//   {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}}
//   {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"notes/add","arguments":{"text":"buy milk"}}}
//
// Claude Desktop / claude_desktop_config.json:
//   {"mcpServers": {"notes": {"command": "/path/to/mcp_stdio_server"}}}
//

#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
#include <chrono>
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

std::string note_at(notes_session const& s, std::size_t index)
{
    if (index >= s.notes.size()) throw std::out_of_range {"no note " + std::to_string(index)};
    return s.notes[index];
}

} // namespace

int main()
{
    namespace net = boost::asio;
    using mcp::mcp_arg;
    using mcp::uri_arg;

    mcp::endpoint<notes_session> server {{
        .name = "notes-server",
        .version = "1.0.0",
        .instructions = "Keep short notes for the user.",
        .session_idle_timeout = std::chrono::hours {24}}};     // one long-lived stdio session

    server.on_initialize([](notes_session&, mcp::initialize_params const& p)
    {
        std::clog << "client " << p.client.name << " " << p.client.version << '\n';
    });

    server.tool("notes/add", "Append a note", &add_note, mcp_arg("text", "The note text"));
    server.tool("notes/list", "All notes in this session", &list_notes);
    server.tool("notes/remind", "Push a reminder notification to the client",
        [&](mcp::session_info const& me, std::string text)
        {
            return server.notify(me.id, "notifications/message",
                json::object {{"level", "info"}, {"data", text}});
        },
        mcp_arg("text", "Reminder text"));
    server.resource("notes://{index}", "text/plain", "A single note by index", &note_at, uri_arg("index", "zero-based"));

    net::io_context io;
    net::co_spawn(io, server.serve_stdio(), net::detached);
    io.run();
}
