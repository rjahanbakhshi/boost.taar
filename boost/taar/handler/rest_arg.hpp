//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_REST_ARG_HPP
#define BOOST_TAAR_HANDLER_REST_ARG_HPP

#include <boost/taar/handler/rest_arg_cast.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/core/cookies.hpp>
#include <boost/taar/core/form_kvp.hpp>
#include <boost/taar/core/callable_traits.hpp>
#include <boost/taar/core/always_false.hpp>
#include <boost/taar/core/specialization_of.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/parse_query.hpp>
#include <boost/url/ignore_case.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/system/result.hpp>
#include <exception>
#include <system_error>
#include <unordered_set>
#include <variant>
#include <optional>
#include <format>
#include <string>
#include <algorithm>
#include <utility>
#include <type_traits>

namespace boost::taar::handler {
namespace detail {

template <typename T>
concept string_like = std::constructible_from<std::string, T>;

template <typename ArgProviderType>
concept is_rest_arg_provider = requires
{
    requires (
        std::is_function_v<std::remove_pointer_t<ArgProviderType>> ||
        has_call_operator<ArgProviderType>);
    typename callable_result_type<ArgProviderType>;
    requires callable_args_count<ArgProviderType> == 2;
    typename callable_arg_type<ArgProviderType, 0>;
    typename callable_arg_type<ArgProviderType, 1>;
    requires std::is_convertible_v<callable_arg_type<ArgProviderType, 1>, const matcher::context&>;
};

template <typename ArgProviderType>
using arg_provider_request = std::conditional_t<
    is_rest_arg_provider<ArgProviderType>,
    std::remove_cvref_t<callable_arg_type<ArgProviderType, 0>>,
    boost::beast::http::request_header<>>;

template <typename ArgProviderType>
using arg_provider_result = std::conditional_t<
    is_rest_arg_provider<ArgProviderType>,
    callable_result_type<ArgProviderType>,
    ArgProviderType>;

} // namespace detail

// REST arg provider providing default value if the underlying value is not found
template <typename ArgProviderType, typename ValueType>
struct with_default
{
    using arg_provider_type = ArgProviderType;
    using value_type = ValueType;
    using arg_provider_request_type = detail::arg_provider_request<arg_provider_type>;
    using arg_provider_result_type = detail::arg_provider_result<arg_provider_type>;

    with_default(arg_provider_type arg_provider, value_type default_value)
        : arg_provider_ {std::move(arg_provider)}
        , default_value_ {std::move(default_value)}
    {
        static_assert(
            detail::is_rest_arg_provider<arg_provider_type>,
            "with_default must be used with an arg_provider as input!");
    }

    std::string name() const
    {
        return arg_provider_.name();
    }

    decltype(auto) operator()(
        const arg_provider_request_type& request,
        const matcher::context& context) const
    {
        return arg_provider_(request, context);
    }

    const value_type& default_value() const
    {
        return default_value_;
    }

    arg_provider_type arg_provider_;
    value_type default_value_;
};

// CTAD
template <typename T, typename U>
with_default(T, U) -> with_default<std::remove_cvref_t<T>, std::remove_cvref_t<U>>;

// Generic rest_arg
template <typename ArgType, typename ArgProviderType>
struct rest_arg
{
    using arg_type = std::remove_cvref_t<ArgType>;
    using arg_provider_type = std::remove_cvref_t<ArgProviderType>;
    using arg_provider_request_type = detail::arg_provider_request<arg_provider_type>;
    using arg_provider_result_type = detail::arg_provider_result<arg_provider_type>;

    rest_arg(arg_provider_type arg_provider)
        : arg_provider_ {std::move(arg_provider)}
    {}

    arg_type operator()(
        std::size_t index,
        const arg_provider_request_type& request,
        const matcher::context& context) &&
    {
        try
        {
            if constexpr (
                specialization_of<with_default, arg_provider_type> &&
                rest_arg_castable<typename arg_provider_result_type::value_type, arg_type>)
            {
                static_assert(
                    rest_arg_castable<typename arg_provider_type::value_type, arg_type>,
                    "Incompatible rest arg default value!");

                // It's necessary to store the retrieved value to make sure it outlives
                // the call operator. Case in point, std::string -> std::string_view
                provider_result_ = arg_provider_(request, context);
                if (provider_result_.has_error() &&
                    provider_result_.error() == error::argument_not_found)
                {
                    return rest_arg_cast<arg_type>(arg_provider_.default_value());
                }

                return rest_arg_cast<arg_type>(std::move(provider_result_.value()));
            }
            else if constexpr (
                specialization_of<std::optional, arg_type> &&
                detail::is_rest_arg_provider<arg_provider_type> &&
                rest_arg_castable<typename arg_provider_result_type::value_type, arg_type>)
            {
                // It's necessary to store the retrieved value to make sure it outlives
                // the call operator. Case in point, std::string -> std::string_view
                provider_result_ = arg_provider_(request, context);
                if (provider_result_.has_error() &&
                    provider_result_.error() == error::argument_not_found)
                {
                    return std::nullopt;
                }

                return rest_arg_cast<arg_type>(std::move(provider_result_.value()));
            }
            else if constexpr (
                detail::is_rest_arg_provider<arg_provider_type> &&
                rest_arg_castable<typename arg_provider_result_type::value_type, arg_type>)
            {
                // It's necessary to store the retrieved value to make sure it outlives
                // the call operator. Case in point, std::string -> std::string_view
                provider_result_ = arg_provider_(request, context);
                return rest_arg_cast<arg_type>(std::move(provider_result_.value()));
            }
            else if constexpr (rest_arg_castable<arg_provider_type, arg_type>)
            {
                return rest_arg_cast<arg_type>(std::move(arg_provider_));
            }
            else
            {
                static_assert(
                    always_false<arg_provider_type, arg_type>,
                    "Incompatible rest arg provider!");
            }
        }
        catch(const std::exception& e)
        {
            throw boost::system::system_error{
                error::invalid_argument_format,
                std::format(
                    "{} - REST arg({}:{})",
                    e.what(),
                    index + 1,
                    arg_provider_.name())};
        }
        catch(...)
        {
            throw boost::system::system_error{
                error::invalid_argument_format,
                std::format(
                    "REST arg({}:{})",
                    index + 1,
                    arg_provider_.name())};
        }
    }

    arg_provider_type arg_provider_;
    arg_provider_result_type provider_result_;
};

// REST arg provider from the request path
struct path_arg
{
    path_arg(std::string path_key)
        : path_key_ {std::move(path_key)}
    {}

    const std::string& name() const
    {
        return path_key_;
    }

    boost::system::result<std::string> operator()(
        const boost::beast::http::request_header<>&,
        const matcher::context& context) const
    {
        auto iter = context.path_args.find(path_key_);
        if (iter != context.path_args.end())
        {
            return std::string {iter->second};
        }
        return error::argument_not_found;
    }

    std::string path_key_;
};

// REST arg provider from the request query params
struct query_arg
{
    query_arg(
            std::string query_key,
            boost::urls::ignore_case_param ic = {})
        : query_key_ {std::move(query_key)}
        , ic_ {std::move(ic)}
    {}

    const std::string& name() const
    {
        return query_key_;
    }

    boost::system::result<std::string> operator()(
        const boost::beast::http::request_header<>& request,
        const matcher::context&) const
    {
        auto url_view = boost::urls::parse_origin_form(request.target());
        if (!url_view)
        {
            return error::invalid_url_format;
        }

        auto params = url_view->params();

        if (params.count(query_key_, ic_) > 1)
        {
            return error::argument_ambiguous;
        }

        auto iter = params.find(query_key_, ic_);
        if (iter != params.end())
        {
            const auto& param = *iter;
            return std::string {param.value};
        }
        return error::argument_not_found;
    }

    std::string query_key_;
    boost::urls::ignore_case_param ic_;
};

// REST arg provider from the request headers
struct header_arg
{
    header_arg(std::string header_name)
        : header_ {std::move(header_name)}
    {}

    header_arg(boost::beast::http::field field)
        : header_ {field}
    {}

    std::string name() const
    {
        std::string result;
        std::visit([&](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>)
                result = arg;
            else if constexpr (std::is_same_v<T, boost::beast::http::field>)
                result = to_string(arg);
            else
                static_assert(false, "Not supported type!");
        }, header_);

        return result;
    }

    boost::system::result<std::string> operator()(
        const boost::beast::http::request_header<>& request,
        const matcher::context&) const
    {
        boost::system::result<std::string> result;

        std::visit([&](auto&& arg)
        {
            auto iter_range = request.equal_range(arg);

            if (iter_range.first == request.end())
            {
                result = error::argument_not_found;
            }
            else
            {
                result = std::string {iter_range.first->value()};
                if (std::any_of(
                        iter_range.first,
                        iter_range.second,
                        [&](auto const& val)
                        {
                            return val.value() != result.value();
                        }
                   ))
                {
                    // Same header is repeated with different values.
                    result = error::argument_ambiguous;
                }
            }
        }, header_);

        return result;
    }

    std::variant<boost::beast::http::field, std::string> header_;
};

// REST arg provider from the request cookie header
struct cookie_arg
{
    cookie_arg(std::string name)
        : name_ {std::move(name)}
    {}

    const std::string& name() const
    {
        return name_;
    }

    boost::system::result<std::string> operator()(
        const boost::beast::http::request_header<>& request,
        const matcher::context&) const
    {
        cookies parsed_cookies;
        auto r = request.equal_range(boost::beast::http::field::cookie);
        for (; r.first != r.second; ++r.first)
        {
            parse_cookies(r.first->value(), parsed_cookies);
        }

        auto const iter = parsed_cookies.find(name_);
        if (iter != parsed_cookies.end())
        {
            return iter->second;
        }

        return error::argument_not_found;
    }

    std::string name_;
};

struct all_content_types_t {};
constexpr all_content_types_t all_content_types {};

// REST arg provider from the request body as string
struct string_body_arg
{
    template <detail::string_like... T>
    string_body_arg(T&&... content_types)
        : content_types_ {std::forward<T>(content_types)...}
    {
        if constexpr (sizeof...(T) == 0)
        {
            content_types_.emplace("text/plain");
        }
    }

    std::string name() const
    {
        return "string_body";
    }

    string_body_arg(all_content_types_t)
    {}

    boost::system::result<std::string> operator()(
        const boost::beast::http::request<boost::beast::http::string_body>& request,
        const matcher::context&) const
    {
        if (!content_types_.empty())
        {
            auto range = request.equal_range(boost::beast::http::field::content_type);
            if (std::find_if(range.first, range.second, [&](auto const& elem)
                {
                    return content_types_.contains(elem.value());
                }) == range.second)
            {
                return error::invalid_content_type;
            }
        }

        return std::string {request.body()};
    }

    std::unordered_set<std::string> content_types_;
};

// REST arg provider from the request body as json value
struct json_body_arg
{
    template <detail::string_like... T>
    json_body_arg(T&&... content_types)
        : content_types_ {std::forward<T>(content_types)...}
    {
        if constexpr (sizeof...(T) == 0)
        {
            content_types_.emplace("application/json");
        }
    }

    std::string name() const
    {
        return "json_body";
    }

    json_body_arg(all_content_types_t)
    {}

    boost::system::result<boost::json::value> operator()(
        const boost::beast::http::request<boost::beast::http::string_body>& request,
        const matcher::context&) const
    {
        if (!content_types_.empty())
        {
            auto range = request.equal_range(boost::beast::http::field::content_type);
            if (std::find_if(range.first, range.second, [&](auto const& elem)
                {
                    return content_types_.contains(elem.value());
                }) == range.second)
            {
                return error::invalid_content_type;
            }
        }

        std::error_code ec;
        auto json = boost::json::parse(request.body(), ec);
        if (ec)
        {
            return error::invalid_request_format;
        }
        return json;
    }

    std::unordered_set<std::string> content_types_;
};

// REST arg provider from the request body for application/x-www-form-urlencoded
struct url_encoded_from_data_arg
{
    template <detail::string_like... T>
    url_encoded_from_data_arg(T&&... content_types)
        : content_types_ {std::forward<T>(content_types)...}
    {
        if constexpr (sizeof...(T) == 0)
        {
            content_types_.emplace("application/x-www-form-urlencoded");
        }
    }

    std::string name() const
    {
        return "url_encoded_from_data";
    }

    url_encoded_from_data_arg(all_content_types_t)
    {}

    boost::system::result<form_kvp> operator()(
        const boost::beast::http::request<boost::beast::http::string_body>& request,
        const matcher::context&) const
    {
        if (!content_types_.empty())
        {
            auto range = request.equal_range(boost::beast::http::field::content_type);
            if (std::find_if(range.first, range.second, [&](auto const& elem)
                {
                    return content_types_.contains(elem.value());
                }) == range.second)
            {
                return error::invalid_content_type;
            }
        }

        auto pev = boost::urls::parse_query(request.body());
        if (!pev)
        {
            return error::invalid_request_format;
        }

        form_kvp result;
        for (auto const& param : pev.value())
        {
            boost::urls::decode_view decoded_key {param.key};
            boost::urls::decode_view decoded_value {param.value};
            result.emplace(
                std::string(decoded_key.begin(), decoded_key.end()),
                std::string(decoded_value.begin(), decoded_value.end()));
        }
        return result;
    }

    std::unordered_set<std::string> content_types_;
};

} // boost::taar::handler

#endif // BOOST_TAAR_HANDLER_REST_ARG_HPP
