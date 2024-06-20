//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HANDLER_REST_HPP
#define BOOST_WEB_HANDLER_REST_HPP

#include <boost/web/handler/rest_arg.hpp>
#include <boost/web/matcher/context.hpp>
#include "boost/web/core/always_false.hpp"
#include "boost/web/core/callable_traits.hpp"
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/json/value.hpp>
#include <boost/json/serialize.hpp>
#include <boost/system/result.hpp>
#include <concepts>
#include <utility>
#include <type_traits>

namespace boost::web::handler {

class rest
{
private:
    struct internal_tag {};

    // TODO: the response_of mechanism should be revised. Perhaps using tag_invoke
    // and other tools for more flexibility and robustness.
    static auto response_of(const rest_arg_types::request_type& request)
    {
        namespace http = boost::beast::http;
        http::response<http::string_body> res {
            boost::beast::http::status::ok,
            request.version()};
        res.set(boost::beast::http::field::content_type, "text/plain");
        res.keep_alive(request.keep_alive());
        res.body() = "Success.";
        res.prepare_payload();
        return res;
    }

    template <typename ResultType>
    static auto response_of(
        const rest_arg_types::request_type& request,
        const ResultType& result)
    {
        namespace http = boost::beast::http;

        if constexpr (std::convertible_to<const ResultType&, std::string_view>)
        {
            http::response<http::string_body> res {
                boost::beast::http::status::ok,
                request.version()};
            res.set(boost::beast::http::field::content_type, "text/plain");
            res.keep_alive(request.keep_alive());
            res.body() = result;
            res.prepare_payload();
            return res;
        }
        else if constexpr (std::constructible_from<boost::json::value, const ResultType&>)
        {
            namespace http = boost::beast::http;
            http::response<http::string_body> res {
                boost::beast::http::status::ok,
                request.version()};
            res.set(boost::beast::http::field::content_type, "application/json");
            res.keep_alive(request.keep_alive());
            res.body() = boost::json::serialize(result);
            res.prepare_payload();
            return res;
        }
        else
        {
            static_assert(
                always_false<ResultType>,
                "The result type of the rest handler function is unsupported.");
        }
    }

    template <typename Callable, typename... Args>
    rest(internal_tag, Callable&& callable, rest_arg<Args>... rest_args)
        : invoker_ {
            [=, callable = std::forward<Callable>(callable)](
                const rest_arg_types::request_type& request,
                const matcher::context& context) mutable
            {
                namespace http = boost::beast::http;

                try
                {
                    if constexpr (
                        std::is_same_v<callable_result_type<std::remove_cvref_t<Callable>>, void>)
                    {
                        callable(rest_args.get_value(request, context).value()...);
                        return response_of(request);
                    }
                    else
                    {
                        return response_of(
                            request,
                            callable(rest_args.get_value(request, context).value()...));
                    }
                }
                catch(const std::exception& e)
                {
                    http::response<http::string_body> res {
                        boost::beast::http::status::bad_request,
                        request.version()};
                    res.set(boost::beast::http::field::content_type, "text/html");
                    res.keep_alive(request.keep_alive());
                    res.body() = e.what();
                    res.prepare_payload();
                    return res;
                }
                catch(...)
                {
                    http::response<http::string_body> res {
                        boost::beast::http::status::internal_server_error,
                        request.version()};
                    res.set(boost::beast::http::field::content_type, "text/html");
                    res.keep_alive(request.keep_alive());
                    res.body() = "Unknown error.";
                    res.prepare_payload();
                    return res;
                }
            }
        }
    {}

    template <typename CallableType, typename... RestArgsType, std::size_t... Indexes>
    rest(
        std::index_sequence<Indexes...>,
        CallableType&& callable,
        RestArgsType&&... rest_args)
        : rest(
            internal_tag {},
            std::forward<CallableType>(callable),
            rest_arg<
                std::remove_cvref_t<
                    callable_arg_type<
                        std::remove_cvref_t<CallableType>,
                        Indexes
                    >
                >
            >(rest_args)...)
    {
        // TODO: Add more constrains
        //static_assert(
        //    callable_args_count<std::remove_cvref<CallableType>> == sizeof...(RestArgsType),
        //    "Callable arguments count does not match the provided REST arguments.");
    }

public:
    template <typename CallableType, typename... RestArgsType>
    rest(
        CallableType&& callable,
        RestArgsType&&... rest_args)
        : rest(
            std::make_index_sequence<sizeof...(RestArgsType)>{},
            std::forward<CallableType>(callable),
            std::forward<RestArgsType>(rest_args)...)
    {}

    boost::beast::http::message_generator operator()(
        const rest_arg_types::request_type& request,
        const matcher::context& context) const
    {
        return invoker_(request, context);
    }

private:
    std::function<boost::beast::http::message_generator(
        const rest_arg_types::request_type&,
        const matcher::context&)> invoker_;
};

} // namespace boost::web::handler

#endif // BOOST_WEB_HANDLER_REST_HPP
