//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_TYPE_TRAITS_NUMERIC_TYPE_HPP
#define BOOST_TAAR_TYPE_TRAITS_NUMERIC_TYPE_HPP

#include <type_traits>

namespace boost::taar::type_traits {

template <typename T>
concept numeric_type =
    !std::is_same_v<T, bool> && (
        std::is_integral_v<T> ||
        std::is_floating_point_v<T>);

} // boost::taar::type_traits

#endif // BOOST_TAAR_TYPE_TRAITS_NUMERIC_TYPE_HPP
