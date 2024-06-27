//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_MATCHER_DETAIL_RANGES_TO_HPP
#define BOOST_WEB_MATCHER_DETAIL_RANGES_TO_HPP

#include <string>
#include <utility>

namespace boost::web::matcher {
namespace detail {

// std::ranges::to is not available everywhere yet.
template<typename ContainerType>
struct to_container_t
{
    template<typename RangeType>
    ContainerType operator()(RangeType&& range) const
    {
        return ContainerType(
            begin(std::forward<RangeType>(range)),
            end(std::forward<RangeType>(range)) );
    }

    template<typename RangeType>
    friend ContainerType operator|(RangeType&& range, to_container_t self)
    {
        return self(std::forward<RangeType>(range));
    }
};

template<typename... T>
struct to_container_t<std::basic_string<T...>>
{
    template<typename RangeType>
    std::basic_string<T...> operator()(RangeType&& range) const
    {
        std::basic_string<T...> result;
        for (auto const& s : range)
        {
            result += s;
        }
        return result;
    }

    template<typename RangeType>
    friend std::basic_string<T...> operator|(RangeType&& range, to_container_t self)
    {
        return self(std::forward<RangeType>(range));
    }
};

template<typename ContainerType>
constexpr to_container_t<ContainerType> to_container {};

} // detail
} // namespace boost::web::matcher

#endif // BOOST_WEB_MATCHER_DETAIL_RANGES_TO_HPP
