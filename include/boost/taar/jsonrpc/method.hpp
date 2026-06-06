//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_JSONRPC_METHOD_HPP
#define BOOST_TAAR_JSONRPC_METHOD_HPP

#include <boost/taar/jsonrpc/envelope.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/json/value.hpp>
#include <string>
#include <utility>

namespace boost::taar::jsonrpc {

/** A placeholder that produces matchers over a JSON-RPC envelope's method.

    Instances of `method_t` are not consumed directly. Apply `==` or `!=`
    against a method name to obtain a matcher that the
    @ref boost::taar::session::http session can use to dispatch a request:

    @code
    using boost::taar::matcher::method;
    using boost::taar::matcher::target;
    namespace http     = boost::beast::http;
    namespace jsonrpc  = boost::taar::jsonrpc;

    session.register_request_handler(
        method == http::verb::post &&
        target == "/jsonrpc"      &&
        jsonrpc::method == "add",
        my_add_handler);
    @endcode

    A `jsonrpc::method` leaf advertises `with_parsed_body` on the
    produced operand. When *any* registered matcher in the session
    requires body caching, the session reads the request body once
    upfront, parses it as JSON, and threads the value into every
    matcher in the chain. The `jsonrpc::method` leaf then attempts to
    parse the JSON value as a JSON-RPC envelope and compares its
    method name. Bodies that are absent, not valid JSON, or not a
    valid envelope simply make the matcher return `false` — they are
    not errors, and the session continues with the next handler.

    The produced operand carries @ref jsonrpc_method_eq_constraint or
    @ref jsonrpc_method_ne_constraint metadata with the captured
    method name in its @ref boost::taar::matcher::operand::captures
    "captures" tuple, so future generators (an OpenAPI / MCP /
    tools-listing producer) can introspect the route.

    @tparam FieldsType The Beast HTTP fields container of the request to
                       inspect. Matched in the same way the HTTP
                       matchers are, so this leaf can compose with
                       `boost::taar::matcher::method == http::verb::post`
                       and friends.
*/
template <class FieldsType = boost::beast::http::fields>
struct method_t
{
    /// The request type these matchers accept.
    using request_type = boost::beast::http::request_header<FieldsType>;

    /// Build a matcher that succeeds when the envelope's method is @a name.
    friend auto operator==(method_t, std::string name)
    {
        matcher::data_tuple_t<matcher::jsonrpc_method_eq_constraint> captures
        {
            matcher::jsonrpc_method_constraint_value {name}
        };

        return matcher::operand
        {
            [name = std::move(name)](
                request_type const& /*request*/,
                matcher::context& /*context*/,
                boost::json::value const& parsed_body)
            {
                auto env = parse_envelope(parsed_body);
                return env.has_value() && env->method == name;
            },
            matcher::jsonrpc_method_eq_constraint {},
            std::move(captures)
        };
    }

    friend auto operator==(std::string name, method_t)
    {
        return operator==(method_t{}, std::move(name));
    }

    /// Build a matcher that succeeds when the envelope's method is *not* @a name.
    ///
    /// Bodies that don't parse as JSON-RPC envelopes are still treated as
    /// non-matches (the matcher returns `false`) — the leaf only succeeds
    /// for a *valid* envelope whose method differs.
    friend auto operator!=(method_t, std::string name)
    {
        matcher::data_tuple_t<matcher::jsonrpc_method_ne_constraint> captures
        {
            matcher::jsonrpc_method_constraint_value {name}
        };

        return matcher::operand
        {
            [name = std::move(name)](
                request_type const& /*request*/,
                matcher::context& /*context*/,
                boost::json::value const& parsed_body)
            {
                auto env = parse_envelope(parsed_body);
                return env.has_value() && env->method != name;
            },
            matcher::jsonrpc_method_ne_constraint {},
            std::move(captures)
        };
    }

    friend auto operator!=(std::string name, method_t)
    {
        return operator!=(method_t{}, std::move(name));
    }
};

/// A JSON-RPC method-matcher placeholder parameterised by the Fields type.
template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_method = method_t<FieldsType>{};

/// The default JSON-RPC method-matcher placeholder.
constexpr auto method = basic_method<boost::beast::http::fields>;

} // namespace boost::taar::jsonrpc

#endif // BOOST_TAAR_JSONRPC_METHOD_HPP
