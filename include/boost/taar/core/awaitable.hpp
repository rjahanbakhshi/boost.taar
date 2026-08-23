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

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace boost::taar {

/** Convenience alias for `asio::awaitable` on `any_io_executor`.

    Every Taar coroutine runs on an `any_io_executor`, so it can be spawned
    on a plain `io_context` executor or on a strand of one (which is how
    the servers run each connection when the `io_context` is driven by
    several threads). Returning `taar::awaitable<T>` from a coroutine
    spares the user from spelling out the executor.
*/
template <typename T>
using awaitable = boost::asio::awaitable<T, boost::asio::any_io_executor>;

/// Completion token: continue this coroutine, throw on error.
inline constexpr boost::asio::use_awaitable_t<boost::asio::any_io_executor> use_awaitable{};

/// Completion token: continue this coroutine, return `(error_code, result)` instead of throwing.
inline constexpr boost::asio::as_tuple_t<
    boost::asio::use_awaitable_t<boost::asio::any_io_executor>> use_awaitable_tuple{};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_AWAITABLE_HPP

