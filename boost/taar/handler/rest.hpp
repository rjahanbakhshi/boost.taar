//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_REST_HPP
#define BOOST_TAAR_HANDLER_REST_HPP

#include "boost/taar/handler/rest_error.hpp"
#include <boost/test/execution_monitor.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/core/always_false.hpp>
#include <boost/taar/core/callable_traits.hpp>
#include <boost/taar/core/member_function_of.hpp>
#include <boost/taar/core/invoke_response.hpp>
#include <boost/taar/core/super_type.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/json/value.hpp>
#include <boost/json/serialize.hpp>
#include <boost/system/result.hpp>
#include <utility>
#include <type_traits>

namespace boost::taar::handler {
namespace detail {

template <typename... T>
concept compatible_arg_providers = requires
{
    requires have_super_type_v<detail::arg_provider_request<T>...>;
};

template <typename... T>
struct common_requests_type_impl
{
    static_assert(compatible_arg_providers<T...>, "Incompatible arg providers.");

    using type = super_type_t<detail::arg_provider_request<T>...>;
};

template <typename... T>
struct common_requests_type
{
    using deduced_type = typename common_requests_type_impl<T...>::type;
    using type = std::conditional_t<
        std::is_same_v<boost::beast::http::request_header<>, deduced_type>,
        boost::beast::http::request<boost::beast::http::empty_body, typename deduced_type::fields_type>,
        deduced_type>;
};

template <>
struct common_requests_type<>
{
    using type = boost::beast::http::request<boost::beast::http::empty_body>;
};

template <typename... T>
using common_requests_type_t = typename common_requests_type<T...>::type;

template <typename CallableType, std::size_t... Indexes, typename... ArgProvidersType>
auto rest_for_callable(
    CallableType&& callable,
    std::index_sequence<Indexes...>,
    ArgProvidersType&&... arg_providers)
{
    using noref_fn_type = std::remove_reference_t<CallableType>;

    static_assert(
        callable_args_count<noref_fn_type> == sizeof...(ArgProvidersType),
        "The number of REST arguments providers must be equal to number of "
        "callable arguments.");

    static_assert(
        (detail::is_rest_arg_provider<std::remove_cvref_t<ArgProvidersType>> && ...),
        "Invalid REST arg provider!");

    using request_type = detail::common_requests_type_t<
        std::remove_reference_t<ArgProvidersType>...>;

    return [
        callable = std::forward<CallableType>(callable),
        ...arg_providers = std::forward<ArgProvidersType>(arg_providers)
    ](const request_type& request, const matcher::context& context) mutable
    {
        try
        {
            return invoke_response(
                request,
                boost::beast::http::status::ok,
                callable,
                rest_arg<
                    callable_arg_type<noref_fn_type, Indexes>,
                    std::remove_reference_t<ArgProvidersType>>{arg_providers}(
                        request,
                        context
                    )...
            );
        }
        catch(const boost::system::system_error& e)
        {
            if (e.code().category() == error_category())
            {
                return invoke_response(
                    request,
                    boost::beast::http::status::bad_request,
                    [&e] { return e.what(); });
            }
            throw;
        }
    };
}

template <typename MemFnType, typename ObjectType, std::size_t... Indexes, typename... ArgProvidersType>
auto rest_for_memfn(
    MemFnType memfn,
    ObjectType&& object,
    std::index_sequence<Indexes...>,
    ArgProvidersType&&... arg_providers)
{
    using noref_fn_type = std::remove_reference_t<MemFnType>;

    static_assert(
        callable_args_count<noref_fn_type> == sizeof...(ArgProvidersType),
        "The number of REST arguments providers must be equal to number of "
        "callable arguments.");

    static_assert(
        (detail::is_rest_arg_provider<std::remove_cvref_t<ArgProvidersType>> && ...),
        "Invalid REST arg provider!");

    using request_type = detail::common_requests_type_t<
        std::remove_reference_t<ArgProvidersType>...>;

    return [
        memfn,
        object = std::forward<ObjectType>(object),
        ...arg_providers = std::forward<ArgProvidersType>(arg_providers)
    ](const request_type& request, const matcher::context& context) mutable
    {
        try
        {
            return invoke_response(
                request,
                boost::beast::http::status::ok,
                memfn,
                object,
                rest_arg<callable_arg_type<noref_fn_type, Indexes>, std::remove_reference_t<ArgProvidersType>>{arg_providers}(request, context)...);
        }
        catch(const std::exception& e) // TODO: Only handle rest related errors
        {
            return invoke_response(
                request,
                boost::beast::http::status::bad_request,
                [&e] { return e.what(); });
        }
    };
}

} // namespace detail

template <typename CallableType, typename... ArgProvidersType>
requires (!std::is_member_function_pointer_v<std::remove_cvref_t<CallableType>>)
auto rest(
    CallableType&& callable,
    ArgProvidersType&&... arg_providers)
{
    return detail::rest_for_callable(
        std::forward<CallableType>(callable),
        std::index_sequence_for<ArgProvidersType...>{},
        std::forward<ArgProvidersType>(arg_providers)...);
}

template <typename MemFnType, typename ObjectType, typename... ArgProvidersType>
requires (member_function_of<MemFnType, std::remove_cvref_t<ObjectType>>)
auto rest(
    MemFnType memfn,
    ObjectType&& object,
    ArgProvidersType&&... arg_providers)
{
    return detail::rest_for_memfn(
        memfn,
        std::forward<ObjectType>(object),
        std::index_sequence_for<ArgProvidersType...>{},
        std::forward<ArgProvidersType>(arg_providers)...);
}

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_REST_HPP
