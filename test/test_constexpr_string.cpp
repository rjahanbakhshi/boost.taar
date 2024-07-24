//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/constexpr_string.hpp>
#include <boost/test/unit_test.hpp>
#include <cstring>

namespace {

using boost::taar::constexpr_string;
using namespace boost::taar::literals;

template<constexpr_string Str>
struct s
{
    static constexpr auto str = Str;
};

BOOST_AUTO_TEST_CASE(test_constexpr_string)
{
    using Hi = s<"Hi">;
    using hI = s<"hI">;

    static_assert(Hi::str.size() == 2, "Failed!");
    static_assert(Hi::str.c_str()[0] == 'H', "Failed!");
    static_assert(Hi::str.c_str()[1] == 'i', "Failed!");

    static_assert(hI::str.size() == 2, "Failed!");
    static_assert(hI::str.c_str()[0] == 'h', "Failed!");
    static_assert(hI::str.c_str()[1] == 'I', "Failed!");

    static_assert(s<"Hoi">::str.size() == 3, "Failed!");
    static_assert(s<"Hello">::str.size() == 5, "Failed!");

    static_assert(s<"World">::str == constexpr_string{"World"}, "Failed!");
    static_assert(s<"World">::str != constexpr_string{"Hi"}, "Failed!");
    static_assert(s<"Blah">::str == "Blah"_cs, "Failed!");
    static_assert(s<"Blah">::str != "Hi"_cs, "Failed!");

    BOOST_TEST(std::strcmp(s<"Hi">::str.c_str(), "Hi") == 0);
    BOOST_TEST(std::strcmp(s<"hellO">::str.c_str(), "hellO") == 0);
    BOOST_TEST(std::strcmp(s<"halo">::str.c_str(), "halo") == 0);
    BOOST_TEST(std::strcmp(s<"hola">::str.c_str(), "hola") == 0);

    BOOST_TEST(s<"World">::str.string() == "World");
    BOOST_TEST(static_cast<std::string>(s<"world">::str) == "world");
}

} // namespace
