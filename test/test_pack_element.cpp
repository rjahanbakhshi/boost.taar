//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/pack_element.hpp>
#include <boost/test/unit_test.hpp>

namespace {

using boost::taar::pack_element_t;

BOOST_AUTO_TEST_CASE(test_pack_element)
{
    static_assert(std::is_same_v<pack_element_t<0, int, double, char>, int>, "Error!");
    static_assert(std::is_same_v<pack_element_t<1, int, double, char>, double>, "Error!");
    static_assert(std::is_same_v<pack_element_t<2, int, double, char>, char>, "Error!");
}

} // namespace
