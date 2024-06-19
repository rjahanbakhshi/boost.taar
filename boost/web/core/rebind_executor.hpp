//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HTTP_CORE_REBIND_EXECUTOR_HPP
#define BOOST_WEB_HTTP_CORE_REBIND_EXECUTOR_HPP

#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>

namespace boost::web {

template <typename T>
using rebind_executor = typename T::template 
    rebind_executor<
        boost::asio::as_tuple_t<
            boost::asio::use_awaitable_t<
                boost::asio::io_context::executor_type
            >
        >::executor_with_default<boost::asio::io_context::executor_type>
    >::other;

} // namespace boost::web

#endif // BOOST_WEB_HTTP_CORE_REBIND_EXECUTOR_HPP
