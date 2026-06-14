//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_JSON_SCHEMA_HPP
#define BOOST_TAAR_CORE_JSON_SCHEMA_HPP

#include <boost/taar/type_traits/string_like.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace boost::taar {

/** Describes one parameter for schema generation (OpenAPI, MCP inputSchema).

    Built at registration time from a callable's parameter type and the
    argument provider that feeds it.
*/
struct param_info
{
    std::string name;
    std::string description;
    std::string json_type;      ///< JSON Schema type name, empty for "any".
    bool required = true;
};

namespace detail {

template <typename T> struct is_optional : std::false_type {};
template <typename T> struct is_optional<std::optional<T>> : std::true_type {};

template <typename T> struct is_vector : std::false_type {};
template <typename T, typename A> struct is_vector<std::vector<T, A>> : std::true_type {};

} // namespace detail

/// True if @a T (after stripping cv/ref) is a `std::optional`.
template <typename T>
inline constexpr bool is_optional_v = detail::is_optional<std::remove_cvref_t<T>>::value;

/** The JSON Schema type name for C++ type @a T.

    `bool` -> `boolean`; integral -> `integer`; floating point -> `number`;
    string-like -> `string`; `std::vector` and `boost::json::array` ->
    `array`; `boost::json::object` -> `object`; `boost::json::value` -> an
    empty string (any type); `std::optional<U>` -> the name for `U`;
    anything else -> `object`.
*/
template <typename T>
constexpr std::string_view json_type_name()
{
    using U = std::remove_cvref_t<T>;
    if constexpr (is_optional_v<U>)
    {
        return json_type_name<typename U::value_type>();
    }
    else if constexpr (std::same_as<U, bool>)
    {
        return "boolean";
    }
    else if constexpr (std::integral<U>)
    {
        return "integer";
    }
    else if constexpr (std::floating_point<U>)
    {
        return "number";
    }
    else if constexpr (type_traits::string_like<U>)
    {
        return "string";
    }
    else if constexpr (std::same_as<U, boost::json::value>)
    {
        return "";
    }
    else if constexpr (std::same_as<U, boost::json::object>)
    {
        return "object";
    }
    else if constexpr (std::same_as<U, boost::json::array> || detail::is_vector<U>::value)
    {
        return "array";
    }
    else
    {
        return "object";
    }
}

/// A JSON Schema fragment for one parameter: `{"type": ..., "description": ...}`.
inline boost::json::object param_schema(param_info const& p)
{
    boost::json::object schema;
    if (!p.json_type.empty())
    {
        schema["type"] = p.json_type;
    }
    if (!p.description.empty())
    {
        schema["description"] = p.description;
    }
    return schema;
}

/// A JSON Schema object with `properties` and `required` built from @a params.
inline boost::json::object object_schema(std::vector<param_info> const& params)
{
    boost::json::object properties;
    boost::json::array required;
    for (auto const& p : params)
    {
        properties[p.name] = param_schema(p);
        if (p.required)
        {
            required.emplace_back(p.name);
        }
    }
    boost::json::object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(properties);
    if (!required.empty())
    {
        schema["required"] = std::move(required);
    }
    return schema;
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_JSON_SCHEMA_HPP
