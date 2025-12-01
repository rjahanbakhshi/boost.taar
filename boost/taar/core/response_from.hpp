//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_RESPONSE_FROM_HPP
#define BOOST_TAAR_CORE_RESPONSE_FROM_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/is_http_response.hpp>
#include <boost/taar/core/response_from_tag.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <concepts>
#include <type_traits>

namespace boost::taar {
namespace detail {

template <typename T>
concept is_response_from_result = requires
{
    requires
        is_http_response<T> ||
        std::same_as<T, boost::beast::http::message_generator>;
};

template <typename... T>
struct response_from_built_in_tag {};

// Built-in response_from implementations. The user-defined ones can always
// override the built-in ones.
inline auto tag_invoke(response_from_built_in_tag<>)
{
    namespace http = boost::beast::http;
    http::response<http::empty_body> response {http::status::ok, 11};
    response.prepare_payload();
    return response;
}

template <typename T> requires (
    is_response_from_result<std::remove_cvref_t<T>>)
inline decltype(auto) tag_invoke(
    response_from_built_in_tag<std::remove_cvref_t<T>>,
    T&& response)
{
    return std::forward<T>(response);
}

template <typename T> requires (
    std::integral<std::remove_cvref_t<T>> ||
    std::floating_point<std::remove_cvref_t<T>>)
inline auto tag_invoke(
    response_from_built_in_tag<std::remove_cvref_t<T>>,
    T&& value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "text/plain");
    response.body() = std::format("{}", value);
    response.prepare_payload();
    return response;
}

inline auto tag_invoke(
    response_from_built_in_tag<std::string>,
    std::string value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "text/plain");
    response.body() = std::move(value);
    response.prepare_payload();
    return response;
}

inline auto tag_invoke(
    response_from_built_in_tag<std::string_view>,
    std::string_view value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "text/plain");
    response.body() = value;
    response.prepare_payload();
    return response;
}

inline auto tag_invoke(
    response_from_built_in_tag<char const*>,
    char const* value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "text/plain");
    response.body() = value;
    response.prepare_payload();
    return response;
}

inline auto tag_invoke(
    response_from_built_in_tag<boost::json::value>,
    const boost::json::value& value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "application/json");
    response.body() = boost::json::serialize(value);
    response.prepare_payload();
    return response;
}

template<typename... T>
concept has_user_defined_response_from = requires (T&&... args)
{
    tag_invoke(
        response_from_tag<std::decay_t<T>...>{},
        std::forward<T>(args)...);

    requires is_response_from_result<
        std::remove_cvref_t<
            decltype(
                tag_invoke(
                    response_from_tag<std::decay_t<T>...>{},
                    std::forward<T>(args)...))>>;
};

template<typename... T>
concept has_built_in_response_from = requires (T&&... args)
{
    tag_invoke(
        response_from_built_in_tag<std::decay_t<T>...>{},
        std::forward<T>(args)...);

    requires is_response_from_result<
        std::remove_cvref_t<
            decltype(
                tag_invoke(
                    response_from_built_in_tag<std::decay_t<T>...>{},
                    std::forward<T>(args)...))>>;
};

} // namespace detail

template <typename... T>
concept has_response_from =
    detail::has_user_defined_response_from<T...> ||
    detail::has_built_in_response_from<T...>;

template <has_response_from... T>
inline constexpr auto response_from(T&&... value)
{
    if constexpr (detail::has_user_defined_response_from<T...>)
    {
        return tag_invoke(
            response_from_tag<std::decay_t<T>...>{},
            std::forward<T>(value)...);
    }
    else if constexpr (detail::has_built_in_response_from<T...>)
    {
        return tag_invoke(
            detail::response_from_built_in_tag<std::decay_t<T>...>{},
            std::forward<T>(value)...);
    }
}

template <has_response_from... T>
using response_from_t = std::invoke_result_t<decltype(response_from<T...>), T...>;

template <typename CallableType, typename... ArgsType> requires (
    std::is_void_v<std::invoke_result_t<CallableType, ArgsType...>>)
inline auto response_from_invoke(
    CallableType&& callable,
    ArgsType&&... args) ->
awaitable<response_from_t<>>
{
    std::invoke(
        std::forward<CallableType>(callable),
        std::forward<ArgsType>(args)...);
    co_return response_from();
}

template <typename CallableType, typename... ArgsType> requires (
    has_response_from<std::invoke_result_t<CallableType, ArgsType...>>)
inline auto response_from_invoke(
    CallableType&& callable,
    ArgsType&&... args) ->
awaitable<
    response_from_t<typename std::invoke_result_t<CallableType, ArgsType...>>>
{
    co_return response_from(std::invoke(
        std::forward<CallableType>(callable),
        std::forward<ArgsType>(args)...));
}

template <typename CallableType, typename... ArgsType> requires (
    is_awaitable<std::invoke_result_t<CallableType, ArgsType...>> &&
    std::is_void_v<typename std::invoke_result_t<CallableType, ArgsType...>::value_type>)
inline auto response_from_invoke(
    CallableType&& callable,
    ArgsType&&... args) ->
awaitable<
    response_from_t<>>
{
    co_await std::invoke(
        std::forward<CallableType>(callable),
        std::forward<ArgsType>(args)...);
    co_return response_from();
}

template <typename CallableType, typename... ArgsType> requires (
    is_awaitable<std::invoke_result_t<CallableType, ArgsType...>> &&
    has_response_from<typename std::invoke_result_t<CallableType, ArgsType...>::value_type>)
inline auto response_from_invoke(
    CallableType&& callable,
    ArgsType&&... args) ->
awaitable<
    response_from_t<typename std::invoke_result_t<CallableType, ArgsType...>::value_type>>
{
    co_return response_from(co_await std::invoke(
        std::forward<CallableType>(callable),
        std::forward<ArgsType>(args)...));
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_RESPONSE_FROM_HPP
