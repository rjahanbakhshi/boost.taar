//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HTTP_MATCHER_DETAIL_CALLABLE_TRAITS_HPP
#define BOOST_WEB_HTTP_MATCHER_DETAIL_CALLABLE_TRAITS_HPP

#include <type_traits>

namespace boost::web::matcher {
namespace detail {

template <typename MatcherType>
struct arg1
{};

template <typename ResultType, typename RequestType, typename... ArgsType>
struct arg1<ResultType(*)(RequestType, ArgsType...)>
{
    using type = std::remove_cvref_t<RequestType>;
};

template <typename ObjectType, typename ResultType, typename RequestType, typename... ArgsType>
struct arg1<ResultType(ObjectType::*)(RequestType, ArgsType...)>
{
    using type = std::remove_cvref_t<RequestType>;
};

template <typename ObjectType, typename ResultType, typename RequestType, typename... ArgsType>
struct arg1<ResultType(ObjectType::*)(RequestType, ArgsType...)const>
{
    using type = std::remove_cvref_t<RequestType>;
};

template <typename MatcherType>
using arg1_t = typename arg1<MatcherType>::type;

} // detail
} // namespace boost::web::matcher

#endif // BOOST_WEB_HTTP_MATCHER_DETAIL_CALLABLE_TRAITS_HPP
