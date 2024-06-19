//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/web/core/callable_traits.hpp>
#include <boost/test/unit_test.hpp>

namespace {

void void_fn();
void my_function(int, double, float) {}

struct functor {
    double operator()(float, int, char) { return {}; }
};

struct functor_const {
    double operator()(float, int, char) { return {}; }
};

BOOST_AUTO_TEST_CASE(test_matcher_callable_traits)
{
    using namespace boost::web;

    static_assert(callable_traits<decltype(&void_fn)>::args_count == 0);
    static_assert(std::is_same_v<callable_traits<decltype(&void_fn)>::result_type, void>);

    static_assert(callable_traits<decltype(void_fn)>::args_count == 0);
    static_assert(std::is_same_v<callable_traits<decltype(void_fn)>::result_type, void>);

    static_assert(callable_traits<decltype(&my_function)>::args_count == 3);
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::signature_type,
        void(int, double, float)>);
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::result_type,
        void>);
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::arg_type<0>,
        int>);
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::arg_type<1>,
        double>);
    static_assert(std::is_same_v<
        callable_traits<decltype(&my_function)>::arg_type<2>,
        float>);

    static_assert(callable_traits<decltype(my_function)>::args_count == 3);
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::signature_type,
        void(int, double, float)>);
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::result_type,
        void>);
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::arg_type<0>,
        int>);
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::arg_type<1>,
        double>);
    static_assert(std::is_same_v<
        callable_traits<decltype(my_function)>::arg_type<2>,
        float>);

    functor f;
    static_assert(callable_traits<decltype(f)>::args_count == 3);
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::signature_type,
        double(float, int, char)>);
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::result_type,
        double>);
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::arg_type<0>,
        float>);
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::arg_type<1>,
        int>);
    static_assert(std::is_same_v<
        callable_traits<decltype(f)>::arg_type<2>,
        char>);

    functor_const fc;
    static_assert(callable_traits<decltype(fc)>::args_count == 3);
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::signature_type,
        double(float, int, char)>);
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::result_type,
        double>);
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::arg_type<0>,
        float>);
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::arg_type<1>,
        int>);
    static_assert(std::is_same_v<
        callable_traits<decltype(fc)>::arg_type<2>,
        char>);

    auto l = [](char) {};
    static_assert(callable_traits<decltype(l)>::args_count == 1);
    static_assert(std::is_same_v<
        callable_traits<decltype(l)>::signature_type,
        void(char)>);
    static_assert(std::is_same_v<
        callable_traits<decltype(l)>::result_type,
        void>);
    static_assert(std::is_same_v<
        callable_traits<decltype(l)>::arg_type<0>,
        char>);
}

BOOST_AUTO_TEST_CASE(test_matcher_callable_traits_aliases)
{
    using namespace boost::web;

    static_assert(callable_args_count<decltype(my_function)> == 3);
    static_assert(std::is_same_v<
        callable_signature_type<decltype(my_function)>,
        void(int, double, float)>);
    static_assert(std::is_same_v<
        callable_result_type<decltype(my_function)>,
        void>);
    static_assert(std::is_same_v<
        callable_arg_type<decltype(my_function), 0>,
        int>);
    static_assert(std::is_same_v<
        callable_arg_type<decltype(my_function), 1>,
        double>);
    static_assert(std::is_same_v<
        callable_arg_type<decltype(my_function), 2>,
        float>);
}

} // namespace
