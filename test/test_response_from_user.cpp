//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/response_from.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/test/unit_test.hpp>

namespace {

using namespace std::literals;
namespace http = boost::beast::http;
using boost::taar::response_from;
using boost::taar::has_response_from;

struct custom_type
{
    int i;
};

auto tag_invoke(boost::taar::response_from_tag<custom_type>, const custom_type& c)
{
    return boost::taar::response_from(c.i);
}

BOOST_AUTO_TEST_CASE(test_response_from_user)
{
    custom_type c1 {13};
    BOOST_TEST(response_from(c1).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from(c1).result() == http::status::ok);
    BOOST_TEST(response_from(c1).body() == "13");
}

} // namespace
