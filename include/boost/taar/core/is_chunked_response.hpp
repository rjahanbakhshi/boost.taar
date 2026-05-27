//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_IS_CHUNKED_RESPONSE_HPP
#define BOOST_TAAR_CORE_IS_CHUNKED_RESPONSE_HPP

#include <boost/taar/core/chunked_response.hpp>

namespace boost::taar {
namespace detail {

template<class T>
class is_chunked_response_impl
{
    template<typename U>
    static std::true_type check(chunked_response<U> const*);
    static std::false_type check(...);

public:
    using type = decltype(check((T*)0));
};

} // namespace detail

/// True if @a T is a specialisation of @ref boost::taar::chunked_response.
template<class T>
concept is_chunked_response = requires
{
    requires detail::is_chunked_response_impl<T>::type::value;
};

/// Trait: extract the yielded chunk type from a @ref chunked_response.
template <typename T>
struct chunked_response_value;

template <typename T>
struct chunked_response_value<chunked_response<T>>
{
    using type = T;
};

/// Convenience alias for `chunked_response_value<T>::type`.
template <typename T>
using chunked_response_value_t = typename chunked_response_value<T>::type;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_IS_CHUNKED_RESPONSE_HPP
