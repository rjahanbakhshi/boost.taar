//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_JSONRPC_ERROR_HPP
#define BOOST_TAAR_JSONRPC_ERROR_HPP

#include <boost/system/error_category.hpp>
#include <string>
#include <type_traits>

namespace boost::taar::jsonrpc {

/** JSON-RPC 2.0 error codes.

    Values follow the spec's reserved range. The general-purpose pre-defined
    codes (`parse_error` through `internal_error`) cover protocol-level
    failures; the application-error range (`-32000`..`-32099`) is left to
    the caller — the spec reserves it for "implementation-defined server
    errors" and we do not enumerate those here.

    The enum is wired to @ref error_category through @c make_error_code
    plus a `boost::system::is_error_code_enum` specialisation at the bottom
    of this header, so values convert implicitly to
    `boost::system::error_code` and can be returned from APIs that use
    @c boost::system::result.

    The category is distinct from @c boost::taar::error_category so a
    soft-error handler can dispatch on
    `code.category() == jsonrpc::error_category()` to format failures as
    JSON-RPC envelopes rather than HTTP errors.
 */
enum class error
{
    success = 0,                ///< No error. Not a spec code.

    parse_error      = -32700,  ///< JSON could not be parsed.
    invalid_request  = -32600,  ///< Not a valid JSON-RPC request envelope.
    method_not_found = -32601,  ///< Method does not exist or is not available.
    invalid_params   = -32602,  ///< Method parameters were invalid.
    internal_error   = -32603,  ///< Internal JSON-RPC error.
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
            return "jsonrpc";
        }

        [[nodiscard]] std::string message(int ev) const override
        {
            switch (static_cast<error>(ev))
            {
            case error::success:          return "Success.";
            case error::parse_error:      return "Parse error.";
            case error::invalid_request:  return "Invalid Request.";
            case error::method_not_found: return "Method not found.";
            case error::invalid_params:   return "Invalid params.";
            case error::internal_error:   return "Internal error.";
            }

            return "(Unknown JSON-RPC error)";
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

} // namespace boost::taar::jsonrpc

namespace boost::system {

template <>
struct is_error_code_enum<::boost::taar::jsonrpc::error>
{
    static bool const value = true;
};

} // namespace boost::system

#endif // BOOST_TAAR_JSONRPC_ERROR_HPP
