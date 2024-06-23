//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/web/session/http.hpp>
#include <boost/web/handler/rest.hpp>
#include "boost/web/handler/rest_arg.hpp"
#include <boost/web/matcher/method.hpp>
#include <boost/web/matcher/target.hpp>
#include <boost/web/matcher/context.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/test/unit_test.hpp>
#include <string>

namespace {

// Function to convert message_generator to http::response
template<class BodyType>
boost::beast::http::response<BodyType> to_response(
    boost::beast::http::message_generator& generator)
{
    boost::beast::error_code ec;
    boost::beast::http::response_parser<BodyType> parser;

    while (!parser.is_done())
    {
        auto buffers = generator.prepare(ec);
        if(ec)
        {
            throw boost::system::system_error{ec};
        }

        auto n = parser.put(buffers, ec);
        if(ec && ec != boost::beast::http::error::need_buffer)
        {
            throw boost::system::system_error{ec};
        }

        generator.consume(n);
    }

    return parser.release();
}

BOOST_AUTO_TEST_CASE(test_rest_arg_provider_request_type)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::handler::detail::arg_provider_request;
    using web::handler::detail::common_requests_type_t;
    using web::handler::detail::compatible_arg_providers;
    using web::matcher::context;
    using req_header = http::request_header<>;
    using string_req = http::request<http::string_body>;
    using empty_req = http::request<http::empty_body>;
    using file_req = http::request<http::file_body>;

    using provider_header = void(const req_header&, const context&);
    using provider_string = void(const string_req&, const context&);
    using provider_empty = void(const empty_req&, const context&);
    using provider_file = void(const file_req&, const context&);

    static_assert(std::is_same_v<arg_provider_request<provider_header>, req_header>);
    static_assert(std::is_same_v<arg_provider_request<provider_string>, string_req>);
    static_assert(std::is_same_v<arg_provider_request<provider_empty>, empty_req>);
    static_assert(std::is_same_v<arg_provider_request<provider_file>, file_req>);

    static_assert(std::is_same_v<common_requests_type_t<>, empty_req>);
    static_assert(std::is_same_v<common_requests_type_t<provider_header>, empty_req>);
    static_assert(std::is_same_v<common_requests_type_t<provider_string>, string_req>);
    static_assert(std::is_same_v<common_requests_type_t<provider_empty>, empty_req>);
    static_assert(std::is_same_v<common_requests_type_t<provider_file>, file_req>);

    static_assert(compatible_arg_providers<provider_header, provider_string>);
    static_assert(compatible_arg_providers<provider_header, provider_empty>);
    static_assert(compatible_arg_providers<provider_header, provider_file>);
    static_assert(compatible_arg_providers<provider_header, provider_header>);
    static_assert(!compatible_arg_providers<provider_string, provider_file>);
    static_assert(compatible_arg_providers<provider_header, provider_header, provider_header>);
    static_assert(!compatible_arg_providers<provider_header, provider_string, provider_file>);
    static_assert(!compatible_arg_providers<provider_empty, provider_string, provider_file>);

    static_assert(std::is_same_v<common_requests_type_t<provider_string, provider_header>, string_req>);
    static_assert(std::is_same_v<common_requests_type_t<provider_empty, provider_header>, empty_req>);
    static_assert(std::is_same_v<common_requests_type_t<provider_file, provider_header>, file_req>);
    static_assert(std::is_same_v<common_requests_type_t<provider_header, provider_header>, empty_req>);
    static_assert(std::is_same_v<common_requests_type_t<provider_header, provider_header, provider_string>, string_req>);
}

void voidfn(int i, int j)
{
}

BOOST_AUTO_TEST_CASE(test_rest_void)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::path_arg;

    http::request<http::empty_body> req;
    context ctx;
    ctx.path_args = {{"a", "13"}, {"b", "42"}};

    auto rh1 = web::handler::rest(
        &voidfn,
        path_arg("a"),
        path_arg("b"));
    auto mg1 = rh1(req, ctx);
    auto resp1 = to_response<http::string_body>(mg1);
    BOOST_TEST(resp1.body().empty());
    BOOST_TEST(resp1.count(http::field::content_type) == 0);

    auto rh2 = web::handler::rest(
        voidfn,
        path_arg("a"),
        path_arg("b"));
    auto mg2 = rh2(req, ctx);
    auto resp2 = to_response<http::string_body>(mg2);
    BOOST_TEST(resp2.body().empty());
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
    namespace web = boost::web;
    using web::matcher::context;

    http::request<http::empty_body> req;
    context ctx;

    auto rh1 = web::handler::rest(const_obj_handler());
    auto mg1 = rh1(req, ctx);
    auto resp1 = to_response<http::string_body>(mg1);
    BOOST_TEST(resp1.body() == "blah1");

    auto rh2 = web::handler::rest(obj_handler());
    auto mg2 = rh2(req, ctx);
    auto resp2 = to_response<http::string_body>(mg2);
    BOOST_TEST(resp2.body() == "blah2");
}

auto sum(int i, int j)
{
    return std::to_string(i + j);
}

BOOST_AUTO_TEST_CASE(test_rest_sum_target)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::path_arg;

    http::request<http::empty_body> req;
    context ctx;
    ctx.path_args = {{"a", "13"}, {"b", "42"}};

    auto rh_sum = web::handler::rest(
        sum,
        path_arg("a"),
        path_arg("b"));
    auto mg_sum = rh_sum(req, ctx);
    auto resp_sum = to_response<http::string_body>(mg_sum);
    BOOST_TEST(resp_sum.body() == "55");
    BOOST_TEST(resp_sum[http::field::content_type] == "text/plain");
}

BOOST_AUTO_TEST_CASE(test_rest_sum_query)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::query_arg;

    http::request<http::empty_body> req{http::verb::get, "/?a=13&b=42", 10};
    context ctx;

    auto rh_sum = web::handler::rest(
        sum,
        query_arg("a"),
        query_arg("b"));
    auto mg_sum = rh_sum(req, ctx);
    auto resp_sum = to_response<http::string_body>(mg_sum);
    BOOST_TEST(resp_sum.body() == "55");
    BOOST_TEST(resp_sum[http::field::content_type] == "text/plain");
}

BOOST_AUTO_TEST_CASE(test_rest_sum_header)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::header_arg;

    http::request<http::empty_body> req;
    context ctx;
    req.insert("a", "13");
    req.insert("b", "42");

    auto rh_sum = web::handler::rest(
        sum,
        header_arg("a"),
        header_arg("b"));
    auto mg_sum = rh_sum(req, ctx);
    auto resp_sum = to_response<http::string_body>(mg_sum);
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
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::header_arg;

    http::request<http::empty_body> req;
    context ctx;

    auto rh = web::handler::rest(stock_string);
    auto mg = rh(req, ctx);
    auto resp = to_response<http::string_body>(mg);
    BOOST_TEST(resp.body() == stock_string());
    BOOST_TEST(resp[http::field::content_type] == "text/plain");
}

auto to_json(std::string value)
{
    return boost::json::value {{"value", value}};
}

BOOST_AUTO_TEST_CASE(test_rest_json_response)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::header_arg;

    http::request<http::empty_body> req;
    context ctx;
    req.insert("value", "Hello");

    auto rh = web::handler::rest(to_json, header_arg("value"));
    auto mg = rh(req, ctx);
    auto resp = to_response<http::string_body>(mg);
    BOOST_TEST(resp.body() == R"({"value":"Hello"})");
    BOOST_TEST(resp[http::field::content_type] == "application/json");
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
//    namespace web = boost::web;
//    using web::matcher::context;
//    using web::handler::path_arg;
//
//    http::request<http::string_body> req;
//    context ctx;
//    ctx.path_args = {{"a", "13"}, {"b", "42"}};
//
//    concat_handler bh;
//    auto rh_sum = web::handler::rest(
//        std::function{std::bind(&concat_handler::concat, &bh)},
//        path_arg("a"),
//        path_arg("b"));
//    auto mg = rh_sum(req, ctx);
//    auto resp = to_response<http::string_body>(mg);
//    BOOST_TEST(resp.body() == "1342");
//    BOOST_TEST(resp[http::field::content_type] == "text/plain");
//}

BOOST_AUTO_TEST_CASE(test_rest_mfn)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::header_arg;

    http::request<http::empty_body> req;
    context ctx;
    req.insert("a", "13");
    req.insert("b", "42");

    concat_handler bh;

    auto rh = web::handler::rest(
        &concat_handler::concat,
        &bh,
        header_arg("a"),
        header_arg("b"));
    auto mg = rh(req, ctx);
    auto resp = to_response<http::string_body>(mg);
    BOOST_TEST(resp.body() == "1342");
    BOOST_TEST(resp[http::field::content_type] == "text/plain");

    auto rhc = web::handler::rest(
        &concat_handler::const_concat,
        bh,
        header_arg("a"),
        header_arg("b"));
    auto mgc = rhc(req, ctx);
    auto respc = to_response<http::string_body>(mgc);
    BOOST_TEST(respc.body() == "4213");
    BOOST_TEST(respc[http::field::content_type] == "text/plain");
}

BOOST_AUTO_TEST_CASE(test_rest_string_body)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::string_body_arg;

    http::request<http::string_body> req;
    req.body() = "Hello";
    req.insert(http::field::content_type, "text/plain");
    req.prepare_payload();
    context ctx;

    auto l = [](const std::string& str) { return str + " world!"; };

    auto rh = web::handler::rest(
        l,
        string_body_arg());
    auto mg = rh(req, ctx);
    auto resp = to_response<http::string_body>(mg);
    BOOST_TEST(resp.body() == "Hello world!");
    BOOST_TEST(resp[http::field::content_type] == "text/plain");
}

} // namespace
