//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CHUNKED_RESPONSE_HPP
#define BOOST_TAAR_CORE_CHUNKED_RESPONSE_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace boost::taar {

template <typename T>
class chunked_response
{
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    using executor_type = boost::asio::io_context::executor_type;
    using completion_handler_type = std::move_only_function<void(std::optional<T>)>;

    struct promise_type
    {
        chunked_response get_return_object()
        {
            return chunked_response{handle_type::from_promise(*this)};
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

        // Data yield — suspends like async_generator
        yield_awaiter yield_value(T value)
        {
            data_yielded_ = true;
            current_value_.emplace(std::move(value));
            return {};
        }

        // Metadata yields — don't suspend
        std::suspend_never yield_value(chunked_set_status meta)
        {
            if (data_yielded_)
            {
                throw boost::system::system_error{error::late_chunk_metadata};
            }
            status_ = meta.value;
            return {};
        }

        std::suspend_never yield_value(chunked_set_header meta)
        {
            if (data_yielded_)
            {
                throw boost::system::system_error{error::late_chunk_metadata};
            }
            headers_.push_back(std::move(meta));
            return {};
        }

        // Flattening awaiter for yielding inner chunked_response generators
        struct flatten_awaiter
        {
            chunked_response<T> inner;

            bool await_ready() noexcept { return false; }

            void await_suspend(handle_type h)
            {
                auto& promise = h.promise();
                // Move inner into promise storage
                promise.flattening_inner_storage_.emplace(std::move(inner));
                promise.flattening_inner_ = &*promise.flattening_inner_storage_;
                promise.flattening_outer_handle_ = h;
                // Set inner's executor from outer
                if (promise.executor_)
                    promise.flattening_inner_->set_executor(*promise.executor_);
                // Propagate cancellation slot to inner
                if (promise.cancellation_slot_ && promise.cancellation_slot_->is_connected())
                    promise.flattening_inner_->set_cancellation_slot(*promise.cancellation_slot_);

                // Now drive the inner generator to get its first value.
                // The completion_handler_ is still set from the outer's next() call.
                if (promise.completion_handler_ && promise.executor_)
                {
                    auto handler = std::move(promise.completion_handler_);
                    auto* inner_ptr = promise.flattening_inner_;
                    auto exec = *promise.executor_;

                    auto completion_handler = [&promise, h, handler = std::move(handler)](
                        std::exception_ptr ep,
                        std::tuple<boost::system::error_code, std::optional<T>> result) mutable
                    {
                        if (ep)
                        {
                            promise.exception_ = ep;
                            promise.flattening_inner_ = nullptr;
                            promise.flattening_inner_storage_.reset();
                            promise.flattening_outer_handle_ = nullptr;
                            handler(std::nullopt);
                            return;
                        }

                        auto [ec, val] = std::move(result);
                        if (val)
                        {
                            handler(std::move(val));
                        }
                        else
                        {
                            // Inner exhausted immediately - check for exception
                            auto* inner_ptr = promise.flattening_inner_;
                            try
                            {
                                inner_ptr->rethrow_if_exception();
                            }
                            catch (...)
                            {
                                promise.exception_ = std::current_exception();
                                promise.flattening_inner_ = nullptr;
                                promise.flattening_inner_storage_.reset();
                                promise.flattening_outer_handle_ = nullptr;
                                handler(std::nullopt);
                                return;
                            }

                            // Inner empty, resume outer
                            promise.flattening_inner_ = nullptr;
                            promise.flattening_inner_storage_.reset();
                            promise.flattening_outer_handle_ = nullptr;

                            promise.completion_handler_ = std::move(handler);
                            h.resume();
                        }
                    };

                    if (promise.cancellation_slot_ && promise.cancellation_slot_->is_connected())
                    {
                        boost::asio::co_spawn(exec,
                            [inner_ptr]() -> boost::taar::awaitable<std::tuple<boost::system::error_code, std::optional<T>>>
                            {
                                co_return co_await inner_ptr->next();
                            },
                            boost::asio::bind_cancellation_slot(
                                *promise.cancellation_slot_,
                                std::move(completion_handler)));
                    }
                    else
                    {
                        boost::asio::co_spawn(exec,
                            [inner_ptr]() -> boost::taar::awaitable<std::tuple<boost::system::error_code, std::optional<T>>>
                            {
                                co_return co_await inner_ptr->next();
                            },
                            std::move(completion_handler));
                    }
                }
            }

            void await_resume() noexcept
            {
                // Called when inner exhausted and outer resumes
            }
        };

        flatten_awaiter yield_value(chunked_response<T> inner)
        {
            // Note: Inner's metadata is intentionally ignored - only outer's metadata is used
            data_yielded_ = true;  // Yielding an inner generator counts as data yield
            return {std::move(inner)};
        }

        void return_void() noexcept {}

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }

        // await_transform to support co_await on asio awaitables inside the generator
        template <typename U, typename Executor>
        auto await_transform(boost::asio::awaitable<U, Executor> aw)
        {
            struct awaitable_bridge
            {
                boost::asio::awaitable<U, Executor> inner;
                promise_type& promise;
                std::unique_ptr<U> result;
                std::exception_ptr eptr;

                bool await_ready() noexcept { return false; }

                void await_suspend(handle_type h)
                {
                    auto& p = h.promise();
                    if (!p.executor_)
                    {
                        eptr = std::make_exception_ptr(
                            std::runtime_error("chunked_response: no executor set for co_await"));
                        h.resume();
                        return;
                    }

                    // Wrap the awaitable to return optional<U> to avoid default construction
                    // requirement in co_spawn's error path
                    auto wrapper = [aw = std::move(inner)]() mutable
                        -> boost::asio::awaitable<std::optional<U>, Executor>
                    {
                        co_return std::optional<U>{co_await std::move(aw)};
                    };

                    auto completion_handler = [h, this](std::exception_ptr ep, std::optional<U> val) mutable
                    {
                        if (ep)
                            eptr = ep;
                        else if (val)
                            result = std::make_unique<U>(std::move(*val));
                        h.resume();
                    };

                    if (p.cancellation_slot_ && p.cancellation_slot_->is_connected())
                    {
                        boost::asio::co_spawn(
                            *p.executor_,
                            wrapper(),
                            boost::asio::bind_cancellation_slot(
                                *p.cancellation_slot_,
                                std::move(completion_handler)));
                    }
                    else
                    {
                        boost::asio::co_spawn(
                            *p.executor_,
                            wrapper(),
                            std::move(completion_handler));
                    }
                }

                U await_resume()
                {
                    if (eptr)
                        std::rethrow_exception(eptr);
                    return std::move(*result);
                }
            };

            return awaitable_bridge{std::move(aw), *this, nullptr, {}};
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
                            std::runtime_error("chunked_response: no executor set for co_await"));
                        h.resume();
                        return;
                    }

                    auto completion_handler = [h, this](std::exception_ptr ep) mutable
                    {
                        if (ep)
                            eptr = ep;
                        h.resume();
                    };

                    if (p.cancellation_slot_ && p.cancellation_slot_->is_connected())
                    {
                        boost::asio::co_spawn(
                            *p.executor_,
                            std::move(inner),
                            boost::asio::bind_cancellation_slot(
                                *p.cancellation_slot_,
                                std::move(completion_handler)));
                    }
                    else
                    {
                        boost::asio::co_spawn(
                            *p.executor_,
                            std::move(inner),
                            std::move(completion_handler));
                    }
                }

                void await_resume()
                {
                    if (eptr)
                        std::rethrow_exception(eptr);
                }
            };

            return awaitable_bridge_void{std::move(aw), *this, {}};
        }

        // Support for co_await boost::asio::this_coro::executor
        auto await_transform(boost::asio::this_coro::executor_t) noexcept
        {
            struct executor_awaiter
            {
                promise_type& promise;

                bool await_ready() const noexcept { return true; }
                void await_suspend(handle_type) noexcept { }
                executor_type await_resume() const
                {
                    if (!promise.executor_)
                        throw std::runtime_error("chunked_response: no executor set");
                    return *promise.executor_;
                }
            };
            return executor_awaiter{*this};
        }

        // Support for deferred operations (e.g., timer.async_wait() without explicit token)
        template <typename Deferred>
        auto await_transform(Deferred&& d)
            -> decltype(
                this->await_transform(
                    std::forward<Deferred>(d)(
                        boost::asio::use_awaitable_t<executor_type>{})))
            requires boost::asio::is_deferred<std::decay_t<Deferred>>::value
        {
            // Invoke the deferred operation with use_awaitable to produce an awaitable,
            // then forward to our existing awaitable bridge
            return await_transform(
                std::forward<Deferred>(d)(
                    boost::asio::use_awaitable_t<executor_type>{}));
        }

        // Support for co_await boost::asio::this_coro::cancellation_state
        auto await_transform(boost::asio::this_coro::cancellation_state_t) noexcept
        {
            struct cancellation_state_awaiter
            {
                promise_type& promise;

                bool await_ready() const noexcept { return true; }
                void await_suspend(handle_type) noexcept {}

                boost::asio::cancellation_state await_resume() const noexcept
                {
                    if (promise.cancellation_state_)
                    {
                        return *promise.cancellation_state_;
                    }
                    return boost::asio::cancellation_state{};
                }
            };
            return cancellation_state_awaiter{*this};
        }

        std::optional<T> current_value_;
        completion_handler_type completion_handler_;
        std::optional<executor_type> executor_;
        std::exception_ptr exception_;
        std::optional<boost::asio::cancellation_slot> cancellation_slot_;
        boost::asio::cancellation_signal internal_signal_;
        std::optional<boost::asio::cancellation_state> cancellation_state_;
        boost::beast::http::status status_ = boost::beast::http::status::ok;
        std::vector<chunked_set_header> headers_;
        bool data_yielded_ = false;

        // Flattening state
        std::optional<chunked_response<T>> flattening_inner_storage_;
        chunked_response<T>* flattening_inner_ = nullptr;
        handle_type flattening_outer_handle_ = nullptr;
    };

    chunked_response() noexcept = default;

    explicit chunked_response(handle_type h) noexcept
        : handle_(h)
    {}

    chunked_response(chunked_response&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr))
    {}

    chunked_response& operator=(chunked_response&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    chunked_response(chunked_response const&) = delete;
    chunked_response& operator=(chunked_response const&) = delete;

    ~chunked_response()
    {
        if (handle_)
            handle_.destroy();
    }

    void set_executor(executor_type executor)
    {
        if (handle_)
            handle_.promise().executor_ = std::move(executor);
    }

    void set_cancellation_slot(boost::asio::cancellation_slot slot)
    {
        if (handle_ && slot.is_connected())
        {
            auto& promise = handle_.promise();
            promise.cancellation_slot_ = slot;

            // Create state from our OWN internal signal (starts clean)
            promise.cancellation_state_.emplace(
                promise.internal_signal_.slot(),
                boost::asio::enable_total_cancellation{});

            // Forward cancellation from external slot to our internal signal
            slot.assign([&sig = promise.internal_signal_](boost::asio::cancellation_type_t type)
            {
                sig.emit(type);
            });
        }
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

    boost::beast::http::status status() const
    {
        if (handle_)
            return handle_.promise().status_;
        return boost::beast::http::status::ok;
    }

    std::vector<chunked_set_header> const& headers() const
    {
        static std::vector<chunked_set_header> const empty;
        if (handle_)
            return handle_.promise().headers_;
        return empty;
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
                next_impl(std::move(handler));
            },
            token_type{});
    }

private:
    template <typename Handler>
    void next_impl(Handler handler)
    {
        using boost::taar::awaitable;

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

        // Check if we're in the middle of flattening an inner generator
        if (promise.flattening_inner_)
        {
            auto* inner = promise.flattening_inner_;
            auto exec = boost::asio::get_associated_executor(handler);

            auto completion_handler = [this, handler = std::move(handler)](
                std::exception_ptr ep,
                std::tuple<boost::system::error_code, std::optional<T>> result) mutable
            {
                auto& promise = handle_.promise();

                if (ep)
                {
                    // Inner generator threw during iteration
                    promise.exception_ = ep;
                    promise.flattening_inner_ = nullptr;
                    promise.flattening_inner_storage_.reset();
                    promise.flattening_outer_handle_ = nullptr;
                    handler(boost::system::error_code{}, std::nullopt);
                    return;
                }

                auto [ec, val] = std::move(result);
                if (val)
                {
                    // Inner yielded a value - pass to consumer
                    handler(ec, std::move(val));
                }
                else
                {
                    // Inner exhausted - check for inner's exception
                    auto* inner_ptr = promise.flattening_inner_;
                    try
                    {
                        inner_ptr->rethrow_if_exception();
                    }
                    catch (...)
                    {
                        promise.exception_ = std::current_exception();
                        promise.flattening_inner_ = nullptr;
                        promise.flattening_inner_storage_.reset();
                        promise.flattening_outer_handle_ = nullptr;
                        handler(boost::system::error_code{}, std::nullopt);
                        return;
                    }

                    // Clear flattening state
                    promise.flattening_inner_ = nullptr;
                    promise.flattening_inner_storage_.reset();
                    auto outer_handle = promise.flattening_outer_handle_;
                    promise.flattening_outer_handle_ = nullptr;

                    // Resume outer coroutine to get next value
                    promise.completion_handler_ =
                        [handler = std::move(handler)](std::optional<T> v) mutable
                        {
                            handler(boost::system::error_code{}, std::move(v));
                        };
                    outer_handle.resume();
                }
            };

            // Use co_spawn to drive the inner generator's next()
            if (promise.cancellation_slot_ && promise.cancellation_slot_->is_connected())
            {
                boost::asio::co_spawn(exec,
                    [inner]() -> awaitable<std::tuple<boost::system::error_code, std::optional<T>>>
                    {
                        co_return co_await inner->next();
                    },
                    boost::asio::bind_cancellation_slot(
                        *promise.cancellation_slot_,
                        std::move(completion_handler)));
            }
            else
            {
                boost::asio::co_spawn(exec,
                    [inner]() -> awaitable<std::tuple<boost::system::error_code, std::optional<T>>>
                    {
                        co_return co_await inner->next();
                    },
                    std::move(completion_handler));
            }
            return;
        }

        // Normal path: set up completion handler and resume
        promise.completion_handler_ =
            [handler = std::move(handler)](std::optional<T> value) mutable
            {
                handler(boost::system::error_code{}, std::move(value));
            };

        handle_.resume();
    }

    handle_type handle_ = nullptr;
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CHUNKED_RESPONSE_HPP
