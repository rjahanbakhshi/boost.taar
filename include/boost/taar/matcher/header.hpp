//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_HEADER_HPP
#define BOOST_TAAR_MATCHER_HEADER_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>

namespace boost::taar::matcher {

/** A placeholder that produces matchers over individual request headers.

    `header_t` is not used directly; call its function-call operator with a
    well-known `boost::beast::http::field` value or a string field name to
    obtain a @ref header_item, then compose that with the equality
    operators or with the free function @ref exist.

    @code
    using boost::taar::matcher::header;
    namespace http = boost::beast::http;

    auto is_json   = header(http::field::content_type) == "application/json";
    auto has_auth  = exist(header("X-Auth"));
    @endcode

    @tparam FieldsType The Beast HTTP fields container of the request to
                       inspect. Defaults to `boost::beast::http::fields`.
*/
template <class FieldsType = boost::beast::http::fields>
struct header_t
{
    /// The request type these matchers accept.
    using request_type = boost::beast::http::request_header<FieldsType>;

    /** A handle to a particular header by key.

        @tparam KeyType Either `boost::beast::http::field` for well-known
                        headers, or `std::string` for custom headers.
    */
    template <typename KeyType>
    struct header_item
    {
        /// The header key this item refers to.
        KeyType key;

        friend auto operator==(header_item lhs, std::string rhs)
        {
            return matcher::operand
            {
                [lhs = std::move(lhs), rhs = std::move(rhs)](
                    request_type const& request,
                    context& context)
                {
                    return request[lhs.key] == rhs;
                }
            };
        }

        friend auto operator==(std::string lhs, header_item rhs)
        {
            return operator==(std::move(rhs), std::move(lhs));
        }

        friend auto operator!=(header_item lhs, std::string rhs)
        {
            return !operator==(std::move(lhs), std::move(rhs));
        }

        friend auto operator!=(std::string lhs, header_item rhs)
        {
            return !operator==(std::move(rhs), std::move(lhs));
        }

        /// Match requests that carry @a header, regardless of its value.
        friend auto exist(header_item header)
        {
            return matcher::operand
            {
                [header = std::move(header)](
                    request_type const& request,
                    context& context)
                {
                    return request.find(header.key) != request.end();
                }
            };
        }
    };

    /// Build a @ref header_item for a well-known Beast field.
    auto operator()(boost::beast::http::field field) const
    {
        return header_item<boost::beast::http::field> {std::move(field)};
    }

    /// Build a @ref header_item for an arbitrary header name.
    auto operator()(std::string field) const
    {
        return header_item<std::string> {std::move(field)};
    }
};

/// A header-matcher placeholder parameterised by the Fields type.
template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_header = header_t<FieldsType>{};

/// The default header-matcher placeholder.
constexpr auto header = basic_header<boost::beast::http::fields>;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_HEADER_HPP
