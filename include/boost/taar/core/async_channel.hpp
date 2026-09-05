//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_ASYNC_CHANNEL_HPP
#define BOOST_TAAR_CORE_ASYNC_CHANNEL_HPP

#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/post.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace boost::taar {

/** A thread-safe, executor-agnostic multi-producer FIFO of values with
    asynchronous receive.

    Producers call @ref push from any thread. Consumers suspend in
    @ref async_receive until a value is available; each waiter resumes on
    the executor associated with its own completion handler. Receiving
    completes with `std::nullopt` once the channel is closed and drained,
    or when the waiting operation is cancelled through its cancellation
    slot. Used by the MCP endpoint to feed server-to-client event streams.

    The cancellation handler installed on a consumer's slot refers to the
    channel through a weak pointer, so a slot that still carries it after
    the channel is gone emits into nothing. Producers never touch a
    consumer's slot: only the consumer's own thread assigns it.
*/
template <typename T>
class async_channel
{
public:
    async_channel() = default;
    async_channel(async_channel const&) = delete;
    async_channel& operator=(async_channel const&) = delete;

    /// Enqueue @a value, or hand it directly to the first waiter. Returns false if closed.
    bool push(T value)
    {
        std::unique_lock<std::mutex> g {state_->mutex};
        if (state_->closed)
        {
            return false;
        }
        if (state_->waiters.empty())
        {
            state_->values.push_back(std::move(value));
            return true;
        }
        auto w = std::move(state_->waiters.front());
        state_->waiters.pop_front();
        g.unlock();
        w.complete(std::optional<T> {std::move(value)});
        return true;
    }

    /// Drop the oldest queued values until at most @a max remain. Returns how many were dropped.
    std::size_t trim(std::size_t max)
    {
        std::lock_guard<std::mutex> const g {state_->mutex};
        std::size_t dropped = 0;
        while (state_->values.size() > max)
        {
            state_->values.pop_front();
            ++dropped;
        }
        return dropped;
    }

    /// Close the channel: pending waiters complete with `nullopt`, later pushes fail.
    void close()
    {
        std::unique_lock<std::mutex> g {state_->mutex};
        state_->closed = true;
        auto waiters = std::move(state_->waiters);
        state_->waiters.clear();
        g.unlock();
        for (auto& w : waiters)
        {
            w.complete(std::nullopt);
        }
    }

    bool closed() const
    {
        std::lock_guard<std::mutex> const g {state_->mutex};
        return state_->closed;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> const g {state_->mutex};
        return state_->values.size();
    }

    /** Receive the next value. Completion signature: `void(std::optional<T>)`.

        Values queued before @ref close are still delivered; afterwards
        `nullopt`. Cancellation via the completion handler's associated
        slot also completes with `nullopt`.
    */
    template <typename CompletionToken>
    auto async_receive(CompletionToken&& token)
    {
        return boost::asio::async_initiate<CompletionToken, void(std::optional<T>)>(
            [this](auto handler)
            {
                auto ex = boost::asio::get_associated_executor(handler);
                auto slot = boost::asio::get_associated_cancellation_slot(handler);

                std::unique_lock<std::mutex> g {state_->mutex};
                if (!state_->values.empty())
                {
                    std::optional<T> v {std::move(state_->values.front())};
                    state_->values.pop_front();
                    g.unlock();
                    boost::asio::post(ex, [h = std::move(handler), v = std::move(v)]() mutable
                    {
                        h(std::move(v));
                    });
                    return;
                }
                if (state_->closed)
                {
                    g.unlock();
                    boost::asio::post(ex, [h = std::move(handler)]() mutable
                    {
                        h(std::nullopt);
                    });
                    return;
                }

                auto const id = state_->next_id++;
                // Install the cancellation handler before the waiter becomes
                // visible to producers, on the consumer's own thread.
                if (slot.is_connected())
                {
                    slot.assign([weak = std::weak_ptr<state> {state_}, id](boost::asio::cancellation_type_t)
                    {
                        if (auto s = weak.lock()) cancel(*s, id);
                    });
                }
                state_->waiters.push_back(waiter {
                    id,
                    [ex, h = std::move(handler)](std::optional<T> v) mutable
                    {
                        boost::asio::post(ex, [h = std::move(h), v = std::move(v)]() mutable
                        {
                            h(std::move(v));
                        });
                    }});
            },
            token);
    }

private:
/// @cond
    struct waiter
    {
        std::uint64_t id;
        std::move_only_function<void(std::optional<T>)> complete;
    };

    struct state
    {
        mutable std::mutex mutex;
        std::deque<T> values;
        std::deque<waiter> waiters;
        std::uint64_t next_id = 0;
        bool closed = false;
    };

    static void cancel(state& s, std::uint64_t id)
    {
        std::unique_lock<std::mutex> g {s.mutex};
        for (auto it = s.waiters.begin(); it != s.waiters.end(); ++it)
        {
            if (it->id == id)
            {
                auto w = std::move(*it);
                s.waiters.erase(it);
                g.unlock();
                w.complete(std::nullopt);
                return;
            }
        }
    }

    std::shared_ptr<state> state_ = std::make_shared<state>();
/// @endcond
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_ASYNC_CHANNEL_HPP
