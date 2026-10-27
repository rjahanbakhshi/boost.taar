//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_HANDLER_REST_HPP
#define BOOST_TAAR_HANDLER_REST_HPP

#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/path_arg_check.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/rest_param.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/core/member_function_of.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/type_traits/callable.hpp>
#include <boost/taar/type_traits/specialization_of.hpp>
#include <boost/taar/type_traits/super_type.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/message.hpp>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace boost::taar::handler {
namespace detail {

template <typename... T>
concept compatible_arg_providers = requires
{
    requires type_traits::have_super_type_v<detail::arg_provider_request_t<T>...>;
};

template <typename... T>
struct common_requests_type_impl
{
    static_assert(compatible_arg_providers<T...>, "Incompatible arg providers.");

    using type = type_traits::super_type_t<detail::arg_provider_request_t<T>...>;
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

/// True if @a T is a `path_arg`, possibly wrapped in `with_default` and/or `constrained`.
template <typename T>
concept is_path_arg_provider = is_path_arg<T>;

template <typename ProvidersTupleType>
auto collect_path_arg_names(ProvidersTupleType const& providers)
    -> std::vector<std::string>
{
    std::vector<std::string> result;
    std::apply([&]<typename... AP>(AP const&... ap)
    {
        ([&]<typename T>(T const& provider)
        {
            if constexpr (is_path_arg_provider<T>)
            {
                result.push_back(provider.name());
            }
        }(ap), ...);
    }, providers);
    return result;
}


// The awaitable a handler produces, computed from the callable's own
// parameter types so that no declaration depends on the contexts tuple
// (context lookup failures must stay inside the call operator's body,
// where they are real errors, not inside overload resolution).
template <typename Callable, typename... Bound, std::size_t... Js>
auto rest_result_type_helper(std::index_sequence<Js...>)
    -> decltype(response_from_invoke(
        std::declval<Bound&>()...,
        std::declval<type_traits::callable_arg<Callable, Js>>()...));

template <
    typename CallableType,
    typename ProvidersTupleType,
    typename RequestType,
    typename CtxSeq,
    typename ArgSeq>
struct rest_handler;

template <
    typename CallableType,
    typename ProvidersTupleType,
    typename RequestType,
    std::size_t... CtxIs,
    std::size_t... ArgIs>
struct rest_handler<
    CallableType,
    ProvidersTupleType,
    RequestType,
    std::index_sequence<CtxIs...>,
    std::index_sequence<ArgIs...>>
{
    static constexpr bool context_aware = true;
    static constexpr std::size_t context_count = sizeof...(CtxIs);

    using request_type = RequestType;
    using callable_storage = std::decay_t<CallableType>;
    /// What the wrapped callable returns, before conversion to an HTTP response.
    using callable_result_type = type_traits::callable_result<CallableType>;
    using result_type = decltype(rest_result_type_helper<CallableType, callable_storage>(
        std::make_index_sequence<context_count + sizeof...(ArgIs)>{}));

    callable_storage callable_;
    ProvidersTupleType arg_providers_tuple_;

    template <typename Contexts>
    result_type operator()(
        Contexts const& ctxs,
        request_type const& request,
        matcher::context const& context)
    {
        co_return co_await response_from_invoke(
            callable_,
            context_arg<type_traits::callable_arg<CallableType, CtxIs>>(ctxs)...,
            get_rest_arg<
                type_traits::callable_arg<CallableType, context_count + ArgIs>,
                std::tuple_element_t<ArgIs, ProvidersTupleType>
            >(std::get<ArgIs>(arg_providers_tuple_), ArgIs, request, context)...);
    }

    /// Invoke with no contexts in scope. Convenient for unit tests and for
    /// handlers whose callable takes no context parameters.
    result_type operator()(request_type const& request, matcher::context const& context)
        requires (context_count == 0)
    {
        return (*this)(contexts<>{}, request, context);
    }

    /// Names of every `path_arg` (and `with_default<path_arg, ...>`) provider
    /// declared on this handler, in declaration order.
    auto path_arg_names() const -> std::vector<std::string>
    {
        return collect_path_arg_names(arg_providers_tuple_);
    }

    /// Description of every provider-fed parameter, for documentation generators.
    auto params() const -> std::vector<rest_param_info>
    {
        return {describe_rest_param<type_traits::callable_arg<CallableType, context_count + ArgIs>>(
            std::get<ArgIs>(arg_providers_tuple_))...};
    }
};

template <
    typename MemFnType,
    typename ObjectType,
    typename ProvidersTupleType,
    typename RequestType,
    typename CtxSeq,
    typename ArgSeq>
struct rest_memfn_handler;

template <
    typename MemFnType,
    typename ObjectType,
    typename ProvidersTupleType,
    typename RequestType,
    std::size_t... CtxIs,
    std::size_t... ArgIs>
struct rest_memfn_handler<
    MemFnType,
    ObjectType,
    ProvidersTupleType,
    RequestType,
    std::index_sequence<CtxIs...>,
    std::index_sequence<ArgIs...>>
{
    static constexpr bool context_aware = true;
    static constexpr std::size_t context_count = sizeof...(CtxIs);

    using request_type = RequestType;
    using memfn_type = std::remove_reference_t<MemFnType>;
    using callable_result_type = type_traits::callable_result<memfn_type>;
    using result_type = decltype(rest_result_type_helper<memfn_type, MemFnType, ObjectType>(
        std::make_index_sequence<context_count + sizeof...(ArgIs)>{}));

    MemFnType memfn_;
    ObjectType object_;
    ProvidersTupleType arg_providers_tuple_;

    template <typename Contexts>
    result_type operator()(
        Contexts const& ctxs,
        request_type const& request,
        matcher::context const& context)
    {
        co_return co_await response_from_invoke(
            memfn_,
            object_,
            context_arg<type_traits::callable_arg<memfn_type, CtxIs>>(ctxs)...,
            get_rest_arg<
                type_traits::callable_arg<memfn_type, context_count + ArgIs>,
                std::tuple_element_t<ArgIs, ProvidersTupleType>
            >(std::get<ArgIs>(arg_providers_tuple_), ArgIs, request, context)...);
    }

    result_type operator()(request_type const& request, matcher::context const& context)
        requires (context_count == 0)
    {
        return (*this)(contexts<>{}, request, context);
    }

    auto path_arg_names() const -> std::vector<std::string>
    {
        return collect_path_arg_names(arg_providers_tuple_);
    }

    auto params() const -> std::vector<rest_param_info>
    {
        return {describe_rest_param<type_traits::callable_arg<memfn_type, context_count + ArgIs>>(
            std::get<ArgIs>(arg_providers_tuple_))...};
    }
};

template <typename CallableType, typename... ArgProvidersType>
inline auto rest_for_callable(
    CallableType&& callable,
    ArgProvidersType... arg_providers)
{
    using noref_fn_type = std::remove_cvref_t<CallableType>;
    using request_type = detail::common_requests_type_t<ArgProvidersType...>;
    using arg_providers_tuple_t = std::tuple<ArgProvidersType...>;

    constexpr std::size_t arg_count = type_traits::callable_args_count<noref_fn_type>;
    constexpr std::size_t provider_count = sizeof...(ArgProvidersType);

    static_assert(
        arg_count >= provider_count,
        "More REST argument providers than callable parameters.");

    constexpr std::size_t context_count = arg_count - provider_count;
    using ctx_seq = std::make_index_sequence<context_count>;
    using arg_seq = std::make_index_sequence<provider_count>;

    static_assert(
        leading_params_are_contexts<noref_fn_type>(ctx_seq{}),
        "The number of REST argument providers must equal the number of "
        "callable parameters, except for leading context parameters "
        "(T&, T const&, or std::shared_ptr<T>) which are injected by the "
        "session.");

    return rest_handler<
        noref_fn_type,
        arg_providers_tuple_t,
        request_type,
        ctx_seq,
        arg_seq
    >{
        std::forward<CallableType>(callable),
        arg_providers_tuple_t {std::move(arg_providers)...}
    };
}

template <typename MemFnType, typename ObjectType, typename... ArgProvidersType>
inline auto rest_for_memfn(
    MemFnType memfn,
    ObjectType&& object,
    ArgProvidersType... arg_providers)
{
    using noref_fn_type = std::remove_cvref_t<MemFnType>;
    using request_type = detail::common_requests_type_t<ArgProvidersType...>;
    using arg_providers_tuple_t = std::tuple<ArgProvidersType...>;
    using object_storage = std::remove_cvref_t<ObjectType>;

    constexpr std::size_t arg_count = type_traits::callable_args_count<noref_fn_type>;
    constexpr std::size_t provider_count = sizeof...(ArgProvidersType);

    static_assert(
        arg_count >= provider_count,
        "More REST argument providers than member function parameters.");

    constexpr std::size_t context_count = arg_count - provider_count;
    using ctx_seq = std::make_index_sequence<context_count>;
    using arg_seq = std::make_index_sequence<provider_count>;

    static_assert(
        leading_params_are_contexts<noref_fn_type>(ctx_seq{}),
        "The number of REST argument providers must equal the number of "
        "member function parameters, except for leading context parameters "
        "(T&, T const&, or std::shared_ptr<T>) which are injected by the "
        "session.");

    return rest_memfn_handler<
        MemFnType,
        object_storage,
        arg_providers_tuple_t,
        request_type,
        ctx_seq,
        arg_seq
    >{
        memfn,
        std::forward<ObjectType>(object),
        arg_providers_tuple_t {std::move(arg_providers)...}
    };
}

} // namespace detail

/** Adapt an arbitrary callable into an HTTP request handler.

    `rest` wraps @a callable into a handler suitable for
    @ref boost::taar::session::basic_http::register_request_handler. Each
    @a arg_provider is responsible for producing one argument of @a callable
    from the incoming request:

    @li @ref path_arg pulls a captured path-template placeholder.
    @li @ref query_arg pulls a URL query parameter.
    @li @ref query_list_arg pulls every occurrence of a query parameter into a container.
    @li @ref header_arg pulls a header value.
    @li @ref cookie_arg pulls a cookie value.
    @li @ref string_body_arg, @ref json_body_arg, and
        @ref url_encoded_form_data_arg read and decode the request body.

    Leading parameters of @a callable whose type is `T&`, `T const&`, or
    `std::shared_ptr<T>` for a context type `T` in scope are injected by the
    session instead of coming from a provider: the per-connection context of
    a `basic_http<T>`, or a keyed context added by `with_session`. The
    number of providers must therefore equal the number of remaining
    parameters. Asking for a context type that is not in scope fails to
    compile at the registration site.

    The return value of @a callable is converted to an HTTP response with
    `boost::taar::response_from`; user-defined return types work as long as
    they have a `tag_invoke` overload for @ref boost::taar::response_from_tag.

    @code
    struct conn_stats { int requests = 0; };

    taar::session::basic_http<conn_stats> http_session;

    http_session.register_request_handler(
        method == http::verb::get && target == "/users/{id}",
        taar::handler::rest(
            [](conn_stats& stats, int id) { ++stats.requests; return load_user(id); },
            taar::handler::path_arg("id")));
    @endcode
*/
template <typename CallableType, typename... ArgProvidersType>
requires (!std::is_member_function_pointer_v<std::remove_cvref_t<CallableType>>)
inline decltype(auto) rest(
    CallableType&& callable,
    ArgProvidersType... arg_providers)
{
    return detail::rest_for_callable(
        std::forward<CallableType>(callable),
        std::move(arg_providers)...);
}

/// Adapt a member-function pointer bound to @a object into an HTTP request handler.
template <typename MemFnType, typename ObjectType, typename... ArgProvidersType>
requires (member_function_of<MemFnType, std::remove_cvref_t<ObjectType>>)
inline decltype(auto) rest(
    MemFnType memfn,
    ObjectType&& object,
    ArgProvidersType... arg_providers)
{
    return detail::rest_for_memfn(
        memfn,
        std::forward<ObjectType>(object),
        std::move(arg_providers)...);
}

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_REST_HPP
