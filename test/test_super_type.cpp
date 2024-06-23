//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/web/core/super_type.hpp>
#include <boost/test/unit_test.hpp>
#include <type_traits>

namespace {

struct base {};
struct derrived : base {};
struct derrived2 : derrived {};
struct derrived3 : base {};

BOOST_AUTO_TEST_CASE(test_matcher_super_type)
{
    using namespace boost::web;
    static_assert(have_super_type_v<base>);
    static_assert(have_super_type_v<base, derrived>);
    static_assert(have_super_type_v<base, derrived, derrived2>);
    static_assert(!have_super_type_v<derrived, derrived3>);
    static_assert(!have_super_type_v<base, derrived, derrived3>);
    static_assert(!have_super_type_v<derrived, base, derrived3>);
    static_assert(!have_super_type_v<base, derrived, derrived2, derrived3>);
    static_assert(!have_super_type_v<base, derrived2, derrived3>);

    static_assert(std::is_same_v<super_type_t<base>, base>);
    static_assert(std::is_same_v<super_type_t<base, derrived>, derrived>);
    static_assert(std::is_same_v<super_type_t<derrived, base>, derrived>);
    static_assert(std::is_same_v<super_type_t<int, float>, float>);
    static_assert(std::is_same_v<super_type_t<int, char>, int>);

    static_assert(std::is_same_v<super_type_t<base, derrived, derrived>, derrived>);
    static_assert(std::is_same_v<super_type_t<base, derrived, derrived>, derrived>);
    static_assert(std::is_same_v<super_type_t<base, base, derrived, derrived>, derrived>);
}

} // namespace
