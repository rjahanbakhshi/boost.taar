//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_RESPONSE_FROM_TAG_HPP
#define BOOST_TAAR_CORE_RESPONSE_FROM_TAG_HPP

namespace boost::taar {

/** Tag used to customise @ref response_from for user-defined types.

    To teach Boost.Taar how to convert a value of your type `MyType` to an
    HTTP response, declare a `tag_invoke` overload in the same namespace as
    `MyType`:

    @code
    auto tag_invoke(boost::taar::response_from_tag<my::Type>, my::Type const& v)
    {
        namespace http = boost::beast::http;
        http::response<http::string_body> r {http::status::ok, 11};
        r.set(http::field::content_type, "application/x.my-type");
        r.body() = serialize(v);
        r.prepare_payload();
        return r;
    }
    @endcode

    The overload may take any number of value arguments. The matching
    `response_from_tag<T1, T2, ...>` specialisation will be used. The
    returned type must satisfy
    @ref boost::taar::is_http_response or be a
    `boost::beast::http::message_generator`.

    A user-defined overload takes precedence over the built-in conversions
    for `std::string`, `std::string_view`, `char const*`, integral and
    floating-point values, `boost::json::value`, and byte buffers.

    @tparam T The decayed argument types of the wrapped value.
*/
template <typename... T>
struct response_from_tag {};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_RESPONSE_FROM_TAG_HPP
