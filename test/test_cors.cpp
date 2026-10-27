//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include "e2e.hpp"
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/cors.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;

http::request<http::empty_body> options_request(std::string origin)
{
    http::request<http::empty_body> req {http::verb::options, "/api/x", 11};
    req.set(http::field::origin, origin);
    req.set(http::field::access_control_request_method, "PUT");
    req.set(http::field::access_control_request_headers, "content-type");
    return req;
}

BOOST_AUTO_TEST_CASE(test_cors_apply_default_policy_allows_any_origin)
{
    taar::cors policy;
    http::request<http::empty_body> req {http::verb::get, "/", 11};
    req.set(http::field::origin, "http://evil.example");
    http::response<http::string_body> res {http::status::ok, 11};
    policy.apply(req, res);
    BOOST_TEST(res[http::field::access_control_allow_origin] == "*");
    BOOST_TEST(res[http::field::access_control_expose_headers] == "Mcp-Session-Id");
    BOOST_TEST((res.find(http::field::vary) == res.end()));
    BOOST_TEST((res.find(http::field::access_control_allow_credentials) == res.end()));

    http::request<http::empty_body> no_origin {http::verb::get, "/", 11};
    http::response<http::string_body> plain {http::status::ok, 11};
    policy.apply(no_origin, plain);
    BOOST_TEST((plain.find(http::field::access_control_allow_origin) == plain.end()));
}

BOOST_AUTO_TEST_CASE(test_cors_apply_allow_list_with_credentials_echoes_origin)
{
    taar::cors policy;
    policy.allowed_origins = {"http://localhost:3000"};
    policy.allow_credentials = true;

    http::request<http::empty_body> ok {http::verb::get, "/", 11};
    ok.set(http::field::origin, "http://localhost:3000");
    http::response<http::string_body> res {http::status::ok, 11};
    policy.apply(ok, res);
    BOOST_TEST(res[http::field::access_control_allow_origin] == "http://localhost:3000");
    BOOST_TEST(res[http::field::vary] == "Origin");
    BOOST_TEST(res[http::field::access_control_allow_credentials] == "true");

    http::request<http::empty_body> bad {http::verb::get, "/", 11};
    bad.set(http::field::origin, "http://evil.example");
    http::response<http::string_body> denied {http::status::ok, 11};
    policy.apply(bad, denied);
    BOOST_TEST((denied.find(http::field::access_control_allow_origin) == denied.end()));
}

BOOST_AUTO_TEST_CASE(test_cors_preflight_handler)
{
    taar::cors policy;
    policy.allowed_origins = {"http://localhost:3000"};
    policy.max_age = std::chrono::seconds {60};
    taar::matcher::context ctx;
    auto handler = policy.preflight_handler();

    auto ok = handler(options_request("http://localhost:3000"), ctx);
    BOOST_TEST(ok.result() == http::status::no_content);
    BOOST_TEST(ok[http::field::access_control_allow_origin] == "http://localhost:3000");
    BOOST_TEST(ok[http::field::access_control_allow_methods] == "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    BOOST_TEST(std::string {ok[http::field::access_control_allow_headers]}.find("Mcp-Session-Id") != std::string::npos);
    BOOST_TEST(ok[http::field::access_control_max_age] == "60");

    auto denied = handler(options_request("http://evil.example"), ctx);
    BOOST_TEST(denied.result() == http::status::forbidden);
    BOOST_TEST((denied.find(http::field::access_control_allow_origin) == denied.end()));
}

BOOST_AUTO_TEST_CASE(test_e2e_cors_headers_on_every_response_kind)
{
    net::io_context io;
    taar::session::http session;
    taar::cors policy;
    session.register_request_handler(method == http::verb::options, policy.preflight_handler());
    session.set_response_filter(policy.response_filter());
    session.set_soft_error_handler([](taar::failure const&)
    {
        http::response<http::string_body> res {http::status::unprocessable_entity, 11};
        res.body() = "bad";
        res.prepare_payload();
        return res;
    });
    session.register_request_handler(method == http::verb::get && target == "/hello", rest([] { return "hi"; }));
    session.register_request_handler(method == http::verb::get && target == "/boom", rest([]() -> int { throw std::runtime_error {"x"}; }));
    session.register_request_handler(method == http::verb::get && target == "/stream",
        rest([]() -> taar::chunked_response<std::string> { co_yield std::string {"a"}; co_yield std::string {"b"}; }));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto with_origin = [](http::verb v, std::string t)
        {
            auto r = e2e::make_request(v, std::move(t));
            r.set(http::field::origin, "http://app.example");
            if (v == http::verb::options) r.set(http::field::access_control_request_method, "GET");
            return r;
        };
        auto res = co_await e2e::send_all(port,
            with_origin(http::verb::options, "/hello"),
            with_origin(http::verb::get, "/hello"),
            with_origin(http::verb::get, "/boom"),
            with_origin(http::verb::get, "/stream"),
            with_origin(http::verb::get, "/missing"),
            e2e::make_request(http::verb::get, "/hello"));
        BOOST_TEST(res[0].result() == http::status::no_content);
        BOOST_TEST(res[0][http::field::access_control_allow_methods] != "");
        for (int i = 0; i < 5; ++i)
        {
            BOOST_TEST(res[i][http::field::access_control_allow_origin] == "*");
        }
        BOOST_TEST(res[1].body() == "hi");
        BOOST_TEST(res[2].result() == http::status::unprocessable_entity);
        BOOST_TEST(res[3].body() == "ab");
        BOOST_TEST(res[4].result() == http::status::not_found);
        BOOST_TEST((res[5].find(http::field::access_control_allow_origin) == res[5].end()));   // no Origin, no CORS
    });
}

BOOST_AUTO_TEST_CASE(test_cors_credentials_never_reflect_a_wildcard)
{
    taar::cors policy;
    policy.allow_credentials = true;                // allowed_origins still {"*"}
    http::request<http::empty_body> req {http::verb::get, "/", 11};
    req.set(http::field::origin, "https://evil.example");
    http::response<http::string_body> res;
    policy.apply(req, res);
    BOOST_TEST(res.count(http::field::access_control_allow_origin) == 0u);
    BOOST_TEST(res.count(http::field::access_control_allow_credentials) == 0u);

    policy.allowed_origins = {"https://app.example"};
    http::response<http::string_body> denied;
    policy.apply(req, denied);
    BOOST_TEST(denied.count(http::field::access_control_allow_origin) == 0u);
    req.set(http::field::origin, "https://app.example");
    http::response<http::string_body> allowed;
    policy.apply(req, allowed);
    BOOST_TEST(allowed[http::field::access_control_allow_origin] == "https://app.example");
    BOOST_TEST(allowed[http::field::access_control_allow_credentials] == "true");
}

BOOST_AUTO_TEST_CASE(test_cors_vary_is_appended_not_replaced)
{
    taar::cors policy;
    policy.allowed_origins = {"https://app.example"};
    http::request<http::empty_body> req {http::verb::get, "/", 11};
    req.set(http::field::origin, "https://app.example");
    http::response<http::string_body> res;
    res.set(http::field::vary, "Accept-Encoding");
    policy.apply(req, res);
    std::vector<std::string> vary;
    auto range = res.equal_range(http::field::vary);
    for (auto it = range.first; it != range.second; ++it) vary.emplace_back(it->value());
    BOOST_TEST((vary == std::vector<std::string> {"Accept-Encoding", "Origin"}));
    policy.apply(req, res);                                     // idempotent
    BOOST_TEST(res.count(http::field::vary) == 2u);
}

} // namespace
