//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_CORE_JSON_SCHEMA_HPP
#define BOOST_TAAR_CORE_JSON_SCHEMA_HPP

#include <boost/taar/type_traits/string_like.hpp>
#include <boost/core/type_name.hpp>
#include <boost/describe/enumerators.hpp>
#include <boost/describe/members.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/mp11/algorithm.hpp>
#include <concepts>
#include <optional>
#include <string>
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
    std::string json_type;              ///< JSON Schema type name, empty for "any".
    bool required = true;
    boost::json::object schema {};      ///< Full JSON Schema for the parameter type.
    std::optional<boost::json::value> default_value {};   ///< From `with_default`, when representable.
};

namespace detail {

template <typename T> struct is_optional : std::false_type {};
template <typename T> struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
concept string_map_like = requires
{
    typename T::key_type;
    typename T::mapped_type;
    requires type_traits::string_like<typename T::key_type>;
};

template <typename T>
concept sequence_like = requires(T const& t)
{
    typename T::value_type;
    t.begin();
    t.end();
} && !type_traits::string_like<T> && !string_map_like<T> &&
    !std::same_as<T, boost::json::object> && !std::same_as<T, boost::json::value>;

} // namespace detail

/// True if @a T (after stripping cv/ref) is a `std::optional`.
template <typename T>
inline constexpr bool is_optional_v = detail::is_optional<std::remove_cvref_t<T>>::value;

/** A JSON Schema (2020-12 vocabulary) for C++ type @a T.

    @li `bool` -> `{"type":"boolean"}`; integral -> `integer`; floating
        point -> `number`; string-like -> `string`.
    @li `std::optional<U>` -> the schema of `U`.
    @li `boost::json::value` -> `{}` (any); `boost::json::object` ->
        `{"type":"object"}`; `boost::json::array` -> `{"type":"array"}`.
    @li Sequences (`std::vector`, `std::array`, ...) -> `array` with
        `items` from the element type.
    @li Maps with string keys -> `object` with `additionalProperties`.
    @li `BOOST_DESCRIBE_STRUCT`ed types -> `object` with `properties`
        from the public members, `required` listing the non-optional
        ones and `title` naming the type (used by the OpenAPI generator
        to hoist the schema into `components`). `BOOST_DESCRIBE_ENUM`ed
        types -> `string` with `enum`.
    @li Anything else -> `{"type":"object"}`.
*/
template <typename T>
boost::json::object json_schema_for()
{
    using U = std::remove_cvref_t<T>;
    boost::json::object schema;

    if constexpr (is_optional_v<U>)
    {
        return json_schema_for<typename U::value_type>();
    }
    else if constexpr (std::same_as<U, bool>)
    {
        schema["type"] = "boolean";
    }
    else if constexpr (std::integral<U>)
    {
        schema["type"] = "integer";
    }
    else if constexpr (std::floating_point<U>)
    {
        schema["type"] = "number";
    }
    else if constexpr (type_traits::string_like<U>)
    {
        schema["type"] = "string";
    }
    else if constexpr (std::same_as<U, boost::json::value>)
    {
        // any
    }
    else if constexpr (std::same_as<U, boost::json::object>)
    {
        schema["type"] = "object";
    }
    else if constexpr (std::same_as<U, boost::json::array>)
    {
        schema["type"] = "array";
    }
    else if constexpr (boost::describe::has_describe_enumerators<U>::value)
    {
        boost::json::array names;
        boost::mp11::mp_for_each<boost::describe::describe_enumerators<U>>([&](auto d)
        {
            names.emplace_back(d.name);
        });
        schema["type"] = "string";
        schema["enum"] = std::move(names);
    }
    else if constexpr (boost::describe::has_describe_members<U>::value)
    {
        boost::json::object properties;
        boost::json::array required;
        boost::mp11::mp_for_each<boost::describe::describe_members<U, boost::describe::mod_public>>([&](auto d)
        {
            using member_type = std::remove_cvref_t<decltype(std::declval<U&>().*(d.pointer))>;
            properties[d.name] = json_schema_for<member_type>();
            if (!is_optional_v<member_type>)
            {
                required.emplace_back(d.name);
            }
        });
        schema["type"] = "object";
        schema["title"] = boost::core::type_name<U>();
        schema["properties"] = std::move(properties);
        if (!required.empty())
        {
            schema["required"] = std::move(required);
        }
    }
    else if constexpr (detail::string_map_like<U>)
    {
        schema["type"] = "object";
        schema["additionalProperties"] = json_schema_for<typename U::mapped_type>();
    }
    else if constexpr (detail::sequence_like<U>)
    {
        schema["type"] = "array";
        schema["items"] = json_schema_for<typename U::value_type>();
    }
    else
    {
        schema["type"] = "object";
    }
    return schema;
}

/// The JSON Schema `type` name for @a T (empty for "any"). Shorthand over @ref json_schema_for.
template <typename T>
std::string json_type_name()
{
    auto schema = json_schema_for<T>();
    if (auto const* t = schema.if_contains("type"); t && t->is_string())
    {
        return std::string {t->as_string()};
    }
    return {};
}

/// Build a @ref param_info for a parameter of type @a T.
template <typename T>
param_info make_param_info(std::string name, std::string description, bool required)
{
    param_info p;
    p.name = std::move(name);
    p.description = std::move(description);
    p.schema = json_schema_for<T>();
    p.json_type = json_type_name<T>();
    p.required = required;
    return p;
}

/// A parameter's type schema: its schema (or the bare type) plus `default`, without `description`.
inline boost::json::object param_type_schema(param_info const& p)
{
    boost::json::object schema = p.schema;
    if (schema.empty() && !p.json_type.empty())
    {
        schema["type"] = p.json_type;
    }
    if (p.default_value)
    {
        schema["default"] = *p.default_value;
    }
    return schema;
}

/// A JSON Schema fragment for one parameter: @ref param_type_schema plus `description`.
inline boost::json::object param_schema(param_info const& p)
{
    boost::json::object schema = param_type_schema(p);
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
