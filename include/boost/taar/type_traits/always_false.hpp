//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_TYPE_TRAITS_ALWAYS_FALSE_HPP
#define BOOST_TAAR_TYPE_TRAITS_ALWAYS_FALSE_HPP

#include <type_traits>

namespace boost::taar::type_traits {

/** A dependent `std::false_type` for use in `static_assert`.

    Used to write `static_assert(always_false<T...>, "...")` inside a
    template branch that should never be instantiated. The dependency on
    @a T... defers evaluation to the template instantiation site, which
    avoids triggering the assertion when the surrounding template is only
    declared but never instantiated.
*/
template <typename...>
inline constexpr std::false_type always_false {};

} // namespace boost::taar::type_traits

#endif // BOOST_TAAR_TYPE_TRAITS_ALWAYS_FALSE_HPP
