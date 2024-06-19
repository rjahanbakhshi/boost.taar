//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_MATCHER_METHOD_HPP
#define BOOST_WEB_MATCHER_METHOD_HPP

#include <boost/web/matcher/context.hpp>
#include <boost/web/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>

namespace boost::web::matcher {

template<class FieldsType = boost::beast::http::fields>
struct method_t
{
    using request_type = boost::beast::http::request_header<FieldsType>;

    friend auto operator==(method_t, std::string verb)
    {
        return matcher::operand
        {
            [verb = std::move(verb)](
                const request_type& request,
                context& context)
            {
                return request.method_string() == verb;
            }
        };
    }

    friend auto operator==(method_t, boost::beast::http::verb verb)
    {
        return matcher::operand
        {
            [verb = std::move(verb)](
                const request_type& request,
                context& context)
            {
                return request.method() == verb;
            }
        };
    }

    friend auto operator==(std::string verb, method_t)
    {
        return operator==(method_t{}, std::move(verb));
    }

    friend auto operator==(boost::beast::http::verb verb, method_t)
    {
        return operator==(method_t{}, std::move(verb));
    }

    friend auto operator!=(method_t, std::string verb)
    {
        return !operator==(method_t{}, std::move(verb));
    }

    friend auto operator!=(method_t, boost::beast::http::verb verb)
    {
        return !operator==(method_t{}, std::move(verb));
    }

    friend auto operator!=(std::string verb, method_t)
    {
        return operator!=(method_t{}, std::move(verb));
    }

    friend auto operator!=(boost::beast::http::verb verb, method_t)
    {
        return operator!=(method_t{}, std::move(verb));
    }
};

template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_method = method_t<FieldsType>{};

constexpr auto method = basic_method<boost::beast::http::fields>;

} // namespace boost::web::matcher

#endif // BOOST_WEB_MATCHER_METHOD_HPP
