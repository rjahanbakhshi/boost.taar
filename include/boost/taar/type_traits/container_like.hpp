//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_TYPE_TRAITS_CONTAINER_LIKE_HPP
#define BOOST_TAAR_TYPE_TRAITS_CONTAINER_LIKE_HPP

#include <ranges>

namespace boost::taar::type_traits {

/// Concept: a `std::ranges::range` with a nested `value_type`.
template<typename C>
concept container_like =
    std::ranges::range<C> &&
    requires(C c)
    {
        typename C::value_type;
    };

/// Concept: a container that exposes `push_back(T)`.
template<typename C, typename T>
concept push_backable_container =
    requires(C c, T&& t)
    {
        c.push_back(std::forward<T>(t));
    };

/// Concept: a container that exposes `insert(T)`.
template<typename C, typename T>
concept insertable_container =
    requires(C c, T&& t)
    {
        c.insert(std::forward<T>(t));
    };

} // boost::taar::type_traits

#endif // BOOST_TAAR_TYPE_TRAITS_CONTAINER_LIKE_HPP
