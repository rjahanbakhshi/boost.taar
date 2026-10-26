//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

// The same domain functions exposed over REST, JSON-RPC and MCP from one
// HTTP session: results, errors, argument validation and schemas agree.

#include "e2e.hpp"
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
namespace mcp = boost::taar::mcp;
namespace openapi = boost::taar::openapi;
using namespace std::chrono_literals;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::path_arg;
using taar::handler::query_arg;
using taar::handler::rest;
using taar::handler::session_policy;
using taar::handler::with_session;
using jsonrpc::rpc_arg;
using mcp::mcp_arg;
namespace session_key = taar::handler::session_key;

struct calc_session { int last = 0; };
struct division_by_zero : std::domain_error { using domain_error::domain_error; };

int add(calc_session& s, int a, int b) { s.last = a + b; return s.last; }
int divide(int a, int b) { if (b == 0) throw division_by_zero {"division by zero"}; return a / b; }
int last(calc_session const& s) { return s.last; }

http::response<http::string_body> soft_error(taar::failure const& f)
{
    if (auto const* e = f.as<division_by_zero>())
    {
        http::response<http::string_body> res {http::status::unprocessable_entity, 11};
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(json::object {{"error", e->what()}});
        res.prepare_payload();
        return res;
    }
    if (f.code().category() != taar::error_category()) f.rethrow();
    http::response<http::string_body> res {http::status::bad_request, 11};
    res.body() = f.code().message();
    res.prepare_payload();
    return res;
}

jsonrpc::error_info rpc_error(taar::failure const& f)
{
    if (auto const* e = f.as<division_by_zero>())
    {
        return {-32001, e->what(), std::nullopt};
    }
    return jsonrpc::default_error_mapper(f);
}

struct fixture
{
    taar::context_store<std::string, calc_session> sessions {10min};
    taar::session::http session;
    jsonrpc::endpoint<calc_session> rpc;
    mcp::endpoint<calc_session> mcp_endpoint {{.name = "calc", .version = "1"}};
    openapi::generator openapi_gen {{.title = "calc", .version = "1"}};

    fixture()
    {
        session.set_soft_error_handler(&soft_error);

        session.register_request_handler(
            method == http::verb::post && target == "/api/add",
            with_session(sessions, session_key::header {"X-Session"}, session_policy::create_if_missing,
                rest(&add, query_arg("a", "First addend"), query_arg("b", "Second addend"))),
            openapi_gen.describe("Add"));
        session.register_request_handler(
            method == http::verb::get && target == "/api/divide/{a}/{b}",
            rest(&divide, path_arg("a"), path_arg("b")));
        session.register_request_handler(
            method == http::verb::get && target == "/api/last",
            with_session(sessions, session_key::header {"X-Session"}, session_policy::require,
                rest(&last)));

        rpc.method("add", &add, rpc_arg("a", "First addend"), rpc_arg("b", "Second addend"));
        rpc.method("divide", &divide, rpc_arg("a"), rpc_arg("b"));
        rpc.method("last", &last);
        rpc.set_error_mapper(&rpc_error);
        session.register_request_handler(
            method == http::verb::post && target == "/rpc",
            with_session(sessions, session_key::header {"X-Session"}, session_policy::create_if_missing,
                rpc.handler()));

        mcp_endpoint.tool("add", "Add", &add, mcp_arg("a", "First addend"), mcp_arg("b", "Second addend"));
        mcp_endpoint.tool("divide", "Divide", &divide, mcp_arg("a"), mcp_arg("b"));
        mcp_endpoint.tool("last", "Last", &last);
        session.register_request_handler(target == "/mcp", mcp_endpoint.handler());
    }
};

e2e::request rpc_call(std::string body, std::string const& key)
{
    auto req = e2e::make_request(http::verb::post, "/rpc", std::move(body));
    if (!key.empty()) req.set("X-Session", key);
    return req;
}

e2e::request mcp_call(std::string body, std::string const& sid)
{
    auto req = e2e::make_request(http::verb::post, "/mcp", std::move(body));
    if (!sid.empty()) req.set("Mcp-Session-Id", sid);
    return req;
}

json::value body_of(e2e::response const& r) { return json::parse(r.body()); }

BOOST_AUTO_TEST_CASE(test_dual_surface_same_result_and_shared_state)
{
    net::io_context io;
    fixture f;

    e2e::run(io, f.session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, e2e::make_request(http::verb::post, "/api/add?a=2&b=3"));
        BOOST_TEST(r[0].result() == http::status::ok);
        BOOST_TEST(r[0][http::field::content_type] == "text/plain");
        BOOST_TEST(r[0].body() == "5");
        std::string key {r[0]["X-Session"]};
        BOOST_TEST(key.size() == 32u);

        auto j = co_await e2e::send_all(port,
            rpc_call(R"({"jsonrpc":"2.0","id":1,"method":"last"})", key),
            rpc_call(R"({"jsonrpc":"2.0","id":2,"method":"add","params":{"a":10,"b":20}})", key));
        BOOST_TEST(body_of(j[0]).at("result").as_int64() == 5);        // REST state visible to RPC
        BOOST_TEST(body_of(j[1]).at("result").as_int64() == 30);

        auto back = e2e::make_request(http::verb::get, "/api/last");
        back.set("X-Session", key);
        auto k = co_await e2e::send_all(port, back);
        BOOST_TEST(k[0].body() == "30");                                // RPC state visible to REST

        auto init = co_await e2e::send_all(port, mcp_call(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})", ""));
        std::string sid {init[0]["Mcp-Session-Id"]};
        auto m = co_await e2e::send_all(port,
            mcp_call(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"last"}})", sid),
            mcp_call(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"add","arguments":{"a":2,"b":3}}})", sid),
            mcp_call(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"last"}})", sid));
        BOOST_TEST(body_of(m[0]).at("result").at("structuredContent").at("result").as_int64() == 0);  // MCP session is its own
        BOOST_TEST(body_of(m[1]).at("result").at("structuredContent").at("result").as_int64() == 5);
        BOOST_TEST(body_of(m[2]).at("result").at("structuredContent").at("result").as_int64() == 5);
        BOOST_TEST(f.sessions.size() == 1u);
    });
}

BOOST_AUTO_TEST_CASE(test_dual_surface_domain_error_surfaces_natively)
{
    net::io_context io;
    fixture f;

    e2e::run(io, f.session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/api/divide/1/0"),
            e2e::make_request(http::verb::get, "/api/divide/6/3"));
        BOOST_TEST(r[0].result() == http::status::unprocessable_entity);
        BOOST_TEST(r[0][http::field::content_type] == "application/json");
        BOOST_TEST(body_of(r[0]).at("error").as_string() == "division by zero");
        BOOST_TEST(r[1].body() == "2");                                 // connection survived the soft error

        auto j = co_await e2e::send_all(port, rpc_call(R"({"jsonrpc":"2.0","id":1,"method":"divide","params":{"a":1,"b":0}})", ""));
        BOOST_TEST(j[0].result() == http::status::ok);
        BOOST_TEST(body_of(j[0]).at("error").at("code").as_int64() == -32001);
        BOOST_TEST(body_of(j[0]).at("error").at("message").as_string() == "division by zero");

        auto init = co_await e2e::send_all(port, mcp_call(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})", ""));
        std::string sid {init[0]["Mcp-Session-Id"]};
        auto m = co_await e2e::send_all(port, mcp_call(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"divide","arguments":{"a":1,"b":0}}})", sid));
        auto const result = body_of(m[0]).at("result");
        BOOST_TEST(result.at("isError").as_bool());
        BOOST_TEST(result.at("content").as_array()[0].at("text").as_string() == "division by zero");
    });
}

BOOST_AUTO_TEST_CASE(test_dual_surface_missing_argument_validation)
{
    net::io_context io;
    fixture f;

    e2e::run(io, f.session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port, e2e::make_request(http::verb::post, "/api/add?a=1"));
        BOOST_TEST(r[0].result() == http::status::bad_request);

        auto j = co_await e2e::send_all(port, rpc_call(R"({"jsonrpc":"2.0","id":1,"method":"add","params":{"a":1}})", ""));
        BOOST_TEST(body_of(j[0]).at("error").at("code").as_int64() == -32602);

        auto init = co_await e2e::send_all(port, mcp_call(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})", ""));
        std::string sid {init[0]["Mcp-Session-Id"]};
        auto m = co_await e2e::send_all(port, mcp_call(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add","arguments":{"a":1}}})", sid));
        BOOST_TEST(body_of(m[0]).at("error").at("code").as_int64() == -32602);
    });
}

BOOST_AUTO_TEST_CASE(test_dual_surface_schema_consistency)
{
    net::io_context io;
    fixture f;

    std::map<std::string, std::pair<std::string, std::string>> rest_params;   // name -> (type, description)
    auto const doc = f.openapi_gen.document();
    for (auto const& p : doc.at("paths").at("/api/add").at("post").at("parameters").as_array())
    {
        rest_params[std::string {p.at("name").as_string()}] = {
            std::string {p.at("schema").at("type").as_string()},
            std::string {p.at("description").as_string()}};
    }
    BOOST_TEST(rest_params.size() == 2u);

    e2e::run(io, f.session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto init = co_await e2e::send_all(port, mcp_call(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})", ""));
        std::string sid {init[0]["Mcp-Session-Id"]};
        auto tools = co_await e2e::send_all(port, mcp_call(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})", sid));
        // Keep the parsed body alive: before C++23 P2718 (clang < 19, GCC < 15) a
        // temporary in a range-for initializer dies before the loop body runs.
        auto const listed = body_of(tools[0]);
        for (auto const& t : listed.at("result").at("tools").as_array())
        {
            if (t.at("name").as_string() != "add") continue;
            auto const& props = t.at("inputSchema").at("properties").as_object();
            BOOST_TEST(props.size() == rest_params.size());
            for (auto const& [name, schema] : props)
            {
                auto it = rest_params.find(std::string {name});
                BOOST_REQUIRE((it != rest_params.end()));
                BOOST_TEST(schema.at("type").as_string() == it->second.first);
                BOOST_TEST(schema.at("description").as_string() == it->second.second);
            }
        }
    });
}

} // namespace
