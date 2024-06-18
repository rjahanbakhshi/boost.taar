//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/web/matcher/detail/super_type.hpp>
#include <boost/test/unit_test.hpp>
#include <type_traits>

namespace {

struct base {};
struct derrived : base {};

BOOST_AUTO_TEST_CASE(test_matcher_super_type)
{
    using namespace boost::web::matcher::detail;
    static_assert(std::is_same_v<super_type_t<base, derrived>, derrived>);
    static_assert(std::is_same_v<super_type_t<derrived, base>, derrived>);
    static_assert(std::is_same_v<super_type_t<int, float>, float>);
    static_assert(std::is_same_v<super_type_t<int, char>, int>);
}

} // namespace
