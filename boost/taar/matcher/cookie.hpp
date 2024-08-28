//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_COOKIE_HPP
#define BOOST_TAAR_MATCHER_COOKIE_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>

namespace boost::taar::matcher {
namespace detail {

} // namespace detail

template <class FieldsType = boost::beast::http::fields>
struct cookie_t
{
    using request_type = boost::beast::http::request_header<FieldsType>;

    struct cookie_item
    {
        std::string name;

        friend auto operator==(cookie_item lhs, std::string rhs)
        {
            return matcher::operand
            {
                [lhs = std::move(lhs), rhs = std::move(rhs)](
                    const request_type&,
                    context&,
                    const cookies& parsed_cookies)
                {
                    auto iter = parsed_cookies.find(lhs.name);
                    return
                        iter != parsed_cookies.end() &&
                        iter->second == rhs;
                }
            };
        }

        friend auto operator==(std::string lhs, cookie_item rhs)
        {
            return operator==(std::move(rhs), std::move(lhs));
        }

        friend auto operator!=(cookie_item lhs, std::string rhs)
        {
            return !operator==(std::move(lhs), std::move(rhs));
        }

        friend auto operator!=(std::string lhs, cookie_item rhs)
        {
            return !operator==(std::move(rhs), std::move(lhs));
        }

        friend auto exist(cookie_item cookie)
        {
            return matcher::operand
            {
                [cookie = std::move(cookie)](
                    const request_type&,
                    context&,
                    const cookies& parsed_cookies)
                {
                    return parsed_cookies.contains(cookie.name);
                }
            };
        }
    };

    auto operator()(std::string name) const
    {
        return cookie_item {std::move(name)};
    }
};

template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_cookie = cookie_t<FieldsType>{};

constexpr auto cookie = basic_cookie<boost::beast::http::fields>;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_COOKIE_HPP
