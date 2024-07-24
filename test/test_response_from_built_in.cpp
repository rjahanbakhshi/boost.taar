//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/response_from.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>

namespace {

using namespace std::literals;
namespace http = boost::beast::http;
using boost::taar::response_from;
using boost::taar::response_from_t;
using boost::taar::has_response_from;

BOOST_AUTO_TEST_CASE(test_response_from_built_in)
{
    static_assert(has_response_from<>, "Failed!");
    static_assert(has_response_from<char>, "Failed!");
    static_assert(has_response_from<unsigned char>, "Failed!");
    static_assert(has_response_from<short>, "Failed!");
    static_assert(has_response_from<unsigned short>, "Failed!");
    static_assert(has_response_from<int>, "Failed!");
    static_assert(has_response_from<unsigned int>, "Failed!");
    static_assert(has_response_from<long>, "Failed!");
    static_assert(has_response_from<unsigned long>, "Failed!");
    static_assert(has_response_from<long long>, "Failed!");
    static_assert(has_response_from<unsigned long long>, "Failed!");
    static_assert(has_response_from<float>, "Failed!");
    static_assert(has_response_from<double>, "Failed!");
    static_assert(has_response_from<long double>, "Failed!");
    static_assert(has_response_from<std::string>, "Failed!");
    static_assert(has_response_from<std::string_view>, "Failed!");
    static_assert(has_response_from<char const*>, "Failed!");
    static_assert(has_response_from<const int>, "Failed!");
    static_assert(has_response_from<int&>, "Failed!");
    static_assert(has_response_from<const int&>, "Failed!");

    static_assert(std::is_same_v<decltype(response_from()), response_from_t<>>);
    static_assert(!std::is_same_v<decltype(response_from(13.42)), response_from_t<>>);
    static_assert(std::is_same_v<decltype(response_from('a')), response_from_t<char>>);
    static_assert(std::is_same_v<decltype(response_from(13)), response_from_t<int>>);
    static_assert(std::is_same_v<decltype(response_from(13)), response_from_t<int&>>);
    static_assert(std::is_same_v<decltype(response_from(13)), response_from_t<const int&>>);

    BOOST_TEST(response_from().count(http::field::content_type) == 0);
    BOOST_TEST(response_from().result() == http::status::ok);

    BOOST_TEST(response_from('a').at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from().result() == http::status::ok);
    BOOST_TEST(response_from('a').body() == "a");

    BOOST_TEST(response_from(13).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from(13).result() == http::status::ok);
    BOOST_TEST(response_from(13).body() == "13");

    BOOST_TEST(response_from(42).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from(42).result() == http::status::ok);
    BOOST_TEST(response_from(42).body() == "42");

    BOOST_TEST(response_from(13.42).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from(13.42).result() == http::status::ok);
    BOOST_TEST(response_from(13.42).body() == "13.42");

    BOOST_TEST(response_from("Blah").at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from("Blah").result() == http::status::ok);
    BOOST_TEST(response_from("Blah").body() == "Blah");

    BOOST_TEST(response_from("Blah"s).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from("Blah"s).result() == http::status::ok);
    BOOST_TEST(response_from("Blah"s).body() == "Blah");

    BOOST_TEST(response_from("Blah"sv).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from("Blah"sv).result() == http::status::ok);
    BOOST_TEST(response_from("Blah"sv).body() == "Blah");

    int i1 = 13;
    BOOST_TEST(response_from(i1).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from(i1).result() == http::status::ok);
    BOOST_TEST(response_from(i1).body() == "13");

    const int i2 = 14;
    BOOST_TEST(response_from(i2).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from(i2).result() == http::status::ok);
    BOOST_TEST(response_from(i2).body() == "14");

    int i3 = 15;
    BOOST_TEST(response_from(std::move(i3)).at(http::field::content_type) == "text/plain");
    BOOST_TEST(response_from(std::move(i3)).result() == http::status::ok);
    BOOST_TEST(response_from(std::move(i3)).body() == "15");
}

} // namespace
