//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_TYPE_TRAITS_STRING_LIKE_HPP
#define BOOST_TAAR_TYPE_TRAITS_STRING_LIKE_HPP

#include <string>
#include <type_traits>

namespace boost::taar::type_traits {

template <typename T>
using is_string_like = std::is_constructible<std::string, T>;

template <typename T>
inline constexpr bool is_string_like_v = is_string_like<T>::value;

template <typename T>
concept string_like = is_string_like_v<T>;

} // boost::taar::type_traits

#endif // BOOST_TAAR_TYPE_TRAITS_STRING_LIKE_HPP
