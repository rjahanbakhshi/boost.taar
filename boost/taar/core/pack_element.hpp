//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_PACK_ELEMENT_HPP
#define BOOST_TAAR_CORE_PACK_ELEMENT_HPP

#include <cstddef>

namespace boost::taar {

template <std::size_t I, typename... Ts>
struct pack_element;

template <std::size_t I, typename T, typename... Ts>
struct pack_element<I, T, Ts...> : pack_element<I - 1, Ts...> {};

template <typename T, typename... Ts>
struct pack_element<0, T, Ts...>
{
    using type = T;
};

template <std::size_t I, typename... Ts>
using pack_element_t = typename pack_element<I, Ts...>::type;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_PACK_ELEMENT_HPP
