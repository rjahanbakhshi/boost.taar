//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_INTERCEPTOR_HPP
#define BOOST_TAAR_CORE_INTERCEPTOR_HPP

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace boost::taar {

/** What an interceptor or a `handler::guard` check decides.

    `std::nullopt` lets the request proceed; a response rejects it and is
    written to the client instead of running the route's handler. Build
    the values with `taar::proceed()` and `taar::reject()`, or construct
    a response directly when the rejection needs custom headers or a body
    other than plain text.
*/
using interceptor_result = std::optional<
    boost::beast::http::response<boost::beast::http::string_body>>;

/// Let the request proceed (`std::nullopt`).
inline interceptor_result proceed() noexcept
{
    return std::nullopt;
}

/** Reject the request with @a status and a `text/plain` body.

    @param status The response status, such as `401 Unauthorized`.
    @param text   The body; when empty, the status's reason phrase
                  (`"Unauthorized"`).

    The session aligns the response's version and keep-alive with the
    request and applies the response filter, as for any response.
*/
inline interceptor_result reject(
    boost::beast::http::status status,
    std::string_view text = {})
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {status, 11};
    response.set(http::field::content_type, "text/plain");
    response.body() = text.empty()
        ? std::string {http::obsolete_reason(status)}
        : std::string {text};
    response.prepare_payload();
    return response;
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_INTERCEPTOR_HPP
