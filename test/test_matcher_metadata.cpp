//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/matcher/cookie.hpp>
#include <boost/taar/matcher/header.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/matcher/version.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/test/unit_test.hpp>
#include <tuple>
#include <type_traits>

namespace {

using namespace boost::taar::matcher;

// ---- flatten: leaves -------------------------------------------------------

static_assert(std::is_same_v<
    flatten_t<method_eq_constraint>,
    type_list<method_eq_constraint>>);

static_assert(std::is_same_v<
    flatten_t<target_constraint>,
    type_list<target_constraint>>);

static_assert(std::is_same_v<
    flatten_t<version_le_constraint>,
    type_list<version_le_constraint>>);

// ---- flatten: any_metadata is empty ---------------------------------------

static_assert(std::is_same_v<flatten_t<any_metadata>, type_list<>>);

// ---- flatten: not_ preserves the leaf set ---------------------------------

static_assert(std::is_same_v<
    flatten_t<not_<method_eq_constraint>>,
    type_list<method_eq_constraint>>);

static_assert(std::is_same_v<
    flatten_t<not_<and_<method_eq_constraint, target_constraint>>>,
    type_list<method_eq_constraint, target_constraint>>);

// ---- flatten: and_ / or_ concatenate left-to-right ------------------------

static_assert(std::is_same_v<
    flatten_t<and_<method_eq_constraint, target_constraint>>,
    type_list<method_eq_constraint, target_constraint>>);

static_assert(std::is_same_v<
    flatten_t<or_<header_eq_constraint, cookie_exists_constraint>>,
    type_list<header_eq_constraint, cookie_exists_constraint>>);

// ---- flatten: nested AST flattens left-to-right ---------------------------

static_assert(std::is_same_v<
    flatten_t<and_<method_eq_constraint, and_<target_constraint, version_ge_constraint>>>,
    type_list<method_eq_constraint, target_constraint, version_ge_constraint>>);

static_assert(std::is_same_v<
    flatten_t<and_<and_<method_eq_constraint, target_constraint>, version_ge_constraint>>,
    type_list<method_eq_constraint, target_constraint, version_ge_constraint>>);

// ---- list_size -------------------------------------------------------------

static_assert(list_size_v<flatten_t<any_metadata>> == 0);
static_assert(list_size_v<flatten_t<method_eq_constraint>> == 1);
static_assert(list_size_v<flatten_t<and_<method_eq_constraint, target_constraint>>> == 2);
static_assert(list_size_v<flatten_t<and_<method_eq_constraint, and_<target_constraint, version_ge_constraint>>>> == 3);

// ---- data_of: every leaf has a value type --------------------------------

static_assert(std::is_same_v<data_of_t<method_eq_constraint>,     method_constraint_value>);
static_assert(std::is_same_v<data_of_t<method_ne_constraint>,     method_constraint_value>);
static_assert(std::is_same_v<data_of_t<target_constraint>,        target_constraint_value>);
static_assert(std::is_same_v<data_of_t<header_eq_constraint>,     header_constraint_value>);
static_assert(std::is_same_v<data_of_t<header_ne_constraint>,     header_constraint_value>);
static_assert(std::is_same_v<data_of_t<header_exists_constraint>, header_constraint_value>);
static_assert(std::is_same_v<data_of_t<cookie_eq_constraint>,     cookie_constraint_value>);
static_assert(std::is_same_v<data_of_t<cookie_ne_constraint>,     cookie_constraint_value>);
static_assert(std::is_same_v<data_of_t<cookie_exists_constraint>, cookie_constraint_value>);
static_assert(std::is_same_v<data_of_t<version_eq_constraint>,    version_constraint_value>);
static_assert(std::is_same_v<data_of_t<version_ne_constraint>,    version_constraint_value>);
static_assert(std::is_same_v<data_of_t<version_lt_constraint>,    version_constraint_value>);
static_assert(std::is_same_v<data_of_t<version_le_constraint>,    version_constraint_value>);
static_assert(std::is_same_v<data_of_t<version_gt_constraint>,    version_constraint_value>);
static_assert(std::is_same_v<data_of_t<version_ge_constraint>,    version_constraint_value>);

// ---- data_tuple_t: empty for any_metadata --------------------------------

static_assert(std::is_same_v<data_tuple_t<any_metadata>, std::tuple<>>);

// ---- data_tuple_t: single leaf ---------------------------------------------

static_assert(std::is_same_v<
    data_tuple_t<method_eq_constraint>,
    std::tuple<method_constraint_value>>);

// ---- data_tuple_t: composition concatenates left-to-right -----------------

static_assert(std::is_same_v<
    data_tuple_t<and_<method_eq_constraint, target_constraint>>,
    std::tuple<method_constraint_value, target_constraint_value>>);

static_assert(std::is_same_v<
    data_tuple_t<and_<method_eq_constraint, and_<target_constraint, version_ge_constraint>>>,
    std::tuple<method_constraint_value, target_constraint_value, version_constraint_value>>);

// ---- not_<X> preserves both shape and captures of X ----------------------

static_assert(std::is_same_v<
    data_tuple_t<not_<and_<method_eq_constraint, target_constraint>>>,
    std::tuple<method_constraint_value, target_constraint_value>>);

// ---- A runtime check that captured values flow through std::get correctly.

BOOST_AUTO_TEST_CASE(test_matcher_metadata_captures_indexing)
{
    namespace http = boost::beast::http;

    using ast = and_<method_eq_constraint, target_constraint>;
    data_tuple_t<ast> captures
    {
        method_constraint_value {http::verb::get},
        target_constraint_value {"/users/{id}"}
    };

    auto const& method_data = std::get<0>(captures);
    auto const& target_data = std::get<1>(captures);

    BOOST_TEST((std::get<http::verb>(method_data.verb) == http::verb::get));
    BOOST_TEST(target_data.template_str == "/users/{id}");
}

// ---- End-to-end: leaves attach the right metadata and captures -----------

BOOST_AUTO_TEST_CASE(test_matcher_metadata_leaf_tags_and_captures)
{
    namespace http = boost::beast::http;

    auto m_eq = method == http::verb::get;
    static_assert(std::is_same_v<
        decltype(m_eq)::metadata_type,
        method_eq_constraint>);
    BOOST_TEST((std::get<http::verb>(std::get<0>(m_eq.captures()).verb) == http::verb::get));

    auto m_ne = method != http::verb::post;
    static_assert(std::is_same_v<
        decltype(m_ne)::metadata_type,
        method_ne_constraint>);

    auto t_eq = target == "/api/{id}";
    static_assert(std::is_same_v<
        decltype(t_eq)::metadata_type,
        target_constraint>);
    BOOST_TEST(std::get<0>(t_eq.captures()).template_str == "/api/{id}");

    auto h_eq = header(http::field::content_type) == "application/json";
    static_assert(std::is_same_v<
        decltype(h_eq)::metadata_type,
        header_eq_constraint>);
    auto const& h_eq_cap = std::get<0>(h_eq.captures());
    BOOST_TEST((std::get<http::field>(h_eq_cap.key) == http::field::content_type));
    BOOST_TEST(h_eq_cap.value == "application/json");

    auto h_exists = exist(header("X-Trace-Id"));
    static_assert(std::is_same_v<
        decltype(h_exists)::metadata_type,
        header_exists_constraint>);
    BOOST_TEST((std::get<std::string>(std::get<0>(h_exists.captures()).key) == "X-Trace-Id"));

    auto c_eq = cookie("session") == "abc";
    static_assert(std::is_same_v<
        decltype(c_eq)::metadata_type,
        cookie_eq_constraint>);
    BOOST_TEST(std::get<0>(c_eq.captures()).name == "session");
    BOOST_TEST(std::get<0>(c_eq.captures()).value == "abc");

    auto v_ge = version >= 11u;
    static_assert(std::is_same_v<
        decltype(v_ge)::metadata_type,
        version_ge_constraint>);
    BOOST_TEST(std::get<0>(v_ge.captures()).value == 11u);

    // <= and >= are single-leaf constraints now, not `< || ==`.
    auto v_le = version <= 11u;
    static_assert(std::is_same_v<
        decltype(v_le)::metadata_type,
        version_le_constraint>);
}

// ---- End-to-end: composition combines metadata and concatenates captures --

BOOST_AUTO_TEST_CASE(test_matcher_metadata_composition_and)
{
    namespace http = boost::beast::http;

    auto m = (method == http::verb::get) && (target == "/users/{id}");

    static_assert(std::is_same_v<
        decltype(m)::metadata_type,
        and_<method_eq_constraint, target_constraint>>);

    static_assert(std::tuple_size_v<decltype(m)::captures_type> == 2);

    BOOST_TEST((std::get<http::verb>(std::get<0>(m.captures()).verb) == http::verb::get));
    BOOST_TEST(std::get<1>(m.captures()).template_str == "/users/{id}");
}

BOOST_AUTO_TEST_CASE(test_matcher_metadata_composition_or)
{
    namespace http = boost::beast::http;

    auto m = (method == http::verb::get) || (method == http::verb::head);

    static_assert(std::is_same_v<
        decltype(m)::metadata_type,
        or_<method_eq_constraint, method_eq_constraint>>);

    static_assert(std::tuple_size_v<decltype(m)::captures_type> == 2);

    BOOST_TEST((std::get<http::verb>(std::get<0>(m.captures()).verb) == http::verb::get));
    BOOST_TEST((std::get<http::verb>(std::get<1>(m.captures()).verb) == http::verb::head));
}

BOOST_AUTO_TEST_CASE(test_matcher_metadata_composition_not)
{
    auto m = !(target == "/private");
    static_assert(std::is_same_v<
        decltype(m)::metadata_type,
        not_<target_constraint>>);
    BOOST_TEST(std::get<0>(m.captures()).template_str == "/private");
}

BOOST_AUTO_TEST_CASE(test_matcher_metadata_three_way_left_associative)
{
    namespace http = boost::beast::http;

    auto m =
        (method == http::verb::get) &&
        (target == "/items/{id}") &&
        (version >= 11u);

    static_assert(std::is_same_v<
        decltype(m)::metadata_type,
        and_<and_<method_eq_constraint, target_constraint>, version_ge_constraint>>);

    static_assert(std::tuple_size_v<decltype(m)::captures_type> == 3);

    BOOST_TEST((std::get<http::verb>(std::get<0>(m.captures()).verb) == http::verb::get));
    BOOST_TEST(std::get<1>(m.captures()).template_str == "/items/{id}");
    BOOST_TEST(std::get<2>(m.captures()).value == 11u);
}

// ---- Mixing a leaf with a raw callable preserves the leaf's metadata -----

BOOST_AUTO_TEST_CASE(test_matcher_metadata_mixed_with_raw_callable)
{
    namespace http = boost::beast::http;

    auto always_true = [](http::request_header<> const&, context&) { return true; };

    auto m = (method == http::verb::get) && always_true;

    static_assert(std::is_same_v<
        decltype(m)::metadata_type,
        and_<method_eq_constraint, any_metadata>>);

    static_assert(std::tuple_size_v<decltype(m)::captures_type> == 1);
    BOOST_TEST((std::get<http::verb>(std::get<0>(m.captures()).verb) == http::verb::get));
}

} // namespace
