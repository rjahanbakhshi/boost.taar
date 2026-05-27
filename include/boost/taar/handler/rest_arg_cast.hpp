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
#include <boost/taar/type_traits/numeric_type.hpp>
#include <boost/taar/type_traits/container_like.hpp>
#include <boost/taar/type_traits/string_like.hpp>
#include <boost/taar/type_traits/always_false.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/system/system_error.hpp>
#include <utility>
#include <charconv>
#include <concepts>
#include <type_traits>

namespace boost::taar::handler {

// Forward declaration — defined after the detail namespace.
template <typename ToType, typename FromType>
inline constexpr decltype(auto) rest_arg_cast(FromType const& from);

namespace detail {

template <typename ToType>
struct rest_arg_cast_built_in_tag {};

// Built-in rest_arg_cast implementations. The user-defined ones can always
// override the built-in ones.

// Directly forward if the type is the same, except for non-owning string-like
// types (to avoid accidental dangling references).
template <typename Type> requires
    (!std::same_as<Type, const char*>) &&
    (!std::same_as<Type, std::string_view>)
inline Type tag_invoke(rest_arg_cast_built_in_tag<Type>, Type from)
{
    return std::move(from);
}

// Any implicitly convertible types, except for pointer-to-object to bool (to avoid
// common mistakes like accidentally treating a string literal as a boolean) and
// string-like types (to avoid accidental dangling references).
template <typename FromType, typename ToType> requires requires(FromType const& from)
{
    requires !(std::is_pointer_v<std::decay_t<FromType>> && std::same_as<ToType, bool>);
    requires !(std::same_as<ToType, const char*>);
    requires !(std::same_as<ToType, std::string_view>);
    { from } -> std::convertible_to<ToType>;
}
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, FromType const& from)
{
    return from;
}

// From a string-like type to bool: accept "true", "yes", "1" (case-insensitive)
// as true, and "false", "no", "0" (case-insensitive) as false. Throw on any other value.
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

// Helper function for from_chars-based conversions. It performs the conversion
// and throws on error or partial conversion.
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

// From a string-like type to a numeric type: use std::from_chars and throw on
// error or partial conversion.
template <type_traits::numeric_type ToType>
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, std::string_view from)
{
    return from_chars_conversion<ToType>(from.data(), from.size());
}

// From a numeric type to std::string: use std::to_chars and throw on error. The
// buffer size is initially set to 4 times the size of the numeric type (which is
// enough for any base, including binary), and doubled on each retry if the number
// is too large to fit in the buffer.
template <type_traits::numeric_type FromType>
inline std::string tag_invoke(rest_arg_cast_built_in_tag<std::string>, FromType const& from)
{
    std::string result(sizeof(FromType) * 4 + 1, '\0');
    std::to_chars_result tcr;
    for(int i = 0; i < 3; ++i)
    {
        auto [ptr, ec] = std::to_chars(result.data(), result.data() + result.size(), from);
        if (ec == std::errc::value_too_large) [[unlikely]]
        {
            result.resize(result.size() * 2);
            continue;
        }
        else if (ec != std::errc()) [[unlikely]]
        {
            throw boost::system::system_error {
                error::invalid_number_format,
                "REST arg cast from string to number error"};
        }

        [[likely]]
        result.resize(std::distance(result.data(), ptr));
        return result;
    }

    [[unlikely]]
    throw boost::system::system_error {
        error::invalid_number_format,
        "REST arg cast from number to string error (number too large)"};
}

// From bool to std::string: "1" for true and "0" for false.
template <typename FromType, typename ToType> requires(
    std::same_as<FromType, bool> && (
        std::same_as<ToType, char const*> ||
        std::same_as<ToType, std::string> ||
        std::same_as<ToType, std::string_view>))
inline std::string tag_invoke(rest_arg_cast_built_in_tag<ToType>, FromType from)
{
    return from ? "1" : "0";
}

// From a JSON value to any type that boost::json::value_to supports.
template <typename FromType, typename ToType> requires (
    std::same_as<FromType, boost::json::value>)
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, FromType const& jv)
{
    return boost::json::value_to<ToType>(jv);
}

// From a string-like type to std::string.
inline std::string tag_invoke(rest_arg_cast_built_in_tag<std::string>, std::string_view from)
{
    return std::string(from);
}

// Safe-form casts: these overloads return std::string (an owning type)
// rather than the requested non-owning view type, preventing dangling
// references. The safe_arg_type_t mechanism in rest.hpp detects these
// overloads and uses their return type as the safe storage type.
inline std::string tag_invoke(rest_arg_cast_built_in_tag<std::string_view>, std::string from)
{
    return from;
}

inline std::string tag_invoke(rest_arg_cast_built_in_tag<char const*>, std::string from)
{
    return from;
}

// From a string-like type to a container of elements that can be rest_arg_cast
// from string-like types. The string is parsed as a comma-separated list of
// elements, which are trimmed and cast to the value type of the target container,
// and then pushed into the container. This allows convenient parsing of simple
// lists like "1,2,3" into std::vector<int>.
template <typename FromType, typename ToType> requires (
    type_traits::string_like<FromType> &&
    !std::same_as<ToType, std::string> &&
    !std::same_as<ToType, std::wstring> &&
    !std::same_as<ToType, std::u8string> &&
    !std::same_as<ToType, std::u16string> &&
    !std::same_as<ToType, std::u32string> &&
    !std::same_as<ToType, std::string_view> &&
    !std::same_as<ToType, std::wstring_view> &&
    !std::same_as<ToType, std::u8string_view> &&
    !std::same_as<ToType, std::u16string_view> &&
    !std::same_as<ToType, std::u32string_view> &&
    type_traits::container_like<ToType> && (
        type_traits::push_backable_container<ToType, typename ToType::value_type> ||
        type_traits::insertable_container<ToType, typename ToType::value_type>))
inline ToType tag_invoke(rest_arg_cast_built_in_tag<ToType>, FromType const& from)
{
    using value_type = typename ToType::value_type;

    ToType result;
    auto const sv = std::string_view(from);

    // Parse from as csv, cast each element to the value type of the target type,
    // and push them into the result container.
    std::size_t start = 0;
    while (start < sv.size())
    {
        auto end = sv.find(',', start);
        if (end == std::string_view::npos)
        {
            end = sv.size();
        }

        auto trim = [](std::string_view s) noexcept -> std::string_view {
            auto const s_start = s.find_first_not_of(" \t\r\n");
            if (s_start == std::string_view::npos) return {};
            auto const s_end = s.find_last_not_of(" \t\r\n");
            return s.substr(s_start, s_end - s_start + 1);
        };
        auto element_str = trim(std::string_view(sv.data() + start, end - start));
        if constexpr (type_traits::push_backable_container<ToType, value_type>)
            result.push_back(rest_arg_cast<value_type>(element_str));
        else
            result.insert(rest_arg_cast<value_type>(element_str));

        start = end + 1;
    }

    return result;
}

// Check if there is a user-defined rest_arg_cast from FromType to ToType.
template <typename FromType, typename ToType>
concept has_user_defined_rest_arg_cast = requires (FromType const& from)
{
    tag_invoke(rest_arg_cast_tag<ToType>{}, from);
};

// Check if there is a built-in rest_arg_cast from FromType to ToType.
template <typename FromType, typename ToType>
concept has_built_in_rest_arg_cast = requires (FromType const& from)
{
    tag_invoke(rest_arg_cast_built_in_tag<ToType>{}, from);
};

} // namespace detail

/** True if @ref rest_arg_cast can convert @a FromType to @a ToType.

    Satisfied when either a user-defined `tag_invoke` overload exists for
    @ref rest_arg_cast_tag<ToType>, or one of the built-in conversions
    applies.
*/
template <typename FromType, typename ToType>
concept rest_arg_castable =
    detail::has_user_defined_rest_arg_cast<FromType, ToType> ||
    detail::has_built_in_rest_arg_cast<FromType, ToType>;

/** Cast a REST argument from @a FromType to @a ToType.

    Resolution order:

    @li A user-defined `tag_invoke(rest_arg_cast_tag<ToType>, ...)` overload
        found by ADL takes precedence.
    @li Otherwise a built-in conversion is selected. Built-ins cover
        identity, implicit conversions, string-to-bool (`"true"`, `"yes"`,
        `"1"`), string-to-number (via `std::from_chars`), number-to-string
        (via `std::to_chars`), `boost::json::value` to any
        `value_to`-compatible type, and CSV-style string to container.

    Throws `boost::system::system_error` with an @ref boost::taar::error
    code on conversion failure.
*/
template <typename ToType, typename FromType>
inline constexpr decltype(auto) rest_arg_cast(FromType const& from)
{
    if constexpr (detail::has_user_defined_rest_arg_cast<FromType, ToType>)
    {
        return tag_invoke(rest_arg_cast_tag<ToType>{}, from);
    }
    else if constexpr (detail::has_built_in_rest_arg_cast<FromType, ToType>)
    {
        return tag_invoke(detail::rest_arg_cast_built_in_tag<ToType>{}, from);
    }
    else
    {
        static_assert(type_traits::always_false<FromType, ToType>, "Not a REST arg castable type!");
    }
}

// Helper type trait to get the result type of rest_arg_cast without performing the cast.
template <typename ToType, typename FromType>
using rest_arg_cast_result_t = decltype(rest_arg_cast<ToType>(std::declval<FromType>()));

} // boost::taar::handler

#endif // BOOST_TAAR_HANDLER_REST_ARG_CAST_HPP
