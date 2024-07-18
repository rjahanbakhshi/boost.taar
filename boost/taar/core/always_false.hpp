//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_ALWAYS_FALSE_HPP
#define BOOST_TAAR_CORE_ALWAYS_FALSE_HPP

#include <type_traits>

namespace boost::taar {

// A dependent type always evaluating to false_type. This is necessary due to
// a limitation in C++20 which is addressed with P2593 and accepted in C++23
template <class...> constexpr static std::false_type always_false {};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_ALWAYS_FALSE_HPP
