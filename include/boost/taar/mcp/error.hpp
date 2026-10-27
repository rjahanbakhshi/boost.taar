//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_MCP_ERROR_HPP
#define BOOST_TAAR_MCP_ERROR_HPP

#include <boost/system/error_category.hpp>
#include <boost/system/error_code.hpp>
#include <string>
#include <type_traits>

namespace boost::taar::mcp {

/** Error codes of the MCP layer.

    A @ref boost::taar::failure carries these in the @ref error_category
    category. Where MCP assigns a JSON-RPC wire code the enumerator's
    value is that code (`resource_not_found` is `-32002`,
    `request_cancelled` is `-32800`), so `code.value()` is what goes on
    the wire. The others describe failures of server-to-client requests
    and have no wire code of their own.
 */
enum class error
{
    success            = 0,         ///< No error.
    resource_not_found = -32002,    ///< No resource matches the requested URI.
    request_cancelled  = -32800,    ///< The request was cancelled.
    client_error       = 1,         ///< The client answered a server-to-client request with a JSON-RPC error;
                                    ///< the failure's data is `{"code": n, "data": ...}`.
    unknown_session    = 2,         ///< No MCP session has the given id.
};

/// Singleton `boost::system::error_category` instance for @ref error.
#if (__cpp_constexpr >= 202211L)
constexpr
#endif
inline boost::system::error_category const& error_category() noexcept
{
    struct error_category_type : boost::system::error_category
    {
        [[nodiscard]] char const* name() const noexcept override
        {
            return "mcp";
        }

        [[nodiscard]] std::string message(int ev) const override
        {
            switch (static_cast<error>(ev))
            {
            case error::success:            return "Success.";
            case error::resource_not_found: return "Resource not found.";
            case error::request_cancelled:  return "Request cancelled.";
            case error::client_error:       return "Client error.";
            case error::unknown_session:    return "Unknown MCP session.";
            }

            return "(Unknown MCP error)";
        }
    };

#if (__cpp_constexpr >= 202211L)
    constexpr
#endif
    static error_category_type instance;
    return instance;
}

/// ADL-found factory required by `is_error_code_enum<error>`.
#if (__cpp_constexpr >= 202211L)
constexpr
#endif
inline boost::system::error_code make_error_code(error ev) noexcept
{
    return boost::system::error_code {
        static_cast<std::underlying_type<error>::type>(ev),
        error_category()};
}

} // namespace boost::taar::mcp

namespace boost::system {

template <>
struct is_error_code_enum<::boost::taar::mcp::error>
{
    static bool const value = true;
};

} // namespace boost::system

#endif // BOOST_TAAR_MCP_ERROR_HPP
