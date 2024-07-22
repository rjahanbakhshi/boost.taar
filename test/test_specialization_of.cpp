//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/specialization_of.hpp>
#include <boost/test/unit_test.hpp>

namespace {

// Class1
template <typename T, typename U>
struct class1 {};

template <typename T>
using ps_class1 = class1<T, float>;

using fs_class1 = ps_class1<int>;

// Alias1
template <typename T, typename U>
using alias1 = class1<T, U>;

using fs_alias1 = alias1<double, int>;

// Class2
template <int I, typename T>
struct class2 {};

template <typename T>
using ps_class2 = class2<13, T>;

using fs_class2 = ps_class2<float>;

BOOST_AUTO_TEST_CASE(test_specialization_of)
{
    using namespace boost::taar;
    static_assert(is_specialization_of_v<class1, fs_class1>);
    static_assert(specialization_of<class1, fs_class1>);

    //static_assert(is_specialization_of_v<ps_class1, fs_class1>);
    //static_assert(specialization_of<ps_class1, fs_class1>);

    //static_assert(is_specialization_of_v<alias1, fs_alias1>);
    //static_assert(specialization_of<alias1, fs_alias1>);

    //static_assert(is_specialization_of_v<class2, fs_class2>);
    //static_assert(specialization_of<class2, fs_class2>);

    //static_assert(is_specialization_of_v<ps_class2, fs_class2>);
    //static_assert(specialization_of<ps_class2, fs_class2>);
}

} // namespace
