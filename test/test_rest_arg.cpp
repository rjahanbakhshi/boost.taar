//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/web/handler/rest_arg.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/json/value_from.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_rest_arg_traits)
{
    using namespace boost::web::handler::detail;

    static_assert(lexical_castable<std::string, int>);
    static_assert(lexical_castable<std::string_view, int>);

    static_assert(convertible_from_json_value<std::string>);
    static_assert(convertible_from_json_value<std::string_view>);
    static_assert(convertible_from_json_value<int>);

    BOOST_TEST(bool_cast("true").value());
    BOOST_TEST(bool_cast("TRUE").value());
    BOOST_TEST(bool_cast("yes").value());
    BOOST_TEST(bool_cast("YES").value());
    BOOST_TEST(bool_cast("1").value());
    BOOST_TEST(!bool_cast("false").value());
    BOOST_TEST(!bool_cast("FALSE").value());
    BOOST_TEST(!bool_cast("no").value());
    BOOST_TEST(!bool_cast("NO").value());
    BOOST_TEST(!bool_cast("0").value());
    BOOST_TEST(bool_cast("blah").has_error());

    static_assert(rest_arg_castable<int, int>);
    static_assert(rest_arg_castable<std::string_view, int>);
    static_assert(rest_arg_castable<std::string, int>);
    static_assert(rest_arg_castable<int, std::string>);
    static_assert(rest_arg_castable<std::string, float>);
    static_assert(rest_arg_castable<float, std::string>);
    static_assert(rest_arg_castable<std::string, std::string_view>);
}

BOOST_AUTO_TEST_CASE(test_rest_arg_cast)
{
    using namespace boost::web::handler::detail;

    BOOST_TEST(rest_arg_cast<int>("10") == 10);
    BOOST_TEST(rest_arg_cast<float>("3.14") == 3.14f);
    BOOST_TEST(rest_arg_cast<bool>("true"));
    BOOST_TEST(!rest_arg_cast<bool>("false"));
    BOOST_TEST(rest_arg_cast<int>(3.14f) == 3);
    BOOST_TEST(rest_arg_cast<double>(13) == 13.0);
    BOOST_TEST(rest_arg_cast<bool>(1) == true);
    BOOST_TEST(rest_arg_cast<std::string>(42) == "42");
    BOOST_TEST(rest_arg_cast<std::string>(true) == "1");
}

BOOST_AUTO_TEST_CASE(test_rest_arg_provider)
{
    using namespace boost::web::handler::detail;
    using namespace boost::web::matcher;
    using namespace boost::web::handler;

    static_assert(!is_rest_arg_provider<int>);
    static_assert(!is_rest_arg_provider<void>);
    static_assert(!is_rest_arg_provider<void()>);
    static_assert(is_rest_arg_provider<std::string(int, const context&)>);
    static_assert(is_rest_arg_provider<std::string(*)(int, const context&)>);
    static_assert(is_rest_arg_provider<void(int, context&)>);
    static_assert(is_rest_arg_provider<int(int, context)>);
    static_assert(!is_rest_arg_provider<int(int, bool)>);
    static_assert(is_rest_arg_provider<decltype([](int, context){})>);
    static_assert(is_rest_arg_provider<path_arg>);
    static_assert(is_rest_arg_provider<query_arg>);
    static_assert(is_rest_arg_provider<header_arg>);
    static_assert(is_rest_arg_provider<string_body_arg>);
    static_assert(is_rest_arg_provider<json_body_arg>);
}

BOOST_AUTO_TEST_CASE(test_rest_arg)
{
    namespace http = boost::beast::http;
    namespace web = boost::web;
    using web::matcher::context;
    using web::handler::rest_arg;
    using web::handler::query_arg;
    using web::handler::path_arg;
    using web::handler::header_arg;
    using web::handler::string_body_arg;
    using web::handler::json_body_arg;

    http::request<http::string_body> req{http::verb::get, "/?a=13", 10};
    req.insert("header1", "value1");
    req.insert("header2", "value2");
    req.insert("header3", "value3");
    req.insert("pi", "3.14");
    req.insert(http::field::content_type, "text/plain");
    req.insert(http::field::content_type, "application/json");
    req.body() = R"({"everything": 42})";
    req.prepare_payload();
    context ctx;
    ctx.path_args = {{"a", "13"}, {"b", "42"}};

    BOOST_TEST((rest_arg<int, query_arg>{query_arg("a")}(req, ctx) == 13));
    BOOST_TEST((rest_arg<std::string, query_arg>{query_arg("a")}(req, ctx) == "13"));
    BOOST_TEST((rest_arg<std::string_view, query_arg>{query_arg("a")}(req, ctx) == "13"));

    BOOST_TEST((rest_arg<int, path_arg>{path_arg("a")}(req, ctx) == 13));
    BOOST_TEST((rest_arg<int, path_arg>{path_arg("b")}(req, ctx) == 42));
    BOOST_TEST((rest_arg<std::string, path_arg>{path_arg("b")}(req, ctx) == "42"));

    BOOST_TEST((rest_arg<std::string, header_arg>{header_arg("header1")}(req, ctx) == "value1"));
    BOOST_TEST((rest_arg<std::string_view, header_arg>{header_arg("header2")}(req, ctx) == "value2"));
    BOOST_TEST((rest_arg<std::string_view, header_arg>{header_arg("pi")}(req, ctx) == "3.14"));
    BOOST_TEST((rest_arg<float, header_arg>{header_arg("pi")}(req, ctx) == 3.14f));

    BOOST_TEST((rest_arg<std::string, string_body_arg>{string_body_arg()}(req, ctx) == R"({"everything": 42})"));
    BOOST_TEST((rest_arg<std::string_view, string_body_arg>{string_body_arg()}(req, ctx) == R"({"everything": 42})"));

    BOOST_TEST((
      rest_arg<boost::json::value, json_body_arg>{json_body_arg()}(req, ctx) ==
      boost::json::value{{"everything", 42}}));
}

} // namespace
