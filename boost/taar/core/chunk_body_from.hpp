//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CHUNK_BODY_FROM_HPP
#define BOOST_TAAR_CORE_CHUNK_BODY_FROM_HPP

#include <boost/taar/core/chunk_body_from_tag.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <concepts>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace boost::taar {
namespace detail {

template <typename T>
struct chunk_body_from_built_in_tag {};

inline std::string tag_invoke(
    chunk_body_from_built_in_tag<std::string>,
    std::string value)
{
    return value;
}

inline std::string tag_invoke(
    chunk_body_from_built_in_tag<std::string_view>,
    std::string_view value)
{
    return std::string{value};
}

inline std::string tag_invoke(
    chunk_body_from_built_in_tag<char const*>,
    char const* value)
{
    return std::string{value};
}

inline std::string tag_invoke(
    chunk_body_from_built_in_tag<boost::json::value>,
    boost::json::value const& value)
{
    return boost::json::serialize(value);
}

template <typename T> requires (
    std::integral<std::remove_cvref_t<T>> ||
    std::floating_point<std::remove_cvref_t<T>>)
inline std::string tag_invoke(
    chunk_body_from_built_in_tag<std::remove_cvref_t<T>>,
    T&& value)
{
    return std::format("{}", value);
}

inline std::string tag_invoke(
    chunk_body_from_built_in_tag<std::vector<std::byte>>,
    std::vector<std::byte> const& value)
{
    return std::string{
        reinterpret_cast<char const*>(value.data()),
        value.size()};
}

inline std::string tag_invoke(
    chunk_body_from_built_in_tag<std::span<std::byte const>>,
    std::span<std::byte const> value)
{
    return std::string{
        reinterpret_cast<char const*>(value.data()),
        value.size()};
}

template<typename T>
concept has_user_defined_chunk_body_from = requires (T&& arg)
{
    { tag_invoke(chunk_body_from_tag<std::decay_t<T>>{}, std::forward<T>(arg)) }
        -> std::convertible_to<std::string>;
};

template<typename T>
concept has_built_in_chunk_body_from = requires (T&& arg)
{
    { tag_invoke(chunk_body_from_built_in_tag<std::decay_t<T>>{}, std::forward<T>(arg)) }
        -> std::convertible_to<std::string>;
};

} // namespace detail

template <typename T>
concept has_chunk_body_from =
    detail::has_user_defined_chunk_body_from<T> ||
    detail::has_built_in_chunk_body_from<T>;

template <has_chunk_body_from T>
inline std::string chunk_body_from(T&& value)
{
    if constexpr (detail::has_user_defined_chunk_body_from<T>)
    {
        return tag_invoke(
            chunk_body_from_tag<std::decay_t<T>>{},
            std::forward<T>(value));
    }
    else if constexpr (detail::has_built_in_chunk_body_from<T>)
    {
        return tag_invoke(
            detail::chunk_body_from_built_in_tag<std::decay_t<T>>{},
            std::forward<T>(value));
    }
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CHUNK_BODY_FROM_HPP
