//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HTTP_MATCHER_ERROR_TYPES_HPP
#define BOOST_WEB_HTTP_MATCHER_ERROR_TYPES_HPP

#include <boost/system/error_category.hpp>

namespace boost::web::matcher {

enum class error
{
    success = 0,
    no_absolute_target_path,
    invalid_target_path,
};

inline constexpr boost::system::error_category const& error_category() noexcept
{
    struct error_category_type : boost::system::error_category
    {
        [[nodiscard]] const char* name() const noexcept override
        {
            return "boost::web::matcher";
        }

        [[nodiscard]] std::string message(int ev) const override
        {
            switch (static_cast<error>(ev))
            {
            case error::success:
                return "Success.";
            case error::no_absolute_target_path:
                return "Specified target is not an absolute path.";
            case error::invalid_target_path:
                return "Invalid target path.";
            }

            return "(Unknown error)";
        }
    };

    static constexpr error_category_type instance;
    return instance;
}

inline constexpr boost::system::error_code make_error_code(error ev) noexcept
{
    return boost::system::error_code {
        static_cast<std::underlying_type<
            error>::type>(ev),
        error_category()};
}

} // namespace boost::web::matcher

namespace boost::system {

template<>
struct is_error_code_enum<::boost::web::matcher::error>
{
    static bool const value = true;
};

} // namespace boost::web::matcher

#endif // BOOST_WEB_HTTP_MATCHER_ERROR_TYPES_HPP
