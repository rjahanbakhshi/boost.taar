//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_REST_ARG_CAST_HPP
#define BOOST_TAAR_HANDLER_REST_ARG_CAST_HPP

#include <boost/taar/handler/rest_arg_cast_tag.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/core/always_false.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/system/system_error.hpp>
#include <utility>
#include <charconv>
#include <concepts>
#include <type_traits>

namespace boost::taar::handler {
namespace detail {

template <typename ToType>
struct rest_arg_cast_built_in_tag {};

// Built-in rest_arg_cast implementations. The user-defined ones can always
// override the built-in ones.

template <typename Type>
inline auto tag_invoke(rest_arg_cast_built_in_tag<Type>, Type&& from)
{
    return std::forward<Type>(from);
}

template <typename Type>
inline auto tag_invoke(rest_arg_cast_built_in_tag<Type>, const Type& from)
{
    return from;
}

inline bool tag_invoke(rest_arg_cast_built_in_tag<bool>, std::string_view const& from)
{
    if (boost::iequals(from, "true")) return true;
    if (boost::iequals(from, "yes")) return true;
    if (from == "1") return true;
    if (boost::iequals(from, "false")) return false;
    if (boost::iequals(from, "no")) return false;
    if (from == "0") return false;
    throw boost::system::system_error {
        error::invalid_boolean_format,
        "REST arg cast from string to boolean error"};
}

template <typename ToType>
inline ToType from_chars_conversion(char const* begin, std::size_t size)
{
    ToType result {};
    using std::cbegin;
    using std::cend;
    auto end = std::next(begin, size);
    auto [ptr, ec] = std::from_chars(begin, end, result);

    if (ec != std::errc{})
    {
        throw boost::system::system_error {
            error::invalid_number_format,
            "REST arg cast from string to number error (" +
            std::make_error_code(ec).message() + ")"
        };
    }
    else if (ptr != end && *ptr != '\0')
    {
        throw boost::system::system_error {
            error::invalid_number_format,
            "REST arg cast from string to number error (Partial conversion)"
        };
    }

    return result;
}

template <typename ToType> requires (
    (!std::same_as<ToType, bool> && std::is_integral_v<ToType>) ||
    std::is_floating_point_v<ToType>)
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, char const* from)
{
    return from_chars_conversion<ToType>(from, strlen(from));
}

template <typename ToType> requires (
    (!std::same_as<ToType, bool> && std::is_integral_v<ToType>) ||
    std::is_floating_point_v<ToType>)
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, std::string_view const& from)
{
    return from_chars_conversion<ToType>(from.data(), from.size());
}

template <typename FromType> requires (
    (!std::same_as<FromType, bool> && std::is_integral_v<FromType>) ||
    std::is_floating_point_v<FromType>)
inline std::string tag_invoke(rest_arg_cast_built_in_tag<std::string>, const FromType& from)
{
    std::string result(sizeof(FromType) * 4 + 1, '\0');
    std::to_chars_result tcr;
    for(;;)
    {
        auto [ptr, ec] = std::to_chars(result.data(), result.data() + result.size(), from);
        if (ec != std::errc::value_too_large)
        {
            result.resize(std::distance(result.data(), ptr));
            break;
        }
        result.resize(result.size());
    }

    return result;
}

template <typename FromType, typename ToType> requires requires(FromType&& from)
{
    requires !(std::is_pointer_v<std::decay_t<FromType>> && std::same_as<ToType, bool>);
    { std::forward<FromType>(from) } -> std::convertible_to<ToType>;
}
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, FromType&& from)
{
    return std::forward<FromType>(from);
}

template <typename FromType, typename ToType> requires(
    std::same_as<FromType, bool> &&
    std::is_constructible_v<ToType, char const*>)
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, FromType from)
{
    return from ? "1" : "0";
}

template <typename FromType, typename ToType> requires (
    std::same_as<FromType, boost::json::value>)
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, const FromType& jv)
{
    return boost::json::value_to<ToType>(jv);
}

inline char const* tag_invoke(rest_arg_cast_built_in_tag<char const*>, const std::string& from)
{
    return from.c_str();
}

template <typename FromType, typename ToType>
concept has_user_defined_rest_arg_cast = requires (FromType&& from)
{
    tag_invoke(
        rest_arg_cast_tag<ToType>{},
        std::forward<FromType>(from));
};

template <typename FromType, typename ToType>
concept has_built_in_rest_arg_cast = requires (FromType&& from)
{
    tag_invoke(
        rest_arg_cast_built_in_tag<ToType>{},
        std::forward<FromType>(from));
};

} // namespace detail

template <typename FromType, typename ToType>
concept rest_arg_castable =
    detail::has_user_defined_rest_arg_cast<FromType, ToType> ||
    detail::has_built_in_rest_arg_cast<FromType, ToType>;

template <typename ToType, typename FromType>
inline constexpr decltype(auto) rest_arg_cast(FromType&& from)
{
    if constexpr (detail::has_user_defined_rest_arg_cast<FromType, ToType>)
    {
        return tag_invoke(
            rest_arg_cast_tag<ToType>{},
            std::forward<FromType>(from));
    }
    else if constexpr (detail::has_built_in_rest_arg_cast<FromType, ToType>)
    {
        return tag_invoke(
            detail::rest_arg_cast_built_in_tag<ToType>{},
            std::forward<FromType>(from));
    }
    else
    {
        static_assert(always_false<FromType, ToType>, "Not a REST arg castable type!");
    }
}

} // boost::taar::handler

#endif // BOOST_TAAR_HANDLER_REST_ARG_CAST_HPP
