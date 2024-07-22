//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HTTP_CORE_IGNORE_AND_RETHROW_HPP
#define BOOST_TAAR_HTTP_CORE_IGNORE_AND_RETHROW_HPP

#include <boost/beast/http/message.hpp>

namespace boost::taar {
namespace detail {

template<class T>
class is_http_response_impl
{
    template<class F>
    static std::true_type check(boost::beast::http::response<F> const*);
    static std::false_type check(...);

public:
    using type = decltype(check((T*)0));
};

} // namespace detail

template<class T>
using is_http_response = typename detail::is_http_response_impl<T>::type;

template<class T>
inline constexpr bool is_http_response_v = is_http_response<T>::value;

} // namespace boost::taar

#endif // BOOST_TAAR_HTTP_CORE_IGNORE_AND_RETHROW_HPP
