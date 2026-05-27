//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_AWAITABLE_HPP
#define BOOST_TAAR_CORE_AWAITABLE_HPP

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace boost::taar {

/** Convenience alias for `asio::awaitable` bound to an `io_context` executor.

    Boost.Taar uses an `io_context` executor everywhere, so the second
    template argument of `asio::awaitable` is always
    `io_context::executor_type`. Returning `taar::awaitable<T>` from a
    coroutine spares the user from spelling out that pair.
*/
template <typename T>
using awaitable = boost::asio::awaitable<T, boost::asio::io_context::executor_type>;

/// Completion token: continue this coroutine, throw on error.
inline constexpr boost::asio::use_awaitable_t<
    boost::asio::io_context::executor_type> use_awaitable{};

/// Completion token: continue this coroutine, return `(error_code, result)` instead of throwing.
inline constexpr boost::asio::as_tuple_t<
    boost::asio::use_awaitable_t<
        boost::asio::io_context::executor_type>> use_awaitable_tuple{};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_AWAITABLE_HPP

