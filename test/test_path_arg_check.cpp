//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/handler/path_arg_check.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>

namespace {

namespace http = boost::beast::http;
using namespace boost::taar::matcher;
using boost::taar::handler::rest;
using boost::taar::handler::path_arg;
using boost::taar::handler::query_arg;
using boost::taar::handler::with_default;
using boost::taar::handler::validate_path_args_against_target;
using boost::taar::handler::find_target_template;

// ---- find_target_template ------------------------------------------------

BOOST_AUTO_TEST_CASE(test_find_target_template_when_target_constraint_present)
{
    operand op {target == "/users/{id}"};
    auto t = find_target_template(op);
    BOOST_REQUIRE(t.has_value());
    BOOST_TEST(*t == "/users/{id}");
}

BOOST_AUTO_TEST_CASE(test_find_target_template_inside_composition)
{
    operand op {(method == http::verb::get) && (target == "/items/{id}")};
    auto t = find_target_template(op);
    BOOST_REQUIRE(t.has_value());
    BOOST_TEST(*t == "/items/{id}");
}

BOOST_AUTO_TEST_CASE(test_find_target_template_absent)
{
    operand op {method == http::verb::get};
    auto t = find_target_template(op);
    BOOST_TEST(!t.has_value());
}

// ---- validate: matching cases (no throw) ---------------------------------

BOOST_AUTO_TEST_CASE(test_validate_matching_single_param)
{
    operand op {(method == http::verb::get) && (target == "/users/{id}")};
    auto handler = rest([](std::string id) { return id; }, path_arg("id"));
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, handler));
}

BOOST_AUTO_TEST_CASE(test_validate_matching_multiple_params)
{
    operand op {target == "/users/{uid}/files/{fid}"};
    auto handler = rest(
        [](std::string a, std::string b) { return a + b; },
        path_arg("uid"),
        path_arg("fid"));
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, handler));
}

BOOST_AUTO_TEST_CASE(test_validate_matching_greedy_param)
{
    operand op {target == "/files/{*tail}"};
    auto handler = rest([](std::string t) { return t; }, path_arg("tail"));
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, handler));
}

BOOST_AUTO_TEST_CASE(test_validate_with_default_wrapped_path_arg)
{
    operand op {target == "/users/{id}"};
    auto handler = rest(
        [](std::string id) { return id; },
        with_default(path_arg("id"), std::string{"anonymous"}));
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, handler));
}

BOOST_AUTO_TEST_CASE(test_validate_unused_placeholder_is_allowed)
{
    // The template declares {id} but the handler ignores it. This is
    // intentionally permitted.
    operand op {target == "/users/{id}"};
    auto handler = rest([]() { return std::string{"hello"}; });
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, handler));
}

BOOST_AUTO_TEST_CASE(test_validate_non_path_args_are_ignored)
{
    // query_arg is not a path_arg -- it does not have to appear in the
    // target template.
    operand op {target == "/items"};
    auto handler = rest(
        [](std::string page) { return page; },
        query_arg("page"));
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, handler));
}

// ---- validate: no-op cases (no target / no path_arg_names) ---------------

BOOST_AUTO_TEST_CASE(test_validate_skips_when_no_target_in_matcher)
{
    // A matcher with no target_constraint -- the validator has nothing to
    // check against and must not throw, even with a path_arg present.
    operand op {method == http::verb::get};
    auto handler = rest([](std::string x) { return x; }, path_arg("x"));
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, handler));
}

BOOST_AUTO_TEST_CASE(test_validate_skips_for_raw_lambda_handler)
{
    // A handler that's not produced by rest() does not expose
    // path_arg_names() and must be skipped.
    operand op {target == "/users/{id}"};
    auto raw = [](http::request_header<> const&, boost::taar::matcher::context const&)
    {
        return true;
    };
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, raw));
}

// ---- validate: mismatch cases (throw) ------------------------------------

BOOST_AUTO_TEST_CASE(test_validate_throws_on_path_arg_typo)
{
    operand op {target == "/users/{id}"};
    auto handler = rest([](std::string x) { return x; }, path_arg("ud"));
    BOOST_CHECK_THROW(
        validate_path_args_against_target(op, handler),
        boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(test_validate_throws_when_path_arg_has_no_placeholder)
{
    // The template has no placeholders but the handler declares a path_arg.
    operand op {target == "/static"};
    auto handler = rest([](std::string x) { return x; }, path_arg("id"));
    BOOST_CHECK_THROW(
        validate_path_args_against_target(op, handler),
        boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(test_validate_throws_when_one_of_many_path_args_is_unmatched)
{
    operand op {target == "/users/{uid}"};
    auto handler = rest(
        [](std::string a, std::string b) { return a + b; },
        path_arg("uid"),
        path_arg("fid"));   // fid does not appear in template
    BOOST_CHECK_THROW(
        validate_path_args_against_target(op, handler),
        boost::system::system_error);
}

BOOST_AUTO_TEST_CASE(test_validate_matches_greedy_param_name_against_path_arg)
{
    // {*tail} captures under the name "tail", which a path_arg("tail")
    // can read.
    operand op {target == "/files/{*tail}"};
    auto handler_ok  = rest([](std::string t) { return t; }, path_arg("tail"));
    auto handler_bad = rest([](std::string t) { return t; }, path_arg("path"));
    BOOST_CHECK_NO_THROW(validate_path_args_against_target(op, handler_ok));
    BOOST_CHECK_THROW(
        validate_path_args_against_target(op, handler_bad),
        boost::system::system_error);
}

} // namespace
