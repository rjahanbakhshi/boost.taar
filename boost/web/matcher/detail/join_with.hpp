//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_MATCHER_DETAIL_JOIN_WITH_HPP
#define BOOST_WEB_MATCHER_DETAIL_JOIN_WITH_HPP

#include <ranges>

#if (__cpp_lib_ranges_join_with < 202202L)
#    include <concepts>
#    include <string_view>
#    include <utility>
#endif

namespace boost::web::matcher {
namespace detail {

#if (__cpp_lib_ranges_join_with >= 202202L)

using std::views::join_with;

#else

// A sloppy implementation of std::views::join_with.
template <std::ranges::input_range R, std::copy_constructible T> requires (
    std::constructible_from<std::string_view, std::ranges::range_value_t<R>> &&
    (std::constructible_from<std::string_view, T> || std::same_as<T, char>))
class join_with_view : public std::ranges::view_interface<join_with_view<R, T>>
{
private:
    R base_;
    T separator_;

public:
    join_with_view() = default;

    join_with_view(R base, T separator)
        : base_(std::move(base))
        , separator_(std::move(separator))
    {}

    struct iterator
    {
        using BaseIter = std::ranges::iterator_t<R>;
        BaseIter current_;
        BaseIter end_;
        const T* separator_;
        bool at_separator_;

        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        iterator() = default;
        iterator(BaseIter current, BaseIter end, const T* separator)
            : current_(current)
            , end_(end)
            , separator_(separator)
            , at_separator_(false)
        {}

        value_type operator*() const
        {
            if (at_separator_)
            {
                if constexpr (std::same_as<T, char>)
                    return value_type(separator_, 1);
                else
                    return value_type(*separator_);
            }
            return value_type {*current_};
        }

        iterator& operator++()
        {
            if (!at_separator_)
            {
                if (++current_ != end_) at_separator_ = true;
            }
            else
                at_separator_ = false;
            return *this;
        }

        iterator operator++(int)
        {
            iterator tmp = *this;
            ++*this;
            return tmp;
        }

        friend bool operator==(const iterator& x, const iterator& y)
        {
            return x.current_ == y.current_ && x.at_separator_ == y.at_separator_;
        }

        friend bool operator!=(const iterator& x, const iterator& y)
        {
            return !(x == y);
        }
    };

    auto begin() const
    {
        return iterator(std::ranges::begin(base_), std::ranges::end(base_), &separator_);
    }

    auto end() const
    {
        return iterator(std::ranges::end(base_), std::ranges::end(base_), &separator_);
    }
};

template <class R, class T>
join_with_view(R&&, T) -> join_with_view<std::views::all_t<R>, T>;

template <std::copy_constructible T>
struct join_with_adapter
{
    T separator_;

    join_with_adapter(T separator)
        : separator_ {std::move(separator)}
    {}

    template <std::ranges::input_range R>
    auto operator()(R&& r) const
    {
        return join_with_view{std::forward<R>(r), std::move(separator_)};
    }

    template <std::ranges::input_range R>
    friend auto operator|(R&& r, join_with_adapter a)
    {
        return join_with_view{std::forward<R>(r), std::move(a.separator_)};
    }
};

template <std::copy_constructible T>
inline constexpr auto join_with(T&& separator)
{
    return join_with_adapter {std::forward<T>(separator)};
};

#endif

} // detail
} // namespace boost::web::matcher

#endif // BOOST_WEB_MATCHER_DETAIL_JOIN_WITH_HPP
