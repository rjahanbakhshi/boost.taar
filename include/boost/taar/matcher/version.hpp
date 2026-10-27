//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_MATCHER_VERSION_HPP
#define BOOST_TAAR_MATCHER_VERSION_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>
#include <utility>

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

    Each relational operator produces a *single-leaf* operand carrying one
    of the @ref version_eq_constraint / @ref version_ne_constraint /
    @ref version_lt_constraint / @ref version_le_constraint /
    @ref version_gt_constraint / @ref version_ge_constraint tags with the
    compared version in its @ref operand::captures "captures" tuple.

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
        data_tuple_t<version_eq_constraint> captures
        {
            version_constraint_value {ver}
        };

        return matcher::operand
        {
            [ver](
                request_type const& request,
                context& context)
            {
                return request.version() == ver;
            },
            version_eq_constraint {},
            std::move(captures)
        };
    }

    friend auto operator==(unsigned ver, version_t)
    {
        return operator==(version_t{}, ver);
    }

    friend auto operator!=(version_t, unsigned ver)
    {
        data_tuple_t<version_ne_constraint> captures
        {
            version_constraint_value {ver}
        };

        return matcher::operand
        {
            [ver](
                request_type const& request,
                context& context)
            {
                return request.version() != ver;
            },
            version_ne_constraint {},
            std::move(captures)
        };
    }

    friend auto operator!=(unsigned ver, version_t)
    {
        return operator!=(version_t{}, ver);
    }

    friend auto operator<(version_t, unsigned ver)
    {
        data_tuple_t<version_lt_constraint> captures
        {
            version_constraint_value {ver}
        };

        return matcher::operand
        {
            [ver](
                request_type const& request,
                context& context)
            {
                return request.version() < ver;
            },
            version_lt_constraint {},
            std::move(captures)
        };
    }

    friend auto operator<(unsigned ver, version_t)
    {
        return operator>(version_t{}, ver);
    }

    friend auto operator>(version_t, unsigned ver)
    {
        data_tuple_t<version_gt_constraint> captures
        {
            version_constraint_value {ver}
        };

        return matcher::operand
        {
            [ver](
                request_type const& request,
                context& context)
            {
                return request.version() > ver;
            },
            version_gt_constraint {},
            std::move(captures)
        };
    }

    friend auto operator>(unsigned ver, version_t)
    {
        return operator<(version_t{}, ver);
    }

    friend auto operator<=(version_t, unsigned ver)
    {
        data_tuple_t<version_le_constraint> captures
        {
            version_constraint_value {ver}
        };

        return matcher::operand
        {
            [ver](
                request_type const& request,
                context& context)
            {
                return request.version() <= ver;
            },
            version_le_constraint {},
            std::move(captures)
        };
    }

    friend auto operator<=(unsigned ver, version_t)
    {
        return operator>=(version_t{}, ver);
    }

    friend auto operator>=(version_t, unsigned ver)
    {
        data_tuple_t<version_ge_constraint> captures
        {
            version_constraint_value {ver}
        };

        return matcher::operand
        {
            [ver](
                request_type const& request,
                context& context)
            {
                return request.version() >= ver;
            },
            version_ge_constraint {},
            std::move(captures)
        };
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
