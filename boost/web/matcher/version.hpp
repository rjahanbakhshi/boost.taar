//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_MATCHER_VERSION_HPP
#define BOOST_WEB_MATCHER_VERSION_HPP

#include <boost/web/matcher/context.hpp>
#include <boost/web/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>

namespace boost::web::matcher {

template<class FieldsType = boost::beast::http::fields>
struct version_t
{
    using request_type = boost::beast::http::request_header<FieldsType>;

    friend auto operator==(version_t, unsigned ver)
    {
        return matcher::operand
        {
            [ver = std::move(ver)](
                const request_type& request,
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
                const request_type& request,
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
                const request_type& request,
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

template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_version = version_t<FieldsType>{};

constexpr auto version = basic_version<boost::beast::http::fields>;

} // namespace boost::web::matcher

#endif // BOOST_WEB_MATCHER_VERSION_HPP
