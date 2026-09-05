//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_SESSION_KEY_HPP
#define BOOST_TAAR_HANDLER_SESSION_KEY_HPP

#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/cookies.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <optional>
#include <string>
#include <utility>

namespace boost::taar::handler {

/// How @ref with_session treats a request whose key is absent or unknown.
enum class session_policy
{
    /// Mint a new key, create the entry, and tell the client the key in the response.
    create_if_missing,
    /// Reply 401 Unauthorized without running the inner handler.
    require,
};

/** Key extractors for @ref with_session.

    An extractor reads the session key out of a request and, when a key was
    minted, writes it into the response so the client can present it next
    time.
*/
namespace session_key {

/** Session key carried in a cookie.

    `extract` scans every `Cookie` header for @a name. `attach` adds a
    `Set-Cookie` header with the given attributes. The default attributes
    are `Path=/; HttpOnly; SameSite=Lax`; add `Secure` when serving over
    TLS.
*/
struct cookie
{
    explicit cookie(
        std::string name,
        std::string attributes = "Path=/; HttpOnly; SameSite=Lax")
        : name {std::move(name)}
        , attributes {std::move(attributes)}
    {}

    std::string name;
    std::string attributes;

    std::optional<std::string> extract(
        boost::beast::http::request_header<> const& request) const
    {
        auto value = cookie_arg {name}(request, matcher::context {});
        if (!value) return std::nullopt;
        return std::move(*value);
    }

    template <typename Body, typename Fields>
    void attach(
        boost::beast::http::response<Body, Fields>& response,
        std::string const& key) const
    {
        response.insert(boost::beast::http::field::set_cookie, key_header(key).value);
    }

    /// The same `Set-Cookie` header as a token for a @ref chunked_response.
    chunked_set_header key_header(std::string const& key) const
    {
        std::string value = name + "=" + key;
        if (!attributes.empty())
        {
            value += "; " + attributes;
        }
        return set_header(boost::beast::http::field::set_cookie, std::move(value));
    }
};

/** Session key carried in a request header, echoed in the same response header. */
struct header
{
    explicit header(std::string name)
        : name {std::move(name)}
    {}

    std::string name;

    std::optional<std::string> extract(
        boost::beast::http::request_header<> const& request) const
    {
        auto it = request.find(name);
        if (it == request.end())
        {
            return std::nullopt;
        }
        return std::string {it->value()};
    }

    template <typename Body, typename Fields>
    void attach(
        boost::beast::http::response<Body, Fields>& response,
        std::string const& key) const
    {
        response.set(name, key);
    }

    /// The same header as a token for a @ref chunked_response.
    chunked_set_header key_header(std::string const& key) const
    {
        return set_header(name, key);
    }
};

} // namespace session_key

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_SESSION_KEY_HPP
