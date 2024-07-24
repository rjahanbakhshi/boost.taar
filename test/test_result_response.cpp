//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/result_response.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/test/unit_test.hpp>

namespace {

namespace http = boost::beast::http;
using boost::taar::result_response;
using boost::taar::response_from;
using boost::taar::has_response_from;

struct custom_type
{
    int i;
};

auto tag_invoke(boost::taar::response_from_tag, const custom_type& c)
{
    return boost::taar::response_from(c.i);
}

BOOST_AUTO_TEST_CASE(test_result_response)
{
    result_response r0{};
    static_assert(has_response_from<decltype(r0)>);
    BOOST_TEST(response_from(r0).result() == http::status::ok);
    BOOST_TEST(response_from(r0).count(http::field::content_type) == 0);

    result_response<std::string> r1 {"blah"};
    static_assert(has_response_from<decltype(r1)>);
    BOOST_TEST(response_from(r1).body() == "blah");
    BOOST_TEST(response_from(r1).result() == http::status::ok);
    BOOST_TEST(response_from(r1).at(http::field::content_type) == "text/plain");

    result_response r2 {std::string{"test2"}};
    static_assert(has_response_from<decltype(r2)>);
    BOOST_TEST(response_from(r2).body() == "test2");
    BOOST_TEST(response_from(r2).result() == http::status::ok);
    BOOST_TEST(response_from(r2).at(http::field::content_type) == "text/plain");

    auto r3 = result_response{"3test"}
        .set_header(http::field::content_type, "custom3")
        .set_result(http::status::internal_server_error)
        .set_version(10);
    static_assert(has_response_from<decltype(r3)>);
    BOOST_TEST(response_from(r3).body() == "3test");
    BOOST_TEST(response_from(r3).result() == http::status::internal_server_error);
    BOOST_TEST(response_from(r3).at(http::field::content_type) == "custom3");
    BOOST_TEST(response_from(r3).version() == 10);

    auto r4 = result_response{custom_type{13}}
        .set_result(http::status::internal_server_error);
    BOOST_TEST(response_from(r4).body() == "13");
    BOOST_TEST(response_from(r4).result() == http::status::internal_server_error);
}

} // namespace
