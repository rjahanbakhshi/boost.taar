//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_IS_ASYNC_GENERATOR_HPP
#define BOOST_TAAR_CORE_IS_ASYNC_GENERATOR_HPP

#include <boost/taar/core/async_generator.hpp>

namespace boost::taar {
namespace detail {

template<class T>
class is_async_generator_impl
{
    template<typename U>
    static std::true_type check(async_generator<U> const*);
    static std::false_type check(...);

public:
    using type = decltype(check((T*)0));
};

} // namespace detail

template<class T>
concept is_async_generator = requires
{
    requires detail::is_async_generator_impl<T>::type::value;
};

template <typename T>
struct async_generator_value;

template <typename T>
struct async_generator_value<async_generator<T>>
{
    using type = T;
};

template <typename T>
using async_generator_value_t = typename async_generator_value<T>::type;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_IS_ASYNC_GENERATOR_HPP
