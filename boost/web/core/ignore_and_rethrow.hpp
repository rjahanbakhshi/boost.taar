//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HTTP_CORE_IGNORE_AND_RETHROW_HPP
#define BOOST_WEB_HTTP_CORE_IGNORE_AND_RETHROW_HPP

#include <exception>

namespace boost::web {

struct ignore_and_rethrow_t
{
    void operator()(const std::exception_ptr& eptr)
    {
        if (eptr)
        {
            std::rethrow_exception(eptr);
        }
    }
};
static const ignore_and_rethrow_t ignore_and_rethrow;

} // namespace boost::web

#endif // BOOST_WEB_HTTP_CORE_IGNORE_AND_RETHROW_HPP
