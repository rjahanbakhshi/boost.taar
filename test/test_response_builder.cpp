//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "to_response.h"
#include <boost/taar/core/response_builder.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/test/unit_test.hpp>

namespace {

namespace http = boost::beast::http;
using boost::taar::response_builder;
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

BOOST_AUTO_TEST_CASE(test_result_response)
{
    static_assert(has_response_from<decltype(response_builder{})>);
    BOOST_TEST(to_response<http::empty_body>(response_from(response_builder{})).result() == http::status::ok);
    BOOST_TEST(to_response<http::empty_body>(response_from(response_builder{})).count(http::field::content_type) == 0);

    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{std::string{"test2"}})).body() == "test2");
    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{std::string{"test2"}})).result() == http::status::ok);
    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{std::string{"test2"}})).at(http::field::content_type) == "text/plain");

    static_assert(has_response_from<decltype(response_builder{"3test"})>);
    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{"3test"})).body() == "3test");
    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{"3test"}.set_status(http::status::internal_server_error))).result() == http::status::internal_server_error);
    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{"3test"}.set_header(http::field::content_type, "custom3"))).at(http::field::content_type) == "custom3");
    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{"3test"}.set_version(10))).version() == 10);

    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{custom_type{13}})).body() == "13");
    BOOST_TEST(to_response<http::string_body>(response_from(response_builder{custom_type{13}}.set_status(http::status::internal_server_error))).result() == http::status::internal_server_error);
}

} // namespace
