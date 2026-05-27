//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_TEMPLATE_PARSER_HPP
#define BOOST_TAAR_MATCHER_TEMPLATE_PARSER_HPP

#include <boost/taar/core/error.hpp>
#include <boost/system/result.hpp>
#include <vector>
#include <string_view>

namespace boost::taar::matcher {
namespace detail {

inline constexpr bool is_alpha(char ch)
{
    return
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z');
}

inline constexpr bool is_digit(char ch)
{
    return (ch >= '0' && ch <= '9');
}

inline constexpr bool is_alphanum(char ch)
{
    return is_alpha(ch) || is_digit(ch);
}

inline constexpr bool validate_literal_char(char ch)
{
    return
        is_alphanum(ch) ||
        ch == '-' || ch == '.' || ch == '_' || ch == '~' || ch == '!' ||
        ch == '$' || ch == '&' || ch == '\'' || ch == '(' || ch == ')' ||
        ch == '*' || ch == '+' || ch == ',' || ch == ';' || ch == '=' ||
        ch == ':' || ch == '@';
}

inline constexpr bool validate_param_start(char ch)
{
    return is_alpha(ch) || ch == '_';
}

inline constexpr bool validate_param_char(char ch)
{
    return is_alphanum(ch) || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

} // namespace detail

enum class template_segment_type
{
    literal,
    param,
    greedy_param,
};

struct template_segment
{
    template_segment_type type;
    std::string value;

    constexpr bool operator==(template_segment const& other) const
    {
        return type == other.type && value == other.value;
    }
};

using parsed_template = std::vector<template_segment>;

struct template_segment_ref
{
    template_segment_type type;
    std::string_view value;

    constexpr bool operator==(template_segment_ref const& other) const
    {
        return type == other.type && value == other.value;
    }

    constexpr operator template_segment() const
    {
        return template_segment{type, std::string(value)};
    }
};

using parsed_template_ref = std::vector<template_segment_ref>;

inline /*constexpr*/ boost::system::result<parsed_template_ref> parse_template(
    std::string_view path)
{
    enum class states : char
    {
        start,
        start_segment,
        start_param,
        start_greedy_param,
        in_param,
        end_param,
        in_literal,
    } state = states::start;

    parsed_template_ref result;
    std::string_view::iterator value_begin;
    std::string_view::iterator value_end;
    bool is_greedy_param = false;

    for (auto iter = path.begin(); iter != path.end(); ++iter)
    {
        switch (state)
        {
        case states::start:
            if (*iter != '/')
            {
                return error::no_absolute_template;
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
                state = states::start_param;
                break;
            }

            if (detail::validate_literal_char(*iter))
            {
                value_begin = iter;

                state = states::in_literal;
                break;
            }

            return error::invalid_template;

        case states::start_param:
            if (*iter == '*')
            {
                state = states::start_greedy_param;
                break;
            }

            if (detail::validate_param_start(*iter))
            {
                is_greedy_param = false;
                value_begin = iter;

                state = states::in_param;
                break;
            }

            return error::invalid_template;

        case states::start_greedy_param:
            if (detail::validate_param_start(*iter))
            {
                is_greedy_param = true;
                value_begin = iter;

                state = states::in_param;
                break;
            }

            return error::invalid_template;

        case states::in_param:
            if (*iter == '}')
            {
                value_end = iter;

                result.emplace_back(
                    is_greedy_param ?
                        template_segment_type::greedy_param :
                        template_segment_type::param,
                    std::string_view(value_begin, value_end));


                state = states::end_param;
                break;
            }

            if (detail::validate_param_char(*iter))
            {
                break;
            }

            return error::invalid_template;

        case states::end_param:
            if (*iter == '/')
            {
                state = states::start_segment;
                break;
            }
            return error::invalid_template;

        case states::in_literal:
            if (*iter == '/')
            {
                result.emplace_back(
                    template_segment_type::literal,
                    std::string_view(value_begin, iter));

                state = states::start_segment;
                break;
            }
            else if (detail::validate_literal_char(*iter))
            {
                break;
            }
            return error::invalid_template;
        }
    }

    if (state == states::start)
    {
        return error::no_absolute_template;
    }
    else if (state == states::in_literal)
    {
        result.emplace_back(
            template_segment_type::literal,
            std::string_view(value_begin, path.end()));
    }
    else if (state != states::start_segment && state != states::end_param)
    {
        return error::invalid_template;
    }

    return result;
}

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_TEMPLATE_PARSER_HPP
