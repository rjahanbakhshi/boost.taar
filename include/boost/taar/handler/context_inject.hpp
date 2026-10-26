//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_CONTEXT_INJECT_HPP
#define BOOST_TAAR_HANDLER_CONTEXT_INJECT_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/body_reader.hpp>
#include <boost/taar/core/interceptor.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/type_traits/callable.hpp>
#include <boost/beast/http/message.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost::taar::handler {

/** The set of contexts in scope for a handler invocation.

    Each element is a `std::shared_ptr` to one context object. The session
    supplies the per-connection context; wrappers such as `with_session`
    append keyed contexts; `jsonrpc::endpoint` and `mcp::endpoint` forward
    whatever they received. Handlers never see this tuple directly: the
    injection machinery below picks elements out of it by type.

    @tparam Context The context types in scope, in any order.
*/
template <typename... Context>
using contexts = std::tuple<std::shared_ptr<Context>...>;

namespace detail {

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

template <typename T> struct is_shared_ptr : std::false_type {};
template <typename T> struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

/** The context type a callable parameter asks for, or `void`.

    `C&`, `C const&`, `std::shared_ptr<C>`, and `std::shared_ptr<C> const&`
    all request `C`. Anything else (values, rvalue references, non-class
    references) requests nothing and is treated as a regular argument.
*/
template <typename Param> struct requested_context { using type = void; };

template <typename C> requires std::is_class_v<C>
struct requested_context<C&> { using type = std::remove_const_t<C>; };

template <typename C>
struct requested_context<std::shared_ptr<C>> { using type = std::remove_const_t<C>; };

template <typename C>
struct requested_context<std::shared_ptr<C>&> { using type = std::remove_const_t<C>; };

template <typename C>
struct requested_context<std::shared_ptr<C> const&> { using type = std::remove_const_t<C>; };

template <typename Param>
using requested_context_t = typename requested_context<Param>::type;

/// Index of `std::shared_ptr<C>` within a @ref contexts tuple, or `detail::npos`.
template <typename C, typename Contexts> struct context_index;

template <typename C>
struct context_index<C, std::tuple<>>
{
    static constexpr std::size_t value = npos;
};

template <typename C, typename... Rest>
struct context_index<C, std::tuple<std::shared_ptr<C>, Rest...>>
{
    static constexpr std::size_t value = 0;
};

template <typename C, typename Head, typename... Rest>
struct context_index<C, std::tuple<Head, Rest...>>
{
private:
    static constexpr std::size_t tail = context_index<C, std::tuple<Rest...>>::value;
public:
    static constexpr std::size_t value = tail == npos ? npos : 1 + tail;
};

template <typename C, typename Contexts>
inline constexpr std::size_t context_index_v = context_index<C, Contexts>::value;

/// True if @a Param requests a context type that is present in @a Contexts.
template <typename Param, typename Contexts>
inline constexpr bool in_scope =
    !std::is_void_v<requested_context_t<Param>> &&
    context_index_v<requested_context_t<Param>, Contexts> != npos;

/// True if @a T is a Beast HTTP request message (any body, any fields).
template <typename T> struct is_http_request : std::false_type {};
template <typename Body, typename Fields>
struct is_http_request<boost::beast::http::message<true, Body, Fields>> : std::true_type {};
template <> struct is_http_request<streaming_request> : std::true_type {};

template <typename T>
inline constexpr bool is_http_request_v = is_http_request<std::remove_cvref_t<T>>::value;

template <typename F, std::size_t J, std::size_t N>
struct request_param_index_impl
{
    static constexpr std::size_t value =
        is_http_request_v<type_traits::callable_arg<F, J>>
            ? J
            : request_param_index_impl<F, J + 1, N>::value;
};

template <typename F, std::size_t N>
struct request_param_index_impl<F, N, N>
{
    static constexpr std::size_t value = npos;
};

/** Position of the HTTP request parameter in a raw handler's signature.

    Raw handlers are `(contexts..., request, matcher::context)`. Everything
    before the request parameter is a context request and is injected by
    type; a leading parameter whose type is not in scope is a compile-time
    error at the point of injection.
*/
template <typename F>
inline constexpr std::size_t request_param_index =
    request_param_index_impl<F, 0, type_traits::callable_args_count<F>>::value;

/** Produce the argument for a context parameter of type @a Param.

    Returns the `shared_ptr` itself when the parameter is a shared_ptr, and a
    reference to the pointee otherwise.
*/
template <typename Param, typename Contexts>
decltype(auto) context_arg(Contexts const& ctxs)
{
    using C = requested_context_t<Param>;

    static_assert(
        !std::is_void_v<C>,
        "A leading handler parameter is being treated as a context but its "
        "type is not a context request (T&, T const&, or std::shared_ptr<T>). "
        "Check that the number of argument providers matches the number of "
        "non-context parameters of the callable.");

    static_assert(
        context_index_v<C, Contexts> != npos,
        "A handler parameter requests a context type that is not in scope "
        "for this handler. The session provides its connection context; "
        "with_session adds a keyed context; jsonrpc::endpoint and "
        "mcp::endpoint forward their template arguments. Instantiate the "
        "session or endpoint with this context type, or wrap the handler so "
        "that the type is in scope.");

    constexpr std::size_t idx = context_index_v<C, Contexts>;
    if constexpr (is_shared_ptr<std::remove_cvref_t<Param>>::value)
    {
        return std::get<idx>(ctxs);
    }
    else
    {
        return *std::get<idx>(ctxs);
    }
}

} // namespace detail

/** True if @a Handler follows the context-aware handler protocol.

    Such handlers expose `request_type`, a `context_aware` constant, and an
    `operator()(Contexts const&, request_type const&, matcher::context const&)`
    member template. `rest`, `with_session`, `jsonrpc::endpoint::handler()`
    and `mcp::endpoint::handler()` all produce them.
*/
template <typename Handler>
concept context_aware_handler = requires
{
    typename std::remove_cvref_t<Handler>::request_type;
    requires std::remove_cvref_t<Handler>::context_aware;
};

namespace detail {

template <typename H>
struct request_type_of_impl
{
    static_assert(
        request_param_index<H> != npos,
        "A raw request handler must take an HTTP request parameter "
        "(boost::beast::http::request<Body> const&) followed by "
        "boost::taar::matcher::context const&. Leading context parameters "
        "are allowed before the request.");
    using type = std::remove_cvref_t<type_traits::callable_arg<H, request_param_index<H>>>;
};

template <context_aware_handler H>
struct request_type_of_impl<H>
{
    using type = typename std::remove_cvref_t<H>::request_type;
};

/// The HTTP request type a handler (raw or context-aware) consumes.
template <typename H>
using request_type_of = typename request_type_of_impl<std::remove_cvref_t<H>>::type;

template <typename H, typename Contexts, std::size_t... Js, typename Request>
decltype(auto) invoke_raw_handler(
    H&& h,
    Contexts const& ctxs,
    std::index_sequence<Js...>,
    Request&& request,
    matcher::context const& mctx)
{
    return std::invoke(
        std::forward<H>(h),
        context_arg<type_traits::callable_arg<std::remove_cvref_t<H>, Js>>(ctxs)...,
        std::forward<Request>(request),
        mctx);
}

/** Invoke @a h with the contexts in @a ctxs injected.

    Returns whatever the handler returns: a value, an awaitable, or a
    generator. Context-aware handlers receive the tuple; raw handlers get
    their leading context parameters filled by type.
*/
template <typename H, typename Contexts, typename Request>
decltype(auto) invoke_handler(
    H&& h,
    Contexts const& ctxs,
    Request&& request,
    matcher::context const& mctx)
{
    using HT = std::remove_cvref_t<H>;
    if constexpr (context_aware_handler<HT>)
    {
        return std::invoke(std::forward<H>(h), ctxs, std::forward<Request>(request), mctx);
    }
    else
    {
        return invoke_raw_handler(
            std::forward<H>(h), ctxs,
            std::make_index_sequence<request_param_index<HT>>{},
            std::forward<Request>(request), mctx);
    }
}

template <typename H, typename Contexts, std::size_t... Js, typename Request>
decltype(auto) response_from_invoke_raw_handler(
    H&& h,
    Contexts const& ctxs,
    std::index_sequence<Js...>,
    Request&& request,
    matcher::context const& mctx)
{
    return response_from_invoke(
        std::forward<H>(h),
        context_arg<type_traits::callable_arg<std::remove_cvref_t<H>, Js>>(ctxs)...,
        std::forward<Request>(request),
        mctx);
}

/// `response_from_invoke` with the contexts in @a ctxs injected.
template <typename H, typename Contexts, typename Request>
decltype(auto) response_from_invoke_handler(
    H&& h,
    Contexts const& ctxs,
    Request&& request,
    matcher::context const& mctx)
{
    using HT = std::remove_cvref_t<H>;
    if constexpr (context_aware_handler<HT>)
    {
        return response_from_invoke(
            std::forward<H>(h), ctxs, std::forward<Request>(request), mctx);
    }
    else
    {
        return response_from_invoke_raw_handler(
            std::forward<H>(h), ctxs,
            std::make_index_sequence<request_param_index<HT>>{},
            std::forward<Request>(request), mctx);
    }
}

/// What `invoke_handler` returns for @a H given @a Contexts.
template <typename H, typename Contexts>
using handler_result_t = decltype(invoke_handler(
    std::declval<H&>(),
    std::declval<Contexts const&>(),
    std::declval<request_type_of<H> const&>(),
    std::declval<matcher::context const&>()));

/// The response type produced by awaiting `response_from_invoke_handler`.
template <typename H, typename Contexts>
using handler_response_t = typename std::remove_cvref_t<decltype(response_from_invoke_handler(
    std::declval<H&>(),
    std::declval<Contexts const&>(),
    std::declval<request_type_of<H> const&>(),
    std::declval<matcher::context const&>()))>::value_type;

// The first `ContextCount` parameters of the callable must each request a
// context (T&, T const&, shared_ptr<T>). Checked at rest() so the error
// points at the registration site, not at the first request.
template <typename CallableType, std::size_t... CtxIs>
constexpr bool leading_params_are_contexts(std::index_sequence<CtxIs...>)
{
    return (!std::is_void_v<requested_context_t<type_traits::callable_arg<CallableType, CtxIs>>> && ...);
}

/** Invoke an interceptor (or a `guard` check) with contexts injected.

    @a f takes leading context parameters, injected by type from @a ctxs,
    then `(boost::beast::http::request_header<> const&, matcher::context&)`,
    and returns `interceptor_result` directly or through an awaitable.
*/
template <typename F, typename Contexts>
awaitable<interceptor_result> invoke_interceptor(
    F& f,
    Contexts const& ctxs,
    boost::beast::http::request_header<> const& header,
    matcher::context& mctx)
{
    constexpr std::size_t arity = type_traits::callable_args_count<F>;
    static_assert(arity >= 2,
        "An interceptor takes (boost::beast::http::request_header<> const&, "
        "boost::taar::matcher::context&), optionally preceded by context "
        "parameters, and returns taar::interceptor_result or an awaitable of it.");

    auto call = [&]<std::size_t... Js>(std::index_sequence<Js...>) -> decltype(auto)
    {
        return std::invoke(
            f,
            context_arg<type_traits::callable_arg<F, Js>>(ctxs)...,
            header,
            mctx);
    };
    using result_type = decltype(call(std::make_index_sequence<arity - 2> {}));

    if constexpr (is_awaitable<result_type>)
    {
        static_assert(std::is_convertible_v<awaitable_result_t<result_type>, interceptor_result>,
            "An asynchronous interceptor must produce taar::interceptor_result.");
        co_return co_await call(std::make_index_sequence<arity - 2> {});
    }
    else
    {
        static_assert(std::is_convertible_v<result_type, interceptor_result>,
            "An interceptor must return taar::interceptor_result (taar::proceed() "
            "or taar::reject(...)) or an awaitable of it.");
        co_return call(std::make_index_sequence<arity - 2> {});
    }
}

} // namespace detail

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_CONTEXT_INJECT_HPP
