//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_JSONRPC_ENVELOPE_HPP
#define BOOST_TAAR_JSONRPC_ENVELOPE_HPP

#include <boost/taar/jsonrpc/error.hpp>
#include <boost/json/value.hpp>
#include <boost/json/object.hpp>
#include <boost/system/result.hpp>
#include <optional>
#include <string>

namespace boost::taar::jsonrpc {

/** Parsed JSON-RPC 2.0 request envelope.

    Mirrors the spec request object: a fixed `"jsonrpc"` version of `"2.0"`,
    a method name, optional structured parameters, and an optional id. An
    envelope without an id is a *notification* — the server processes it
    but produces no response.

    The id field is stored as a `boost::json::value` because the spec
    allows strings, numbers, and `null`. Notification envelopes leave the
    optional empty; requests carry the value verbatim so the response can
    echo it back unchanged.

    `params` is similarly optional: present requests may omit the field
    entirely. The spec says when present it MUST be an object or array,
    but the parser leaves shape validation to handlers (which will report
    `invalid_params` on type mismatch) so a future stricter mode can be
    layered on without breaking the parser's contract.
 */
struct envelope
{
    /// Always `"2.0"` for a successfully-parsed envelope.
    std::string jsonrpc;

    /// Method name. Required and non-empty when populated by the parser.
    std::string method;

    /// Request id. Empty when the envelope is a notification.
    std::optional<boost::json::value> id;

    /// Method parameters. Empty when the envelope omits `"params"`.
    std::optional<boost::json::value> params;
};

/// True if @a env carries no id and is therefore a notification.
inline bool is_notification(envelope const& env) noexcept
{
    return !env.id.has_value();
}

/** Parse a JSON value as a JSON-RPC 2.0 request envelope.

    Validates the spec's required structure: @a value must be a JSON
    object, must contain a `"jsonrpc"` string equal to `"2.0"`, and must
    contain a `"method"` string. The `"id"` and `"params"` fields are
    copied into the result when present and left empty otherwise.

    @returns A populated @ref envelope on success, or
             `boost::taar::jsonrpc::error::invalid_request` on any
             structural error. Note that JSON-level parse failure is
             outside this function's scope — callers that hand-roll JSON
             parsing should map `boost::json::parse` errors to
             `error::parse_error` themselves.

    Batch requests (top-level arrays of envelopes) are not handled here.
    `jsonrpc::endpoint` iterates a parsed array and invokes this function
    per element; per the specification, an empty batch is reported as a
    single `invalid_request` response.
 */
inline boost::system::result<envelope> parse_envelope(
    boost::json::value const& value)
{
    auto const* obj = value.if_object();
    if (obj == nullptr)
    {
        return error::invalid_request;
    }

    auto const* jsonrpc_field = obj->if_contains("jsonrpc");
    if (jsonrpc_field == nullptr)
    {
        return error::invalid_request;
    }
    auto const* jsonrpc_str = jsonrpc_field->if_string();
    if (jsonrpc_str == nullptr || *jsonrpc_str != "2.0")
    {
        return error::invalid_request;
    }

    auto const* method_field = obj->if_contains("method");
    if (method_field == nullptr)
    {
        return error::invalid_request;
    }
    auto const* method_str = method_field->if_string();
    if (method_str == nullptr)
    {
        return error::invalid_request;
    }

    envelope result;
    result.jsonrpc = "2.0";
    result.method = std::string {method_str->begin(), method_str->end()};

    if (auto const* id_field = obj->if_contains("id"))
    {
        result.id = *id_field;
    }

    if (auto const* params_field = obj->if_contains("params"))
    {
        result.params = *params_field;
    }

    return result;
}

} // namespace boost::taar::jsonrpc

#endif // BOOST_TAAR_JSONRPC_ENVELOPE_HPP
