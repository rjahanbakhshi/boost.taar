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

/** A placeholder that produces matchers over individual request cookies.

    `cookie_t` is not used directly; call its function-call operator with a
    cookie name to obtain a @ref cookie_item, then compose that with the
    equality operators or with the free function @ref exist.

    The session is responsible for parsing the `Cookie` header into a
    @ref boost::taar::cookies map and passing it to the matcher. As a result
    cookie matchers can only be used inside a session that has cookie
    parsing enabled.

    @code
    using boost::taar::matcher::cookie;

    auto logged_in = exist(cookie("session"));
    auto admin     = cookie("role") == "admin";
    @endcode

    @tparam FieldsType The Beast HTTP fields container of the request to
                       inspect. Defaults to `boost::beast::http::fields`.
*/
template <class FieldsType = boost::beast::http::fields>
struct cookie_t
{
    /// The request type these matchers accept.
    using request_type = boost::beast::http::request_header<FieldsType>;

    /// A handle to a particular cookie by name.
    struct cookie_item
    {
        /// The cookie name this item refers to.
        std::string name;

        friend auto operator==(cookie_item lhs, std::string rhs)
        {
            return matcher::operand
            {
                [lhs = std::move(lhs), rhs = std::move(rhs)](
                    request_type const&,
                    context&,
                    cookies const& parsed_cookies)
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

        /// Match requests that carry @a cookie, regardless of its value.
        friend auto exist(cookie_item cookie)
        {
            return matcher::operand
            {
                [cookie = std::move(cookie)](
                    request_type const&,
                    context&,
                    cookies const& parsed_cookies)
                {
                    return parsed_cookies.contains(cookie.name);
                }
            };
        }
    };

    /// Build a @ref cookie_item for the cookie named @a name.
    auto operator()(std::string name) const
    {
        return cookie_item {std::move(name)};
    }
};

/// A cookie-matcher placeholder parameterised by the Fields type.
template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_cookie = cookie_t<FieldsType>{};

/// The default cookie-matcher placeholder.
constexpr auto cookie = basic_cookie<boost::beast::http::fields>;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_COOKIE_HPP
