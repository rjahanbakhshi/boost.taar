//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_JSONRPC_BIND_HPP
#define BOOST_TAAR_JSONRPC_BIND_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/json_schema.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/rest_param.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/type_traits/callable.hpp>
#include <boost/taar/type_traits/specialization_of.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Binding machinery shared by jsonrpc::endpoint (methods) and mcp::endpoint
// (tools, resources): adapt a callable + argument providers into a
// type-erased invoker that injects contexts by type, pulls the remaining
// arguments from the providers, and converts the result to a JSON value.

namespace boost::taar::jsonrpc::detail {

/// A provider that reads the decoded call itself (`rpc_arg`, `mcp_arg`, `uri_arg`).
template <typename T, typename CallType>
concept call_provider_for = requires(T const& p)
{
    { p.name() } -> std::convertible_to<std::string>;
    requires std::is_same_v<
        std::remove_cvref_t<handler::detail::arg_provider_request_t<T>>, CallType>;
};

/** A provider that reads the carrying HTTP request (`header_arg`,
    `cookie_arg`, `query_arg`, ...). Every call type exposes the request
    as `request`, so the REST providers work unchanged on RPC methods and
    MCP tools: `rpc.method("whoami", &whoami, handler::header_arg("Authorization"))`.
*/
template <typename T, typename CallType>
concept request_provider_for = requires(T const& p)
{
    { p.name() } -> std::convertible_to<std::string>;
    requires std::is_convertible_v<
        decltype(std::declval<CallType const&>().request),
        handler::detail::arg_provider_request_t<T> const&>;
};

/// A provider usable for @a CallType: reads either the call or its HTTP request.
template <typename T, typename CallType>
concept provider_for = call_provider_for<T, CallType> || request_provider_for<T, CallType>;

/// What to hand a provider: the call, or the HTTP request it arrived on.
template <typename Provider, typename CallType>
decltype(auto) provider_input(CallType const& c)
{
    if constexpr (call_provider_for<Provider, CallType>)
    {
        return (c);
    }
    else
    {
        return (c.request);
    }
}

/// A call type that supports positional parameters exposes `position`.
template <typename CallType>
concept positional_call = requires(CallType c) { c.position = std::size_t {}; };

/// Position of provider @a I among the call-reading providers of the tuple.
template <typename CallType, typename ProvidersTuple, std::size_t I>
constexpr std::size_t call_position()
{
    return []<std::size_t... Js>(std::index_sequence<Js...>)
    {
        return ((Js < I && call_provider_for<std::tuple_element_t<Js, ProvidersTuple>, CallType> ? 1 : 0) + ... + 0);
    }(std::make_index_sequence<std::tuple_size_v<ProvidersTuple>> {});
}

template <typename P>
std::string provider_description(P const& p)
{
    if constexpr (requires { p.description(); })
    {
        return p.description();
    }
    else if constexpr (type_traits::specialization_of<handler::with_default, P>)
    {
        return provider_description(p.arg_provider_);
    }
    else
    {
        return {};
    }
}

template <typename Callable, std::size_t Offset, typename ProvidersTuple, std::size_t... Is>
std::vector<param_info> describe_params(ProvidersTuple const& providers, std::index_sequence<Is...>)
{
    std::vector<param_info> out;
    auto describe_one = [&]<std::size_t I>()
    {
        using provider_type = std::remove_cvref_t<std::tuple_element_t<I, ProvidersTuple>>;
        auto info = make_param_info<type_traits::callable_arg<Callable, Offset + I>>(
            std::get<I>(providers).name(),
            provider_description(std::get<I>(providers)),
            !is_optional_v<type_traits::callable_arg<Callable, Offset + I>> &&
                !handler::detail::strip_default<provider_type>::defaulted);
        handler::detail::annotate_param(info, std::get<I>(providers));
        out.push_back(std::move(info));
    };
    (describe_one.template operator()<Is>(), ...);
    return out;
}

template <typename T>
boost::json::value to_json_result(T&& value)
{
    if constexpr (std::is_same_v<std::remove_cvref_t<T>, boost::json::value>)
    {
        return std::forward<T>(value);
    }
    else
    {
        return boost::json::value_from(std::forward<T>(value));
    }
}

template <typename Callable, std::size_t... CtxIs>
constexpr bool leading_params_are_contexts(std::index_sequence<CtxIs...>)
{
    return (!std::is_void_v<handler::detail::requested_context_t<
        type_traits::callable_arg<Callable, CtxIs>>> && ...);
}

template <typename Callable, typename ProvidersTuple, typename CallType, typename CtxSeq, typename ArgSeq>
struct method_handler;

template <typename Callable, typename ProvidersTuple, typename CallType, std::size_t... CtxIs, std::size_t... ArgIs>
struct method_handler<Callable, ProvidersTuple, CallType, std::index_sequence<CtxIs...>, std::index_sequence<ArgIs...>>
{
    static constexpr std::size_t context_count = sizeof...(CtxIs);
    using callable_storage = std::decay_t<Callable>;

    callable_storage callable_;
    ProvidersTuple providers_;

    /// The call as seen by provider @a I: positioned among the call providers.
    template <std::size_t I>
    static CallType call_for(CallType const& c)
    {
        if constexpr (positional_call<CallType>)
        {
            CallType positioned = c;
            positioned.position = call_position<CallType, ProvidersTuple, I>();
            return positioned;
        }
        else
        {
            return c;
        }
    }

    template <typename Contexts>
    awaitable<boost::json::value> operator()(
        Contexts const& ctxs,
        CallType const& c,
        matcher::context const& mctx)
    {
        std::tuple<decltype(call_for<ArgIs>(c))...> calls {call_for<ArgIs>(c)...};
        auto invoke = [&]() -> decltype(auto)
        {
            return std::invoke(
                callable_,
                handler::detail::context_arg<type_traits::callable_arg<Callable, CtxIs>>(ctxs)...,
                handler::get_rest_arg<
                    type_traits::callable_arg<Callable, context_count + ArgIs>,
                    std::tuple_element_t<ArgIs, ProvidersTuple>
                >(std::get<ArgIs>(providers_), ArgIs,
                  provider_input<std::tuple_element_t<ArgIs, ProvidersTuple>>(std::get<ArgIs>(calls)), mctx)...);
        };
        using result_type = decltype(invoke());

        if constexpr (std::is_void_v<result_type>)
        {
            invoke();
            co_return boost::json::value {};
        }
        else if constexpr (is_awaitable<result_type>)
        {
            if constexpr (std::is_void_v<typename result_type::value_type>)
            {
                co_await invoke();
                co_return boost::json::value {};
            }
            else
            {
                co_return to_json_result(co_await invoke());
            }
        }
        else
        {
            co_return to_json_result(invoke());
        }
    }
};

/// A bound callable: its parameter descriptions plus a type-erased invoker.
template <typename CallType, typename Contexts>
struct bound_method
{
    std::vector<param_info> params;
    std::move_only_function<awaitable<boost::json::value>(
        Contexts const&, CallType const&, matcher::context const&)> invoke;
};

/** Bind @a callable with @a providers for calls of type @a CallType.

    Leading callable parameters that request a context type are injected
    from a @a Contexts tuple; the remaining parameters come from
    @a providers in order. All structural checks are `static_assert`s so
    that mistakes surface at the registration site.
*/
template <typename CallType, typename Contexts, typename Callable, typename... Providers>
bound_method<CallType, Contexts> bind_method(Callable&& callable, Providers... providers)
{
    using fn = std::remove_cvref_t<Callable>;

    static_assert((provider_for<Providers, CallType> && ...),
        "Argument providers for this kind of call must read either the call "
        "(rpc_arg for JSON-RPC methods, mcp_arg for MCP tools, uri_arg for MCP "
        "resources) or the carrying HTTP request (header_arg, cookie_arg, "
        "query_arg), optionally wrapped in with_default.");

    constexpr std::size_t arg_count = type_traits::callable_args_count<fn>;
    constexpr std::size_t provider_count = sizeof...(Providers);
    static_assert(arg_count >= provider_count,
        "More argument providers than callable parameters.");

    constexpr std::size_t context_count = arg_count - provider_count;
    using ctx_seq = std::make_index_sequence<context_count>;
    using arg_seq = std::make_index_sequence<provider_count>;
    static_assert(leading_params_are_contexts<fn>(ctx_seq {}),
        "The number of argument providers must equal the number of callable "
        "parameters, except for leading context parameters (T&, T const&, or "
        "std::shared_ptr<T>) which are injected by the endpoint.");

    using providers_tuple = std::tuple<Providers...>;
    providers_tuple tuple {std::move(providers)...};

    bound_method<CallType, Contexts> out;
    out.params = describe_params<fn, context_count>(tuple, arg_seq {});

    method_handler<fn, providers_tuple, CallType, ctx_seq, arg_seq> h {
        std::forward<Callable>(callable), std::move(tuple)};
    out.invoke =
        [h = std::move(h)](Contexts const& ctxs, CallType const& c, matcher::context const& mctx) mutable
            -> awaitable<boost::json::value>
        {
            return h(ctxs, c, mctx);
        };
    return out;
}

/// Pick the contexts @a Contexts expects out of an incoming tuple, by type.
template <typename Contexts, typename Incoming, std::size_t... Is>
Contexts select_contexts(Incoming const& in, std::index_sequence<Is...>)
{
    return Contexts {
        std::get<handler::detail::context_index_v<
            typename std::tuple_element_t<Is, Contexts>::element_type, Incoming>>(in)...};
}

template <typename Contexts, typename Incoming>
Contexts select_contexts(Incoming const& in)
{
    return select_contexts<Contexts>(in, std::make_index_sequence<std::tuple_size_v<Contexts>> {});
}

} // namespace boost::taar::jsonrpc::detail

#endif // BOOST_TAAR_JSONRPC_BIND_HPP
