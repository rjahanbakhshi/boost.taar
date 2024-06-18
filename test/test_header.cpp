//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/web/matcher/header.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>
#include <boost/test/unit_test.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_matcher_header)
{
    namespace http = boost::beast::http;
    using namespace boost::web::matcher;
    using http::field;

    http::request<http::string_body> req{http::verb::get, "/a/b", 10};
    req.set(field::host, "example.com");
    req.set(field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set("example", "Example Value");
    context ctx;

    BOOST_TEST((header(field::host) == "example.com")(req, ctx));
    BOOST_TEST((header("host") == "example.com")(req, ctx));
    BOOST_TEST((header(field::user_agent) == BOOST_BEAST_VERSION_STRING)(req, ctx));
    BOOST_TEST((header("example") == "Example Value")(req, ctx));
    BOOST_TEST(!(header("example") == "example value")(req, ctx));
}

} // namespace
