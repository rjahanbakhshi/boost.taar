//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CALLABLE_TRAITS_HPP
#define BOOST_TAAR_CORE_CALLABLE_TRAITS_HPP

#include <boost/taar/core/always_false.hpp>
#include <tuple>

namespace boost::taar {

// Concept to check if a type has a callable operator()
template <typename T>
concept has_call_operator = requires(T t)
{
    &T::operator();
};

// Primary template for retrieving the nth argument type (unspecialized)
template <typename CallableType>
struct callable_traits
{
    static_assert(always_false<CallableType>, "Not a callble type!");
};

// Specialization for function pointers
template <typename ResultType, typename... ArgsType>
struct callable_traits<ResultType(ArgsType...)>
{
    template <std::size_t N>
    using arg_type = typename std::tuple_element<N, std::tuple<ArgsType...>>::type;
    using result_type = ResultType;
    using signature_type = ResultType(ArgsType...);
    static constexpr auto args_count = sizeof...(ArgsType);
};

// Specialization for function pointers
template <typename ResultType, typename... ArgsType>
struct callable_traits<ResultType(*)(ArgsType...)>
{
    template <std::size_t N>
    using arg_type = typename std::tuple_element<N, std::tuple<ArgsType...>>::type;
    using result_type = ResultType;
    using signature_type = ResultType(ArgsType...);
    static constexpr auto args_count = sizeof...(ArgsType);
};

// Specialization for member function pointers
template <typename ResultType, typename ObjectType, typename... ArgsType>
struct callable_traits<ResultType(ObjectType::*)(ArgsType...)>
{
    template <std::size_t N>
    using arg_type = typename std::tuple_element<N, std::tuple<ArgsType...>>::type;
    using result_type = ResultType;
    using signature_type = ResultType(ArgsType...);
    static constexpr auto args_count = sizeof...(ArgsType);
};

// Specialization for const member function pointers
template <typename ResultType, typename ObjectType, typename... ArgsType>
struct callable_traits<ResultType(ObjectType::*)(ArgsType...) const>
{
    template <std::size_t N>
    using arg_type = typename std::tuple_element<N, std::tuple<ArgsType...>>::type;
    using result_type = ResultType;
    using signature_type = ResultType(ArgsType...);
    static constexpr auto args_count = sizeof...(ArgsType);
};

// Specialization for functors and lambdas
template <has_call_operator CallableType>
struct callable_traits<CallableType>
{
    template <std::size_t N>
    using arg_type = callable_traits<decltype(&CallableType::operator())>::template arg_type<N>;
    using result_type = typename callable_traits<decltype(&CallableType::operator())>::result_type;
    using signature_type = typename callable_traits<decltype(&CallableType::operator())>::signature_type;
    static constexpr auto args_count = callable_traits<decltype(&CallableType::operator())>::args_count;
};

template <typename CallableType, std::size_t N>
using callable_arg_type = typename callable_traits<CallableType>::template arg_type<N>;

template <typename CallableType>
using callable_result_type = typename callable_traits<CallableType>::result_type;

template <typename CallableType>
using callable_signature_type = typename callable_traits<CallableType>::signature_type;

template <typename CallableType>
static constexpr auto callable_args_count = callable_traits<CallableType>::args_count;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CALLABLE_TRAITS_HPP
