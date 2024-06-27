//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HANDLER_REST_ERROR_HPP
#define BOOST_WEB_HANDLER_REST_ERROR_HPP

#include <boost/system/error_category.hpp>

namespace boost::web::handler {

enum class rest_error
{
    success = 0,
    argument_not_found = 10,
    argument_ambiguous,
    invalid_request_format,
    invalid_url_format,
    invalid_content_type,
};

#if (__cpp_constexpr >= 202211L)
constexpr
#endif
inline boost::system::error_category const& error_category() noexcept
{
    struct error_category_type : boost::system::error_category
    {
        [[nodiscard]] const char* name() const noexcept override
        {
            return "boost::web::handler::rest";
        }

        [[nodiscard]] std::string message(int ev) const override
        {
            switch (static_cast<rest_error>(ev))
            {
            case rest_error::success:
                return "Success.";
            case rest_error::argument_not_found:
                return "Argument not found";
            case rest_error::argument_ambiguous:
                return "Argument not ambiguous";
            case rest_error::invalid_request_format:
                return "Invalid request format";
            case rest_error::invalid_url_format:
                return "Invalid url format";
            case rest_error::invalid_content_type:
                return "Invalid content type";
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

#if (__cpp_constexpr >= 202211L)
constexpr
#endif
inline boost::system::error_code make_error_code(rest_error ev) noexcept
{
    return boost::system::error_code {
        static_cast<std::underlying_type<
            rest_error>::type>(ev),
        error_category()};
}

} // namespace boost::web::matcher

namespace boost::system {

template<>
struct is_error_code_enum<::boost::web::handler::rest_error>
{
    static bool const value = true;
};

} // namespace boost::system

#endif // BOOST_WEB_HANDLER_REST_ERROR_HPP
