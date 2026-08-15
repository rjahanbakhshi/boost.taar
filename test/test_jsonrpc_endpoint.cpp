//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/jsonrpc/arg.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;

using jsonrpc::rpc_arg;
using taar::handler::contexts;
using taar::handler::with_default;

struct calc_session
{
    int last = 0;
    std::vector<std::string> history;
};

struct division_by_zero : std::domain_error { using domain_error::domain_error; };

int add(calc_session& s, int a, int b)
{
    s.last = a + b;
    s.history.push_back(std::to_string(a) + "+" + std::to_string(b));
    return s.last;
}

int sub(int a, int b) { return a - b; }

int divide(int a, int b)
{
    if (b == 0) throw division_by_zero {"division by zero"};
    return a / b;
}

std::vector<std::string> history(calc_session const& s) { return s.history; }

void ping() {}

taar::awaitable<int> async_double(int x)
{
    net::steady_timer t {co_await net::this_coro::executor};
    t.expires_after(std::chrono::milliseconds {1});
    co_await t.async_wait(taar::use_awaitable);
    co_return x * 2;
}

std::string greet(std::string name, std::optional<std::string> title)
{
    return (title ? *title + " " : std::string {}) + name;
}

int with_opt(int a, int b) { return a * 10 + b; }

// Builds the standard endpoint used by most tests.
auto make_endpoint()
{
    auto rpc = std::make_unique<jsonrpc::endpoint<calc_session>>();
    rpc->method("add", "Add two integers", &add, rpc_arg("a", "first addend"), rpc_arg("b", "second addend"));
    rpc->method("sub", &sub, rpc_arg("a"), rpc_arg("b"));
    rpc->method("divide", &divide, rpc_arg("a"), rpc_arg("b"));
    rpc->method("history", &history);
    rpc->method("ping", &ping);
    rpc->method("double", &async_double, rpc_arg("x"));
    rpc->method("greet", &greet, rpc_arg("name"), rpc_arg("title"));
    rpc->method("with_opt", &with_opt, rpc_arg("a"), with_default(rpc_arg("b"), 7));
    return rpc;
}

template <typename Endpoint>
http::response<http::string_body> post(
    Endpoint& rpc,
    std::string body,
    std::shared_ptr<calc_session> session = std::make_shared<calc_session>())
{
    http::request<http::string_body> req {http::verb::post, "/rpc", 11};
    req.set(http::field::content_type, "application/json");
    req.body() = std::move(body);
    req.prepare_payload();

    contexts<calc_session> ctxs {std::move(session)};
    taar::matcher::context mctx;
    auto handler = rpc.handler();

    net::io_context io;
    std::optional<http::response<http::string_body>> out;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        out = co_await handler(ctxs, req, mctx);
    }, net::detached);
    io.run();
    return *out;
}

json::value body_json(http::response<http::string_body> const& res)
{
    return json::parse(res.body());
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_basic_call)
{
    auto rpc = make_endpoint();
    auto res = post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"sub","params":{"a":5,"b":2}})");
    BOOST_TEST(res.result() == http::status::ok);
    BOOST_TEST(res[http::field::content_type] == "application/json");
    auto v = body_json(res).as_object();
    BOOST_TEST(v.at("jsonrpc").as_string() == "2.0");
    BOOST_TEST(v.at("id").as_int64() == 1);
    BOOST_TEST(v.at("result").as_int64() == 3);
    BOOST_TEST(!v.contains("error"));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_context_injected)
{
    auto rpc = make_endpoint();
    auto session = std::make_shared<calc_session>();
    auto r1 = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"add","params":{"a":2,"b":3}})", session));
    BOOST_TEST(r1.at("result").as_int64() == 5);
    BOOST_TEST(session->last == 5);
    auto r2 = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":2,"method":"history"})", session));
    BOOST_TEST(r2.at("result").as_array().size() == 1u);
    BOOST_TEST(r2.at("result").as_array()[0].as_string() == "2+3");
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_id_echoed_verbatim)
{
    auto rpc = make_endpoint();
    for (std::string id : {"\"abc\"", "7", "null", "9007199254740993", "1.5"})
    {
        auto res = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":)" + id + R"(,"method":"ping"})"));
        BOOST_TEST((res.at("id") == json::parse(id)));
    }
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_notification_runs_and_returns_202)
{
    auto rpc = make_endpoint();
    auto session = std::make_shared<calc_session>();
    auto res = post(*rpc, R"({"jsonrpc":"2.0","method":"add","params":{"a":1,"b":1}})", session);
    BOOST_TEST(res.result() == http::status::accepted);
    BOOST_TEST(res.body().empty());
    BOOST_TEST(session->last == 2);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_void_result_is_null)
{
    auto rpc = make_endpoint();
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"ping"})"));
    BOOST_TEST(v.at("result").is_null());
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_async_method)
{
    auto rpc = make_endpoint();
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"double","params":{"x":21}})"));
    BOOST_TEST(v.at("result").as_int64() == 42);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_optional_and_default_params)
{
    auto rpc = make_endpoint();
    auto g1 = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"greet","params":{"name":"Ada"}})"));
    BOOST_TEST(g1.at("result").as_string() == "Ada");
    auto g2 = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"greet","params":{"name":"Ada","title":"Dr"}})"));
    BOOST_TEST(g2.at("result").as_string() == "Dr Ada");
    auto d1 = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"with_opt","params":{"a":1}})"));
    BOOST_TEST(d1.at("result").as_int64() == 17);
    auto d2 = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"with_opt","params":{"a":1,"b":2}})"));
    BOOST_TEST(d2.at("result").as_int64() == 12);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_extra_params_ignored)
{
    auto rpc = make_endpoint();
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"sub","params":{"a":5,"b":2,"c":99}})"));
    BOOST_TEST(v.at("result").as_int64() == 3);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_missing_param_is_invalid_params)
{
    auto rpc = make_endpoint();
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":4,"method":"sub","params":{"a":5}})"));
    BOOST_TEST(v.at("id").as_int64() == 4);
    BOOST_TEST(v.at("error").at("code").as_int64() == -32602);
    BOOST_TEST(json::serialize(v.at("error").at("data")).find("b") != std::string::npos);
    BOOST_TEST(!v.as_object().contains("result"));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_wrong_type_param_is_invalid_params)
{
    auto rpc = make_endpoint();
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"sub","params":{"a":"x","b":2}})"));
    BOOST_TEST(v.at("error").at("code").as_int64() == -32602);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_positional_params_bind_in_order_and_scalars_are_rejected)
{
    auto rpc = make_endpoint();
    auto arr = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"sub","params":[5,2]})"));
    BOOST_TEST(arr.at("result").as_int64() == 3);
    auto extra = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":2,"method":"sub","params":[5,2,99]})"));
    BOOST_TEST(extra.at("result").as_int64() == 3);                     // extra elements ignored
    auto missing = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":3,"method":"sub","params":[5]})"));
    BOOST_TEST(missing.at("error").at("code").as_int64() == -32602);    // trailing parameter missing
    auto typed = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":4,"method":"sub","params":["x",2]})"));
    BOOST_TEST(typed.at("error").at("code").as_int64() == -32602);
    auto opt = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":5,"method":"with_opt","params":[4]})"));
    BOOST_TEST(opt.at("result").as_int64() == 47);                      // with_opt(4, b = 7)
    auto scalar = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":6,"method":"sub","params":5})"));
    BOOST_TEST(scalar.at("error").at("code").as_int64() == -32602);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_positional_params_skip_http_providers)
{
    jsonrpc::endpoint<calc_session> rpc;
    rpc.method("tagged", [](std::string token, int a, std::optional<std::string> trace, int b)
    {
        return token + ":" + std::to_string(a) + ":" + trace.value_or("-") + ":" + std::to_string(b);
    }, taar::handler::header_arg("X-Token"), rpc_arg("a"), taar::handler::header_arg("X-Trace"), rpc_arg("b"));

    http::request<http::string_body> req {http::verb::post, "/rpc", 11};
    req.set(http::field::content_type, "application/json");
    req.set("X-Token", "tok");
    req.body() = R"({"jsonrpc":"2.0","id":1,"method":"tagged","params":[10,20]})";
    req.prepare_payload();
    contexts<calc_session> ctxs {std::make_shared<calc_session>()};
    taar::matcher::context mctx;
    auto handler = rpc.handler();
    net::io_context io;
    std::optional<http::response<http::string_body>> out;
    net::co_spawn(io, [&]() -> taar::awaitable<void> { out = co_await handler(ctxs, req, mctx); }, net::detached);
    io.run();
    BOOST_TEST(body_json(*out).at("result").as_string() == "tok:10:-:20");   // a=params[0], b=params[1]
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_missing_params_for_no_arg_method_ok)
{
    auto rpc = make_endpoint();
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"history"})"));
    BOOST_TEST(v.at("result").is_array());
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_unknown_method_and_case_sensitivity)
{
    auto rpc = make_endpoint();
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":9,"method":"nope"})"));
    BOOST_TEST(v.at("id").as_int64() == 9);
    BOOST_TEST(v.at("error").at("code").as_int64() == -32601);
    auto c = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":9,"method":"Sub","params":{"a":1,"b":1}})"));
    BOOST_TEST(c.at("error").at("code").as_int64() == -32601);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_invalid_json_is_parse_error)
{
    auto rpc = make_endpoint();
    auto res = post(*rpc, "not json");
    BOOST_TEST(res.result() == http::status::ok);
    auto v = body_json(res);
    BOOST_TEST(v.at("id").is_null());
    BOOST_TEST(v.at("error").at("code").as_int64() == -32700);

    auto empty = body_json(post(*rpc, ""));
    BOOST_TEST(empty.at("error").at("code").as_int64() == -32700);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_invalid_request_shapes)
{
    auto rpc = make_endpoint();
    for (std::string body : {
        R"({"jsonrpc":"1.0","id":1,"method":"ping"})",
        R"({"id":1,"method":"ping"})",
        R"({"jsonrpc":2.0,"id":1,"method":"ping"})",
        R"({"jsonrpc":"2.0","id":1,"method":42})",
        R"({"jsonrpc":"2.0","id":1})",
        R"("just a string")",
        R"(42)"})
    {
        auto v = body_json(post(*rpc, body));
        BOOST_TEST(v.at("error").at("code").as_int64() == -32600);
    }
    // The id is echoed when it can be recovered from an invalid request.
    auto v = body_json(post(*rpc, R"({"jsonrpc":"1.0","id":77,"method":"ping"})"));
    BOOST_TEST(v.at("id").as_int64() == 77);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_batch_mixed)
{
    auto rpc = make_endpoint();
    auto session = std::make_shared<calc_session>();
    auto res = post(*rpc, R"([
        {"jsonrpc":"2.0","id":1,"method":"add","params":{"a":2,"b":3}},
        {"jsonrpc":"2.0","method":"add","params":{"a":10,"b":10}},
        {"jsonrpc":"2.0","id":2,"method":"nope"},
        {"jsonrpc":"2.0","id":3,"method":"divide","params":{"a":1,"b":0}},
        1,
        {"foo":"bar"},
        {"jsonrpc":"2.0","id":4,"method":"history"}
    ])", session);

    BOOST_TEST(res.result() == http::status::ok);
    auto arr = body_json(res).as_array();
    BOOST_REQUIRE(arr.size() == 6u);

    auto find_id = [&](json::value const& id) -> json::object const*
    {
        for (auto const& r : arr)
        {
            if (r.as_object().at("id") == id) return &r.as_object();
        }
        return nullptr;
    };

    BOOST_REQUIRE(find_id(1) != nullptr);
    BOOST_TEST(find_id(1)->at("result").as_int64() == 5);
    BOOST_REQUIRE(find_id(2) != nullptr);
    BOOST_TEST(find_id(2)->at("error").at("code").as_int64() == -32601);
    BOOST_REQUIRE(find_id(3) != nullptr);
    BOOST_TEST(find_id(3)->at("error").at("code").as_int64() == -32603);
    BOOST_REQUIRE(find_id(4) != nullptr);
    BOOST_TEST(find_id(4)->at("result").as_array().size() == 2u); // both adds ran
    int null_errors = 0;
    for (auto const& r : arr)
    {
        if (r.as_object().at("id").is_null())
        {
            ++null_errors;
            BOOST_TEST(r.as_object().at("error").at("code").as_int64() == -32600);
        }
    }
    BOOST_TEST(null_errors == 2);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_batch_edge_cases)
{
    auto rpc = make_endpoint();
    auto empty = post(*rpc, "[]");
    BOOST_TEST(empty.result() == http::status::ok);
    BOOST_TEST(body_json(empty).is_object());
    BOOST_TEST(body_json(empty).at("error").at("code").as_int64() == -32600);

    auto notifs = post(*rpc, R"([{"jsonrpc":"2.0","method":"ping"},{"jsonrpc":"2.0","method":"ping"}])");
    BOOST_TEST(notifs.result() == http::status::accepted);
    BOOST_TEST(notifs.body().empty());

    auto one = post(*rpc, R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])");
    BOOST_TEST(body_json(one).is_array());
    BOOST_TEST(body_json(one).as_array().size() == 1u);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_default_error_mapper)
{
    auto rpc = make_endpoint();
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"divide","params":{"a":1,"b":0}})"));
    BOOST_TEST(v.at("error").at("code").as_int64() == -32603);
    BOOST_TEST(v.at("error").at("message").as_string() == "division by zero");
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_custom_error_mapper)
{
    auto rpc = make_endpoint();
    int mapper_calls = 0;
    rpc->set_error_mapper([&](std::exception_ptr e) -> jsonrpc::error_info
    {
        ++mapper_calls;
        try { std::rethrow_exception(e); }
        catch (division_by_zero const& x) { return {-32001, x.what(), json::value {{"retry", false}}}; }
        catch (...) { return jsonrpc::default_error_mapper(e); }
    });

    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"divide","params":{"a":1,"b":0}})"));
    BOOST_TEST(v.at("error").at("code").as_int64() == -32001);
    BOOST_TEST(v.at("error").at("message").as_string() == "division by zero");
    BOOST_TEST(v.at("error").at("data").at("retry").as_bool() == false);

    // Still falls through to the default for other exceptions.
    auto w = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"sub","params":{"a":"x","b":1}})"));
    BOOST_TEST(w.at("error").at("code").as_int64() == -32602);

    // Notification that fails: nothing returned, mapper still called.
    auto n = post(*rpc, R"({"jsonrpc":"2.0","method":"divide","params":{"a":1,"b":0}})");
    BOOST_TEST(n.result() == http::status::accepted);
    BOOST_TEST(mapper_calls == 3);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_mapper_that_throws_falls_back)
{
    auto rpc = make_endpoint();
    rpc->set_error_mapper([](std::exception_ptr) -> jsonrpc::error_info { throw std::runtime_error {"mapper broke"}; });
    auto v = body_json(post(*rpc, R"({"jsonrpc":"2.0","id":1,"method":"divide","params":{"a":1,"b":0}})"));
    BOOST_TEST(v.at("error").at("code").as_int64() == -32603);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_jsonrpc_category_error_passes_through)
{
    jsonrpc::endpoint<> rpc;
    rpc.method("fail", [] { throw boost::system::system_error {jsonrpc::error::invalid_params, "custom"}; });
    http::request<http::string_body> req {http::verb::post, "/rpc", 11};
    req.body() = R"({"jsonrpc":"2.0","id":1,"method":"fail"})";
    req.prepare_payload();
    auto handler = rpc.handler();
    net::io_context io;
    std::optional<http::response<http::string_body>> out;
    taar::matcher::context mctx;
    net::co_spawn(io, [&]() -> taar::awaitable<void> { out = co_await handler(contexts<>{}, req, mctx); }, net::detached);
    io.run();
    BOOST_TEST(body_json(*out).at("error").at("code").as_int64() == -32602);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_duplicate_method_throws)
{
    jsonrpc::endpoint<> rpc;
    rpc.method("a", &sub, rpc_arg("a"), rpc_arg("b"));
    BOOST_CHECK_THROW(rpc.method("a", &sub, rpc_arg("a"), rpc_arg("b")), std::logic_error);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_method_info_for_generators)
{
    auto rpc = make_endpoint();
    auto methods = rpc->methods();
    BOOST_REQUIRE(methods.size() == 8u);
    BOOST_TEST(methods[0].name == "add");
    BOOST_TEST(methods[0].description == "Add two integers");
    BOOST_REQUIRE(methods[0].params.size() == 2u); // context param not listed
    BOOST_TEST(methods[0].params[0].name == "a");
    BOOST_TEST(methods[0].params[0].description == "first addend");
    BOOST_TEST(methods[0].params[0].json_type == "integer");
    BOOST_TEST(methods[0].params[0].required);

    auto const& greet_info = methods[6];
    BOOST_TEST(greet_info.name == "greet");
    BOOST_TEST(greet_info.params[0].json_type == "string");
    BOOST_TEST(greet_info.params[1].json_type == "string");
    BOOST_TEST(!greet_info.params[1].required); // optional<string>

    auto const& opt_info = methods[7];
    BOOST_TEST(!opt_info.params[1].required); // with_default
    BOOST_TEST(opt_info.params[1].json_type == "integer");
    BOOST_TEST(rpc->has_method("ping"));
    BOOST_TEST(!rpc->has_method("pong"));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_endpoint_http_providers_and_schemas)
{
    jsonrpc::endpoint<calc_session> rpc;
    rpc.method("whoami", "Caller", [](std::string token, std::optional<std::string> trace)
    {
        return token + "/" + trace.value_or("none");
    }, taar::handler::header_arg("X-Token", "API token"), taar::handler::header_arg("X-Trace"));
    rpc.method("sum", [](std::vector<int> values)
    {
        int s = 0;
        for (int v : values) s += v;
        return s;
    }, rpc_arg("values", "Numbers to add"));

    auto send = [&](std::string body, std::optional<std::string> token)
    {
        http::request<http::string_body> req {http::verb::post, "/rpc", 11};
        req.set(http::field::content_type, "application/json");
        if (token) req.set("X-Token", *token);
        req.body() = std::move(body);
        req.prepare_payload();
        contexts<calc_session> ctxs {std::make_shared<calc_session>()};
        taar::matcher::context mctx;
        auto handler = rpc.handler();
        net::io_context io;
        std::optional<http::response<http::string_body>> out;
        net::co_spawn(io, [&]() -> taar::awaitable<void> { out = co_await handler(ctxs, req, mctx); }, net::detached);
        io.run();
        return body_json(*out);
    };

    auto ok = send(R"({"jsonrpc":"2.0","id":1,"method":"whoami"})", "abc");
    BOOST_TEST(ok.at("result").as_string() == "abc/none");
    auto missing = send(R"({"jsonrpc":"2.0","id":2,"method":"whoami"})", std::nullopt);
    BOOST_TEST(missing.at("error").at("code").as_int64() == -32602);
    auto sum = send(R"({"jsonrpc":"2.0","id":3,"method":"sum","params":{"values":[1,2,3]}})", std::nullopt);
    BOOST_TEST(sum.at("result").as_int64() == 6);

    auto infos = rpc.methods();
    BOOST_REQUIRE(infos.size() == 2u);
    BOOST_TEST(infos[0].params[0].name == "X-Token");
    BOOST_TEST(infos[0].params[0].description == "API token");
    BOOST_TEST(infos[0].params[0].required);
    BOOST_TEST(!infos[0].params[1].required);
    BOOST_TEST(infos[1].params[0].schema.at("type").as_string() == "array");
    BOOST_TEST(infos[1].params[0].schema.at("items").at("type").as_string() == "integer");
}

} // namespace
