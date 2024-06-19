//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_MATCHER_DETAIL_SUPER_TYPE_HPP
#define BOOST_WEB_MATCHER_DETAIL_SUPER_TYPE_HPP

#include <type_traits>

namespace boost::web::matcher {
namespace detail {

// Type trait evaluating to the super type between two types
template <typename T, typename U>
struct super_type
{
    using type = std::conditional_t<
        std::is_base_of_v<T, U>,
        U,
        std::conditional_t<
            std::is_base_of_v<U, T>,
            T,
            std::common_type_t<T, U>
        >
    >;
};
template <typename T, typename U>
using super_type_t = typename super_type<T, U>::type;

} // detail
} // namespace boost::web::matcher

#endif // BOOST_WEB_MATCHER_DETAIL_SUPER_TYPE_HPP
