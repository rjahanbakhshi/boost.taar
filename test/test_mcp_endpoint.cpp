//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace mcp = boost::taar::mcp;
namespace jsonrpc = boost::taar::jsonrpc;

using mcp::mcp_arg;
using mcp::uri_arg;
using taar::handler::contexts;

struct calc_session
{
    int last = 0;
    std::vector<std::string> history;
    std::string client;
};

struct division_by_zero : std::domain_error { using domain_error::domain_error; };

int add(calc_session& s, int a, int b)
{
    s.last = a + b;
    s.history.push_back(std::to_string(a) + "+" + std::to_string(b) + "=" + std::to_string(s.last));
    return s.last;
}

int divide(int a, int b)
{
    if (b == 0) throw division_by_zero {"division by zero"};
    return a / b;
}

std::string shout(std::string text) { return text + "!"; }
void reset(calc_session& s) { s.history.clear(); s.last = 0; }
json::object stats(calc_session const& s) { return {{"count", s.history.size()}, {"last", s.last}}; }
std::string typed(int a, double b, bool c, std::string d, std::optional<int> e) { return d; }
std::string last_entry(calc_session const& s) { return s.history.empty() ? "" : s.history.back(); }
std::string entry_at(calc_session const& s, std::size_t index) { return s.history.at(index); }

template <typename Endpoint>
auto build(Endpoint& e)
{
    e.tool("add", "Add two integers", &add, mcp_arg("a", "first addend"), mcp_arg("b", "second addend"));
    e.tool("divide", "Integer division", &divide, mcp_arg("a"), mcp_arg("b"));
    e.tool("shout", "Shout", &shout, mcp_arg("text"));
    e.tool("reset", "Reset history", &reset);
    e.tool("stats", "History stats", &stats);
    e.tool("typed", "Type test", &typed, mcp_arg("a"), mcp_arg("b"), mcp_arg("c"), mcp_arg("d"), mcp_arg("e"));
    e.resource("calc://history/last", "text/plain", "Most recent calculation", &last_entry);
    e.resource("calc://history/{index}", "text/plain", "One history entry", &entry_at, uri_arg("index", "zero-based"));
    e.rpc().method("calc/version", [] { return std::string {"1.0.0"}; });
}

// Stateless mode still gets calc_session, but as a per-request context handed
// in by the HTTP side rather than a keyed MCP session.
using stateless_endpoint = mcp::endpoint<mcp::stateless, calc_session>;
using stateful_endpoint = mcp::endpoint<calc_session>;

template <typename Tuple, std::size_t... I>
Tuple make_incoming_impl(std::index_sequence<I...>)
{
    return Tuple {std::make_shared<typename std::tuple_element_t<I, Tuple>::element_type>()...};
}

template <typename Tuple>
Tuple make_incoming()
{
    return make_incoming_impl<Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>> {});
}

struct header
{
    std::string name;
    std::string value;
};

template <typename Endpoint>
http::response<http::string_body> send(
    Endpoint& e,
    http::verb verb,
    std::string body,
    std::vector<header> headers = {})
{
    http::request<http::string_body> req {verb, "/mcp", 11};
    req.set(http::field::content_type, "application/json");
    for (auto const& h : headers) req.set(h.name, h.value);
    req.body() = std::move(body);
    req.prepare_payload();

    taar::matcher::context mctx;
    auto handler = e.handler();
    net::io_context io;
    std::optional<http::response<http::string_body>> out;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        out = co_await handler(make_incoming<typename Endpoint::incoming_contexts>(), req, mctx);
    }, net::detached);
    io.run();
    return *out;
}

template <typename Endpoint>
http::response<http::string_body> post(Endpoint& e, std::string body, std::vector<header> headers = {})
{
    return send(e, http::verb::post, std::move(body), std::move(headers));
}

json::value body_json(http::response<http::string_body> const& res) { return json::parse(res.body()); }

std::string const initialize_body =
    R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"agent","version":"0.1"}}})";

std::string initialize_stateful(stateful_endpoint& e)
{
    auto res = post(e, initialize_body);
    return std::string {res["Mcp-Session-Id"]};
}

// ---- URI template matcher ----------------------------------------------------

BOOST_AUTO_TEST_CASE(test_mcp_uri_template_matching)
{
    auto m = mcp::match_uri_template("calc://history/{index}", "calc://history/3");
    BOOST_REQUIRE(m.has_value());
    BOOST_TEST(m->at("index") == "3");

    BOOST_TEST(!mcp::match_uri_template("calc://history/{index}", "calc://history/").has_value());
    BOOST_TEST(!mcp::match_uri_template("calc://history/{index}", "calc://other/3").has_value());
    BOOST_TEST(mcp::match_uri_template("calc://history/{index}", "calc://history/3/x").has_value()); // slash allowed in capture
    auto two = mcp::match_uri_template("db://{table}/rows/{id}", "db://users/rows/42");
    BOOST_REQUIRE(two.has_value());
    BOOST_TEST(two->at("table") == "users");
    BOOST_TEST(two->at("id") == "42");
    auto fixed = mcp::match_uri_template("calc://history/last", "calc://history/last");
    BOOST_REQUIRE(fixed.has_value());
    BOOST_TEST(fixed->empty());
    BOOST_TEST(mcp::is_uri_template("a://{x}"));
    BOOST_TEST(!mcp::is_uri_template("a://x"));
}

// ---- initialize ----------------------------------------------------------------

BOOST_AUTO_TEST_CASE(test_mcp_initialize_stateless)
{
    stateless_endpoint e {{.name = "calc-server", .version = "1.0.0", .instructions = "Be nice"}};
    build(e);

    auto res = post(e, initialize_body);
    BOOST_TEST(res.result() == http::status::ok);
    BOOST_TEST((res.find("Mcp-Session-Id") == res.end()));
    auto r = body_json(res).at("result");
    BOOST_TEST(r.at("protocolVersion").as_string() == "2025-11-25");
    BOOST_TEST(r.at("capabilities").as_object().contains("tools"));
    BOOST_TEST(r.at("capabilities").as_object().contains("resources"));
    BOOST_TEST(r.at("serverInfo").at("name").as_string() == "calc-server");
    BOOST_TEST(r.at("serverInfo").at("version").as_string() == "1.0.0");
    BOOST_TEST(r.at("instructions").as_string() == "Be nice");
}

BOOST_AUTO_TEST_CASE(test_mcp_protocol_version_negotiation)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    auto older = post(e, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26"}})");
    BOOST_TEST(body_json(older).at("result").at("protocolVersion").as_string() == "2025-03-26");
    auto unknown = post(e, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"1999-01-01"}})");
    BOOST_TEST(body_json(unknown).at("result").at("protocolVersion").as_string() == "2025-11-25");
    auto missing = post(e, R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
    BOOST_TEST(body_json(missing).at("result").at("protocolVersion").as_string() == "2025-11-25");
}

BOOST_AUTO_TEST_CASE(test_mcp_initialize_stateful_issues_session_and_runs_hook)
{
    stateful_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    e.on_initialize([](calc_session& s, mcp::initialize_params const& p) { s.client = p.client.name + "/" + p.client.version; });

    auto res = post(e, initialize_body);
    std::string sid {res["Mcp-Session-Id"]};
    BOOST_TEST(sid.size() == 32u);
    BOOST_TEST(e.session_count() == 1u);

    auto second = post(e, initialize_body);
    BOOST_TEST(std::string {second["Mcp-Session-Id"]} != sid);
    BOOST_TEST(e.session_count() == 2u);

    // The hook ran on the session: observe through a tool.
    auto st = post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"stats"}})", {{"Mcp-Session-Id", sid}});
    BOOST_TEST(st.result() == http::status::ok);
}

BOOST_AUTO_TEST_CASE(test_mcp_initialize_hook_failure_discards_session)
{
    stateful_endpoint e {{.name = "s", .version = "1"}};
    e.on_initialize([](calc_session&, mcp::initialize_params const&) { throw std::runtime_error {"rejected"}; });
    auto res = post(e, initialize_body);
    BOOST_TEST(body_json(res).at("error").at("code").as_int64() == -32603);
    BOOST_TEST(body_json(res).at("error").at("message").as_string() == "rejected");
    BOOST_TEST(e.session_count() == 0u);
}

// ---- session header rules --------------------------------------------------

BOOST_AUTO_TEST_CASE(test_mcp_stateful_session_header_required)
{
    stateful_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    auto sid = initialize_stateful(e);

    auto missing = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
    BOOST_TEST(missing.result() == http::status::bad_request);

    auto unknown = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", {{"Mcp-Session-Id", "nope"}});
    BOOST_TEST(unknown.result() == http::status::not_found);

    auto ok = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", {{"Mcp-Session-Id", sid}});
    BOOST_TEST(ok.result() == http::status::ok);
    BOOST_TEST(body_json(ok).at("result").as_object().empty());
}

BOOST_AUTO_TEST_CASE(test_mcp_session_state_persists_and_delete_ends_it)
{
    stateful_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    auto sid = initialize_stateful(e);

    post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"add","arguments":{"a":2,"b":3}}})", {{"Mcp-Session-Id", sid}});
    auto st = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"stats"}})", {{"Mcp-Session-Id", sid}}));
    BOOST_TEST(st.at("result").at("structuredContent").at("count").as_int64() == 1);
    BOOST_TEST(st.at("result").at("structuredContent").at("last").as_int64() == 5);

    auto del_missing = send(e, http::verb::delete_, "");
    BOOST_TEST(del_missing.result() == http::status::bad_request);
    auto del = send(e, http::verb::delete_, "", {{"Mcp-Session-Id", sid}});
    BOOST_TEST(del.result() == http::status::ok);
    BOOST_TEST(e.session_count() == 0u);
    auto gone = post(e, R"({"jsonrpc":"2.0","id":3,"method":"ping"})", {{"Mcp-Session-Id", sid}});
    BOOST_TEST(gone.result() == http::status::not_found);
    auto del_again = send(e, http::verb::delete_, "", {{"Mcp-Session-Id", sid}});
    BOOST_TEST(del_again.result() == http::status::not_found);
}

BOOST_AUTO_TEST_CASE(test_mcp_stateless_has_no_session_and_rejects_delete_and_get)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    auto ping = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
    BOOST_TEST(ping.result() == http::status::ok);
    auto del = send(e, http::verb::delete_, "");
    BOOST_TEST(del.result() == http::status::method_not_allowed);
    BOOST_TEST(del[http::field::allow] == "POST");
    auto get = send(e, http::verb::get, "");
    BOOST_TEST(get.result() == http::status::method_not_allowed);
}

// ---- lifecycle and discovery ----------------------------------------------

BOOST_AUTO_TEST_CASE(test_mcp_notifications_and_client_responses_are_202)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    auto n = post(e, R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    BOOST_TEST(n.result() == http::status::accepted);
    BOOST_TEST(n.body().empty());
    auto c = post(e, R"({"jsonrpc":"2.0","id":5,"result":{}})");
    BOOST_TEST(c.result() == http::status::accepted);
}

BOOST_AUTO_TEST_CASE(test_mcp_tools_list_schema)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    auto tools = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})")).at("result").at("tools").as_array();
    BOOST_REQUIRE(tools.size() == 6u);

    auto const& add_tool = tools[0].as_object();
    BOOST_TEST(add_tool.at("name").as_string() == "add");
    BOOST_TEST(add_tool.at("description").as_string() == "Add two integers");
    auto const& schema = add_tool.at("inputSchema").as_object();
    BOOST_TEST(schema.at("type").as_string() == "object");
    BOOST_TEST(schema.at("properties").as_object().size() == 2u); // calc_session& not listed
    BOOST_TEST(schema.at("properties").at("a").at("type").as_string() == "integer");
    BOOST_TEST(schema.at("properties").at("a").at("description").as_string() == "first addend");
    BOOST_TEST(schema.at("required").as_array().size() == 2u);

    auto const& typed_schema = tools[5].as_object().at("inputSchema").as_object();
    BOOST_TEST(typed_schema.at("properties").at("a").at("type").as_string() == "integer");
    BOOST_TEST(typed_schema.at("properties").at("b").at("type").as_string() == "number");
    BOOST_TEST(typed_schema.at("properties").at("c").at("type").as_string() == "boolean");
    BOOST_TEST(typed_schema.at("properties").at("d").at("type").as_string() == "string");
    BOOST_TEST(typed_schema.at("properties").at("e").at("type").as_string() == "integer");
    BOOST_TEST(typed_schema.at("required").as_array().size() == 4u);

    auto const& reset_schema = tools[3].as_object().at("inputSchema").as_object();
    BOOST_TEST(reset_schema.at("properties").as_object().empty());
    BOOST_TEST(!reset_schema.contains("required"));
}

// ---- tools/call -----------------------------------------------------------

BOOST_AUTO_TEST_CASE(test_mcp_tools_call_shapes)
{
    stateful_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    auto sid = initialize_stateful(e);
    std::vector<header> h {{"Mcp-Session-Id", sid}};

    auto add_res = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"add","arguments":{"a":2,"b":3}}})", h)).at("result");
    BOOST_TEST(add_res.at("content").as_array()[0].at("type").as_string() == "text");
    BOOST_TEST(add_res.at("content").as_array()[0].at("text").as_string() == "5");
    BOOST_TEST(add_res.at("structuredContent").at("result").as_int64() == 5);
    BOOST_TEST(add_res.at("isError").as_bool() == false);

    auto shout_res = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"shout","arguments":{"text":"hi"}}})", h)).at("result");
    BOOST_TEST(shout_res.at("content").as_array()[0].at("text").as_string() == "hi!"); // not JSON-quoted

    auto stats_res = body_json(post(e, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"stats"}})", h)).at("result");
    BOOST_TEST(stats_res.at("structuredContent").at("count").as_int64() == 1); // object kept as-is

    auto reset_res = body_json(post(e, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"reset","arguments":{}}})", h)).at("result");
    BOOST_TEST(reset_res.at("content").as_array().empty());
    BOOST_TEST(!reset_res.as_object().contains("structuredContent"));
}

BOOST_AUTO_TEST_CASE(test_mcp_tools_call_errors)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    build(e);

    auto domain = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"divide","arguments":{"a":1,"b":0}}})"));
    BOOST_TEST(domain.as_object().contains("result"));
    BOOST_TEST(domain.at("result").at("isError").as_bool() == true);
    BOOST_TEST(domain.at("result").at("content").as_array()[0].at("text").as_string() == "division by zero");

    auto unknown = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"nope","arguments":{}}})"));
    BOOST_TEST(unknown.at("error").at("code").as_int64() == -32602);
    BOOST_TEST(unknown.at("error").at("message").as_string() == "Unknown tool: nope");

    auto missing_arg = body_json(post(e, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"divide","arguments":{"a":1}}})"));
    BOOST_TEST(missing_arg.at("error").at("code").as_int64() == -32602);

    auto bad_type = body_json(post(e, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"divide","arguments":{"a":"x","b":1}}})"));
    BOOST_TEST(bad_type.at("error").at("code").as_int64() == -32602);

    auto no_name = body_json(post(e, R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{}})"));
    BOOST_TEST(no_name.at("error").at("code").as_int64() == -32602);

    auto bad_args = body_json(post(e, R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"divide","arguments":[1,2]}})"));
    BOOST_TEST(bad_args.at("error").at("code").as_int64() == -32602);
}

BOOST_AUTO_TEST_CASE(test_mcp_custom_tool_error_handler)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    e.set_tool_error_handler([](std::exception_ptr ex) -> std::optional<std::string>
    {
        try { std::rethrow_exception(ex); }
        catch (division_by_zero const&) { return std::nullopt; } // escalate to a JSON-RPC error
        catch (...) { return mcp::default_tool_error_handler(ex); }
    });
    e.rpc().set_error_mapper([](std::exception_ptr ex) -> jsonrpc::error_info
    {
        try { std::rethrow_exception(ex); }
        catch (division_by_zero const& x) { return {-32001, x.what(), std::nullopt}; }
        catch (...) { return jsonrpc::default_error_mapper(ex); }
    });
    auto v = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"divide","arguments":{"a":1,"b":0}}})"));
    BOOST_TEST(v.at("error").at("code").as_int64() == -32001);
}

// ---- resources ------------------------------------------------------------

BOOST_AUTO_TEST_CASE(test_mcp_resources_list_read_and_errors)
{
    stateful_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    auto sid = initialize_stateful(e);
    std::vector<header> h {{"Mcp-Session-Id", sid}};

    auto list = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"resources/list"})", h)).at("result").at("resources").as_array();
    BOOST_REQUIRE(list.size() == 1u);
    BOOST_TEST(list[0].at("uri").as_string() == "calc://history/last");
    BOOST_TEST(list[0].at("mimeType").as_string() == "text/plain");
    BOOST_TEST(list[0].at("description").as_string() == "Most recent calculation");

    auto templates = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"resources/templates/list"})", h)).at("result").at("resourceTemplates").as_array();
    BOOST_REQUIRE(templates.size() == 1u);
    BOOST_TEST(templates[0].at("uriTemplate").as_string() == "calc://history/{index}");

    post(e, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"add","arguments":{"a":2,"b":3}}})", h);
    post(e, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"add","arguments":{"a":1,"b":1}}})", h);

    auto last = body_json(post(e, R"({"jsonrpc":"2.0","id":5,"method":"resources/read","params":{"uri":"calc://history/last"}})", h)).at("result").at("contents").as_array()[0];
    BOOST_TEST(last.at("uri").as_string() == "calc://history/last");
    BOOST_TEST(last.at("mimeType").as_string() == "text/plain");
    BOOST_TEST(last.at("text").as_string() == "1+1=2");

    auto first = body_json(post(e, R"({"jsonrpc":"2.0","id":6,"method":"resources/read","params":{"uri":"calc://history/0"}})", h)).at("result").at("contents").as_array()[0];
    BOOST_TEST(first.at("uri").as_string() == "calc://history/0");
    BOOST_TEST(first.at("text").as_string() == "2+3=5");

    auto unknown = body_json(post(e, R"({"jsonrpc":"2.0","id":7,"method":"resources/read","params":{"uri":"calc://nope"}})", h));
    BOOST_TEST(unknown.at("error").at("code").as_int64() == -32002);

    auto bad_index = body_json(post(e, R"({"jsonrpc":"2.0","id":8,"method":"resources/read","params":{"uri":"calc://history/abc"}})", h));
    BOOST_TEST(bad_index.at("error").at("code").as_int64() == -32602);

    auto out_of_range = body_json(post(e, R"({"jsonrpc":"2.0","id":9,"method":"resources/read","params":{"uri":"calc://history/99"}})", h));
    BOOST_TEST(out_of_range.at("error").at("code").as_int64() == -32603);

    auto no_uri = body_json(post(e, R"({"jsonrpc":"2.0","id":10,"method":"resources/read","params":{}})", h));
    BOOST_TEST(no_uri.at("error").at("code").as_int64() == -32602);
}

// ---- custom methods, transport rules ---------------------------------------

BOOST_AUTO_TEST_CASE(test_mcp_custom_method_and_unknown_method)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    auto v = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"calc/version"})"));
    BOOST_TEST(v.at("result").as_string() == "1.0.0");
    auto u = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"nope"})"));
    BOOST_TEST(u.at("error").at("code").as_int64() == -32601);
}

BOOST_AUTO_TEST_CASE(test_mcp_transport_rules)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    build(e);

    auto bad_version = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", {{"MCP-Protocol-Version", "1999-01-01"}});
    BOOST_TEST(bad_version.result() == http::status::bad_request);
    auto good_version = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", {{"MCP-Protocol-Version", "2025-06-18"}});
    BOOST_TEST(good_version.result() == http::status::ok);

    auto batch = body_json(post(e, R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])"));
    BOOST_TEST(batch.at("error").at("code").as_int64() == -32600);

    auto parse = body_json(post(e, "{"));
    BOOST_TEST(parse.at("error").at("code").as_int64() == -32700);

    auto invalid = body_json(post(e, R"({"jsonrpc":"2.0","id":9})"));
    BOOST_TEST(invalid.at("error").at("code").as_int64() == -32600);
    BOOST_TEST(invalid.at("id").as_int64() == 9);

    auto put = send(e, http::verb::put, "{}");
    BOOST_TEST(put.result() == http::status::method_not_allowed);
}

BOOST_AUTO_TEST_CASE(test_mcp_origin_validation)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    e.set_origin_validator([](std::string_view origin) { return origin == "http://localhost:3000"; });

    auto ok = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", {{"Origin", "http://localhost:3000"}});
    BOOST_TEST(ok.result() == http::status::ok);
    auto no_origin = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
    BOOST_TEST(no_origin.result() == http::status::ok);
    auto evil = post(e, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", {{"Origin", "http://evil.example"}});
    BOOST_TEST(evil.result() == http::status::forbidden);
}

BOOST_AUTO_TEST_CASE(test_mcp_registration_introspection_and_duplicates)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    build(e);
    BOOST_TEST(e.tools().size() == 6u);
    BOOST_TEST(e.resources().size() == 2u);
    BOOST_TEST(e.resources()[1].is_template);
    BOOST_TEST(e.resources()[1].params[0].description == "zero-based");
    BOOST_CHECK_THROW(e.tool("add", "dup", &add, mcp_arg("a"), mcp_arg("b")), std::logic_error);
    BOOST_CHECK_THROW(e.resource("calc://history/last", "text/plain", "dup", &last_entry), std::logic_error);
}

} // namespace
