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

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/dispatch.hpp>
#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace boost::taar {

/** A thread-safe pool of `asio::cancellation_signal` instances.

    A `cancellation_signals` instance hands out cancellation slots to async
    operations and can emit a cancellation to all of them in one call. This
    is the primary mechanism Boost.Taar offers for shutting down a server:

    @li Pass the pool to `server::tcp` / `server::tls`, which bind a slot
        to every connection they spawn.
    @li Install a signal handler that calls @ref emit() from a `SIGINT`
        / `SIGTERM` slot.

    Asio requires a cancellation signal to be emitted within the same
    implicit or explicit strand as the operation it cancels. The pool
    therefore offers two kinds of slot:

    @li @ref slot(boost::asio::any_io_executor): the slot is tied to an
        executor (a strand when the `io_context` runs on several threads),
        and @ref emit dispatches the emission through it. This is what the
        servers use for every connection, so @ref emit may be called from
        any thread, including one that is not running the `io_context`.
    @li @ref slot(): a plain slot emitted inline on the calling thread.
        Safe only when the bound operation runs on the thread that calls
        @ref emit (a single-threaded `io_context`, or a handler posted to
        it).
*/
class cancellation_signals
{
    struct bound_record
    {
        boost::asio::cancellation_signal signal;
        boost::asio::any_io_executor executor;
    };

    struct state
    {
        std::mutex mutex;
        std::list<boost::asio::cancellation_signal> signals;
        std::vector<std::shared_ptr<bound_record>> bound;
    };

public:
    /** Handle to a slot tied to an executor. Destroying it removes the slot
        from the pool, so keep it alive for as long as the operation runs
        (typically by capturing it in the operation's completion handler).
    */
    class bound_slot
    {
    public:
        bound_slot() = default;
        bound_slot(bound_slot&&) noexcept = default;
        bound_slot& operator=(bound_slot&& other) noexcept
        {
            if (this != &other)
            {
                release();
                state_ = std::move(other.state_);
                record_ = std::move(other.record_);
            }
            return *this;
        }
        bound_slot(bound_slot const&) = delete;
        bound_slot& operator=(bound_slot const&) = delete;
        ~bound_slot() { release(); }

        /// The cancellation slot to bind to the operation.
        boost::asio::cancellation_slot slot() const
        {
            return record_ ? record_->signal.slot() : boost::asio::cancellation_slot {};
        }

        /// Emit on this slot alone, dispatched through its executor.
        void emit(boost::asio::cancellation_type ct = boost::asio::cancellation_type::all) const
        {
            if (record_)
            {
                boost::asio::dispatch(record_->executor, [record = record_, ct] { record->signal.emit(ct); });
            }
        }

        /// Remove the slot from the pool now.
        void release()
        {
            if (state_ && record_)
            {
                std::lock_guard<std::mutex> const lock {state_->mutex};
                std::erase(state_->bound, record_);
            }
            state_.reset();
            record_.reset();
        }

    private:
        friend class cancellation_signals;
        bound_slot(std::shared_ptr<state> s, std::shared_ptr<bound_record> r)
            : state_ {std::move(s)}
            , record_ {std::move(r)}
        {}

        std::shared_ptr<state> state_;
        std::shared_ptr<bound_record> record_;
    };

    /** Emit cancellation of type @a ct to every slot in the pool.

        Plain slots are emitted inline; executor-bound slots have their
        emission dispatched through their executor.
    */
    void emit(boost::asio::cancellation_type ct = boost::asio::cancellation_type::all)
    {
        std::vector<std::shared_ptr<bound_record>> bound;
        {
            std::lock_guard<std::mutex> const lock {state_->mutex};
            for (auto& signal : state_->signals)
            {
                signal.emit(ct);
            }
            bound = state_->bound;
        }
        for (auto& record : bound)
        {
            boost::asio::dispatch(record->executor, [record, ct] { record->signal.emit(ct); });
        }
    }

    /** Obtain a plain cancellation slot bound to one of the signals in the pool.

        Reuses a signal that does not currently have a handler attached,
        appending a new signal to the pool if every existing one is
        already bound. Subsequent calls hand out distinct slots. The
        emission runs on the thread calling @ref emit.
    */
    boost::asio::cancellation_slot slot()
    {
        std::lock_guard<std::mutex> const lock {state_->mutex};

        auto itr = std::find_if(
            state_->signals.begin(),
            state_->signals.end(),
            [](boost::asio::cancellation_signal& signal)
            {
                return !signal.slot().has_handler();
            });

        if (itr != state_->signals.end())
        {
            return itr->slot();
        }

        return state_->signals.emplace_back().slot();
    }

    /** Obtain a slot whose emission is dispatched through @a executor.

        Pass the strand (or other serialising executor) the operation runs
        on. The slot lives until the returned handle is destroyed.
    */
    [[nodiscard]] bound_slot slot(boost::asio::any_io_executor executor)
    {
        auto record = std::make_shared<bound_record>();
        record->executor = std::move(executor);
        {
            std::lock_guard<std::mutex> const lock {state_->mutex};
            state_->bound.push_back(record);
        }
        return bound_slot {state_, std::move(record)};
    }

    /// Number of executor-bound slots currently alive.
    std::size_t bound_count() const
    {
        std::lock_guard<std::mutex> const lock {state_->mutex};
        return state_->bound.size();
    }

private:
    std::shared_ptr<state> state_ = std::make_shared<state>();
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CANCELLATION_SIGNALS_HPP
