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

enum class error
{
    success = 0,
    no_absolute_template,
    invalid_template,
    argument_not_found,
    argument_ambiguous,
    invalid_argument_format,
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
                return "Argument not ambiguous";
            case error::invalid_argument_format:
                return "Invalid argument format.";
            case error::invalid_request_format:
                return "Invalid request format";
            case error::invalid_url_format:
                return "Invalid url format";
            case error::invalid_content_type:
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
