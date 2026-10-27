//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/mcp/content.hpp>
#include <boost/taar/core/base64.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/describe.hpp>
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

struct point
{
    int x;
    int y;
    std::optional<std::string> label;
};
BOOST_DESCRIBE_STRUCT(point, (), (x, y, label))

enum class color { red, green };
BOOST_DESCRIBE_ENUM(color, red, green)

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
    BOOST_TEST(!mcp::match_uri_template("calc://history/{index}", "calc://history/3/x").has_value()); // {var} stops at a slash

    // RFC 6570 operators.
    auto plus = mcp::match_uri_template("file://{+path}", "file://a/b/c.txt");
    BOOST_REQUIRE(plus.has_value());
    BOOST_TEST(plus->at("path") == "a/b/c.txt");
    auto pair_match = mcp::match_uri_template("x://{a,b}/end", "x://1,2/end");
    BOOST_REQUIRE(pair_match.has_value());
    BOOST_TEST(pair_match->at("a") == "1");
    BOOST_TEST(pair_match->at("b") == "2");
    BOOST_TEST(!mcp::match_uri_template("x://{a,b}/end", "x://1/end").has_value());
    auto frag = mcp::match_uri_template("doc://{id}{#section}", "doc://7#intro");
    BOOST_REQUIRE(frag.has_value());
    BOOST_TEST(frag->at("id") == "7");
    BOOST_TEST(frag->at("section") == "intro");
    auto nofrag = mcp::match_uri_template("doc://{id}{#section}", "doc://7");
    BOOST_REQUIRE(nofrag.has_value());
    BOOST_TEST(!nofrag->contains("section"));
    auto dot = mcp::match_uri_template("img://{name}{.ext}", "img://logo.png");
    BOOST_REQUIRE(dot.has_value());
    BOOST_TEST(dot->at("ext") == "png");
    auto segs = mcp::match_uri_template("repo://{owner}{/project,branch}", "repo://me/taar");
    BOOST_REQUIRE(segs.has_value());
    BOOST_TEST(segs->at("project") == "taar");
    BOOST_TEST(!segs->contains("branch"));
    auto exploded = mcp::match_uri_template("fs://{/path*}", "fs:///var/log/x");
    BOOST_REQUIRE(exploded.has_value());
    BOOST_TEST(exploded->at("path") == "var/log/x");
    auto semi = mcp::match_uri_template("m://item{;v,flag}", "m://item;v=2;flag");
    BOOST_REQUIRE(semi.has_value());
    BOOST_TEST(semi->at("v") == "2");
    BOOST_TEST(semi->at("flag") == "");
    auto query = mcp::match_uri_template("calc://history/{index}{?format,page}", "calc://history/4?page=2&other=x&format=json");
    BOOST_REQUIRE(query.has_value());
    BOOST_TEST(query->at("index") == "4");
    BOOST_TEST(query->at("format") == "json");
    BOOST_TEST(query->at("page") == "2");
    BOOST_TEST(!query->contains("other"));
    auto noquery = mcp::match_uri_template("calc://history/{index}{?format}", "calc://history/4");
    BOOST_REQUIRE(noquery.has_value());
    BOOST_TEST(!noquery->contains("format"));
    auto cont = mcp::match_uri_template("s://x?fixed=1{&tag}", "s://x?fixed=1&tag=a&tag=b");
    BOOST_REQUIRE(cont.has_value());
    BOOST_TEST(cont->at("tag") == "a,b");
    BOOST_TEST(mcp::match_uri_template("c://{code:3}", "c://abc").has_value());
    BOOST_TEST(!mcp::match_uri_template("c://{code:3}", "c://abcd").has_value());
    BOOST_TEST(!mcp::match_uri_template("c://{", "c://x").has_value());
    BOOST_TEST(!mcp::match_uri_template("c://{%}", "c://x").has_value());
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
    e.set_tool_error_handler([](taar::failure const& f) -> std::optional<std::string>
    {
        if (f.as<division_by_zero>()) return std::nullopt;   // escalate to a JSON-RPC error
        return mcp::default_tool_error_handler(f);
    });
    e.rpc().set_error_mapper([](taar::failure const& f) -> jsonrpc::error_info
    {
        if (auto const x = f.as<division_by_zero>()) return {-32001, x->what(), std::nullopt};
        return jsonrpc::default_error_mapper(f);
    });
    auto v = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"divide","arguments":{"a":1,"b":0}}})"));
    BOOST_TEST(v.at("error").at("code").as_int64() == -32001);
}

BOOST_AUTO_TEST_CASE(test_mcp_error_category_and_default_tool_error_handler)
{
    using taar::failure;

    // Wire codes are the enumerator values.
    boost::system::error_code const nf = mcp::error::resource_not_found;
    BOOST_TEST((nf.category() == mcp::error_category()));
    BOOST_TEST(nf.value() == -32002);
    BOOST_TEST(mcp::resource_not_found_code == -32002);
    BOOST_TEST(static_cast<int>(mcp::error::request_cancelled) == -32800);
    BOOST_TEST(std::string {mcp::error_category().name()} == "mcp");

    // Protocol categories stay JSON-RPC errors; anything else is isError text.
    BOOST_TEST(!mcp::default_tool_error_handler(failure {taar::error::argument_not_found}).has_value());
    BOOST_TEST(!mcp::default_tool_error_handler(failure {jsonrpc::error::invalid_params}).has_value());
    BOOST_TEST(!mcp::default_tool_error_handler(failure {mcp::error::client_error, "no"}).has_value());
    auto const user = mcp::default_tool_error_handler(
        failure::from_exception(std::make_exception_ptr(std::runtime_error {"boom"})));
    BOOST_TEST_REQUIRE(user.has_value());
    BOOST_TEST(*user == "boom");
    auto const timeout = mcp::default_tool_error_handler(
        failure {boost::system::errc::make_error_code(boost::system::errc::timed_out), "MCP request roots/list"});
    BOOST_TEST_REQUIRE(timeout.has_value());
    BOOST_TEST(*timeout == "MCP request roots/list");
}

BOOST_AUTO_TEST_CASE(test_mcp_resource_not_found_failure_keeps_wire_code)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    e.resource("calc://missing", "text/plain", "Always missing", []() -> std::string
    {
        throw taar::exception {taar::failure {mcp::error::resource_not_found, "gone"}};
    });
    auto v = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"calc://missing"}})"));
    BOOST_TEST(v.at("error").at("code").as_int64() == -32002);
    BOOST_TEST(v.at("error").at("message").as_string() == "gone");
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

BOOST_AUTO_TEST_CASE(test_mcp_tool_schemas_for_structs_sequences_and_enums)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    e.tool("move", "Move to a point", [](point p, std::vector<int> path, color c)
    {
        return json::value {{"x", p.x}, {"y", p.y}, {"steps", path.size()}, {"color", c == color::red ? "red" : "green"}};
    }, mcp_arg("to", "Target"), mcp_arg("path"), mcp_arg("color"));

    auto tools = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})")).at("result").at("tools").as_array();
    auto const& props = tools[0].at("inputSchema").at("properties");
    BOOST_TEST(props.at("to").at("type").as_string() == "object");
    BOOST_TEST(props.at("to").at("description").as_string() == "Target");
    BOOST_TEST(props.at("to").at("properties").at("x").at("type").as_string() == "integer");
    BOOST_TEST(props.at("to").at("properties").at("label").at("type").as_string() == "string");
    auto const& req = props.at("to").at("required").as_array();
    BOOST_TEST(req.size() == 2u);           // label is optional
    BOOST_TEST(props.at("path").at("type").as_string() == "array");
    BOOST_TEST(props.at("path").at("items").at("type").as_string() == "integer");
    BOOST_TEST(props.at("color").at("type").as_string() == "string");
    BOOST_TEST(props.at("color").at("enum").as_array().size() == 2u);

    auto call = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"move","arguments":{"to":{"x":1,"y":2},"path":[1,2,3],"color":"green"}}})"));
    BOOST_TEST(call.at("result").at("structuredContent").at("steps").as_int64() == 3);
    BOOST_TEST(call.at("result").at("structuredContent").at("color").as_string() == "green");
}

BOOST_AUTO_TEST_CASE(test_mcp_tool_reads_http_headers_through_rest_providers)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    e.tool("whoami", "Caller identity", [](std::string auth, std::optional<std::string> trace)
    {
        return auth + "/" + trace.value_or("none");
    }, taar::handler::header_arg(http::field::authorization, "Bearer token"), taar::handler::header_arg("X-Trace"));

    auto schema = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})")).at("result").at("tools").as_array()[0].at("inputSchema");
    BOOST_TEST(schema.at("properties").at("Authorization").at("description").as_string() == "Bearer token");
    BOOST_TEST(schema.at("required").as_array().size() == 1u);

    auto ok = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"whoami"}})",
        {{"Authorization", "Bearer abc"}}));
    BOOST_TEST(ok.at("result").at("structuredContent").at("result").as_string() == "Bearer abc/none");

    auto missing = body_json(post(e, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"whoami"}})"));
    BOOST_TEST(missing.at("error").at("code").as_int64() == -32602);
}

BOOST_AUTO_TEST_CASE(test_mcp_prompts_list_and_get)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    e.prompt("summarize", "Summarize a text", [](std::string text, std::optional<std::string> tone)
    {
        return "Summarize" + (tone ? " in a " + *tone + " tone" : std::string {}) + ": " + text;
    }, mcp_arg("text", "The text"), mcp_arg("tone"));
    e.prompt("chat", "Two messages", [](std::string topic)
    {
        return json::array {
            json::object {{"role", "user"}, {"content", json::object {{"type", "text"}, {"text", "Tell me about " + topic}}}},
            json::object {{"role", "assistant"}, {"content", json::object {{"type", "text"}, {"text", "Sure."}}}}};
    }, mcp_arg("topic"));

    auto init = body_json(post(e, initialize_body));
    BOOST_TEST(init.at("result").at("capabilities").at("prompts").at("listChanged").as_bool());
    BOOST_TEST(init.at("result").at("capabilities").at("resources").at("subscribe").as_bool() == false);   // stateless

    auto list = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"prompts/list"})")).at("result").at("prompts").as_array();
    BOOST_REQUIRE(list.size() == 2u);
    BOOST_TEST(list[0].at("name").as_string() == "summarize");
    BOOST_TEST(list[0].at("description").as_string() == "Summarize a text");
    auto const& args = list[0].at("arguments").as_array();
    BOOST_TEST(args[0].at("name").as_string() == "text");
    BOOST_TEST(args[0].at("description").as_string() == "The text");
    BOOST_TEST(args[0].at("required").as_bool());
    BOOST_TEST(!args[1].at("required").as_bool());

    auto got = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"prompts/get","params":{"name":"summarize","arguments":{"text":"hello","tone":"formal"}}})")).at("result");
    BOOST_TEST(got.at("description").as_string() == "Summarize a text");
    auto const& msgs = got.at("messages").as_array();
    BOOST_REQUIRE(msgs.size() == 1u);
    BOOST_TEST(msgs[0].at("role").as_string() == "user");
    BOOST_TEST(msgs[0].at("content").at("text").as_string() == "Summarize in a formal tone: hello");

    auto chat = body_json(post(e, R"({"jsonrpc":"2.0","id":3,"method":"prompts/get","params":{"name":"chat","arguments":{"topic":"asio"}}})")).at("result");
    BOOST_TEST(chat.at("messages").as_array().size() == 2u);

    auto missing = body_json(post(e, R"({"jsonrpc":"2.0","id":4,"method":"prompts/get","params":{"name":"summarize","arguments":{}}})"));
    BOOST_TEST(missing.at("error").at("code").as_int64() == -32602);
    auto unknown = body_json(post(e, R"({"jsonrpc":"2.0","id":5,"method":"prompts/get","params":{"name":"nope"}})"));
    BOOST_TEST(unknown.at("error").at("code").as_int64() == -32602);
    BOOST_CHECK_THROW(e.prompt("chat", "dup", [] { return ""; }), std::logic_error);
    BOOST_TEST(e.prompts().size() == 2u);

    // Without prompts the capability is not advertised, and subscribe is unknown when stateless.
    stateless_endpoint bare {{.name = "s", .version = "1"}};
    auto bare_init = body_json(post(bare, initialize_body));
    BOOST_TEST(!bare_init.at("result").at("capabilities").as_object().contains("prompts"));
    auto sub = body_json(post(bare, R"({"jsonrpc":"2.0","id":6,"method":"resources/subscribe","params":{"uri":"x://y"}})"));
    BOOST_TEST(sub.at("error").at("code").as_int64() == -32601);
}

BOOST_AUTO_TEST_CASE(test_mcp_list_pagination)
{
    stateless_endpoint e {{.name = "s", .version = "1", .page_size = 2}};
    build(e);       // 6 tools, 1 resource, 1 template
    auto page1 = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})")).at("result");
    BOOST_TEST(page1.at("tools").as_array().size() == 2u);
    BOOST_TEST(page1.at("tools").as_array()[0].at("name").as_string() == "add");
    BOOST_TEST(page1.at("nextCursor").as_string() == "2");
    auto page2 = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{"cursor":"2"}})")).at("result");
    BOOST_TEST(page2.at("tools").as_array()[0].at("name").as_string() == "shout");
    BOOST_TEST(page2.at("nextCursor").as_string() == "4");
    auto page3 = body_json(post(e, R"({"jsonrpc":"2.0","id":3,"method":"tools/list","params":{"cursor":"4"}})")).at("result");
    BOOST_TEST(page3.at("tools").as_array().size() == 2u);
    BOOST_TEST(!page3.as_object().contains("nextCursor"));
    auto bad = body_json(post(e, R"({"jsonrpc":"2.0","id":4,"method":"tools/list","params":{"cursor":"zz"}})"));
    BOOST_TEST(bad.at("error").at("code").as_int64() == -32602);
    auto beyond = body_json(post(e, R"({"jsonrpc":"2.0","id":5,"method":"tools/list","params":{"cursor":"99"}})"));
    BOOST_TEST(beyond.at("error").at("code").as_int64() == -32602);
    auto res = body_json(post(e, R"({"jsonrpc":"2.0","id":6,"method":"resources/list"})")).at("result");
    BOOST_TEST(res.at("resources").as_array().size() == 1u);
    BOOST_TEST(!res.as_object().contains("nextCursor"));           // fits in one page
}

BOOST_AUTO_TEST_CASE(test_base64_round_trip)
{
    BOOST_TEST(taar::base64_encode("") == "");
    BOOST_TEST(taar::base64_encode("f") == "Zg==");
    BOOST_TEST(taar::base64_encode("fo") == "Zm8=");
    BOOST_TEST(taar::base64_encode("foo") == "Zm9v");
    BOOST_TEST(taar::base64_encode("foobar") == "Zm9vYmFy");
    std::string bytes;
    for (int i = 0; i < 256; ++i) bytes += static_cast<char>(i);
    BOOST_TEST(taar::base64_decode(taar::base64_encode(bytes)).value() == bytes);
    BOOST_TEST(taar::base64_decode("Zm8=").value() == "fo");
    BOOST_TEST(!taar::base64_decode("Zm8").has_value());
    BOOST_TEST(!taar::base64_decode("Zm=8").has_value());
    BOOST_TEST(!taar::base64_decode("Zm9!").has_value());
}

BOOST_AUTO_TEST_CASE(test_mcp_binary_tool_results_and_blob_resources)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    std::string const png = std::string {"\x89PNG\r\n\x1a\n", 8} + "fake";
    e.tool("picture", "An image", [png] { return mcp::image::from_bytes(png, "image/png"); });
    e.tool("mixed", "Several items", [png]
    {
        mcp::tool_result r;
        r.add(mcp::text {"here you go"});
        r.add(mcp::image::from_bytes(png, "image/png"));
        r.add(mcp::embedded_resource {.uri = "notes://1", .mime_type = "text/plain", .text = "note one"});
        r.add(mcp::embedded_resource {.uri = "file://x.bin", .mime_type = "application/octet-stream", .data = taar::base64_encode(std::string {"\x00\x01", 2})});
        r.structured = json::object {{"count", 4}};
        return r;
    });
    e.tool("failed", "Explicit error", []
    {
        return mcp::tool_result {.content = {mcp::text {"nope"}}, .is_error = true};
    });
    e.resource("file://logo", "image/png", "Logo bytes", [png] { return mcp::blob::from_bytes(png, "image/png"); });
    e.resource("file://note", "text/plain", "Text", [] { return std::string {"plain"}; });

    auto pic = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"picture"}})")).at("result");
    auto const& item = pic.at("content").as_array()[0];
    BOOST_TEST(item.at("type").as_string() == "image");
    BOOST_TEST(item.at("mimeType").as_string() == "image/png");
    BOOST_TEST(taar::base64_decode(std::string {item.at("data").as_string()}).value() == png);
    BOOST_TEST(pic.at("isError").as_bool() == false);
    BOOST_TEST(!pic.as_object().contains("structuredContent"));

    auto mixed = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"mixed"}})")).at("result");
    auto const& items = mixed.at("content").as_array();
    BOOST_REQUIRE(items.size() == 4u);
    BOOST_TEST(items[0].at("type").as_string() == "text");
    BOOST_TEST(items[0].at("text").as_string() == "here you go");
    BOOST_TEST(items[1].at("type").as_string() == "image");
    BOOST_TEST(items[2].at("type").as_string() == "resource");
    BOOST_TEST(items[2].at("resource").at("text").as_string() == "note one");
    BOOST_TEST(items[3].at("resource").at("blob").as_string() == "AAE=");
    BOOST_TEST(mixed.at("structuredContent").at("count").as_int64() == 4);

    auto failed = body_json(post(e, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"failed"}})")).at("result");
    BOOST_TEST(failed.at("isError").as_bool());
    BOOST_TEST(failed.at("content").as_array()[0].at("text").as_string() == "nope");

    auto logo = body_json(post(e, R"({"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"file://logo"}})")).at("result").at("contents").as_array()[0];
    BOOST_TEST(logo.at("mimeType").as_string() == "image/png");
    BOOST_TEST(!logo.as_object().contains("text"));
    BOOST_TEST(taar::base64_decode(std::string {logo.at("blob").as_string()}).value() == png);
    auto note = body_json(post(e, R"({"jsonrpc":"2.0","id":5,"method":"resources/read","params":{"uri":"file://note"}})")).at("result").at("contents").as_array()[0];
    BOOST_TEST(note.at("text").as_string() == "plain");
    BOOST_TEST(!note.as_object().contains("blob"));
}

BOOST_AUTO_TEST_CASE(test_mcp_logging_levels)
{
    stateful_endpoint e {{.name = "s", .version = "1"}};
    auto init = post(e, initialize_body);
    std::string const sid {init["Mcp-Session-Id"]};
    BOOST_TEST(body_json(init).at("result").at("capabilities").as_object().contains("logging"));
    BOOST_TEST(!body_json(init).at("result").at("capabilities").as_object().contains("completions"));

    BOOST_TEST(e.log(sid, mcp::log_level::debug, "starting"));             // default level: everything
    auto set = post(e, R"({"jsonrpc":"2.0","id":2,"method":"logging/setLevel","params":{"level":"warning"}})", {{"Mcp-Session-Id", sid}});
    BOOST_TEST(body_json(set).at("result").as_object().empty());
    BOOST_TEST(!e.log(sid, mcp::log_level::info, "chatty"));
    BOOST_TEST(!e.log(sid, mcp::log_level::notice, "still chatty"));
    BOOST_TEST(e.log(sid, mcp::log_level::warning, json::object {{"disk", "90%"}}, "storage"));
    BOOST_TEST(e.log(sid, mcp::log_level::emergency, "fire"));
    BOOST_TEST(e.log_all(mcp::log_level::error, "all") == 1u);
    BOOST_TEST(e.log_all(mcp::log_level::info, "all") == 0u);
    BOOST_TEST(!e.log("nope", mcp::log_level::error, "x"));

    auto bad = post(e, R"({"jsonrpc":"2.0","id":3,"method":"logging/setLevel","params":{"level":"loud"}})", {{"Mcp-Session-Id", sid}});
    BOOST_TEST(body_json(bad).at("error").at("code").as_int64() == -32602);
    BOOST_TEST((mcp::parse_log_level("critical") == mcp::log_level::critical));
    BOOST_TEST(mcp::to_string(mcp::log_level::alert) == "alert");

    // Stateless endpoints have no stream to log to: the capability is not
    // advertised and the method is unknown.
    stateless_endpoint stateless {{.name = "s", .version = "1"}};
    BOOST_TEST(!body_json(post(stateless, initialize_body)).at("result").at("capabilities").as_object().contains("logging"));
    auto unknown = post(stateless, R"({"jsonrpc":"2.0","id":1,"method":"logging/setLevel","params":{"level":"error"}})");
    BOOST_TEST(body_json(unknown).at("error").at("code").as_int64() == -32601);
}

BOOST_AUTO_TEST_CASE(test_mcp_completion)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    e.prompt("greet", "Say hi", [](std::string style) { return "hi, " + style; }, mcp_arg("style", "tone"));
    e.resource("calc://history/{index}", "text/plain", "entry", [](int index) { return std::to_string(index); }, uri_arg("index"));
    BOOST_TEST(!body_json(post(e, initialize_body)).at("result").at("capabilities").as_object().contains("completions"));

    e.complete_prompt("greet", "style", [](std::string_view value)
    {
        std::vector<std::string> out;
        for (std::string_view s : {"terse", "friendly", "formal"})
        {
            if (s.starts_with(value)) out.emplace_back(s);
        }
        return out;
    });
    e.complete_resource("calc://history/{index}", "index", [](mcp::completion_request const& r)
    {
        mcp::completion_result out;
        BOOST_TEST(r.ref_type == "ref/resource");
        BOOST_TEST(r.ref == "calc://history/{index}");
        BOOST_TEST(r.argument == "index");
        std::size_t const n = r.context.contains("limit") ? 5 : 150;
        for (std::size_t i = 0; i < n; ++i) out.values.push_back(std::to_string(i));
        return out;
    });
    BOOST_CHECK_THROW(e.complete_prompt("nope", "x", [](std::string_view) { return std::vector<std::string> {}; }), std::logic_error);
    BOOST_CHECK_THROW(e.complete_resource("calc://history/last", "x", [](std::string_view) { return std::vector<std::string> {}; }), std::logic_error);
    BOOST_TEST(body_json(post(e, initialize_body)).at("result").at("capabilities").as_object().contains("completions"));

    auto styles = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"completion/complete","params":{"ref":{"type":"ref/prompt","name":"greet"},"argument":{"name":"style","value":"f"}}})")).at("result").at("completion");
    BOOST_REQUIRE(styles.at("values").as_array().size() == 2u);
    BOOST_TEST(styles.at("values").as_array()[0].as_string() == "friendly");
    BOOST_TEST(styles.at("hasMore").as_bool() == false);
    BOOST_TEST(!styles.as_object().contains("total"));

    auto many = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"completion/complete","params":{"ref":{"type":"ref/resource","uri":"calc://history/{index}"},"argument":{"name":"index","value":""}}})")).at("result").at("completion");
    BOOST_TEST(many.at("values").as_array().size() == 100u);
    BOOST_TEST(many.at("hasMore").as_bool());
    BOOST_TEST(many.at("total").to_number<std::uint64_t>() == 150u);
    auto few = body_json(post(e, R"({"jsonrpc":"2.0","id":3,"method":"completion/complete","params":{"ref":{"type":"ref/resource","uri":"calc://history/{index}"},"argument":{"name":"index","value":""},"context":{"arguments":{"limit":"yes"}}}})")).at("result").at("completion");
    BOOST_TEST(few.at("values").as_array().size() == 5u);
    BOOST_TEST(!few.at("hasMore").as_bool());

    // No completer for the argument: an empty list, not an error.
    auto none = body_json(post(e, R"({"jsonrpc":"2.0","id":4,"method":"completion/complete","params":{"ref":{"type":"ref/prompt","name":"greet"},"argument":{"name":"other","value":"x"}}})")).at("result").at("completion");
    BOOST_TEST(none.at("values").as_array().empty());
    // Invalid shapes and unknown targets are -32602.
    for (std::string params : {
        R"({"ref":{"type":"ref/other","name":"greet"},"argument":{"name":"style","value":""}})",
        R"({"ref":{"type":"ref/prompt","name":"missing"},"argument":{"name":"style","value":""}})",
        R"({"ref":{"type":"ref/resource","uri":"calc://nope/{x}"},"argument":{"name":"x","value":""}})",
        R"({"ref":{"type":"ref/prompt","name":"greet"}})",
        R"({"argument":{"name":"style","value":""}})"})
    {
        auto err = body_json(post(e, R"({"jsonrpc":"2.0","id":9,"method":"completion/complete","params":)" + params + "}"));
        BOOST_TEST(err.at("error").at("code").as_int64() == -32602);
    }
}

BOOST_AUTO_TEST_CASE(test_mcp_output_schema_and_annotations)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    e.tool("sq", "Square", [](int n) { return n * n; }, mcp_arg("n"));
    e.tool("mk", "Make a point", [](int x) { return point {x, x, "p"}; }, mcp_arg("x"));
    e.tool("names", "Some names", [] { return std::vector<std::string> {"a", "b"}; });
    e.tool("raw", "Untyped", [] { return json::value {1}; });
    e.tool("typed", "Typed content", [] { return mcp::tool_result {.content = {mcp::text {"x"}}}; });
    e.tool("async", "Async string", []() -> taar::awaitable<std::string> { co_return "s"; });
    e.tool(mcp::tool_spec {
            .name = "wipe", .description = "Delete everything", .title = "Wipe",
            .annotations = mcp::tool_annotations {.title = "Wipe all", .destructive_hint = true, .idempotent_hint = false},
            .output_schema = json::object {}},
        [] { return 1; });
    e.tool(mcp::tool_spec {.name = "custom", .description = "Own schema",
            .output_schema = json::object {{"type", "object"}, {"properties", json::object {{"result", json::object {{"type", "number"}}}}}}},
        [] { return 2; });

    auto tools = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})")).at("result").at("tools").as_array();
    auto find = [&](std::string const& name) -> json::object const&
    {
        for (auto const& t : tools) if (t.at("name").as_string() == name) return t.as_object();
        throw std::runtime_error {"no tool " + name};
    };
    auto const& sq = find("sq").at("outputSchema");
    BOOST_TEST(sq.at("type").as_string() == "object");
    BOOST_TEST(sq.at("properties").at("result").at("type").as_string() == "integer");
    BOOST_TEST(sq.at("required").as_array()[0].as_string() == "result");
    auto const& mk = find("mk").at("outputSchema");
    BOOST_TEST(mk.at("type").as_string() == "object");
    BOOST_TEST(mk.at("properties").at("label").at("type").as_string() == "string");
    BOOST_TEST(find("names").at("outputSchema").at("properties").at("result").at("type").as_string() == "array");
    BOOST_TEST(find("async").at("outputSchema").at("properties").at("result").at("type").as_string() == "string");
    BOOST_TEST(!find("raw").contains("outputSchema"));
    BOOST_TEST(!find("typed").contains("outputSchema"));
    BOOST_TEST(!find("typed").contains("annotations"));

    auto const& wipe = find("wipe");
    BOOST_TEST(wipe.at("title").as_string() == "Wipe");
    BOOST_TEST(!wipe.contains("outputSchema"));
    BOOST_TEST(wipe.at("annotations").at("title").as_string() == "Wipe all");
    BOOST_TEST(wipe.at("annotations").at("destructiveHint").as_bool());
    BOOST_TEST(wipe.at("annotations").at("idempotentHint").as_bool() == false);
    BOOST_TEST(!wipe.at("annotations").as_object().contains("readOnlyHint"));
    BOOST_TEST(find("custom").at("outputSchema").at("properties").at("result").at("type").as_string() == "number");

    // The declared shape matches what tools/call returns.
    auto called = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"mk","arguments":{"x":3}}})")).at("result");
    BOOST_TEST(called.at("structuredContent").at("label").as_string() == "p");
    BOOST_TEST(e.tools().back().output_schema.at("properties").at("result").at("type").as_string() == "number");
}

BOOST_AUTO_TEST_CASE(test_mcp_resource_templates_with_operators)
{
    stateless_endpoint e {{.name = "s", .version = "1"}};
    e.resource("file://{+path}", "text/plain", "A file", [](std::string path) { return "contents of " + path; }, uri_arg("path"));
    e.resource("calc://history/{index}{?format}", "text/plain", "Entry", [](int index, std::optional<std::string> format)
    {
        return std::to_string(index) + " as " + format.value_or("text");
    }, uri_arg("index"), uri_arg("format"));

    auto file = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"file://etc/hosts"}})")).at("result").at("contents").as_array()[0];
    BOOST_TEST(file.at("text").as_string() == "contents of etc/hosts");
    auto plain = body_json(post(e, R"({"jsonrpc":"2.0","id":2,"method":"resources/read","params":{"uri":"calc://history/2"}})")).at("result").at("contents").as_array()[0];
    BOOST_TEST(plain.at("text").as_string() == "2 as text");
    auto json_fmt = body_json(post(e, R"({"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"calc://history/2?format=json"}})")).at("result").at("contents").as_array()[0];
    BOOST_TEST(json_fmt.at("text").as_string() == "2 as json");
    auto missing = body_json(post(e, R"({"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"calc://history/2/3"}})"));
    BOOST_TEST(missing.at("error").at("code").as_int64() == -32002);
    auto templates = body_json(post(e, R"({"jsonrpc":"2.0","id":5,"method":"resources/templates/list"})")).at("result").at("resourceTemplates").as_array();
    BOOST_TEST(templates.size() == 2u);
    BOOST_TEST(templates[0].at("uriTemplate").as_string() == "file://{+path}");
}

BOOST_AUTO_TEST_CASE(test_mcp_coroutine_callable_with_reference_parameters)
{
    // Arguments converted for a coroutine callable must outlive its first
    // suspension: `name` is a reference into the binder's frame.
    stateless_endpoint e {{.name = "s", .version = "1"}};
    e.tool("greet", "Greets", [](std::string const& name, std::vector<int> const& nums) -> taar::awaitable<std::string>
    {
        net::steady_timer timer {co_await net::this_coro::executor};
        timer.expires_after(std::chrono::milliseconds {5});
        co_await timer.async_wait(taar::use_awaitable);
        std::string out = "hi " + name + ":";
        for (int n : nums) out += std::to_string(n);
        co_return out;
    }, mcp_arg("name"), mcp_arg("nums"));
    auto res = body_json(post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"greet","arguments":{"name":"Ada Lovelace of Analytical Engines","nums":[1,2,3]}}})"));
    BOOST_TEST(res.at("result").at("content").as_array()[0].at("text").as_string() == "hi Ada Lovelace of Analytical Engines:123");

    // Path parameters in a template may come in any order and be skipped.
    auto semi = mcp::match_uri_template("calc://x{;a,b}", "calc://x;b=1");
    BOOST_REQUIRE(semi.has_value());
    BOOST_TEST(semi->at("b") == "1");
    BOOST_TEST(!semi->contains("a"));
    auto swapped = mcp::match_uri_template("calc://x{;a,b}", "calc://x;b=1;a=2");
    BOOST_REQUIRE(swapped.has_value());
    BOOST_TEST(swapped->at("a") == "2");
    BOOST_TEST(!mcp::match_uri_template("calc://x{;a,b}", "calc://x;c=1").has_value());
}

} // namespace
