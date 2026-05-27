//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CANCELLATION_SIGNALS_HPP
#define BOOST_TAAR_CORE_CANCELLATION_SIGNALS_HPP

#include <boost/asio/cancellation_signal.hpp>
#include <list>
#include <algorithm>
#include <mutex>

namespace boost::taar {

/** A thread-safe pool of `asio::cancellation_signal` instances.

    A `cancellation_signals` instance hands out cancellation slots to async
    operations and can emit a cancellation to all of them in one call. This
    is the primary mechanism Boost.Taar offers for shutting down a server:

    @li Bind @ref slot() to each spawned coroutine.
    @li Install a signal handler that calls @ref emit() from a `SIGINT`
        / `SIGTERM` slot.

    The pool grows as needed: every call to @ref slot() returns the first
    unused signal, or appends a new one if all existing signals already have
    a handler. A mutex serialises @ref slot() and @ref emit() so the same
    instance can be used from multiple threads.
*/
class cancellation_signals
{
public:
    /// Emit cancellation of type @a ct to every signal in the pool.
    void emit(boost::asio::cancellation_type ct = boost::asio::cancellation_type::all)
    {
        std::lock_guard<std::mutex> const lock{mutex_};
        for (auto& signal: signals_)
        {
            signal.emit(ct);
        }
    }

    /** Obtain a cancellation slot bound to one of the signals in the pool.

        Reuses a signal that does not currently have a handler attached,
        appending a new signal to the pool if every existing one is
        already bound. Subsequent calls hand out distinct slots.
    */
    boost::asio::cancellation_slot slot()
    {
        std::lock_guard<std::mutex> const lock{mutex_};

        auto itr = std::find_if(
            signals_.begin(),
            signals_.end(),
            [](boost::asio::cancellation_signal& signal)
            {
                return !signal.slot().has_handler();
            });

        if (itr != signals_.end())
        {
            return itr->slot();
        }

        return signals_.emplace_back().slot();
    }

private:
    std::list<boost::asio::cancellation_signal> signals_;
    std::mutex mutex_;
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CANCELLATION_SIGNALS_HPP
