//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CORS_HPP
#define BOOST_TAAR_CORE_CORS_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace boost::taar {

/** Cross-Origin Resource Sharing policy.

    Two pieces plug into a session: @ref preflight_handler answers
    `OPTIONS` requests, and @ref response_filter stamps the CORS headers on
    every other response (install it with
    `basic_http::set_response_filter`). Defaults allow any origin without
    credentials and the headers the MCP transport needs.

    @code
    taar::cors cors;
    cors.allowed_origins = {"http://localhost:3000"};
    cors.allow_credentials = true;

    session.register_request_handler(method == http::verb::options, cors.preflight_handler());
    session.set_response_filter(cors.response_filter());
    @endcode
*/
struct cors
{
    /// Origins that may call. `"*"` allows any origin without credentials;
    /// with `allow_credentials` the wildcard is ignored and origins must be
    /// listed explicitly (the matching one is echoed with `Vary: Origin`).
    std::vector<std::string> allowed_origins {"*"};
    std::vector<std::string> allowed_methods {"GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
    std::vector<std::string> allowed_headers {
        "Content-Type", "Authorization", "Accept",
        "Mcp-Session-Id", "MCP-Protocol-Version", "Last-Event-ID"};
    std::vector<std::string> exposed_headers {"Mcp-Session-Id"};
    bool allow_credentials = false;
    std::chrono::seconds max_age {600};

    /// True if @a origin may call. The wildcard does not count when
    /// credentials are allowed: a credentialed response must name the origin.
    bool origin_allowed(std::string_view origin) const
    {
        return std::ranges::any_of(allowed_origins, [&](auto const& o)
        {
            return (o == "*" && !allow_credentials) || o == origin;
        });
    }

    /// Add the CORS response headers for @a request to @a response, if its Origin is allowed.
    template <typename Request, typename Response>
    void apply(Request const& request, Response& response) const
    {
        namespace http = boost::beast::http;
        auto origin = request.find(http::field::origin);
        if (origin == request.end() || !origin_allowed(origin->value()))
        {
            return;
        }
        bool const any = !allow_credentials &&
            std::ranges::find(allowed_origins, "*") != allowed_origins.end();
        if (any)
        {
            response.set(http::field::access_control_allow_origin, "*");
        }
        else
        {
            response.set(http::field::access_control_allow_origin, origin->value());
            response.set(http::field::vary, "Origin");
        }
        if (allow_credentials)
        {
            response.set(http::field::access_control_allow_credentials, "true");
        }
        if (!exposed_headers.empty())
        {
            response.set(http::field::access_control_expose_headers, join(exposed_headers));
        }
    }

    /// A handler for `OPTIONS` preflight requests: 204 with the allow headers, or 403.
    auto preflight_handler() const
    {
        return [policy = *this](
            boost::beast::http::request<boost::beast::http::empty_body> const& request,
            matcher::context const&)
        {
            namespace http = boost::beast::http;
            http::response<http::empty_body> response {http::status::no_content, request.version()};
            auto origin = request.find(http::field::origin);
            if (origin != request.end() && !policy.origin_allowed(origin->value()))
            {
                response.result(http::status::forbidden);
            }
            else
            {
                policy.apply(request, response);
                response.set(http::field::access_control_allow_methods, join(policy.allowed_methods));
                response.set(http::field::access_control_allow_headers, join(policy.allowed_headers));
                response.set(http::field::access_control_max_age, std::to_string(policy.max_age.count()));
            }
            response.keep_alive(request.keep_alive());
            response.prepare_payload();
            return response;
        };
    }

    /// A response filter for `basic_http::set_response_filter` applying @ref apply to every response.
    std::function<void(boost::beast::http::request_header<> const&, boost::beast::http::response_header<>&)>
    response_filter() const
    {
        return [policy = *this](
            boost::beast::http::request_header<> const& request,
            boost::beast::http::response_header<>& response)
        {
            policy.apply(request, response);
        };
    }

private:
    /// @cond
    static std::string join(std::vector<std::string> const& items)
    {
        std::string out;
        for (auto const& i : items)
        {
            if (!out.empty()) out += ", ";
            out += i;
        }
        return out;
    }
    /// @endcond
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CORS_HPP
