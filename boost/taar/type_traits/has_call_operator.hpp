//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_TYPE_TRAITS_HAS_CALL_OPERATOR_HPP
#define BOOST_TAAR_TYPE_TRAITS_HAS_CALL_OPERATOR_HPP

namespace boost::taar::type_traits {

// Concept to check if a type has a callable operator()
template <typename T>
concept has_call_operator = requires(T t)
{
    &T::operator();
};

} // namespace boost::taar::type_traits

#endif // BOOST_TAAR_TYPE_TRAITS_HAS_CALL_OPERATOR_HPP
