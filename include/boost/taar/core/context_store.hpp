//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CONTEXT_STORE_HPP
#define BOOST_TAAR_CORE_CONTEXT_STORE_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/post.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace boost::taar {
namespace detail {

/** A coroutine-friendly mutual exclusion primitive.

    Not bound to any executor: each waiter is resumed on the executor
    associated with its own completion handler, so the same lock can be
    acquired from coroutines running on different `io_context`s over its
    lifetime. The lock is handed directly from a releasing holder to the
    first waiter (FIFO), never re-acquired by a late arrival in between.
*/
class async_lock
{
public:
    template <typename CompletionToken>
    auto async_acquire(CompletionToken&& token)
    {
        return boost::asio::async_initiate<CompletionToken, void()>(
            [this](auto handler)
            {
                auto resume = [h = std::move(handler)]() mutable
                {
                    auto ex = boost::asio::get_associated_executor(h);
                    boost::asio::post(ex, std::move(h));
                };

                std::unique_lock<std::mutex> g {mutex_};
                if (!locked_)
                {
                    locked_ = true;
                    g.unlock();
                    resume();
                }
                else
                {
                    waiters_.emplace_back(std::move(resume));
                }
            },
            token);
    }

    void release()
    {
        std::unique_lock<std::mutex> g {mutex_};
        if (waiters_.empty())
        {
            locked_ = false;
            return;
        }
        auto next = std::move(waiters_.front());
        waiters_.pop_front();
        g.unlock();
        next();
    }

    bool locked() const
    {
        std::lock_guard<std::mutex> const g {mutex_};
        return locked_;
    }

private:
/// @cond
    mutable std::mutex mutex_;
    bool locked_ = false;
    std::deque<std::move_only_function<void()>> waiters_;
/// @endcond
};

} // namespace detail

/** Default key generator used by @ref context_store::create().

    Specialised for `std::string`: 128 random bits from `std::random_device`
    rendered as 32 lowercase hexadecimal characters, which is safe in both
    cookies and headers. Provide your own callable to the store constructor
    for other key types or other formats.
*/
template <typename Key>
struct default_key_generator
{
    static_assert(
        sizeof(Key) == 0,
        "No default key generator for this key type; pass a generator "
        "callable to the context_store constructor.");
};

template <>
struct default_key_generator<std::string>
{
    std::string operator()() const
    {
        std::random_device rd;
        auto const hi = (static_cast<std::uint64_t>(rd()) << 32) | rd();
        auto const lo = (static_cast<std::uint64_t>(rd()) << 32) | rd();
        return std::format("{:016x}{:016x}", hi, lo);
    }
};

/** Thread-safe keyed store of context objects with idle expiry and
    per-entry serialisation.

    A `context_store` holds one @a Context per @a Key. Every entry records
    the time it was last used; entries idle for longer than the store's
    timeout are removed by @ref sweep, which @ref find, @ref emplace and
    @ref create run opportunistically, so expired keys behave as unknown
    without any background timer. Owners that want proactive cleanup (for
    example to close streams belonging to expired sessions) call
    @ref sweep themselves on a schedule.

    Each entry carries an asynchronous lock. A handler that works on an
    entry acquires it with @ref lock and holds the returned guard for the
    duration of the call, so two requests for the same key never run their
    callables concurrently while requests for different keys run in
    parallel. Entries that are locked are never swept.

    @ref find, @ref emplace and @ref create return `shared_ptr`s, so a
    caller that obtained an entry keeps it alive even if it is erased in
    the meantime.

    All members are safe to call from any thread.

    @tparam Key     The lookup key, typically `std::string`.
    @tparam Context The user state type held per key.
*/
template <typename Key, typename Context>
class context_store
{
public:
    using key_type = Key;
    using context_type = Context;
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;
    using key_generator = std::function<Key()>;
    using clock_function = std::function<time_point()>;

    /// One stored context with its bookkeeping. Obtain through @ref find, @ref emplace or @ref create.
    class entry
    {
    public:
        entry(Key key, std::shared_ptr<Context> context, time_point now)
            : key_ {std::move(key)}
            , context_ {std::move(context)}
            , last_activity_ {now.time_since_epoch().count()}
        {}

        Key const& key() const noexcept { return key_; }
        std::shared_ptr<Context> const& context() const noexcept { return context_; }

        time_point last_activity() const noexcept
        {
            return time_point {duration {last_activity_.load(std::memory_order_relaxed)}};
        }

        bool in_flight() const noexcept
        {
            return in_flight_.load(std::memory_order_relaxed) > 0;
        }

    private:
    /// @cond
        friend class context_store;

        void touch(time_point now) noexcept
        {
            last_activity_.store(now.time_since_epoch().count(), std::memory_order_relaxed);
        }

        Key key_;
        std::shared_ptr<Context> context_;
        std::atomic<duration::rep> last_activity_;
        std::atomic<int> in_flight_ {0};
        detail::async_lock lock_;
    /// @endcond
    };

    using entry_ptr = std::shared_ptr<entry>;

    /// Releases an entry's lock (and refreshes its activity) on destruction.
    class lock_guard
    {
    public:
        lock_guard() = default;
        lock_guard(lock_guard const&) = delete;
        lock_guard& operator=(lock_guard const&) = delete;

        lock_guard(lock_guard&& other) noexcept
            : entry_ {std::move(other.entry_)}
            , now_ {std::move(other.now_)}
        {
            other.entry_.reset();
        }

        lock_guard& operator=(lock_guard&& other) noexcept
        {
            if (this != &other)
            {
                release();
                entry_ = std::move(other.entry_);
                now_ = std::move(other.now_);
                other.entry_.reset();
            }
            return *this;
        }

        ~lock_guard() { release(); }

        entry_ptr const& get() const noexcept { return entry_; }
        explicit operator bool() const noexcept { return entry_ != nullptr; }

    private:
    /// @cond
        friend class context_store;

        lock_guard(entry_ptr e, clock_function now)
            : entry_ {std::move(e)}
            , now_ {std::move(now)}
        {}

        void release() noexcept
        {
            if (entry_)
            {
                entry_->touch(now_());
                entry_->in_flight_.fetch_sub(1, std::memory_order_relaxed);
                entry_->lock_.release();
                entry_.reset();
            }
        }

        entry_ptr entry_;
        clock_function now_;
    /// @endcond
    };

    /** Construct a store.

        @param idle_timeout Entries unused for longer than this are removed
                            by the next sweep.
        @param generate     Produces keys for @ref create(). Defaults to
                            @ref default_key_generator.
        @param now          Clock source. Defaults to `steady_clock::now`;
                            tests inject a controllable clock.
    */
    explicit context_store(
        duration idle_timeout,
        key_generator generate = default_key_generator<Key>{},
        clock_function now = [] { return clock::now(); })
        : idle_timeout_ {idle_timeout}
        , generate_ {std::move(generate)}
        , now_ {std::move(now)}
        , last_sweep_ {now_()}
    {}

    context_store(context_store const&) = delete;
    context_store& operator=(context_store const&) = delete;

    duration idle_timeout() const noexcept { return idle_timeout_; }

    /// Number of entries currently held (including expired, not yet swept).
    std::size_t size() const
    {
        std::lock_guard<std::mutex> const g {mutex_};
        return entries_.size();
    }

    /** Look up @a key.

        Expired entries are treated as unknown (and removed). A hit
        refreshes the entry's activity time.

        @returns The entry, or an empty pointer.
    */
    entry_ptr find(Key const& key)
    {
        auto const now = now_();
        std::lock_guard<std::mutex> const g {mutex_};
        maybe_sweep(now);

        auto it = entries_.find(key);
        if (it == entries_.end())
        {
            return nullptr;
        }
        if (expired(*it->second, now))
        {
            entries_.erase(it);
            return nullptr;
        }
        it->second->touch(now);
        return it->second;
    }

    /** Create an entry for @a key, or return the existing one.

        @a args are forwarded to the `Context` constructor when a new entry
        is created.
    */
    template <typename... Args>
    entry_ptr emplace(Key key, Args&&... args)
    {
        auto const now = now_();
        std::lock_guard<std::mutex> const g {mutex_};
        maybe_sweep(now);

        auto it = entries_.find(key);
        if (it != entries_.end() && !expired(*it->second, now))
        {
            it->second->touch(now);
            return it->second;
        }

        auto e = std::make_shared<entry>(
            key,
            std::make_shared<Context>(std::forward<Args>(args)...),
            now);
        entries_.insert_or_assign(std::move(key), e);
        return e;
    }

    /// Create an entry under a freshly generated key.
    template <typename... Args>
    std::pair<Key, entry_ptr> create(Args&&... args)
    {
        auto const now = now_();
        std::lock_guard<std::mutex> const g {mutex_};
        maybe_sweep(now);
        for (;;)
        {
            Key key = generate_();
            auto it = entries_.find(key);
            if (it != entries_.end() && !expired(*it->second, now))
            {
                continue;       // a live entry already owns this key; draw again
            }
            auto e = std::make_shared<entry>(
                key,
                std::make_shared<Context>(std::forward<Args>(args)...),
                now);
            entries_.insert_or_assign(key, e);
            return {std::move(key), std::move(e)};
        }
    }

    /** Look up @a key without refreshing its activity or sweeping.

        For server-initiated work (pushing a notification to a session)
        that must not keep an otherwise idle entry alive. Expired entries
        are reported as unknown.
    */
    entry_ptr peek(Key const& key) const
    {
        auto const now = now_();
        std::lock_guard<std::mutex> const g {mutex_};
        auto it = entries_.find(key);
        if (it == entries_.end() || expired(*it->second, now))
        {
            return nullptr;
        }
        return it->second;
    }

    /// Keys of all live entries, in no particular order.
    std::vector<Key> keys() const
    {
        auto const now = now_();
        std::lock_guard<std::mutex> const g {mutex_};
        std::vector<Key> out;
        out.reserve(entries_.size());
        for (auto const& [k, e] : entries_)
        {
            if (!expired(*e, now)) out.push_back(k);
        }
        return out;
    }

    /// Remove @a key. Returns true if an entry was removed.
    bool erase(Key const& key)
    {
        std::lock_guard<std::mutex> const g {mutex_};
        return entries_.erase(key) > 0;
    }

    /** Remove every idle entry that is not currently locked.

        @returns The number of entries removed.
    */
    std::size_t sweep()
    {
        auto const now = now_();
        std::lock_guard<std::mutex> const g {mutex_};
        return sweep_locked(now);
    }

    /** Acquire @a e's lock.

        Suspends until no other holder exists, then returns a guard. Hold
        the guard while using the context; destroy it to release.
    */
    awaitable<lock_guard> lock(entry_ptr e)
    {
        co_await e->lock_.async_acquire(use_awaitable);
        e->in_flight_.fetch_add(1, std::memory_order_relaxed);
        co_return lock_guard {std::move(e), now_};
    }

private:
/// @cond
    bool expired(entry const& e, time_point now) const noexcept
    {
        return !e.in_flight() && now - e.last_activity() > idle_timeout_;
    }

    void maybe_sweep(time_point now)
    {
        if (now - last_sweep_ >= idle_timeout_ / 4)
        {
            sweep_locked(now);
        }
    }

    std::size_t sweep_locked(time_point now)
    {
        last_sweep_ = now;
        std::size_t removed = 0;
        for (auto it = entries_.begin(); it != entries_.end();)
        {
            if (expired(*it->second, now))
            {
                it = entries_.erase(it);
                ++removed;
            }
            else
            {
                ++it;
            }
        }
        return removed;
    }

    duration idle_timeout_;
    key_generator generate_;
    clock_function now_;
    mutable std::mutex mutex_;
    std::unordered_map<Key, entry_ptr> entries_;
    time_point last_sweep_;
/// @endcond
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CONTEXT_STORE_HPP
