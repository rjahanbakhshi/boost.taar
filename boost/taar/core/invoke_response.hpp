//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_INVOKE_RESPONSE_HPP
#define BOOST_TAAR_CORE_INVOKE_RESPONSE_HPP

#include <boost/taar/core/always_false.hpp>
#include <boost/taar/core/is_http_response.hpp>
#include <boost/taar/core/callable_traits.hpp>
#include <boost/taar/core/specialization_of.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <type_traits>

namespace boost::taar {
namespace detail {

template <bool FromIsRequest, typename FromFields, bool ToIsRequest, typename ToFields>
void copy_header_field(
    const boost::beast::http::header<FromIsRequest, FromFields>& from,
    boost::beast::http::header<ToIsRequest, ToFields>& to,
    boost::beast::http::field field)
{
    auto iter = from.find(field);
    if (iter != from.end())
    {
        to.insert(field, iter->value());
    }
}

template <typename From>
concept convertible_to_json_value = requires
{
    { boost::json::value_from(std::declval<From>()) } -> std::same_as<boost::json::value>;
};

} // namespace detail

// TODO: the invoke_response mechanism should be revised. Perhaps using tag_invoke
// and other tools for more flexibility and support.

template <typename FieldsType, typename CallableType, typename... ArgsType>
static boost::beast::http::message_generator invoke_response(
    const boost::beast::http::request_header<FieldsType>& request_header,
    boost::beast::http::status response_status,
    CallableType&& callable,
    ArgsType&&... args)
{
    namespace http = boost::beast::http;
    using result_type = std::invoke_result_t<CallableType, ArgsType...>;

    if constexpr (std::is_same_v<void, result_type>)
    {
        std::invoke(std::forward<CallableType>(callable), std::forward<ArgsType>(args)...);
        http::response<http::empty_body> response {response_status, request_header.version()};
        detail::copy_header_field(request_header, response, http::field::connection);
        detail::copy_header_field(request_header, response, http::field::keep_alive);
        response.prepare_payload();
        return response;
    }
    else if constexpr (is_http_response_v<result_type>)
    {
        return std::invoke(std::forward<CallableType>(callable), std::forward<ArgsType>(args)...);
    }
    else if constexpr (std::constructible_from<std::string_view, result_type>)
    {
        http::response<http::string_body> response {response_status, request_header.version()};
        detail::copy_header_field(request_header, response, http::field::connection);
        detail::copy_header_field(request_header, response, http::field::keep_alive);
        response.set(boost::beast::http::field::content_type, "text/plain");
        response.body() = std::invoke(std::forward<CallableType>(callable), std::forward<ArgsType>(args)...);
        response.prepare_payload();
        return response;
    }
    else if constexpr (detail::convertible_to_json_value<result_type>)
    {
        http::response<http::string_body> response {response_status, request_header.version()};
        detail::copy_header_field(request_header, response, http::field::connection);
        detail::copy_header_field(request_header, response, http::field::keep_alive);
        response.set(boost::beast::http::field::content_type, "application/json");
        response.body() = boost::json::serialize(
            boost::json::value_from(
                std::invoke(std::forward<CallableType>(callable), std::forward<ArgsType>(args)...)));
        response.prepare_payload();
        return response;
    }
    else if constexpr (std::constructible_from<boost::json::value, const result_type&>)
    {
        http::response<http::string_body> response {response_status, request_header.version()};
        detail::copy_header_field(request_header, response, http::field::connection);
        detail::copy_header_field(request_header, response, http::field::keep_alive);
        response.set(boost::beast::http::field::content_type, "application/json");
        response.body() = boost::json::serialize(
            std::invoke(std::forward<CallableType>(callable), std::forward<ArgsType>(args)...));
        response.prepare_payload();
        return response;
    }
    else
    {
        static_assert(always_false<CallableType>, "Unsupported handler result type!");
    }
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_INVOKE_RESPONSE_HPP

