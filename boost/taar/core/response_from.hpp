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

#include <boost/taar/core/is_http_response.hpp>
#include <boost/taar/core/always_false.hpp>
#include <boost/taar/core/response_from_tag.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <concepts>

namespace boost::taar {

namespace detail {

template <typename T>
concept is_response_from_result = requires
{
    requires
        is_http_response<std::remove_cvref_t<T>> ||
        std::same_as<
            std::remove_cvref_t<T>,
            boost::beast::http::message_generator>;
};

struct response_from_built_in_tag {};

// Built-in response_from implementations. The user-defined ones can always
// override the built-in ones.
inline auto tag_invoke(response_from_built_in_tag)
{
    namespace http = boost::beast::http;
    http::response<http::empty_body> response {http::status::ok, 11};
    response.prepare_payload();
    return response;
}

template <typename T> requires (is_response_from_result<T>)
inline decltype(auto) tag_invoke(response_from_built_in_tag, T&& response)
{
    return std::forward<T>(response);
}

template <typename T> requires (
    std::integral<std::remove_cvref_t<T>> ||
    std::floating_point<std::remove_cvref_t<T>>)
inline auto tag_invoke(response_from_built_in_tag, T&& value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "text/plain");
    response.body() = std::format("{}", value);
    response.prepare_payload();
    return response;
}

inline auto tag_invoke(response_from_built_in_tag, std::string value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "text/plain");
    response.body() = std::move(value);
    response.prepare_payload();
    return response;
}

inline auto tag_invoke(response_from_built_in_tag, std::string_view value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "text/plain");
    response.body() = value;
    response.prepare_payload();
    return response;
}

inline auto tag_invoke(response_from_built_in_tag, char const* value)
{
    namespace http = boost::beast::http;
    http::response<http::string_body> response {http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "text/plain");
    response.body() = value;
    response.prepare_payload();
    return response;
}

inline auto tag_invoke(response_from_built_in_tag, const boost::json::value& value)
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
    tag_invoke(response_from_tag{}, std::forward<T>(args)...);
    requires is_response_from_result<
        decltype(
            tag_invoke(
                response_from_tag{},
                std::forward<T>(args)...))>;
};

template<typename... T>
concept has_built_in_response_from = requires (T&&... args)
{
    tag_invoke(response_from_built_in_tag{}, std::forward<T>(args)...);
    requires is_response_from_result<
        decltype(
            tag_invoke(
                response_from_built_in_tag{},
                std::forward<T>(args)...))>;
};

} // namespace detail

template <typename... T>
concept has_response_from =
    detail::has_user_defined_response_from<T...> ||
    detail::has_built_in_response_from<T...>;

template <has_response_from... T>
inline constexpr decltype(auto) response_from(T&&... value)
{
    if constexpr (detail::has_user_defined_response_from<T...>)
    {
        return tag_invoke(response_from_tag{}, std::forward<T>(value)...);
    }
    else if constexpr (detail::has_built_in_response_from<T...>)
    {
        return tag_invoke(detail::response_from_built_in_tag{}, std::forward<T>(value)...);
    }
    else
    {
        static_assert(
            always_false<T...>,
            "response_from is not supported for T!");
    }
}

template <has_response_from... T>
using response_from_t = std::invoke_result_t<decltype(response_from<T...>), T...>;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_RESPONSE_FROM_HPP
