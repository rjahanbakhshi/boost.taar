//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "boost/taar/core/error.hpp"
#include "boost/taar/core/form_kvp.hpp"
#include <boost/beast/http/empty_body.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/json/value_from.hpp>
#include <boost/system/system_error.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_rest_arg_provider)
{
    using namespace boost::taar::handler::detail;
    using namespace boost::taar::matcher;
    using namespace boost::taar::handler;

    static_assert(!is_rest_arg_provider<int>, "Failed!");
    static_assert(!is_rest_arg_provider<float>, "Failed!");
    static_assert(!is_rest_arg_provider<void>, "Failed!");
    static_assert(!is_rest_arg_provider<void()>, "Failed!");
    static_assert(is_rest_arg_provider<std::string(int, const context&)>, "Failed!");
    static_assert(is_rest_arg_provider<std::string(*)(int, const context&)>, "Failed!");
    static_assert(is_rest_arg_provider<void(int, context&)>, "Failed!");
    static_assert(is_rest_arg_provider<int(int, context)>, "Failed!");
    static_assert(!is_rest_arg_provider<int(int, bool)>, "Failed!");
    static_assert(is_rest_arg_provider<decltype([](int, context){})>, "Failed!");
    static_assert(is_rest_arg_provider<path_arg>, "Failed!");
    static_assert(is_rest_arg_provider<query_arg>, "Failed!");
    static_assert(is_rest_arg_provider<header_arg>, "Failed!");
    static_assert(is_rest_arg_provider<cookie_arg>, "Failed!");
    static_assert(is_rest_arg_provider<string_body_arg>, "Failed!");
    static_assert(is_rest_arg_provider<json_body_arg>, "Failed!");
}

BOOST_AUTO_TEST_CASE(test_rest_arg)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::rest_arg;
    using taar::handler::query_arg;
    using taar::handler::path_arg;
    using taar::handler::header_arg;
    using taar::handler::cookie_arg;
    using taar::handler::string_body_arg;
    using taar::handler::json_body_arg;

    http::request<http::string_body> req{http::verb::get, "/?a=13", 10};
    req.set(http::field::host, "boost.org");
    req.insert("header1", "value1");
    req.insert("header2", "value2");
    req.insert("header3", "value3");
    req.insert("pi", "3.14");
    context ctx;
    ctx.path_args = {{"a", "13"}, {"b", "42"}};

    BOOST_TEST((rest_arg<int, query_arg>{query_arg("a")}(0, req, ctx) == 13));
    BOOST_TEST((rest_arg<std::string, query_arg>{query_arg("a")}(0, req, ctx) == "13"));
    BOOST_TEST((rest_arg<std::string_view, query_arg>{query_arg("a")}(0, req, ctx) == "13"));

    BOOST_TEST((rest_arg<int, path_arg>{path_arg("a")}(0, req, ctx) == 13));
    BOOST_TEST((rest_arg<int, path_arg>{path_arg("b")}(0, req, ctx) == 42));
    BOOST_TEST((rest_arg<std::string, path_arg>{path_arg("b")}(0, req, ctx) == "42"));

    BOOST_TEST((rest_arg<std::string, header_arg>{header_arg("header1")}(0, req, ctx) == "value1"));
    BOOST_TEST((rest_arg<std::string_view, header_arg>{header_arg("header2")}(0, req, ctx) == "value2"));
    BOOST_TEST((rest_arg<std::string_view, header_arg>{header_arg("pi")}(0, req, ctx) == "3.14"));
    BOOST_TEST((rest_arg<float, header_arg>{header_arg("pi")}(0, req, ctx) == 3.14f));
    BOOST_TEST((rest_arg<double, header_arg>{header_arg("pi")}(0, req, ctx) == 3.14));
    BOOST_TEST((rest_arg<std::string, header_arg>{header_arg(http::field::host)}(0, req, ctx) == "boost.org"));

    BOOST_TEST((rest_arg<int, float>{3.14f}(0, req, ctx) == 3));
    BOOST_TEST((rest_arg<float, float>{3.14f}(0, req, ctx) == 3.14f));
    BOOST_TEST((rest_arg<std::string, char const*>{"Hi"}(0, req, ctx) == "Hi"));
}

BOOST_AUTO_TEST_CASE(test_rest_arg_optional)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::rest_arg;
    using taar::handler::query_arg;
    using taar::handler::path_arg;
    using taar::handler::header_arg;
    using taar::handler::cookie_arg;
    using taar::handler::string_body_arg;
    using taar::handler::json_body_arg;

    http::request<http::string_body> req{http::verb::get, "/?a=13", 10};
    req.insert("header1", "value1");
    req.insert("pi", "3.14");
    context ctx;
    ctx.path_args = {{"a", "13"}, {"b", "42"}};

    BOOST_TEST((rest_arg<std::optional<int>, query_arg>{query_arg("a")}(0, req, ctx) == 13));
    BOOST_TEST((rest_arg<std::optional<int>, query_arg>{query_arg("b")}(0, req, ctx) == std::nullopt));
    BOOST_TEST((rest_arg<std::optional<std::string>, query_arg>{query_arg("a")}(0, req, ctx) == "13"));
    BOOST_TEST((rest_arg<std::optional<std::string_view>, query_arg>{query_arg("b")}(0, req, ctx) == std::nullopt));

    BOOST_TEST((rest_arg<std::optional<int>, path_arg>{path_arg("a")}(0, req, ctx) == 13));
    BOOST_TEST((rest_arg<std::optional<int>, path_arg>{path_arg("d")}(0, req, ctx) == std::nullopt));
    BOOST_TEST((rest_arg<std::optional<std::string>, path_arg>{path_arg("b")}(0, req, ctx) == "42"));
    BOOST_TEST((rest_arg<std::optional<std::string>, path_arg>{path_arg("c")}(0, req, ctx) == std::nullopt));

    BOOST_TEST((rest_arg<std::optional<std::string>, header_arg>{header_arg("header1")}(0, req, ctx) == "value1"));
    BOOST_TEST((rest_arg<std::optional<std::string>, header_arg>{header_arg("header2")}(0, req, ctx) == std::nullopt));
    BOOST_TEST((rest_arg<std::optional<float>, header_arg>{header_arg("pi")}(0, req, ctx) == 3.14f));
    BOOST_TEST((rest_arg<std::optional<float>, header_arg>{header_arg("di")}(0, req, ctx) == std::nullopt));
}

BOOST_AUTO_TEST_CASE(test_rest_arg_multiple_headers)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::rest_arg;
    using taar::handler::header_arg;
    using taar::error;

    context ctx;

    http::request<http::string_body> req1{http::verb::get, "", 10};
    req1.insert(http::field::host, "host1");
    req1.insert(http::field::host, "host1");
    BOOST_TEST((rest_arg<std::string, header_arg>{header_arg(http::field::host)}(0, req1, ctx) == "host1"));

    http::request<http::string_body> req2{http::verb::get, "", 10};
    req2.insert(http::field::host, "host1");
    req2.insert(http::field::host, "host2");
    BOOST_REQUIRE_THROW((rest_arg<std::string, header_arg>{header_arg(http::field::host)}(0, req2, ctx)), boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(test_rest_arg_cookie)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::rest_arg;
    using taar::handler::cookie_arg;
    using taar::error;

    http::request<http::string_body> req{http::verb::get, "", 10};
    req.set(http::field::cookie, "cookie1=value1; cookie2=value2;cookie3=value3");
    context ctx;

    BOOST_TEST((rest_arg<std::string, cookie_arg>{cookie_arg("cookie1")}(0, req, ctx) == "value1"));
    BOOST_TEST((rest_arg<std::string, cookie_arg>{cookie_arg("cookie2")}(0, req, ctx) == "value2"));
    BOOST_TEST((rest_arg<std::string, cookie_arg>{cookie_arg("cookie3")}(0, req, ctx) == "value3"));

    req.set(http::field::cookie, "cookie1=");
    BOOST_TEST((rest_arg<std::string, cookie_arg>{cookie_arg("cookie1")}(0, req, ctx) == ""));

    req.set(http::field::cookie, "");
    BOOST_REQUIRE_THROW((rest_arg<std::string, cookie_arg>{cookie_arg("cookie1")}(0, req, ctx)), boost::system::system_error);

    req.set(http::field::cookie, "cookie1");
    BOOST_REQUIRE_THROW((rest_arg<std::string, cookie_arg>{cookie_arg("cookie1")}(0, req, ctx)), boost::system::system_error);

    req.set(http::field::cookie, "cookie1;");
    BOOST_REQUIRE_THROW((rest_arg<std::string, cookie_arg>{cookie_arg("cookie1")}(0, req, ctx)), boost::system::system_error);

    req.set(http::field::cookie, ";");
    BOOST_REQUIRE_THROW((rest_arg<std::string, cookie_arg>{cookie_arg("cookie1")}(0, req, ctx)), boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(test_rest_arg_string_body)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::rest_arg;
    using taar::handler::string_body_arg;
    using taar::handler::all_content_types;

    http::request<http::string_body> req{http::verb::get, "/", 10};
    req.insert(http::field::content_type, "text/plain");
    req.body() = "Hello world!";
    req.prepare_payload();
    context ctx;

    BOOST_TEST((
        rest_arg<std::string, string_body_arg>{string_body_arg()}(0, req, ctx) ==
        "Hello world!"));

    BOOST_TEST((
        rest_arg<std::string_view, string_body_arg>{string_body_arg()}(0, req, ctx) ==
        "Hello world!"));

    BOOST_TEST((
        rest_arg<std::string, string_body_arg>{string_body_arg("text/plain")}(0, req, ctx) ==
        "Hello world!"));

    BOOST_TEST((
        rest_arg<std::string, string_body_arg>{string_body_arg("text/plain", "application/json")}(0, req, ctx) ==
        "Hello world!"));

    BOOST_TEST((
        rest_arg<std::string, string_body_arg>{string_body_arg(all_content_types)}(0, req, ctx) ==
        "Hello world!"));

    BOOST_CHECK_THROW(
        (rest_arg<std::string, string_body_arg>{string_body_arg("image/png")}(0, req, ctx)),
        boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(test_rest_arg_json_body)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::rest_arg;
    using taar::handler::json_body_arg;
    using taar::handler::all_content_types;

    http::request<http::string_body> req{http::verb::get, "/", 10};
    req.insert(http::field::content_type, "application/json");
    req.body() = R"({"everything": 42})";
    req.prepare_payload();
    context ctx;

    BOOST_TEST((
      rest_arg<boost::json::value, json_body_arg>{json_body_arg()}(0, req, ctx) ==
      boost::json::value{{"everything", 42}}));

    BOOST_TEST((
      rest_arg<boost::json::value, json_body_arg>{json_body_arg("application/json")}(0, req, ctx) ==
      boost::json::value{{"everything", 42}}));

    BOOST_TEST((
      rest_arg<boost::json::value, json_body_arg>{json_body_arg(all_content_types)}(0, req, ctx) ==
      boost::json::value{{"everything", 42}}));

    BOOST_TEST((
      rest_arg<boost::json::value, json_body_arg>{json_body_arg("application/json", "text/plain")}(0, req, ctx) ==
      boost::json::value{{"everything", 42}}));

    BOOST_CHECK_THROW(
        (rest_arg<boost::json::value, json_body_arg>{json_body_arg("text/plain")}(0, req, ctx)),
        boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(test_rest_arg_url_encoded_form_data)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::rest_arg;
    using taar::handler::url_encoded_form_data_arg;
    using taar::handler::all_content_types;
    using taar::form_kvp;

    http::request<http::string_body> req{http::verb::get, "/", 10};
    req.insert(http::field::content_type, "application/x-www-form-urlencoded");
    req.body() = R"(everything=42&hello=world&hello+world=13%2042)";
    req.prepare_payload();
    context ctx;

    form_kvp expected_result {
        {"everything", "42"},
        {"hello", "world"},
        {"hello world", "13 42"}};

    BOOST_TEST((
      rest_arg<form_kvp, url_encoded_form_data_arg>{url_encoded_form_data_arg()}(0, req, ctx) ==
      expected_result));

    BOOST_TEST((
      rest_arg<form_kvp, url_encoded_form_data_arg>{url_encoded_form_data_arg("application/x-www-form-urlencoded")}(0, req, ctx) ==
      expected_result));

    BOOST_TEST((
      rest_arg<form_kvp, url_encoded_form_data_arg>{url_encoded_form_data_arg(all_content_types)}(0, req, ctx) ==
      expected_result));

    BOOST_TEST((
      rest_arg<form_kvp, url_encoded_form_data_arg>{url_encoded_form_data_arg("application/x-www-form-urlencoded", "text/plain")}(0, req, ctx) ==
      expected_result));

    BOOST_CHECK_THROW(
        (rest_arg<form_kvp, url_encoded_form_data_arg>{url_encoded_form_data_arg("text/plain")}(0, req, ctx)),
        boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(test_rest_arg_with_default)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::context;
    using taar::handler::rest_arg;
    using taar::handler::query_arg;
    using taar::handler::header_arg;
    using taar::handler::with_default;

    http::request<http::empty_body> req{http::verb::get, "/?qval1=13", 10};
    req.insert("hval1", "Hello");
    req.prepare_payload();
    context ctx;

    BOOST_TEST(
        with_default(query_arg("qval1"), 42).name() ==
        query_arg("qval1").name());

    BOOST_TEST(
        with_default(header_arg("hval1"), "world").name() ==
        header_arg("hval1").name());

    BOOST_TEST(
        with_default(query_arg("qval3"), false).name() ==
        header_arg("qval3").name());

    BOOST_TEST((rest_arg<int, with_default<query_arg, int>>{with_default(query_arg("qval1"), 42)}(0, req, ctx) == 13));
    BOOST_TEST((rest_arg<int, with_default<query_arg, int>>{with_default(query_arg("qval2"), 42)}(0, req, ctx) == 42));
    BOOST_TEST((rest_arg<std::string, with_default<header_arg, const char*>>{with_default(header_arg("hval1"), "world")}(0, req, ctx) == "Hello"));
    BOOST_TEST((rest_arg<std::string_view, with_default<header_arg, const char*>>{with_default(header_arg("hval2"), "world")}(0, req, ctx) == "world"));
    BOOST_TEST((rest_arg<int, with_default<query_arg, bool>>{with_default(query_arg("qval3"), false)}(0, req, ctx) == false));

    //BOOST_TEST((
    //  rest_arg<int, with_default<query_arg, int>>{with_default(query_arg("qval1"), 42)}(0, req, ctx) == 13));
}

} // namespace
