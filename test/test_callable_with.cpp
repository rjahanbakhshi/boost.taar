//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/matcher/detail/callable_with.hpp>
#include <boost/test/unit_test.hpp>
#include <optional>

namespace {

struct request {};
struct context {};

bool free_function(
    const request& /*request*/,
    context& /*context*/)
{
    return true;
}

int free_function_extra(
    const request& /*request*/,
    context& /*context*/,
    int i = 0)
{
    return i;
}

std::optional<int> contextual_bool(
    const request& /*request*/,
    context& /*context*/)
{
    return {};
}

static constexpr auto lambda = [](
    const request& /*request*/,
    context& /*context*/)
{
    return true;
};

struct callable
{
    bool operator()(int i, float j)
    {
        return i == static_cast<int>(j);
    }

    int operator()(int i) const
    {
        return i;
    }
};

BOOST_AUTO_TEST_CASE(test_matcher_callable_with)
{
    using namespace boost::taar::matcher::detail;
    static_assert(callable_with<decltype(&free_function), bool, const request&, context&>, "Failed!");
    static_assert(callable_with<decltype(&free_function_extra), bool, const request&, context&, int>, "Failed!");
    static_assert(callable_with<decltype(&contextual_bool), bool, const request&, context&>, "Failed!");
    static_assert(callable_with<decltype(lambda), bool, const request&, context&>, "Failed!");
    static_assert(callable_with<callable, bool, int, float>, "Failed!");
    static_assert(callable_with<callable, bool, int, double>, "Failed!");
    static_assert(callable_with<callable, int, int>, "Failed!");
    static_assert(callable_with<callable, bool, int>, "Failed!");
    static_assert(callable_with<callable, bool, double>, "Failed!");
}

} // namespace
