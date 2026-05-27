//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_METHOD_HPP
#define BOOST_TAAR_MATCHER_METHOD_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>

namespace boost::taar::matcher {

/** A placeholder that produces matchers comparing against a request's HTTP method.

    Instances of `method_t` are not consumed directly. Apply one of the
    relational operators to obtain a matcher that the
    @ref boost::taar::session::http session can use to dispatch a request:

    @code
    using boost::taar::matcher::method;
    namespace http = boost::beast::http;

    auto m = method == http::verb::get;          // matches GET
    auto n = method != "PATCH";                  // matches anything but PATCH
    @endcode

    Both `boost::beast::http::verb` values and arbitrary string verbs are
    accepted. The template parameter selects the request `Fields` type so
    matchers can be paired with non-default beast field containers.

    @tparam FieldsType The Beast HTTP fields container of the request to
                       inspect. Defaults to `boost::beast::http::fields`.
*/
template<class FieldsType = boost::beast::http::fields>
struct method_t
{
    /// The request type these matchers accept.
    using request_type = boost::beast::http::request_header<FieldsType>;

    /// Match requests whose method-string equals @a verb.
    friend auto operator==(method_t, std::string verb)
    {
        return matcher::operand
        {
            [verb = std::move(verb)](
                request_type const& request,
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
                request_type const& request,
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

/** A method-matcher placeholder parameterised by the Fields type.

    Use `basic_method<MyFields>` when your session is built on a non-default
    beast `Fields` container; otherwise prefer @ref method.
*/
template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_method = method_t<FieldsType>{};

/** The default method-matcher placeholder.

    Equivalent to `basic_method<boost::beast::http::fields>`. This is the
    object users normally compose with `==`, `!=`, `&&`, `||`, `!`, etc.
*/
constexpr auto method = basic_method<boost::beast::http::fields>;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_METHOD_HPP
