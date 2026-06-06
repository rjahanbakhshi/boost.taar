//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/jsonrpc/method.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/test/unit_test.hpp>
#include <type_traits>

namespace {

namespace http     = boost::beast::http;
namespace json     = boost::json;
namespace jsonrpc  = boost::taar::jsonrpc;
namespace matcher  = boost::taar::matcher;

using matcher::context;
using matcher::method;
using matcher::target;

// ---- Metadata wiring ------------------------------------------------------

static_assert(std::is_same_v<
    decltype(jsonrpc::method == std::string{"x"})::metadata_type,
    matcher::jsonrpc_method_eq_constraint>);

static_assert(std::is_same_v<
    decltype(jsonrpc::method != std::string{"x"})::metadata_type,
    matcher::jsonrpc_method_ne_constraint>);

// Composition with HTTP leaves preserves the metadata AST shape.
static_assert(std::is_same_v<
    decltype(
        (method == http::verb::post) &&
        (target == "/jsonrpc") &&
        (jsonrpc::method == std::string{"add"}))::metadata_type,
    matcher::and_<
        matcher::and_<matcher::method_eq_constraint, matcher::target_constraint>,
        matcher::jsonrpc_method_eq_constraint>>);

// with_parsed_body propagates through composition.
static_assert(decltype(jsonrpc::method == std::string{"x"})::with_parsed_body);
static_assert(decltype(
    (method == http::verb::post) &&
    (jsonrpc::method == std::string{"add"}))::with_parsed_body);

// And it does NOT spuriously turn on for purely-HTTP composed operands.
static_assert(!decltype(
    (method == http::verb::post) &&
    (target == "/api"))::with_parsed_body);

// ---- Runtime: matcher behaviour ------------------------------------------

BOOST_AUTO_TEST_CASE(test_jsonrpc_method_matches_when_envelope_method_equals_name)
{
    auto m = jsonrpc::method == std::string{"add"};

    http::request<http::empty_body> req;
    context ctx;

    auto body = json::parse(R"({
        "jsonrpc":"2.0","id":1,"method":"add","params":{"a":1,"b":2}
    })");

    BOOST_TEST(m(req, ctx, {}, {}, body));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_method_does_not_match_different_method)
{
    auto m = jsonrpc::method == std::string{"add"};

    http::request<http::empty_body> req;
    context ctx;

    auto body = json::parse(R"({
        "jsonrpc":"2.0","id":1,"method":"subtract","params":{}
    })");

    BOOST_TEST(!m(req, ctx, {}, {}, body));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_method_does_not_match_when_body_is_null)
{
    // Default-constructed json::value is null; this is what the session
    // hands matchers when the body cache is empty or parse failed.
    auto m = jsonrpc::method == std::string{"add"};

    http::request<http::empty_body> req;
    context ctx;

    BOOST_TEST(!m(req, ctx, {}, {}, json::value{}));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_method_does_not_match_when_envelope_is_invalid)
{
    // A valid JSON value that isn't a valid envelope: parse_envelope
    // returns an error → no match.
    auto m = jsonrpc::method == std::string{"add"};

    http::request<http::empty_body> req;
    context ctx;

    auto body = json::parse(R"({"jsonrpc":"1.0","method":"add"})");
    BOOST_TEST(!m(req, ctx, {}, {}, body));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_method_ne_matches_different_method)
{
    auto m = jsonrpc::method != std::string{"add"};

    http::request<http::empty_body> req;
    context ctx;

    auto body = json::parse(R"({"jsonrpc":"2.0","id":1,"method":"sub"})");
    BOOST_TEST(m(req, ctx, {}, {}, body));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_method_ne_does_not_match_invalid_envelope)
{
    // != only succeeds for a valid envelope whose method differs. An
    // invalid envelope (or missing body) is still a non-match — same
    // policy as ==.
    auto m = jsonrpc::method != std::string{"add"};

    http::request<http::empty_body> req;
    context ctx;

    BOOST_TEST(!m(req, ctx, {}, {}, json::value{}));
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_method_composes_with_http_leaves)
{
    auto m = (method == http::verb::post) &&
             (jsonrpc::method == std::string{"add"});

    http::request<http::empty_body> req{http::verb::post, "/jsonrpc", 11};
    context ctx;

    auto body = json::parse(R"({"jsonrpc":"2.0","id":1,"method":"add"})");

    BOOST_TEST(m(req, ctx, {}, {}, body));

    // Wrong HTTP verb → no match even with the right envelope method.
    http::request<http::empty_body> req_get{http::verb::get, "/jsonrpc", 11};
    BOOST_TEST(!m(req_get, ctx, {}, {}, body));

    // Wrong envelope method → no match even with the right verb.
    auto body_sub = json::parse(R"({"jsonrpc":"2.0","id":1,"method":"sub"})");
    BOOST_TEST(!m(req, ctx, {}, {}, body_sub));
}

// ---- Captures store the method name --------------------------------------

BOOST_AUTO_TEST_CASE(test_jsonrpc_method_captures_method_name)
{
    auto m = jsonrpc::method == std::string{"users/list"};
    BOOST_TEST(std::get<0>(m.captures()).method == "users/list");
}

} // namespace
