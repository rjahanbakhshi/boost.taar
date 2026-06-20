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
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace boost::taar {

/** A thread-safe, executor-agnostic multi-producer FIFO of values with
    asynchronous receive.

    Producers call @ref push from any thread. Consumers suspend in
    @ref async_receive until a value is available; each waiter resumes on
    the executor associated with its own completion handler. Receiving
    completes with `std::nullopt` once the channel is closed and drained,
    or when the waiting operation is cancelled through its cancellation
    slot. Used by the MCP endpoint to feed server-to-client event streams.
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
        std::unique_lock<std::mutex> g {mutex_};
        if (closed_)
        {
            return false;
        }
        if (waiters_.empty())
        {
            values_.push_back(std::move(value));
            return true;
        }
        auto w = std::move(waiters_.front());
        waiters_.pop_front();
        g.unlock();
        w.complete(std::optional<T> {std::move(value)});
        return true;
    }

    /// Close the channel: pending waiters complete with `nullopt`, later pushes fail.
    void close()
    {
        std::unique_lock<std::mutex> g {mutex_};
        closed_ = true;
        auto waiters = std::move(waiters_);
        waiters_.clear();
        g.unlock();
        for (auto& w : waiters)
        {
            w.complete(std::nullopt);
        }
    }

    bool closed() const
    {
        std::lock_guard<std::mutex> const g {mutex_};
        return closed_;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> const g {mutex_};
        return values_.size();
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

                std::unique_lock<std::mutex> g {mutex_};
                if (!values_.empty())
                {
                    std::optional<T> v {std::move(values_.front())};
                    values_.pop_front();
                    g.unlock();
                    boost::asio::post(ex, [h = std::move(handler), v = std::move(v)]() mutable
                    {
                        h(std::move(v));
                    });
                    return;
                }
                if (closed_)
                {
                    g.unlock();
                    boost::asio::post(ex, [h = std::move(handler)]() mutable
                    {
                        h(std::nullopt);
                    });
                    return;
                }

                auto const id = next_id_++;
                waiters_.push_back(waiter {
                    id,
                    [ex, h = std::move(handler)](std::optional<T> v) mutable
                    {
                        boost::asio::post(ex, [h = std::move(h), v = std::move(v)]() mutable
                        {
                            h(std::move(v));
                        });
                    }});
                g.unlock();

                if (slot.is_connected())
                {
                    slot.assign([this, id](boost::asio::cancellation_type_t)
                    {
                        cancel(id);
                    });
                }
            },
            token);
    }

private:
    struct waiter
    {
        std::uint64_t id;
        std::move_only_function<void(std::optional<T>)> complete;
    };

    void cancel(std::uint64_t id)
    {
        std::unique_lock<std::mutex> g {mutex_};
        for (auto it = waiters_.begin(); it != waiters_.end(); ++it)
        {
            if (it->id == id)
            {
                auto w = std::move(*it);
                waiters_.erase(it);
                g.unlock();
                w.complete(std::nullopt);
                return;
            }
        }
    }

    mutable std::mutex mutex_;
    std::deque<T> values_;
    std::deque<waiter> waiters_;
    std::uint64_t next_id_ = 0;
    bool closed_ = false;
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_ASYNC_CHANNEL_HPP
