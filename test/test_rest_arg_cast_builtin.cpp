//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/system/system_error.hpp>
#include <boost/taar/handler/rest_arg_cast.hpp>
#include <boost/json/value_from.hpp>
#include <boost/test/unit_test.hpp>

namespace {

using boost::taar::handler::rest_arg_castable;
using boost::taar::handler::rest_arg_cast;

struct jsonable
{
    int i;
    std::string s;
    friend bool operator==(const jsonable& lhs, const jsonable& rhs)
    {
        return lhs.i == rhs.i && lhs.s == rhs.s;
    }
};

jsonable tag_invoke(const boost::json::value_to_tag<jsonable>&, const boost::json::value& jv)
{
    auto const& obj = jv.as_object();
    return jsonable {
        .i = boost::json::value_to<int>(obj.at("i")),
        .s = boost::json::value_to<std::string>(obj.at("s")),
    };
}
void tag_invoke(
    boost::json::value_from_tag const&,
    boost::json::value& jv,
    jsonable const& obj)
{
    // Store the IP address as a 4-element array of octets
    jv = { {{"i", obj.i}, {"s", obj.s}} };
}

BOOST_AUTO_TEST_CASE(test_rest_arg_cast_builtin_traits)
{
    static_assert(rest_arg_castable<int, int>, "Failed!");
    static_assert(rest_arg_castable<std::string_view, bool>, "Failed!");
    static_assert(rest_arg_castable<std::string, bool>, "Failed!");
    static_assert(rest_arg_castable<std::string, int>, "Failed!");
    static_assert(rest_arg_castable<std::string, long>, "Failed!");
    static_assert(rest_arg_castable<std::string, short>, "Failed!");
    static_assert(rest_arg_castable<std::string, double>, "Failed!");
    static_assert(rest_arg_castable<std::string, float>, "Failed!");
    static_assert(rest_arg_castable<std::string_view, int>, "Failed!");
    static_assert(rest_arg_castable<std::string_view, long>, "Failed!");
    static_assert(rest_arg_castable<std::string_view, float>, "Failed!");
    static_assert(rest_arg_castable<std::string_view, double>, "Failed!");
    static_assert(rest_arg_castable<char const*, bool>, "Failed!");
    static_assert(rest_arg_castable<char const*, int>, "Failed!");
    static_assert(rest_arg_castable<int, std::string>, "Failed!");
    static_assert(rest_arg_castable<float, std::string>, "Failed!");
    static_assert(rest_arg_castable<double, std::string>, "Failed!");
    static_assert(rest_arg_castable<long, std::string>, "Failed!");
    static_assert(rest_arg_castable<short, std::string>, "Failed!");
    static_assert(rest_arg_castable<unsigned int, std::string>, "Failed!");
    static_assert(rest_arg_castable<unsigned short, std::string>, "Failed!");
    static_assert(rest_arg_castable<unsigned long, std::string>, "Failed!");
    static_assert(!rest_arg_castable<int, std::string_view>, "Failed!");
    static_assert(!rest_arg_castable<int, char const*>, "Failed!");
    static_assert(rest_arg_castable<std::string, std::string_view>, "Failed!");
    static_assert(rest_arg_castable<float, int>, "Failed!");
    static_assert(rest_arg_castable<int, float>, "Failed!");
    static_assert(rest_arg_castable<double, float>, "Failed!");
    static_assert(rest_arg_castable<double, int>, "Failed!");
    static_assert(rest_arg_castable<bool, std::string>, "Failed!");
    static_assert(rest_arg_castable<bool, std::string_view>, "Failed!");
    static_assert(rest_arg_castable<bool, char const*>, "Failed!");
    static_assert(rest_arg_castable<boost::json::value, jsonable>, "Failed!");
    static_assert(rest_arg_castable<std::string&, char const*>, "Failed!");
}

BOOST_AUTO_TEST_CASE(test_rest_arg_cast_builtin)
{
    using namespace boost::taar::handler::detail;

    BOOST_TEST(rest_arg_cast<int>(13) == 13);
    BOOST_TEST(rest_arg_cast<int>("42") == 42);
    BOOST_TEST(rest_arg_cast<float>("3.14") == 3.14f);
    BOOST_TEST(rest_arg_cast<float>("3") == 3.0f);
    BOOST_REQUIRE_THROW(rest_arg_cast<int>("not a number"), boost::system::system_error);
    BOOST_REQUIRE_THROW(rest_arg_cast<int>("13.7"), boost::system::system_error);
    BOOST_TEST(rest_arg_cast<bool>("true"));
    BOOST_TEST(!rest_arg_cast<bool>("false"));
    BOOST_TEST(rest_arg_cast<std::string>(13) == "13");
    BOOST_TEST(rest_arg_cast<std::string>(42.13) == "42.13");
    BOOST_TEST(rest_arg_cast<std::string>(13.42f) == "13.42");
    BOOST_TEST(rest_arg_cast<int>(42.13) == 42);
    BOOST_TEST(rest_arg_cast<float>(13) == 13.0);
    BOOST_TEST(rest_arg_cast<int>(3.14f) == 3);
    BOOST_TEST(rest_arg_cast<double>(13) == 13.0);
    BOOST_TEST(rest_arg_cast<bool>(1) == true);
    BOOST_TEST(rest_arg_cast<std::string>(true) == "1");
    BOOST_TEST(rest_arg_cast<std::string_view>(false) == "0");
    BOOST_TEST(rest_arg_cast<char const*>(true) == std::string_view("1"));
    BOOST_TEST((rest_arg_cast<jsonable>(boost::json::value({{"i", 13}, {"s", "Hello"}})) == jsonable{13, "Hello"}));
}

} // namespace
