//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_IGNORE_AND_RETHROW_HPP
#define BOOST_TAAR_CORE_IGNORE_AND_RETHROW_HPP

#include <exception>

namespace boost::taar {

/** Completion handler that rethrows any reported exception.

    Intended as the completion token for `co_spawn` on a top-level
    coroutine — most usefully the @ref boost::taar::server::tcp acceptor.
    A reported exception is rethrown into the surrounding scope so that
    `io_context::run()` returns and the program can shut down rather than
    silently swallowing the failure.
*/
struct ignore_and_rethrow_t
{
    void operator()(std::exception_ptr const& eptr)
    {
        if (eptr)
        {
            std::rethrow_exception(eptr);
        }
    }
};

/// Stateless instance of @ref ignore_and_rethrow_t.
static ignore_and_rethrow_t const ignore_and_rethrow;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_IGNORE_AND_RETHROW_HPP
