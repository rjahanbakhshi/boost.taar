//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_ERROR_TYPES_HPP
#define BOOST_TAAR_MATCHER_ERROR_TYPES_HPP

#include <boost/system/error_category.hpp>

namespace boost::taar::matcher {

enum class error
{
    success = 0,
    no_absolute_template,
    invalid_template,
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
            return "boost::taar::matcher";
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
        static_cast<std::underlying_type<
            error>::type>(ev),
        error_category()};
}

} // namespace boost::taar::matcher

namespace boost::system {

template<>
struct is_error_code_enum<::boost::taar::matcher::error>
{
    static bool const value = true;
};

} // namespace boost::system

#endif // BOOST_TAAR_MATCHER_ERROR_TYPES_HPP
