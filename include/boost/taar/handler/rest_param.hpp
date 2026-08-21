//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_REST_PARAM_HPP
#define BOOST_TAAR_HANDLER_REST_PARAM_HPP

#include <boost/taar/core/json_schema.hpp>
#include <boost/taar/handler/constrained.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/type_traits/specialization_of.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <algorithm>
#include <concepts>
#include <ranges>
#include <string>
#include <type_traits>
#include <vector>

namespace boost::taar::handler {

/// Where a REST argument provider reads its value from.
enum class param_location
{
    path,
    query,
    header,
    cookie,
    body,
    none,       ///< Not an input parameter (e.g. `target_arg`, `body_size_arg`, user providers).
};

/// Registration-time description of one REST handler parameter.
struct rest_param_info
{
    param_info info;
    param_location in = param_location::none;
    std::vector<std::string> content_types;   ///< For body parameters, sorted.
};

/// Concept: handler exposes `params()` returning its @ref rest_param_info list.
template <typename Handler>
concept has_rest_params = requires(Handler const& h)
{
    { h.params() } -> std::ranges::input_range;
};

namespace detail {

template <typename P>
struct strip_default
{
    using type = std::remove_cvref_t<P>;
    static constexpr bool defaulted = false;
};

template <typename P, typename V>
struct strip_default<with_default<P, V>>
{
    using type = typename strip_default<std::remove_cvref_t<P>>::type;
    static constexpr bool defaulted = true;
};

template <typename P>
struct strip_default<constrained<P>>
{
    using type = typename strip_default<std::remove_cvref_t<P>>::type;
    static constexpr bool defaulted = strip_default<std::remove_cvref_t<P>>::defaulted;
};

/// Add what the wrappers around a provider declare: `default` and constraint keywords.
template <typename Provider>
void annotate_param(param_info& info, Provider const& p)
{
    using P = std::remove_cvref_t<Provider>;
    if constexpr (type_traits::specialization_of<with_default, P>)
    {
        if constexpr (std::is_constructible_v<boost::json::value, decltype(p.default_value())>)
        {
            info.default_value = boost::json::value(p.default_value());
        }
        else if constexpr (requires { boost::json::value_from(p.default_value()); })
        {
            info.default_value = boost::json::value_from(p.default_value());
        }
        annotate_param(info, p.arg_provider_);
    }
    else if constexpr (type_traits::specialization_of<constrained, P>)
    {
        p.rules().apply_to(info.schema);
        annotate_param(info, p.arg_provider_);
    }
}

template <typename Provider>
constexpr param_location location_of()
{
    using P = typename strip_default<std::remove_cvref_t<Provider>>::type;
    if constexpr (std::same_as<P, path_arg>) return param_location::path;
    else if constexpr (std::same_as<P, query_arg>) return param_location::query;
    else if constexpr (std::same_as<P, header_arg>) return param_location::header;
    else if constexpr (std::same_as<P, cookie_arg>) return param_location::cookie;
    else if constexpr (
        std::same_as<P, string_body_arg> ||
        std::same_as<P, json_body_arg> ||
        std::same_as<P, url_encoded_form_data_arg> ||
        std::same_as<P, stream_body_arg> ||
        std::same_as<P, multipart_form_arg> ||
        std::same_as<P, multipart_stream_arg>) return param_location::body;
    else return param_location::none;
}

template <typename Provider>
std::vector<std::string> content_types_of(Provider const& p)
{
    using P = std::remove_cvref_t<Provider>;
    if constexpr (
        type_traits::specialization_of<with_default, P> ||
        type_traits::specialization_of<constrained, P>)
    {
        return content_types_of(p.arg_provider_);
    }
    else
    {
        std::vector<std::string> out;
        if constexpr (location_of<Provider>() == param_location::body)
        {
            out.assign(p.content_types().begin(), p.content_types().end());
            std::ranges::sort(out);
        }
        return out;
    }
}

template <typename ArgType, typename Provider>
std::string body_json_type()
{
    using P = typename strip_default<std::remove_cvref_t<Provider>>::type;
    if constexpr (std::same_as<P, string_body_arg>) return "string";
    else if constexpr (std::same_as<P, url_encoded_form_data_arg>) return "object";
    else return std::string {json_type_name<ArgType>()};
}

/// Describe the parameter fed by @a provider into a callable parameter of type @a ArgType.
template <typename ArgType, typename Provider>
rest_param_info describe_rest_param(Provider const& provider)
{
    rest_param_info out;
    out.in = location_of<Provider>();
    out.info = make_param_info<ArgType>(
        arg_provider_name(provider),
        arg_provider_description(provider),
        !strip_default<std::remove_cvref_t<Provider>>::defaulted && !is_optional_v<ArgType>);
    annotate_param(out.info, provider);
    if (out.in == param_location::body)
    {
        using P = typename strip_default<std::remove_cvref_t<Provider>>::type;
        if constexpr (std::same_as<P, string_body_arg> || std::same_as<P, url_encoded_form_data_arg>)
        {
            // The wire format is fixed by the provider, whatever the C++ type.
            out.info.json_type = body_json_type<ArgType, Provider>();
            out.info.schema = boost::json::object {{"type", out.info.json_type}};
        }
        else if constexpr (std::same_as<P, stream_body_arg>)
        {
            out.info.json_type = "string";
            out.info.schema = boost::json::object {{"type", "string"}, {"format", "binary"}};
        }
        else if constexpr (std::same_as<P, multipart_form_arg> || std::same_as<P, multipart_stream_arg>)
        {
            out.info.json_type = "object";
            out.info.schema = boost::json::object {{"type", "object"}};
        }
        if constexpr (std::same_as<P, multipart_form_arg> || std::same_as<P, multipart_stream_arg>)
        {
            out.content_types = {"multipart/form-data"};
        }
        else
        {
            out.content_types = content_types_of(provider);
        }
        if (out.content_types.empty() && std::same_as<P, stream_body_arg>)
        {
            out.content_types = {"application/octet-stream"};
        }
    }
    return out;
}

} // namespace detail
} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_REST_PARAM_HPP
