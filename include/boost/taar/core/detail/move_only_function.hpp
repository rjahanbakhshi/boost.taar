//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_CORE_DETAIL_MOVE_ONLY_FUNCTION_HPP
#define BOOST_TAAR_CORE_DETAIL_MOVE_ONLY_FUNCTION_HPP

#include <functional>

// Define BOOST_TAAR_NO_STD_MOVE_ONLY_FUNCTION to use the fallback below
// even when the standard library provides std::move_only_function.
#if defined(__cpp_lib_move_only_function) && !defined(BOOST_TAAR_NO_STD_MOVE_ONLY_FUNCTION)
#    define BOOST_TAAR_HAS_STD_MOVE_ONLY_FUNCTION 1
#else
#    include <cstddef>
#    include <memory>
#    include <type_traits>
#    include <utility>
#endif

namespace boost::taar::detail {

#if defined(BOOST_TAAR_HAS_STD_MOVE_ONLY_FUNCTION)

template <typename Signature>
using move_only_function = std::move_only_function<Signature>;

#else

template <typename Signature>
class move_only_function;

/** Minimal stand-in for `std::move_only_function<R(Args...)>`.

    Used where the standard library lacks it (libc++ up to at least 19).
    Supports only the unqualified `R(Args...)` signature, which is the only
    form the library uses: construction from a move-constructible callable,
    move, reset to `nullptr`, `operator bool` and a non-const call.
*/
template <typename R, typename... Args>
class move_only_function<R(Args...)>
{
    struct callable_base
    {
        virtual ~callable_base() = default;
        virtual R call(Args&&... args) = 0;
    };

    template <typename F>
    struct callable final : callable_base
    {
        template <typename G>
        explicit callable(G&& g) : f(std::forward<G>(g)) {}

        R call(Args&&... args) override
        {
            if constexpr (std::is_void_v<R>)
                std::invoke(f, std::forward<Args>(args)...);
            else
                return std::invoke(f, std::forward<Args>(args)...);
        }

        F f;
    };

    template <typename F>
    static constexpr bool is_null(F const& f) noexcept
    {
        if constexpr (std::is_pointer_v<F> || std::is_member_pointer_v<F>)
            return f == nullptr;
        else
            return false;
    }

public:
    using result_type = R;

    move_only_function() noexcept = default;
    move_only_function(std::nullptr_t) noexcept {}
    move_only_function(move_only_function&&) noexcept = default;
    move_only_function(move_only_function const&) = delete;

    template <typename F>
        requires (
            !std::is_same_v<std::remove_cvref_t<F>, move_only_function> &&
            std::is_constructible_v<std::decay_t<F>, F> &&
            std::is_invocable_r_v<R, std::decay_t<F>&, Args...>)
    move_only_function(F&& f)
    {
        if (!is_null(f))
            impl_ = std::make_unique<callable<std::decay_t<F>>>(std::forward<F>(f));
    }

    move_only_function& operator=(move_only_function&&) noexcept = default;
    move_only_function& operator=(move_only_function const&) = delete;

    move_only_function& operator=(std::nullptr_t) noexcept
    {
        impl_.reset();
        return *this;
    }

    template <typename F>
        requires std::is_constructible_v<move_only_function, F>
    move_only_function& operator=(F&& f)
    {
        move_only_function {std::forward<F>(f)}.swap(*this);
        return *this;
    }

    ~move_only_function() = default;

    void swap(move_only_function& other) noexcept { impl_.swap(other.impl_); }
    friend void swap(move_only_function& a, move_only_function& b) noexcept { a.swap(b); }

    explicit operator bool() const noexcept { return static_cast<bool>(impl_); }

    friend bool operator==(move_only_function const& f, std::nullptr_t) noexcept { return !f; }

    R operator()(Args... args)
    {
        if (!impl_)
            throw std::bad_function_call {};
        return impl_->call(std::forward<Args>(args)...);
    }

private:
    std::unique_ptr<callable_base> impl_;
};

#endif

} // namespace boost::taar::detail

#endif // BOOST_TAAR_CORE_DETAIL_MOVE_ONLY_FUNCTION_HPP
