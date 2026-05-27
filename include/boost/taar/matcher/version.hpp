//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_VERSION_HPP
#define BOOST_TAAR_MATCHER_VERSION_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>

namespace boost::taar::matcher {

/** A placeholder that produces matchers comparing against a request's HTTP version.

    HTTP versions are encoded as a single unsigned integer in Beast: HTTP/1.0
    is `10`, HTTP/1.1 is `11`, HTTP/2.0 is `20`. Use the relational and
    equality operators to build a matcher:

    @code
    using boost::taar::matcher::version;

    auto only_11      = version == 11;
    auto at_least_11  = version >= 11;
    auto pre_2        = version <  20;
    @endcode

    @tparam FieldsType The Beast HTTP fields container of the request to
                       inspect. Defaults to `boost::beast::http::fields`.
*/
template<class FieldsType = boost::beast::http::fields>
struct version_t
{
    /// The request type these matchers accept.
    using request_type = boost::beast::http::request_header<FieldsType>;

    friend auto operator==(version_t, unsigned ver)
    {
        return matcher::operand
        {
            [ver = std::move(ver)](
                request_type const& request,
                context& context)
            {
                return request.version() == ver;
            }
        };
    }

    friend auto operator==(unsigned ver, version_t)
    {
        return operator==(version_t{}, ver);
    }

    friend auto operator!=(version_t, unsigned ver)
    {
        return !operator==(version_t{}, ver);
    }

    friend auto operator!=(unsigned ver, version_t)
    {
        return operator!=(version_t{}, ver);
    }

    friend auto operator<(version_t, unsigned ver)
    {
        return matcher::operand
        {
            [ver = std::move(ver)](
                request_type const& request,
                context& context)
            {
                return request.version() < ver;
            }
        };
    }

    friend auto operator<(unsigned ver, version_t)
    {
        return operator>(version_t{}, ver);
    }

    friend auto operator>(version_t, unsigned ver)
    {
        return matcher::operand
        {
            [ver = std::move(ver)](
                request_type const& request,
                context& context)
            {
                return request.version() > ver;
            }
        };
    }

    friend auto operator>(unsigned ver, version_t)
    {
        return operator<(version_t{}, ver);
    }

    friend auto operator<=(version_t, unsigned ver)
    {
        return operator<(version_t{}, ver) || operator==(version_t{}, ver);
    }

    friend auto operator<=(unsigned ver, version_t)
    {
        return operator>=(version_t{}, ver);
    }

    friend auto operator>=(version_t, unsigned ver)
    {
        return operator>(version_t{}, ver) || operator==(version_t{}, ver);
    }

    friend auto operator>=(unsigned ver, version_t)
    {
        return operator<=(version_t{}, ver);
    }
};

/// A version-matcher placeholder parameterised by the Fields type.
template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_version = version_t<FieldsType>{};

/// The default version-matcher placeholder.
constexpr auto version = basic_version<boost::beast::http::fields>;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_VERSION_HPP
