//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HANDLER_REST_ARG_HPP
#define BOOST_WEB_HANDLER_REST_ARG_HPP

#include <boost/web/matcher/context.hpp>
#include <boost/web/handler/rest_error.hpp>
#include <boost/web/core/always_false.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/url/parse.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/system/result.hpp>
#include <unordered_set>
#include <optional>
#include <string>
#include <algorithm>
#include <utility>
#include <concepts>
#include <type_traits>

namespace boost::web::handler {
namespace rest_arg_types {

using request_body_type = boost::beast::http::string_body;
using request_type = boost::beast::http::request<request_body_type>;

template <typename T>
using result_type = boost::system::result<T>;

template <typename T>
using getter_signature = result_type<T>(const request_type&, const matcher::context&);

template <typename T>
using getter_type = std::function<getter_signature<T>>;

using string_result = result_type<std::string>;
using string_getter = getter_type<std::string>;
using json_result = result_type<boost::json::value>;
using json_getter = getter_type<boost::json::value>;

template <typename From, typename To>
concept lexical_castable = requires(From&& from)
{
    { boost::lexical_cast<To>(std::forward<From>(from)) } -> std::convertible_to<To>;
};

template <typename To>
concept convertible_from_json_value = requires
{
    { boost::json::value_to<To>(std::declval<boost::json::value>()) } -> std::convertible_to<To>;
};

namespace detail {

result_type<bool> inline bool_cast(const std::string& str)
{
    static const std::unordered_map<std::string, bool> MAP = {
        {"true", true},
        {"TRUE", true},
        {"yes", true},
        {"YES", true},
        {"1", true},
        {"false", false},
        {"FALSE", false},
        {"no", false},
        {"NO", false},
        {"0", false},
    };
    auto iter = MAP.find(str);
    if (iter != MAP.end())
    {
        return iter-> second;
    }

    return rest_error::invalid_request_format;
}

// Convert the getter into another one by applying possible cast and conversions.
template <typename FromType, typename ToType>
[[nodiscard]] auto convert_getter(getter_type<FromType> getter)
{
    if constexpr (std::same_as<FromType, ToType>)
    {
        return std::move(getter);
    }
    else if constexpr (
        std::same_as<FromType, std::string> &&
        std::same_as<ToType, bool>)
    {
        return
            [getter = std::move(getter)](
                const request_type& request,
                const matcher::context& context) -> result_type<ToType>
            {
                auto result = getter(request, context);
                if (!result)
                {
                    return result.error();
                }

                return bool_cast(result.value());
            };
    }
    else if constexpr (std::convertible_to<FromType, ToType>)
    {
        return
            [getter = std::move(getter)](
                const request_type& request,
                const matcher::context& context) -> result_type<ToType>
            {
                auto result = getter(request, context);
                if (result)
                {
                    return static_cast<ToType>(result.value());
                }

                return result.error();
            };
    }
    else if constexpr (
        std::same_as<FromType, boost::json::value> &&
        convertible_from_json_value<ToType>)
    {
        return
            [getter = std::move(getter)](
                const request_type& request,
                const matcher::context& context) -> result_type<ToType>
            {
                auto result = getter(request, context);
                if (!result)
                {
                    return result.error();
                }

                auto json = boost::json::try_value_to<ToType>(result.value());
                if (!json)
                {
                    return json.error();
                }

                return json.value();
            };
    }
    else if constexpr (lexical_castable<std::string, ToType>)
    {
        return
            [getter = std::move(getter)](
                const request_type& request,
                const matcher::context& context) -> result_type<ToType>
            {
                auto result = getter(request, context);
                if (result)
                {
                    return boost::lexical_cast<ToType>(result.value());
                }

                return result.error();
            };
    }
    else
    {
        static_assert(
            always_false<FromType, ToType>,
            "Function argument type isn't constructible from rest requests.");
    }
}

// Constructing a lazy getter from a value.
template <typename FromType, typename ToType> requires
    std::convertible_to<FromType, ToType>
auto getter_from_value(FromType&& value) // NOLINT
{
    return
        [value = std::forward<FromType>(value)](
            const rest_arg_types::request_type&,
            const matcher::context&) -> result_type<ToType>
        {
            return static_cast<ToType>(value);
        };
}

} // namespace detail
} // namespace rest_arg_types

// Generic rest_arg
template <typename ArgType> requires
    (!std::is_reference_v<ArgType>) &&
    (!std::is_const_v<ArgType>)
struct rest_arg
{
    rest_arg_types::getter_type<ArgType> get_value;

    // Constructing a generic lazy getter.
    template <typename T>
    rest_arg(rest_arg_types::getter_type<T> getter)
        : get_value {
            rest_arg_types::detail::convert_getter<T, ArgType>(std::move(getter))
        }
    {}

    // Getter getter from a value.
    template <typename T> requires
        // Any type convertible to the actual arg type is acceptable.
        std::convertible_to<std::remove_cvref_t<T>, ArgType> &&
        // Avoid hiding copy and move constructor.
        (!std::same_as<std::remove_cvref_t<T>, rest_arg>)
    rest_arg(T&& value) // NOLINT
        : get_value {
            rest_arg_types::detail::getter_from_value<T, ArgType>(std::forward<T>(value))
        }
    {}
};

// Specialization for std::optional argument type
template <typename ArgType>
struct rest_arg<std::optional<ArgType>>
{
    rest_arg_types::getter_type<std::optional<ArgType>> get_value;

    template <typename T>
    rest_arg(rest_arg_types::getter_type<T> getter)
        : get_value {
            [getter = rest_arg_types::detail::convert_getter<T, ArgType>(std::move(getter))](
                const rest_arg_types::request_type& request,
                const matcher::context& context) -> rest_arg_types::result_type<std::optional<ArgType>>
            {
                auto result = getter(request, context);
                if (!result.has_error())
                {
                    return result.value();
                }

                if (result.error() == rest_error::argument_not_found)
                {
                    return std::nullopt;
                }

                return result.error();
            }
        }
    {}

    // Getter from a value.
    template <typename T> requires
        // Any type convertible to the actual arg type is acceptable.
        std::convertible_to<T&&, std::optional<ArgType>> &&
        // Avoid hiding copy and move constructor.
        (!std::same_as<std::remove_cvref_t<T>, rest_arg>)
    rest_arg(T&& value) // NOLINT
        : get_value {
            rest_arg_types::detail::getter_from_value<T, ArgType>(std::forward<T>(value))
        }
    {}
};

inline rest_arg_types::string_getter path_arg(std::string path_key)
{
    return [path_key = std::move(path_key)](
        const rest_arg_types::request_type&,
        const matcher::context& context) -> rest_arg_types::string_result
    {
        auto iter = context.path_args.find(path_key);
        if (iter != context.path_args.end())
        {
            return iter->second;
        }
        return rest_error::argument_not_found;
    };
}

inline rest_arg_types::string_getter query_arg(std::string query_key)
{
    return [query_key = std::move(query_key)](
        const rest_arg_types::request_type& request,
        const matcher::context&) -> rest_arg_types::string_result
    {
        auto url_view = boost::urls::parse_origin_form(request.target());
        if (!url_view)
        {
            return rest_error::invalid_url_format;
        }

        auto params = url_view->params();
        auto iter = params.find(query_key);
        if (iter != params.end())
        {
            // TODO: Check to see if there's more then it's a failure due to ambiguity.
            const auto& param = *iter;
            return param.value;
        }
        return rest_error::argument_not_found;
    };
}

inline rest_arg_types::string_getter string_body_arg(
    std::unordered_set<std::string> content_types)
{
    return [content_types = std::move(content_types)](
        const rest_arg_types::request_type& request,
        const matcher::context&) -> rest_arg_types::string_result
    {
        if (!content_types.empty())
        {
            auto range = request.equal_range(boost::beast::http::field::content_type);
            if (std::find_if(range.first, range.second, [&](auto const& elem)
                {
                    return content_types.contains(elem.value());
                }) == range.second)
            {
                return rest_error::invalid_content_type;
            }
        }

        return request.body();
    };
}

inline rest_arg_types::json_getter json_body_arg(
    std::unordered_set<std::string> content_types = {"application/json"})
{
    return [content_types = std::move(content_types)](
        const rest_arg_types::request_type& request,
        const matcher::context&) -> rest_arg_types::json_result
    {
        if (!content_types.empty())
        {
            auto range = request.equal_range(boost::beast::http::field::content_type);
            if (std::find_if(range.first, range.second, [&](auto const& elem)
                {
                    return content_types.contains(elem.value());
                }) == range.second)
            {
                return rest_error::invalid_content_type;
            }
        }

        if (request.body().empty())
        {
            return rest_error::argument_not_found;
        }

        std::error_code ec;
        auto json = boost::json::parse(request.body(), ec);
        if (ec)
        {
            return rest_error::invalid_request_format;
        }
        return json;
    };
}

inline rest_arg_types::string_getter header_arg(std::string header_name)
{
    return [header_name = std::move(header_name)](
        const rest_arg_types::request_type& request,
        const matcher::context&) -> rest_arg_types::string_result
    {
        auto iter = request.find(header_name);
        if (iter != request.end())
        {
            // TODO: Check to see if there's more then it's failure due to ambiguity.
            return iter->value();
        }
        return rest_error::argument_not_found;
    };
}

} // boost::web::handler

#endif // BOOST_WEB_HANDLER_REST_ARG_HPP
