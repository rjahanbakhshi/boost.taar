//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_TYPE_TRAITS_CALLABLE_HPP
#define BOOST_TAAR_TYPE_TRAITS_CALLABLE_HPP

#include <boost/taar/type_traits/always_false.hpp>
#include <boost/taar/type_traits/has_call_operator.hpp>
#include <tuple>

namespace boost::taar::type_traits {

// Primary template for retrieving the nth argument type (unspecialized)
template <typename CallableType>
struct callable
{
    static_assert(type_traits::always_false<CallableType>, "Not a callble type!");
};

// Specialization for function pointers
template <typename ResultType, typename... ArgsType>
struct callable<ResultType(ArgsType...)>
{
    template <std::size_t N>
    using arg = typename std::tuple_element<N, std::tuple<ArgsType...>>::type;
    using result = ResultType;
    using signature = ResultType(ArgsType...);
    static constexpr auto args_count = sizeof...(ArgsType);
};

// Specialization for function pointers
template <typename ResultType, typename... ArgsType>
struct callable<ResultType(*)(ArgsType...)>
{
    template <std::size_t N>
    using arg = typename std::tuple_element<N, std::tuple<ArgsType...>>::type;
    using result = ResultType;
    using signature = ResultType(ArgsType...);
    static constexpr auto args_count = sizeof...(ArgsType);
};

// Specialization for member function pointers
template <typename ResultType, typename ObjectType, typename... ArgsType>
struct callable<ResultType(ObjectType::*)(ArgsType...)>
{
    template <std::size_t N>
    using arg = typename std::tuple_element<N, std::tuple<ArgsType...>>::type;
    using result = ResultType;
    using signature = ResultType(ArgsType...);
    static constexpr auto args_count = sizeof...(ArgsType);
};

// Specialization for const member function pointers
template <typename ResultType, typename ObjectType, typename... ArgsType>
struct callable<ResultType(ObjectType::*)(ArgsType...) const>
{
    template <std::size_t N>
    using arg = typename std::tuple_element<N, std::tuple<ArgsType...>>::type;
    using result = ResultType;
    using signature = ResultType(ArgsType...);
    static constexpr auto args_count = sizeof...(ArgsType);
};

// Specialization for functors and lambdas
template <has_call_operator CallableType>
struct callable<CallableType>
{
    template <std::size_t N>
    using arg = callable<decltype(&CallableType::operator())>::template arg<N>;
    using result = typename callable<decltype(&CallableType::operator())>::result;
    using signature = typename callable<decltype(&CallableType::operator())>::signature;
    static constexpr auto args_count = callable<decltype(&CallableType::operator())>::args_count;
};

template <typename CallableType, std::size_t N>
using callable_arg = typename callable<CallableType>::template arg<N>;

template <typename CallableType>
using callable_result = typename callable<CallableType>::result;

template <typename CallableType>
using callable_signature = typename callable<CallableType>::signature;

template <typename CallableType>
inline constexpr auto callable_args_count = callable<CallableType>::args_count;

} // namespace boost::taar::type_traits

#endif // BOOST_TAAR_TYPE_TRAITS_CALLABLE_HPP
