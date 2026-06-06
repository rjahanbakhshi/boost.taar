//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "boost/taar/core/cookies.hpp"
#include <boost/taar/matcher/cookie.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>
#include <boost/test/unit_test.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_matcher_cookie)
{
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using namespace taar::matcher;
    using http::field;

    http::request<http::string_body> req{http::verb::get, "/a/b", 10};
    taar::cookies parsed_cookies{"name1=value1; blah=42;nr=13"};
    context ctx;

    BOOST_TEST((cookie("name1") == "value1")(req, ctx, {}, parsed_cookies));
    BOOST_TEST((cookie("blah") == "42")(req, ctx, {}, parsed_cookies));
    BOOST_TEST((exist(cookie("nr")))(req, ctx, {}, parsed_cookies));
    BOOST_TEST((cookie("nr") == "13")(req, ctx, {}, parsed_cookies));
    BOOST_TEST((!exist(cookie("not_exists")))(req, ctx, {}, parsed_cookies));
}

} // namespace
