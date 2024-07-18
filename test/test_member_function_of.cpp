//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/member_function_of.hpp>
#include <boost/test/unit_test.hpp>

namespace {

struct s1
{
    bool fn1(int i, float j) { return {}; }
};

struct s2
{
    bool fn1(int i, float j) { return {}; }
};

BOOST_AUTO_TEST_CASE(test_member_function_of_with)
{
    using namespace boost::taar;
    static_assert(member_function_of<decltype(&s1::fn1), s1>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 const>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 volatile>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 const volatile>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 const&>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 volatile&>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 const volatile&>);
    static_assert(member_function_of<decltype(&s1::fn1), s1&>);

    static_assert(member_function_of<decltype(&s1::fn1) const, s1>);
    static_assert(member_function_of<decltype(&s1::fn1) volatile, s1>);
    static_assert(member_function_of<decltype(&s1::fn1) const volatile, s1>);
    static_assert(member_function_of<decltype(&s1::fn1) const&, s1>);
    static_assert(member_function_of<decltype(&s1::fn1) volatile&, s1>);
    static_assert(member_function_of<decltype(&s1::fn1) const volatile&, s1>);
    static_assert(member_function_of<decltype(&s1::fn1)&, s1>);

    static_assert(member_function_of<decltype(&s1::fn1), s1*>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 const*>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 volatile*>);
    static_assert(member_function_of<decltype(&s1::fn1), s1 const volatile*>);

    static_assert(!member_function_of<decltype(&s2::fn1), s1>);
    static_assert(!member_function_of<decltype(&s1::fn1), s2>);
}

} // namespace
