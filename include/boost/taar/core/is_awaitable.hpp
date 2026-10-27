//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_CORE_IS_AWAITABLE_HPP
#define BOOST_TAAR_CORE_IS_AWAITABLE_HPP

#include <boost/asio/awaitable.hpp>

namespace boost::taar {
namespace detail {

template<class T>
class is_awaitable_impl
{
    template<typename U, typename Executor>
    static std::true_type check(boost::asio::awaitable<U, Executor> const*);
    static std::false_type check(...);

public:
    using type = decltype(check((T*)0));
};

} // namespace detail

/// True if @a T is a specialisation of `boost::asio::awaitable`.
template<class T>
concept is_awaitable = requires
{
    requires detail::is_awaitable_impl<T>::type::value;
};

/// What @a T produces once awaited: `U` for `awaitable<U, E>`, otherwise @a T itself.
template <typename T> struct awaitable_result { using type = T; };
template <typename T, typename E> struct awaitable_result<boost::asio::awaitable<T, E>> { using type = T; };
template <typename T>
using awaitable_result_t = typename awaitable_result<T>::type;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_IS_AWAITABLE_HPP
