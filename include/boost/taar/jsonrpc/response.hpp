//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_JSONRPC_RESPONSE_HPP
#define BOOST_TAAR_JSONRPC_RESPONSE_HPP

#include <boost/taar/jsonrpc/error.hpp>
#include <boost/json/value.hpp>
#include <boost/json/object.hpp>
#include <optional>
#include <string>
#include <utility>

namespace boost::taar::jsonrpc {

/** Build a successful JSON-RPC 2.0 response envelope.

    Produces the spec's response shape:

    @code
    { "jsonrpc": "2.0", "id": <id>, "result": <result> }
    @endcode

    @param id     The id from the originating request. `null` is acceptable
                  and is emitted verbatim. Notifications do not get a
                  response -- callers should not invoke this builder for
                  notification envelopes.
    @param result The method's return value, already in JSON form.
 */
inline boost::json::value make_response(
    boost::json::value id,
    boost::json::value result)
{
    return boost::json::object
    {
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"result", std::move(result)}
    };
}

/** Build a JSON-RPC 2.0 error response envelope.

    Produces the spec's error response shape:

    @code
    { "jsonrpc": "2.0", "id": <id>,
      "error": { "code": <code>, "message": <message>[, "data": <data>] } }
    @endcode

    @param id      The id from the originating request, or `null` if the
                   request could not be parsed enough to recover one
                   (per the spec, errors emitted before id extraction use
                   `null`).
    @param code    The numeric error code. Use the @ref error enum's
                   `-32700`..`-32603` for protocol-level failures; the
                   `-32000`..`-32099` range is reserved for application
                   server errors.
    @param message Short human-readable description.
    @param data    Optional additional context. Omitted from the envelope
                   when empty.
 */
inline boost::json::value make_error_response(
    boost::json::value id,
    int code,
    std::string message,
    std::optional<boost::json::value> data = std::nullopt)
{
    boost::json::object error_obj
    {
        {"code", code},
        {"message", std::move(message)}
    };

    if (data.has_value())
    {
        error_obj["data"] = std::move(*data);
    }

    return boost::json::object
    {
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"error", std::move(error_obj)}
    };
}

/// Convenience overload using the @ref error enum.
///
/// Callers that want the spec's default message for the code can pass
/// `boost::taar::jsonrpc::error_category().message(static_cast<int>(code))`
/// -- kept out of the overload set to avoid an ambiguity between
/// "default-message + data" and "explicit message" when the third
/// argument is a string literal.
inline boost::json::value make_error_response(
    boost::json::value id,
    error code,
    std::string message,
    std::optional<boost::json::value> data = std::nullopt)
{
    return make_error_response(
        std::move(id),
        static_cast<int>(code),
        std::move(message),
        std::move(data));
}

} // namespace boost::taar::jsonrpc

#endif // BOOST_TAAR_JSONRPC_RESPONSE_HPP
