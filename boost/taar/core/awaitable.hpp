//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HTTP_CORE_AWAITABLE_HPP
#define BOOST_TAAR_HTTP_CORE_AWAITABLE_HPP

#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>

namespace boost::taar {

template <typename T>
using awaitable = boost::asio::awaitable<T, boost::asio::io_context::executor_type>;

} // namespace boost::taar

#endif // BOOST_TAAR_HTTP_CORE_AWAITABLE_HPP

