//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_TYPE_TRAITS_SUPER_TYPE_HPP
#define BOOST_TAAR_TYPE_TRAITS_SUPER_TYPE_HPP

#include <concepts>
#include <type_traits>

namespace boost::taar::type_traits {

/** Trait: true if @a T... admit a common "super" type.

    A super type exists if for every pair of types in @a T... one is a base
    of the other or they have a `std::common_type`. The result type can be
    obtained with @ref super_type_t.

    Used by the @ref boost::taar::matcher::operand "matcher operand"
    composition operators to find the request type that two matchers can
    both accept.
*/
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

/// Convenience variable template for @ref have_super_type.
template <typename... T>
inline constexpr bool have_super_type_v = have_super_type<T...>::value;

/** Trait: the super type of @a T....

    Computes the "most derived" type all of @a T... can be converted to —
    a base if one is a base of the others, otherwise `std::common_type`.
    Use @ref super_type_t for direct access.
*/
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

/// Convenience alias for `super_type<T...>::type`.
template <typename... T>
using super_type_t = typename super_type<T...>::type;

} // namespace boost::taar::type_traits

#endif // BOOST_TAAR_TYPE_TRAITS_SUPER_TYPE_HPP
