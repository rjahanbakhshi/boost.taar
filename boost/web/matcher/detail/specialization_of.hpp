//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_MATCHER_DETAIL_SPECIALIZATION_OF_HPP
#define BOOST_WEB_MATCHER_DETAIL_SPECIALIZATION_OF_HPP

#include <type_traits>

namespace boost::web::matcher {
namespace detail {

template <template <typename...> class, typename>
struct is_specialization_of : std::false_type {};

template <template <typename...> class TemplateType, typename... ArgsType>
struct is_specialization_of<TemplateType, TemplateType<ArgsType...>>
    : std::true_type {};

template <template <typename...> class TemplateType, typename SpecializationType>
inline constexpr bool is_specialization_of_v =
    is_specialization_of<TemplateType, SpecializationType>::value;

template <template <typename...> class TemplateType, typename SpecializationType>
concept specialization_of =
    is_specialization_of_v<TemplateType, SpecializationType>;

} // detail
} // namespace boost::web::matcher

#endif // BOOST_WEB_MATCHER_DETAIL_SPECIALIZATION_OF_HPP
