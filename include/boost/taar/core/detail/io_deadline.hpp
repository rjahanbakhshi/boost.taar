//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_CORE_DETAIL_IO_DEADLINE_HPP
#define BOOST_TAAR_CORE_DETAIL_IO_DEADLINE_HPP

#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <memory>
#include <optional>

namespace boost::taar::detail {

/// Duration of a connection timeout; zero or negative disables it.
using timeout_duration = std::chrono::steady_clock::duration;

/** Arm the expiry of @a stream's lowest layer for the next operation.

    Beast's `basic_stream` (`tcp_stream`, also under a TLS layer) fails
    an operation still pending when the expiry passes with
    `beast::error::timeout` and closes the socket. A zero or negative
    @a d clears the expiry. No-op for a lowest layer without expiry.
*/
template <typename Stream>
void expire_after(Stream& stream, timeout_duration d)
{
    // Requirements are spelled on types, not on the local names: MSVC has
    // mis-evaluated requires-expressions that name function locals.
    if constexpr (requires(Stream& s, timeout_duration t)
    {
        boost::beast::get_lowest_layer(s).expires_after(t);
        boost::beast::get_lowest_layer(s).expires_never();
    })
    {
        auto& lowest = boost::beast::get_lowest_layer(stream);
        if (d > timeout_duration::zero())
            lowest.expires_after(d);
        else
            lowest.expires_never();
    }
}

/// Clear the expiry of @a stream's lowest layer (time between operations never counts).
template <typename Stream>
void expire_never(Stream& stream)
{
    if constexpr (requires(Stream& s) { boost::beast::get_lowest_layer(s).expires_never(); })
    {
        boost::beast::get_lowest_layer(stream).expires_never();
    }
}

/** A deadline that cancels the pending operations of a stream without
    closing it.

    Unlike the stream's own expiry, which closes the socket, expiry here
    cancels the lowest layer's pending operations (they complete with
    `operation_aborted`) and records that the deadline passed, so the
    caller can still answer on the connection (`408 Request Timeout`).
    The cancellation goes through the socket, not the coroutine's
    cancellation slot, so it also reaches reads shielded from shutdown.

    Construct it right before the operation and query @ref expired once
    the operation completed; the destructor disarms it. The timer runs on
    the stream's executor (the connection strand), so its handler is
    serialised with the operation's completion. A zero or negative
    duration disables it.
*/
template <typename Stream>
class read_deadline
{
public:
    read_deadline(Stream& stream, timeout_duration d)
    {
        if constexpr (can_cancel)
        {
            if (d <= timeout_duration::zero()) return;
            state_ = std::make_shared<state>();
            state_->stream = &stream;
            timer_.emplace(stream.get_executor(), d);
            timer_->async_wait(
                [s = state_](boost::system::error_code const& ec)
                {
                    // A deadline destroyed (operation done) before its
                    // handler ran has cleared the stream pointer.
                    if (ec || s->stream == nullptr) return;
                    s->expired = true;
                    cancel(*s->stream);
                });
        }
    }

    read_deadline(read_deadline const&) = delete;
    read_deadline& operator=(read_deadline const&) = delete;

    ~read_deadline()
    {
        // Destroying the timer cancels its wait; a handler already queued
        // finds the stream gone and does nothing.
        if (state_) state_->stream = nullptr;
    }

    /// True if the deadline passed before it was destroyed.
    bool expired() const noexcept
    {
        return state_ && state_->expired;
    }

private:
    struct state
    {
        Stream* stream = nullptr;
        bool expired = false;
    };

    static void cancel(Stream& stream)
    {
        auto& lowest = boost::beast::get_lowest_layer(stream);
        if constexpr (requires(Stream& s, boost::system::error_code& ec) { boost::beast::get_lowest_layer(s).cancel(ec); })
        {
            boost::system::error_code ec;
            lowest.cancel(ec);
        }
        else
        {
            lowest.cancel();
        }
    }

    static constexpr bool can_cancel = requires(Stream& s, boost::system::error_code& ec)
    {
        boost::beast::get_lowest_layer(s).cancel(ec);
    } || requires(Stream& s)
    {
        boost::beast::get_lowest_layer(s).cancel();
    };

    std::shared_ptr<state> state_;
    std::optional<boost::asio::steady_timer> timer_;
};

} // namespace boost::taar::detail

#endif // BOOST_TAAR_CORE_DETAIL_IO_DEADLINE_HPP
