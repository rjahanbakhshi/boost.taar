//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_ASYNC_GENERATOR_HPP
#define BOOST_TAAR_CORE_ASYNC_GENERATOR_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>
#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <utility>

namespace boost::taar {

template <typename T>
class async_generator
{
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    using executor_type = boost::asio::io_context::executor_type;
    using completion_handler_type = std::move_only_function<void(std::optional<T>)>;

    struct promise_type
    {
        async_generator get_return_object()
        {
            return async_generator{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct final_awaiter
        {
            bool await_ready() noexcept { return false; }

            void await_suspend(handle_type h) noexcept
            {
                auto& promise = h.promise();
                if (promise.completion_handler_)
                {
                    auto handler = std::move(promise.completion_handler_);
                    if (promise.executor_)
                    {
                        boost::asio::post(*promise.executor_,
                            [handler = std::move(handler)]() mutable
                            {
                                handler(std::nullopt);
                            });
                    }
                    else
                    {
                        handler(std::nullopt);
                    }
                }
            }

            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept { return {}; }

        struct yield_awaiter
        {
            bool await_ready() noexcept { return false; }

            void await_suspend(handle_type h) noexcept
            {
                auto& promise = h.promise();
                if (promise.completion_handler_)
                {
                    auto handler = std::move(promise.completion_handler_);
                    auto value = std::move(*promise.current_value_);
                    promise.current_value_.reset();
                    if (promise.executor_)
                    {
                        boost::asio::post(*promise.executor_,
                            [handler = std::move(handler),
                             value = std::move(value)]() mutable
                            {
                                handler(std::move(value));
                            });
                    }
                    else
                    {
                        handler(std::move(value));
                    }
                }
            }

            void await_resume() noexcept {}
        };

        yield_awaiter yield_value(T value)
        {
            current_value_.emplace(std::move(value));
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception()
        {
            exception_ = std::current_exception();
            // If there's a completion handler waiting, we need to resume the
            // consumer so it can observe the exception via next().
            // The exception will be rethrown in next() after the coroutine
            // is resumed and the handler is called with nullopt.
        }

        // await_transform to support co_await on asio awaitables inside the generator
        template <typename U, typename Executor>
        auto await_transform(boost::asio::awaitable<U, Executor> aw)
        {
            struct awaitable_bridge
            {
                boost::asio::awaitable<U, Executor> inner;
                promise_type& promise;
                std::optional<U> result;
                std::exception_ptr eptr;

                bool await_ready() noexcept { return false; }

                void await_suspend(handle_type h)
                {
                    auto& p = h.promise();
                    if (!p.executor_)
                    {
                        // No executor set, can't co_spawn
                        eptr = std::make_exception_ptr(
                            std::runtime_error("async_generator: no executor set for co_await"));
                        h.resume();
                        return;
                    }

                    boost::asio::co_spawn(
                        *p.executor_,
                        std::move(inner),
                        [h, this](std::exception_ptr ep, U val) mutable
                        {
                            if (ep)
                                eptr = ep;
                            else
                                result.emplace(std::move(val));
                            h.resume();
                        });
                }

                U await_resume()
                {
                    if (eptr)
                        std::rethrow_exception(eptr);
                    return std::move(*result);
                }
            };

            return awaitable_bridge{std::move(aw), *this, {}, {}};
        }

        // Specialization for awaitable<void>
        template <typename Executor>
        auto await_transform(boost::asio::awaitable<void, Executor> aw)
        {
            struct awaitable_bridge_void
            {
                boost::asio::awaitable<void, Executor> inner;
                promise_type& promise;
                std::exception_ptr eptr;

                bool await_ready() noexcept { return false; }

                void await_suspend(handle_type h)
                {
                    auto& p = h.promise();
                    if (!p.executor_)
                    {
                        eptr = std::make_exception_ptr(
                            std::runtime_error("async_generator: no executor set for co_await"));
                        h.resume();
                        return;
                    }

                    boost::asio::co_spawn(
                        *p.executor_,
                        std::move(inner),
                        [h, this](std::exception_ptr ep) mutable
                        {
                            if (ep)
                                eptr = ep;
                            h.resume();
                        });
                }

                void await_resume()
                {
                    if (eptr)
                        std::rethrow_exception(eptr);
                }
            };

            return awaitable_bridge_void{std::move(aw), *this, {}};
        }

        std::optional<T> current_value_;
        completion_handler_type completion_handler_;
        std::optional<executor_type> executor_;
        std::exception_ptr exception_;
    };

    async_generator() noexcept = default;

    explicit async_generator(handle_type h) noexcept
        : handle_(h)
    {}

    async_generator(async_generator&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr))
    {}

    async_generator& operator=(async_generator&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    async_generator(async_generator const&) = delete;
    async_generator& operator=(async_generator const&) = delete;

    ~async_generator()
    {
        if (handle_)
            handle_.destroy();
    }

    void set_executor(executor_type executor)
    {
        if (handle_)
            handle_.promise().executor_ = std::move(executor);
    }

    void rethrow_if_exception()
    {
        if (handle_ && handle_.promise().exception_)
        {
            auto eptr = handle_.promise().exception_;
            handle_.promise().exception_ = nullptr;
            std::rethrow_exception(eptr);
        }
    }

    // next() returns awaitable<std::optional<T>> for integration with asio coroutines
    auto next()
    {
        using token_type = boost::asio::as_tuple_t<
            boost::asio::use_awaitable_t<executor_type>>;

        return boost::asio::async_initiate<
            void(boost::system::error_code, std::optional<T>)>(
            [this](auto handler)
            {
                // Check if there's a pending exception from a previous resume
                // before checking done() — the coroutine may be at final_suspend
                // with a stored exception that must be propagated.
                if (handle_ && handle_.promise().exception_)
                {
                    auto& promise = handle_.promise();
                    auto eptr = promise.exception_;
                    promise.exception_ = nullptr;
                    auto exec = boost::asio::get_associated_executor(handler);
                    boost::asio::post(exec,
                        [handler = std::move(handler), eptr]() mutable
                        {
                            std::rethrow_exception(eptr);
                        });
                    return;
                }

                if (!handle_ || handle_.done())
                {
                    auto exec = boost::asio::get_associated_executor(handler);
                    boost::asio::post(exec,
                        [handler = std::move(handler)]() mutable
                        {
                            handler(boost::system::error_code{}, std::nullopt);
                        });
                    return;
                }

                auto& promise = handle_.promise();
                promise.completion_handler_ =
                    [handler = std::move(handler)](std::optional<T> value) mutable
                    {
                        handler(boost::system::error_code{}, std::move(value));
                    };

                handle_.resume();
            },
            token_type{});
    }

private:
    handle_type handle_ = nullptr;
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_ASYNC_GENERATOR_HPP
