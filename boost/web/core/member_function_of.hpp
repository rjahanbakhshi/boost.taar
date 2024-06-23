//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_CORE_MEMBER_FUNCTION_OF_HPP
#define BOOST_WEB_CORE_MEMBER_FUNCTION_OF_HPP

#include <type_traits>

namespace boost::web {
namespace detail {

template<typename T, typename U>
struct is_member_function_of_helper : std::false_type {};

template<typename T, typename U>
struct is_member_function_of_helper<T U::*, U> : std::is_function<T> {};

} // namespace detail

template<typename T, typename U>
struct is_member_function_of
    : detail::is_member_function_of_helper<
        std::remove_cvref_t<T>,
        std::remove_cvref_t<std::remove_pointer_t<U>>>
{};

template<typename T, typename U>
inline constexpr bool is_member_function_of_v = is_member_function_of<T, U>::value;

template<typename T, typename U>
concept member_function_of = is_member_function_of_v<T, U>;

} // namespace boost::web

#endif // BOOST_WEB_CORE_ALWAYS_FALSE_HPP
