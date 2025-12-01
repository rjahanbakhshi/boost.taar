//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/type_traits/callable.hpp>
#include <boost/test/unit_test.hpp>

namespace {

void void_fn();
void my_function(int, double&, const float&) {}

struct functor
{
    double operator()(float, int, char) { return {}; }
};

struct functor_const
{
    double operator()(float, int, char) { return {}; }
};

struct memfn_type
{
    int fn(char, double) { return {}; }
};

BOOST_AUTO_TEST_CASE(test_type_traits_callable)
{
    using namespace boost::taar::type_traits;

    static_assert(callable<decltype(&void_fn)>::args_count == 0, "Failed!");
    static_assert(std::is_same_v<callable<decltype(&void_fn)>::result, void>, "Failed!");

    static_assert(callable<decltype(void_fn)>::args_count == 0, "Failed!");
    static_assert(std::is_same_v<callable<decltype(void_fn)>::result, void>, "Failed!");

    static_assert(callable<decltype(&my_function)>::args_count == 3, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&my_function)>::signature,
        void(int, double&, const float&)>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&my_function)>::result,
        void>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&my_function)>::arg<0>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&my_function)>::arg<1>,
        double&>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&my_function)>::arg<2>,
        const float&>, "Failed!");

    static_assert(callable<decltype(my_function)>::args_count == 3, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(my_function)>::signature,
        void(int, double&, const float&)>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(my_function)>::result,
        void>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(my_function)>::arg<0>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(my_function)>::arg<1>,
        double&>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(my_function)>::arg<2>,
        const float&>, "Failed!");

    functor f;
    static_assert(callable<decltype(f)>::args_count == 3, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(f)>::signature,
        double(float, int, char)>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(f)>::result,
        double>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(f)>::arg<0>,
        float>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(f)>::arg<1>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(f)>::arg<2>,
        char>, "Failed!");

    functor_const fc;
    static_assert(callable<decltype(fc)>::args_count == 3, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(fc)>::signature,
        double(float, int, char)>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(fc)>::result,
        double>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(fc)>::arg<0>,
        float>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(fc)>::arg<1>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(fc)>::arg<2>,
        char>, "Failed!");

    auto l = [](char) {};
    static_assert(callable<decltype(l)>::args_count == 1, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(l)>::signature,
        void(char)>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(l)>::result,
        void>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(l)>::arg<0>,
        char>, "Failed!");

    static_assert(callable<decltype(&memfn_type::fn)>::args_count == 2, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&memfn_type::fn)>::signature,
        int(char, double)>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&memfn_type::fn)>::result,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&memfn_type::fn)>::arg<0>,
        char>, "Failed!");
    static_assert(std::is_same_v<
        callable<decltype(&memfn_type::fn)>::arg<1>,
        double>, "Failed!");
}

BOOST_AUTO_TEST_CASE(test_type_traits_callable_aliases)
{
    using namespace boost::taar::type_traits;

    static_assert(callable_args_count<decltype(my_function)> == 3, "Failed!");
    static_assert(std::is_same_v<
        callable_signature<decltype(my_function)>,
        void(int, double&, const float&)>, "Failed!");
    static_assert(std::is_same_v<
        callable_result<decltype(my_function)>,
        void>, "Failed!");
    static_assert(std::is_same_v<
        callable_arg<decltype(my_function), 0>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable_arg<decltype(my_function), 1>,
        double&>, "Failed!");
    static_assert(std::is_same_v<
        callable_arg<decltype(my_function), 2>,
        const float&>, "Failed!");
}

} // namespace
