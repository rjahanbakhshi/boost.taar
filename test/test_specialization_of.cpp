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

template <typename T, typename U>
struct template_class {};

using specialization_class = template_class<int, float>;

BOOST_AUTO_TEST_CASE(test_specialization_of)
{
    using namespace boost::taar;
    static_assert(is_specialization_of_v<template_class, specialization_class>);
    static_assert(specialization_of<template_class, specialization_class>);
}

} // namespace
