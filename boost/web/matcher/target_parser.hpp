//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HTTP_MATCHER_PATH_PARSER_HPP
#define BOOST_WEB_HTTP_MATCHER_PATH_PARSER_HPP

#include <boost/web/matcher/segments_view.hpp>
#include <boost/web/matcher/error.hpp>
#include <boost/system/result.hpp>
#include <string_view>

namespace boost::web::matcher {
namespace detail {

inline bool validate_target_char(char ch)
{
    return
        (ch >= '0' && ch <= '9') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

} // namespace detail

inline boost::system::result<segments_view> parse_target(
    std::string_view path,
    bool allow_template = false)
{
    enum class states
    {
        start,
        start_segment,
        in_segment,
        in_template,
        end_template,
    } state = states::start;

    segments_view result;
    std::string_view::iterator segment_start;

    for (auto iter = path.begin(); iter != path.end(); ++iter)
    {
        switch (state)
        {
        case states::start:
            if (*iter != '/')
            {
                return error::no_absolute_target_path;
            }
            state = states::start_segment;
            break;

        case states::start_segment:
            if (*iter == '/')
            {
                // Extra slashes in paths are ignored.
                break;
            }

            if (*iter == '{')
            {
                if (!allow_template)
                {
                    return error::invalid_target_path;
                }

                segment_start = iter;
                state = states::in_template;
                break;
            }

            if (detail::validate_target_char(*iter))
            {
                segment_start = iter;
                state = states::in_segment;
                break;
            }

            return error::invalid_target_path;

        case states::in_template:
            if (*iter == '{' || *iter == '/')
            {
                return error::invalid_target_path;
            }

            if (*iter == '}')
            {
                state = states::end_template;
            }
            break;

        case states::end_template:
            if (*iter == '/')
            {
                result.emplace_back(segment_start, iter);
                state = states::start_segment;
                break;
            }
            return error::invalid_target_path;

        case states::in_segment:
            if (*iter == '/')
            {
                result.emplace_back(segment_start, iter);
                state = states::start_segment;
                break;
            }
            else if (detail::validate_target_char(*iter))
            {
                break;
            }
            return error::invalid_target_path;
        }
    }

    if (state == states::start)
    {
        return error::no_absolute_target_path;
    }
    else if (state == states::in_segment || state == states::end_template)
    {
        result.emplace_back(segment_start, path.end());
    }
    else if (state == states::in_template)
    {
        return error::invalid_target_path;
    }

    return result;
}

} // namespace boost::web::matcher

#endif // BOOST_WEB_HTTP_MATCHER_PATH_PARSER_HPP
