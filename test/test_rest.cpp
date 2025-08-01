//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "to_response.h"
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include "boost/taar/handler/rest_arg.hpp"
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/test/unit_test.hpp>
#include <string>

namespace {

BOOST_AUTO_TEST_CASE(test_rest_arg_provider_request_type)
{
    namespace http = boost::beast::http;
    namespace taar= boost::taar;
    using taar::handler::detail::arg_provider_request;
    using taar::handler::detail::common_requests_type_t;
    using taar::handler::detail::compatible_arg_providers;
    using taar::matcher::context;
    using req_header = http::request_header<>;
    using string_req = http::request<http::string_body>;
    using empty_req = http::request<http::empty_body>;
    using file_req = http::request<http::file_body>;

    using provider_header = void(const req_header&, const context&);
    using provider_string = void(const string_req&, const context&);
    using provider_empty = void(const empty_req&, const context&);
    using provider_file = void(const file_req&, const context&);

    static_assert(std::is_same_v<arg_provider_request<provider_header>, req_header>, "Failed!");
    static_assert(std::is_same_v<arg_provider_request<provider_string>, string_req>, "Failed!");
    static_assert(std::is_same_v<arg_provider_request<provider_empty>, empty_req>, "Failed!");
    static_assert(std::is_same_v<arg_provider_request<provider_file>, file_req>, "Failed!");

    static_assert(std::is_same_v<common_requests_type_t<>, empty_req>, "Failed!");
    static_assert(std::is_same_v<common_requests_type_t<provider_header>, empty_req>, "Failed!");
    static_assert(std::is_same_v<common_requests_type_t<provider_string>, string_req>, "Failed!");
    static_assert(std::is_same_v<common_requests_type_t<provider_empty>, empty_req>, "Failed!");
    static_assert(std::is_same_v<common_requests_type_t<provider_file>, file_req>, "Failed!");

    static_assert(compatible_arg_providers<provider_header, provider_string>, "Failed!");
    static_assert(compatible_arg_providers<provider_header, provider_empty>, "Failed!");
    static_assert(compatible_arg_providers<provider_header, provider_file>, "Failed!");
    static_assert(compatible_arg_providers<provider_header, provider_header>, "Failed!");
    static_assert(!compatible_arg_providers<provider_string, provider_file>, "Failed!");
    static_assert(compatible_arg_providers<provider_header, provider_header, provider_header>, "Failed!");
    static_assert(!compatible_arg_providers<provider_header, provider_string, provider_file>, "Failed!");
    static_assert(!compatible_arg_providers<provider_empty, provider_string, provider_file>, "Failed!");

    static_assert(std::is_same_v<common_requests_type_t<provider_string, provider_header>, string_req>, "Failed!");
    static_assert(std::is_same_v<common_requests_type_t<provider_empty, provider_header>, empty_req>, "Failed!");
    static_assert(std::is_same_v<common_requests_type_t<provider_file, provider_header>, file_req>, "Failed!");
    static_assert(std::is_same_v<common_requests_type_t<provider_header, provider_header>, empty_req>, "Failed!");
    static_assert(std::is_same_v<common_requests_type_t<provider_header, provider_header, provider_string>, string_req>, "Failed!");
}

void voidfn(int i, int j)
{
}

BOOST_AUTO_TEST_CASE(test_rest_void)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::path_arg;

    http::request<http::empty_body> req;
    context ctx;
    ctx.path_args = {{"a", "13"}, {"b", "42"}};

    auto rh1 = taar::handler::rest(
        &voidfn,
        path_arg("a"),
        path_arg("b"));
    auto resp1 = to_response(rh1(req, ctx));
    BOOST_TEST(resp1.count(http::field::content_type) == 0);

    auto rh2 = taar::handler::rest(
        voidfn,
        path_arg("a"),
        path_arg("b"));
    auto resp2 = to_response(rh2(req, ctx));
    BOOST_TEST(resp2.count(http::field::content_type) == 0);
}

struct const_obj_handler
{
    std::string operator()() const { return "blah1"; }
};

struct obj_handler
{
    std::string operator()() { return "blah2"; }
};

BOOST_AUTO_TEST_CASE(test_rest)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;

    http::request<http::empty_body> req;
    context ctx;

    auto rh1 = taar::handler::rest(const_obj_handler());
    auto resp1 = to_response<http::string_body>(rh1(req, ctx));
    BOOST_TEST(resp1.body() == "blah1");

    auto rh2 = taar::handler::rest(obj_handler());
    auto resp2 = to_response<http::string_body>(rh2(req, ctx));
    BOOST_TEST(resp2.body() == "blah2");
}

auto sum(int i, int j)
{
    return std::to_string(i + j);
}

BOOST_AUTO_TEST_CASE(test_rest_sum_target)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::path_arg;

    http::request<http::empty_body> req;
    context ctx;
    ctx.path_args = {{"a", "13"}, {"b", "42"}};

    auto rh_sum = taar::handler::rest(
        sum,
        path_arg("a"),
        path_arg("b"));
    auto resp_sum = to_response<http::string_body>(rh_sum(req, ctx));
    BOOST_TEST(resp_sum.body() == "55");
    BOOST_TEST(resp_sum[http::field::content_type] == "text/plain");
}

BOOST_AUTO_TEST_CASE(test_rest_sum_query)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::query_arg;

    http::request<http::empty_body> req{http::verb::get, "/?a=13&b=42", 10};
    context ctx;

    auto rh_sum = taar::handler::rest(
        sum,
        query_arg("a"),
        query_arg("b"));
    auto resp_sum = to_response<http::string_body>(rh_sum(req, ctx));
    BOOST_TEST(resp_sum.body() == "55");
    BOOST_TEST(resp_sum[http::field::content_type] == "text/plain");
}

BOOST_AUTO_TEST_CASE(test_rest_sum_header)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::header_arg;

    http::request<http::empty_body> req;
    context ctx;
    req.insert("a", "13");
    req.insert("b", "42");

    auto rh_sum = taar::handler::rest(
        sum,
        header_arg("a"),
        header_arg("b"));
    auto resp_sum = to_response<http::string_body>(rh_sum(req, ctx));
    BOOST_TEST(resp_sum.body() == "55");
    BOOST_TEST(resp_sum[http::field::content_type] == "text/plain");
}

auto stock_string()
{
    return "This is a stock string.";
}

BOOST_AUTO_TEST_CASE(test_rest_stock_string)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::header_arg;

    http::request<http::empty_body> req;
    context ctx;

    auto rh = taar::handler::rest(stock_string);
    auto resp = to_response<http::string_body>(rh(req, ctx));
    BOOST_TEST(resp.body() == stock_string());
    BOOST_TEST(resp[http::field::content_type] == "text/plain");
}

auto to_json(std::string value)
{
    return boost::json::value {{"value", value}};
}

BOOST_AUTO_TEST_CASE(test_rest_string_to_json_response)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::header_arg;

    http::request<http::empty_body> req;
    context ctx;
    req.insert("value", "Hello");

    auto rh = taar::handler::rest(to_json, header_arg("value"));
    auto resp = to_response<http::string_body>(rh(req, ctx));
    BOOST_TEST(resp.body() == R"({"value":"Hello"})");
    BOOST_TEST(resp[http::field::content_type] == "application/json");
}

struct jsonable
{
    int i;
    std::string s;
};

jsonable tag_invoke(const boost::json::value_to_tag<jsonable>&, const boost::json::value& jv)
{
    auto const& obj = jv.as_object();
    return jsonable {
        .i = boost::json::value_to<int>(obj.at("i")),
        .s = boost::json::value_to<std::string>(obj.at("s")),
    };
}

void tag_invoke(
    const boost::json::value_from_tag&,
    boost::json::value& jv,
    const jsonable& obj)
{
    // Store the IP address as a 4-element array of octets
    jv = { {{"i", obj.i}, {"s", obj.s}} };
}

std::string accepts_jsonable(const jsonable& j)
{
    return j.s + " = " + std::to_string(j.i);
}

BOOST_AUTO_TEST_CASE(test_rest_invoke_with_jsonable)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::json_body_arg;

    http::request<http::string_body> req;
    context ctx;
    req.body() = R"({"i":13, "s":"Hello"})";
    req.insert(http::field::content_type, "application/json");
    req.prepare_payload();

    auto rh = taar::handler::rest(accepts_jsonable, json_body_arg());
    auto resp = to_response<http::string_body>(rh(req, ctx));
    BOOST_TEST(resp.body() == "Hello = 13");
    BOOST_TEST(resp[http::field::content_type] == "text/plain");
}

struct concat_handler
{
    std::string concat(const std::string& a, std::string_view b)
    {
        return a + std::string{b};
    }

    std::string const_concat(const std::string& a, std::string_view b) const
    {
        return std::string{b} + a;
    }
};

//BOOST_AUTO_TEST_CASE(test_rest_explicit_signature)
//{
//    namespace http = boost::beast::http;
//    namespace taar = boost::taar;
//    using taar::matcher::context;
//    using taar::handler::path_arg;
//
//    http::request<http::string_body> req;
//    context ctx;
//    ctx.path_args = {{"a", "13"}, {"b", "42"}};
//
//    concat_handler bh;
//    auto rh_sum = taar::handler::rest(
//        std::function{std::bind(&concat_handler::concat, &bh)},
//        path_arg("a"),
//        path_arg("b"));
//    auto resp = to_response<http::string_body>(rh_sum(req, ctx));
//    BOOST_TEST(resp.body() == "1342");
//    BOOST_TEST(resp[http::field::content_type] == "text/plain");
//}

BOOST_AUTO_TEST_CASE(test_rest_mfn)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::header_arg;

    http::request<http::empty_body> req;
    context ctx;
    req.insert("a", "13");
    req.insert("b", "42");

    concat_handler bh;

    auto rh = taar::handler::rest(
        &concat_handler::concat,
        &bh,
        header_arg("a"),
        header_arg("b"));
    auto resp = to_response<http::string_body>(rh(req, ctx));
    BOOST_TEST(resp.body() == "1342");
    BOOST_TEST(resp[http::field::content_type] == "text/plain");

    auto rhc = taar::handler::rest(
        &concat_handler::const_concat,
        bh,
        header_arg("a"),
        header_arg("b"));
    auto respc = to_response<http::string_body>(rhc(req, ctx));
    BOOST_TEST(respc.body() == "4213");
    BOOST_TEST(respc[http::field::content_type] == "text/plain");
}

BOOST_AUTO_TEST_CASE(test_rest_string_body)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::string_body_arg;

    http::request<http::string_body> req;
    req.body() = "Hello";
    req.insert(http::field::content_type, "text/plain");
    req.prepare_payload();
    context ctx;

    auto l = [](const std::string& str) { return str + " world!"; };

    auto rh = taar::handler::rest(
        l,
        string_body_arg());
    auto resp = to_response<http::string_body>(rh(req, ctx));
    BOOST_TEST(resp.body() == "Hello world!");
    BOOST_TEST(resp[http::field::content_type] == "text/plain");
}

BOOST_AUTO_TEST_CASE(test_rest_awaitable_handler)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::string_body_arg;
    using taar::has_response_from;
    using taar::response_from_t;
    using taar::awaitable;
    using taar::is_awaitable;

    http::request<http::string_body> req;
    context ctx;

    auto l = [](const std::string& str) -> awaitable<std::string> { co_return str + " world!"; };
    auto rh = taar::handler::rest(l, string_body_arg());

    static_assert(is_awaitable<decltype(rh(req, ctx))>, "Failed!");
    static_assert(has_response_from<decltype(rh(req, ctx))::value_type>, "Failed!");
    static_assert(
        std::is_same_v<
            response_from_t<std::string>,
            response_from_t<decltype(rh(req, ctx))::value_type>>, "Failed!");
}

BOOST_AUTO_TEST_CASE(test_rest_file_response)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::has_response_from;
    using taar::response_from_t;
    using taar::awaitable;
    using taar::is_awaitable;
    using resp_t = http::response<http::file_body>;

    http::request<http::empty_body> req;
    context ctx;

    auto l = []() -> awaitable<resp_t> {
        resp_t response{http::status::ok, 11};
        co_return response;
    };
    auto rh = taar::handler::rest(l);

    static_assert(is_awaitable<decltype(rh(req, ctx))>, "Failed!");
    static_assert(has_response_from<decltype(rh(req, ctx))::value_type>, "Failed!");
    static_assert(
        std::is_same_v<
            response_from_t<resp_t>,
            response_from_t<decltype(rh(req, ctx))::value_type>>, "Failed!");
}

} // namespace
