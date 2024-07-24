//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/callable_traits.hpp>
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

BOOST_AUTO_TEST_CASE(test_matcher_callable_traits)
{
    using namespace boost::taar;

    static_assert(callable_traits<decltype(&void_fn)>::args_count == 0, "Failed!");
    static_assert(std::is_same_v<callable_traits<decltype(&void_fn)>::result_type, void>, "Failed!");

    static_assert(callable_traits<decltype(void_fn)>::args_count == 0, "Failed!");
    static_assert(std::is_same_v<callable_traits<decltype(void_fn)>::result_type, void>, "Failed!");

    static_assert(callable_traits<decltype(&my_function)>::args_count == 3, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::signature_type,
        void(int, double&, const float&)>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::result_type,
        void>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::arg_type<0>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::arg_type<1>,
        double&>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::arg_type<2>,
        const float&>, "Failed!");

    static_assert(callable_traits<decltype(my_function)>::args_count == 3, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::signature_type,
        void(int, double&, const float&)>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::result_type,
        void>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::arg_type<0>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::arg_type<1>,
        double&>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::arg_type<2>,
        const float&>, "Failed!");

    functor f;
    static_assert(callable_traits<decltype(f)>::args_count == 3, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::signature_type,
        double(float, int, char)>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::result_type,
        double>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::arg_type<0>,
        float>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::arg_type<1>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::arg_type<2>,
        char>, "Failed!");

    functor_const fc;
    static_assert(callable_traits<decltype(fc)>::args_count == 3, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::signature_type,
        double(float, int, char)>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::result_type,
        double>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::arg_type<0>,
        float>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::arg_type<1>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::arg_type<2>,
        char>, "Failed!");

    auto l = [](char) {};
    static_assert(callable_traits<decltype(l)>::args_count == 1, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(l)>::signature_type,
        void(char)>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(l)>::result_type,
        void>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(l)>::arg_type<0>,
        char>, "Failed!");

    static_assert(callable_traits<decltype(&memfn_type::fn)>::args_count == 2, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&memfn_type::fn)>::signature_type,
        int(char, double)>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&memfn_type::fn)>::result_type,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&memfn_type::fn)>::arg_type<0>,
        char>, "Failed!");
    static_assert(std::is_same_v<
        callable_traits<decltype(&memfn_type::fn)>::arg_type<1>,
        double>, "Failed!");
}

BOOST_AUTO_TEST_CASE(test_matcher_callable_traits_aliases)
{
    using namespace boost::taar;

    static_assert(callable_args_count<decltype(my_function)> == 3, "Failed!");
    static_assert(std::is_same_v<
        callable_signature_type<decltype(my_function)>,
        void(int, double&, const float&)>, "Failed!");
    static_assert(std::is_same_v<
        callable_result_type<decltype(my_function)>,
        void>, "Failed!");
    static_assert(std::is_same_v<
        callable_arg_type<decltype(my_function), 0>,
        int>, "Failed!");
    static_assert(std::is_same_v<
        callable_arg_type<decltype(my_function), 1>,
        double&>, "Failed!");
    static_assert(std::is_same_v<
        callable_arg_type<decltype(my_function), 2>,
        const float&>, "Failed!");
}

} // namespace
