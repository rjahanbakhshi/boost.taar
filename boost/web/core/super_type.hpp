//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_MATCHER_SUPER_TYPE_HPP
#define BOOST_WEB_MATCHER_SUPER_TYPE_HPP

#include <concepts>
#include <type_traits>

namespace boost::web {

// Type traits determining whether a group of types have a super type.
template <typename... T>
struct have_super_type;

template <typename T>
struct have_super_type<T>
{
    static constexpr bool value = true;
};

template <typename T, typename U>
struct have_super_type<T, U>
{
    static constexpr bool value =
        std::is_base_of_v<T, U> ||
        std::is_base_of_v<U, T> ||
        std::common_with<T, U>;
};

template <typename T, typename U, typename... Rest>
struct have_super_type<T, U, Rest...>
{
    constexpr static bool value =
        have_super_type<T, U>::value &&
        have_super_type<T, Rest...>::value &&
        have_super_type<U, Rest...>::value;
};

template <typename... T>
constexpr inline bool have_super_type_v = have_super_type<T...>::value;

// Type trait evaluating to the super type between two types
template <typename... T>
struct super_type;

template <typename T>
struct super_type<T>
{
    using type = T;
};

template <typename T, typename U>
struct super_type<T, U>
{
    using type = std::conditional_t<
        std::is_base_of_v<T, U>,
        U,
        std::conditional_t<
            std::is_base_of_v<U, T>,
            T,
            std::common_type_t<T, U>
        >
    >;
};

template <typename T, typename U, typename... Rest>
struct super_type<T, U, Rest...>
{
    using type = typename super_type<typename super_type<T, U>::type, Rest...>::type;
};

template <typename... T>
using super_type_t = typename super_type<T...>::type;

} // namespace boost::web

#endif // BOOST_WEB_MATCHER_SUPER_TYPE_HPP
