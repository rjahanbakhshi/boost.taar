//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_ERROR_HPP
#define BOOST_TAAR_HANDLER_ERROR_HPP

#include <boost/system/error_category.hpp>

namespace boost::taar {

/** Error codes reported by Boost.Taar.

    All values share the @ref error_category category, and they can be used
    transparently with `boost::system::error_code` thanks to the
    `is_error_code_enum` specialisation at the bottom of this header.

    Many of these are reported as *soft* errors by
    @ref boost::taar::session::http through its soft-error handler, which
    by default turns them into 400/404/415 HTTP responses.
*/
enum class error
{
    success = 0,                ///< No error.
    no_absolute_template,       ///< A path template did not begin with '/'.
    invalid_template,           ///< Path-template syntax error.
    argument_not_found,         ///< A required argument was absent from the request.
    argument_ambiguous,         ///< A header or query parameter occurred more than once.
    invalid_argument_format,    ///< An argument could not be parsed to the requested type.
    invalid_request_format,     ///< The request body could not be decoded.
    invalid_url_format,         ///< The request URL could not be parsed.
    invalid_content_type,       ///< The request `Content-Type` does not match the handler.
    invalid_cookie_format,      ///< The `Cookie` header could not be parsed.
    invalid_boolean_format,     ///< A boolean argument could not be parsed.
    invalid_number_format,      ///< A numeric argument could not be parsed.
    late_chunk_metadata,        ///< Chunked metadata was yielded after body data.
};

/// Singleton @c boost::system::error_category instance for the @c error enum.
#if (__cpp_constexpr >= 202211L)
constexpr
#endif
inline boost::system::error_category const& error_category() noexcept
{
    struct error_category_type : boost::system::error_category
    {
        [[nodiscard]] char const* name() const noexcept override
        {
            return "Taar error";
        }

        [[nodiscard]] std::string message(int ev) const override
        {
            switch (static_cast<error>(ev))
            {
            case error::success:
                return "Success.";
            case error::no_absolute_template:
                return "Specified template is not an absolute path.";
            case error::invalid_template:
                return "Invalid template path.";
            case error::argument_not_found:
                return "Argument not found";
            case error::argument_ambiguous:
                return "Argument ambiguous";
            case error::invalid_argument_format:
                return "Invalid argument format.";
            case error::invalid_request_format:
                return "Invalid request format";
            case error::invalid_url_format:
                return "Invalid url format";
            case error::invalid_content_type:
                return "Invalid content type";
            case error::invalid_cookie_format:
                return "Invalid cookie format";
            case error::invalid_boolean_format:
                return "Invalid boolean format";
            case error::invalid_number_format:
                return "Invalid number format";
            case error::late_chunk_metadata:
                return "Chunk metadata yielded after data.";
            }

            return "(Unknown error)";
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

} // namespace boost::taar

namespace boost::system {

template<>
struct is_error_code_enum<::boost::taar::error>
{
    static bool const value = true;
};

} // namespace boost::system

#endif // BOOST_TAAR_HANDLER_ERROR_HPP
