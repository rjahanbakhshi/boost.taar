//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/matcher/method.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>
#include <boost/test/unit_test.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_matcher_method)
{
    namespace http = boost::beast::http;
    using namespace boost::taar::matcher;
    using http::verb;

    http::request<http::string_body> req{http::verb::get, "", 10};
    context ctx;

    BOOST_TEST((method == verb::get)(req, ctx));
    BOOST_TEST(!(verb::put == method)(req, ctx));
    BOOST_TEST(!(!(method == verb::get))(req, ctx));
    BOOST_TEST((!(method == verb::put))(req, ctx));
    BOOST_TEST(!(method != verb::get)(req, ctx));
    BOOST_TEST((verb::put != method)(req, ctx));
    BOOST_TEST(!(method == verb::get && verb::put == method)(req, ctx));
    BOOST_TEST((method == verb::get && verb::get == method)(req, ctx));
    BOOST_TEST((method == verb::get || verb::put == method)(req, ctx));
    BOOST_TEST((method == verb::put || verb::get == method)(req, ctx));;
}

} // namespace
