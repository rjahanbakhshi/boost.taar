//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/handler/constrained.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/jsonrpc/arg.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/parse.hpp>
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
namespace mcp = boost::taar::mcp;
using taar::handler::constrained;
using taar::handler::constraints;
using taar::handler::json_body_arg;
using taar::handler::path_arg;
using taar::handler::query_arg;
using taar::handler::rest;
using taar::handler::with_default;
using taar::matcher::method;
using taar::matcher::target;

template <typename Handler, typename Request>
std::string run_handler(Handler& h, Request const& req, taar::matcher::context const& ctx)
{
    net::io_context io;
    std::string body;
    auto fut = net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        auto res = co_await h(req, ctx);
        body = res.body();
    }, net::use_future);
    io.run();
    fut.get();
    return body;
}

/// True if invoking throws a constraint violation whose message contains @a fragment.
template <typename Handler, typename Request>
bool violates(Handler& h, Request const& req, taar::matcher::context const& ctx, std::string const& fragment)
{
    try
    {
        run_handler(h, req, ctx);
    }
    catch (boost::system::system_error const& e)
    {
        BOOST_TEST_MESSAGE(e.what());
        return e.code() == taar::error::constraint_violated && std::string {e.what()}.find(fragment) != std::string::npos;
    }
    return false;
}

template <typename Endpoint>
json::value post(Endpoint& e, std::string body)
{
    http::request<http::string_body> req {http::verb::post, "/", 11};
    req.set(http::field::content_type, "application/json");
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
    return json::parse(out->body());
}

BOOST_AUTO_TEST_CASE(test_constrained_rest_numbers_strings_and_defaults)
{
    auto h = rest(
        [](int id, std::string q, std::string sort) { return std::to_string(id) + ":" + q + ":" + sort; },
        constrained(path_arg("id", "Item id"), {.minimum = 1, .maximum = 10}),
        constrained(query_arg("q"), {.min_length = 2, .max_length = 5, .pattern = "[a-z]+"}),
        constrained(with_default(query_arg("sort"), std::string {"asc"}), {.one_of = {"asc", "desc"}}));

    taar::matcher::context ctx;
    ctx.path_args = {{"id", "5"}};
    http::request<http::empty_body> ok {http::verb::get, "/items/5?q=abc", 11};
    BOOST_TEST(run_handler(h, ok, ctx) == "5:abc:asc");
    http::request<http::empty_body> sorted {http::verb::get, "/items/5?q=abc&sort=desc", 11};
    BOOST_TEST(run_handler(h, sorted, ctx) == "5:abc:desc");

    taar::matcher::context low;
    low.path_args = {{"id", "0"}};
    BOOST_TEST(violates(h, ok, low, "id must be >= 1"));
    taar::matcher::context high;
    high.path_args = {{"id", "11"}};
    BOOST_TEST(violates(h, ok, high, "id must be <= 10"));
    http::request<http::empty_body> shrt {http::verb::get, "/items/5?q=a", 11};
    BOOST_TEST(violates(h, shrt, ctx, "q must be at least 2 characters"));
    http::request<http::empty_body> lng {http::verb::get, "/items/5?q=abcdef", 11};
    BOOST_TEST(violates(h, lng, ctx, "q must be at most 5 characters"));
    http::request<http::empty_body> caps {http::verb::get, "/items/5?q=ABC", 11};
    BOOST_TEST(violates(h, caps, ctx, "q does not match pattern [a-z]+"));
    http::request<http::empty_body> bad_sort {http::verb::get, "/items/5?q=abc&sort=up", 11};
    BOOST_TEST(violates(h, bad_sort, ctx, "sort must be one of asc, desc"));

    // Declared on the handler: constraint keywords, default, required flags.
    auto params = h.params();
    BOOST_REQUIRE(params.size() == 3u);
    BOOST_TEST(params[0].info.schema.at("minimum").as_int64() == 1);
    BOOST_TEST(params[0].info.schema.at("maximum").as_int64() == 10);
    BOOST_TEST(params[0].info.description == "Item id");
    BOOST_TEST(params[0].info.required);
    BOOST_TEST(params[1].info.schema.at("minLength").as_uint64() == 2u);
    BOOST_TEST(params[1].info.schema.at("maxLength").as_uint64() == 5u);
    BOOST_TEST(params[1].info.schema.at("pattern").as_string() == "[a-z]+");
    BOOST_TEST(params[2].info.schema.at("enum").as_array().size() == 2u);
    BOOST_TEST(!params[2].info.required);
    BOOST_REQUIRE(params[2].info.default_value.has_value());
    BOOST_TEST(params[2].info.default_value->as_string() == "asc");
    BOOST_TEST((params[2].in == taar::handler::param_location::query));
}

BOOST_AUTO_TEST_CASE(test_constrained_optional_sequences_and_documentation_only_rules)
{
    auto h = rest(
        [](std::optional<double> ratio, std::vector<int> items, std::string email)
        {
            return std::to_string(items.size()) + ":" + std::to_string(ratio.value_or(-1)) + ":" + email;
        },
        constrained(query_arg("ratio"), {.exclusive_minimum = 0, .exclusive_maximum = 1}),
        constrained(json_body_arg(), {.min_items = 1, .max_items = 3}),
        constrained(query_arg("email"), {.format = "email", .example = "a@b.c"}));

    taar::matcher::context ctx;
    auto request = [](std::string query, std::string body)
    {
        http::request<http::string_body> req {http::verb::post, "/x" + query, 11};
        req.set(http::field::content_type, "application/json");
        req.body() = std::move(body);
        req.prepare_payload();
        return req;
    };
    BOOST_TEST(run_handler(h, request("?email=x@y.z", "[1,2]"), ctx).starts_with("2:-1"));    // absent optional: unchecked
    BOOST_TEST(run_handler(h, request("?ratio=0.5&email=x@y.z", "[1]"), ctx).starts_with("1:0.5"));
    BOOST_TEST(violates(h, request("?ratio=0&email=x@y.z", "[1]"), ctx, "ratio must be > 0"));
    BOOST_TEST(violates(h, request("?ratio=1&email=x@y.z", "[1]"), ctx, "ratio must be < 1"));
    BOOST_TEST(violates(h, request("?email=x@y.z", "[]"), ctx, "must have at least 1 items"));
    BOOST_TEST(violates(h, request("?email=x@y.z", "[1,2,3,4]"), ctx, "must have at most 3 items"));
    BOOST_TEST(run_handler(h, request("?email=not-an-email", "[1]"), ctx).ends_with("not-an-email"));   // format is documentation

    auto params = h.params();
    BOOST_TEST(params[0].info.schema.at("exclusiveMinimum").as_int64() == 0);
    BOOST_TEST(params[0].info.schema.at("exclusiveMaximum").as_int64() == 1);
    BOOST_TEST(params[1].info.schema.at("minItems").as_uint64() == 1u);
    BOOST_TEST(params[1].info.schema.at("maxItems").as_uint64() == 3u);
    BOOST_TEST(params[1].info.schema.at("type").as_string() == "array");
    BOOST_TEST(params[2].info.schema.at("format").as_string() == "email");
    BOOST_TEST(params[2].info.schema.at("examples").as_array()[0].as_string() == "a@b.c");
}

BOOST_AUTO_TEST_CASE(test_constrained_jsonrpc_method)
{
    jsonrpc::endpoint<> rpc;
    rpc.method("sq", [](int n) { return n * n; }, constrained(jsonrpc::rpc_arg("n", "Base"), {.minimum = 0}));
    BOOST_TEST(post(rpc, R"({"jsonrpc":"2.0","id":1,"method":"sq","params":{"n":3}})").at("result").as_int64() == 9);
    auto err = post(rpc, R"({"jsonrpc":"2.0","id":2,"method":"sq","params":{"n":-1}})").at("error");
    BOOST_TEST(err.at("code").as_int64() == -32602);
    BOOST_TEST(std::string {err.at("data").as_string()}.find("n must be >= 0") != std::string::npos);
    // Positional parameters bind through the wrapper too.
    BOOST_TEST(post(rpc, R"({"jsonrpc":"2.0","id":3,"method":"sq","params":[4]})").at("result").as_int64() == 16);
}

BOOST_AUTO_TEST_CASE(test_constrained_mcp_tool_schema_and_call)
{
    mcp::endpoint<> e {{.name = "s", .version = "1"}};
    e.tool("sq", "Square", [](int n) { return n * n; },
        constrained(mcp::mcp_arg("n", "Base"), {.minimum = 0, .maximum = 100}));
    e.tool("pick", "Pick", [](std::string mode) { return mode; },
        constrained(with_default(mcp::mcp_arg("mode"), std::string {"fast"}), {.one_of = {"fast", "safe"}}));

    auto tools = post(e, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})").at("result").at("tools").as_array();
    auto const& n = tools[0].at("inputSchema").at("properties").at("n");
    BOOST_TEST(n.at("type").as_string() == "integer");
    BOOST_TEST(n.at("description").as_string() == "Base");
    BOOST_TEST(n.at("minimum").as_int64() == 0);
    BOOST_TEST(n.at("maximum").as_int64() == 100);
    auto const& pick = tools[1].at("inputSchema");
    BOOST_TEST(pick.at("properties").at("mode").at("enum").as_array().size() == 2u);
    BOOST_TEST(pick.at("properties").at("mode").at("default").as_string() == "fast");
    BOOST_TEST(!pick.as_object().contains("required"));

    BOOST_TEST(post(e, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"sq","arguments":{"n":7}}})")
        .at("result").at("structuredContent").at("result").as_int64() == 49);
    BOOST_TEST(post(e, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"pick"}})")
        .at("result").at("content").as_array()[0].at("text").as_string() == "fast");
    auto bad = post(e, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"sq","arguments":{"n":101}}})");
    std::string const text = json::serialize(bad);
    BOOST_TEST(text.find("n must be <= 100") != std::string::npos);
    bool const reported = !bad.as_object().contains("result") || bad.at("result").at("isError").as_bool();
    BOOST_TEST(reported);
}

BOOST_AUTO_TEST_CASE(test_e2e_constraint_violation_is_400_with_message)
{
    net::io_context io;
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/items/{id}",
        rest([](int id) { return id; }, constrained(path_arg("id"), {.minimum = 1})));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port, {
            e2e::make_request(http::verb::get, "/items/0"),
            e2e::make_request(http::verb::get, "/items/3"),
        });
        BOOST_TEST(res[0].result() == http::status::bad_request);
        BOOST_TEST(res[0].body().find("id must be >= 1") != std::string::npos);
        BOOST_TEST(res[1].result() == http::status::ok);
        BOOST_TEST(res[1].body() == "3");
    });
}

} // namespace
