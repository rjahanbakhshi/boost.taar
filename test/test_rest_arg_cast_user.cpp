//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "boost/taar/handler/rest_arg_cast_tag.hpp"
#include <boost/system/system_error.hpp>
#include <boost/taar/handler/rest_arg_cast.hpp>
#include <boost/json/value_from.hpp>
#include <boost/test/unit_test.hpp>

namespace {

using boost::taar::handler::rest_arg_cast_tag;
using boost::taar::handler::rest_arg_castable;
using boost::taar::handler::rest_arg_cast;

struct s1
{
    int i;
    friend bool operator==(const s1& lhs, const s1& rhs)
    {
        return lhs.i == rhs.i;
    }
};

s1 tag_invoke(rest_arg_cast_tag<s1>, const std::string& from)
{
    return s1 { .i = std::atoi(from.c_str())};
}

BOOST_AUTO_TEST_CASE(test_rest_arg_cast_user_traits)
{
    static_assert(rest_arg_castable<std::string, s1>, "Failed!");
}

BOOST_AUTO_TEST_CASE(test_rest_arg_cast_user)
{
    using namespace boost::taar::handler::detail;

    BOOST_TEST((rest_arg_cast<s1>("13") == s1{ .i = 13 }));
}

} // namespace
