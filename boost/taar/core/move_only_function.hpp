//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MOVE_ONLY_FUNCTION_HPP
#define BOOST_TAAR_MOVE_ONLY_FUNCTION_HPP

#include <functional>
#include <memory>
#include <utility>
#include <type_traits>

namespace boost::taar {

template <typename SignatureType>
class move_only_function;

template <typename ResultType, typename... ArgsType>
class move_only_function<ResultType(ArgsType...)> {
public:
    move_only_function() noexcept = default;

    move_only_function(const move_only_function&) = delete;
    move_only_function(move_only_function&& other) noexcept = default;
    move_only_function& operator=(const move_only_function&) = delete;
    move_only_function& operator=(move_only_function&& other) noexcept = default;
    ~move_only_function() = default;

    template <typename CallableType>
    move_only_function(CallableType&& callable)
    {
        using decayed_callable_type = std::decay_t<CallableType>;
        if constexpr (std::is_same_v<decayed_callable_type, move_only_function>)
        {
            *this = std::forward<CallableType>(callable);
        }
        else
        {
            impl_ = std::make_unique<impl<decayed_callable_type>>(
                std::forward<CallableType>(callable));
        }
    }

    ResultType operator()(ArgsType... args) const
    {
        if (!impl_)
        {
            throw std::bad_function_call{};
        }
        return impl_->invoke(std::forward<ArgsType>(args)...);
    }

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(impl_);
    }

private:
    struct impl_base
    {
        virtual ~impl_base() = default;
        virtual ResultType invoke(ArgsType...) const = 0;
    };

    template <typename CallableType>
    struct impl final : impl_base
    {
        impl(CallableType&& callable) : callable_(std::move(callable)) {}
        ResultType invoke(ArgsType... args) const override
        {
            return std::invoke(callable_, std::forward<ArgsType>(args)...);
        }
        mutable CallableType callable_;
    };

    std::unique_ptr<impl_base> impl_ = nullptr;
};

} // namespace boost::taar

#endif // BOOST_TAAR_MOVE_ONLY_FUNCTION_HPP

