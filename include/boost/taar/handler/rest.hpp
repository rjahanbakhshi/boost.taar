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

#include <boost/taar/handler/rest_arg.hpp>
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
#include <memory>
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

template <typename T> struct is_shared_ptr_ : std::false_type {};
template <typename T> struct is_shared_ptr_<std::shared_ptr<T>> : std::true_type {};

// std::tuple_element<0, tuple<>> is a hard error, not SFINAE, so we
// can't query arg<0> directly inside a concept for a zero-arg
// callable. Gate the arg<0> access behind a partial specialisation
// that's only instantiated when the callable actually has at least
// one argument.
template <typename CallableType, std::size_t N = type_traits::callable_args_count<CallableType>>
struct takes_leading_shared_ptr_impl_
{
    static constexpr bool value = false;
};

template <typename CallableType, std::size_t N>
    requires (N >= 1)
struct takes_leading_shared_ptr_impl_<CallableType, N>
{
    static constexpr bool value = is_shared_ptr_<
        std::remove_cvref_t<type_traits::callable_arg<CallableType, 0>>>::value;
};

/// True if @a CallableType's first parameter is a `std::shared_ptr<...>`.
///
/// `rest_for_callable` consults this to decide whether to build a
/// context-aware handler. The element type of that shared_ptr is the
/// per-connection context the handler expects.
template <typename CallableType>
inline constexpr bool takes_leading_shared_ptr =
    takes_leading_shared_ptr_impl_<CallableType>::value;

template <typename CallableType>
struct leading_context_type
{
    using type = typename std::remove_cvref_t<
        type_traits::callable_arg<CallableType, 0>>::element_type;
};

template <typename CallableType>
using leading_context_t = typename leading_context_type<CallableType>::type;

/// True if @a T is a `path_arg` or a `with_default<path_arg, …>`.
/// Phrased as a concept so the conjunction short-circuits at constraint
/// evaluation: types that are not `with_default` specialisations never
/// hit `typename T::arg_provider_type`.
template <typename T>
concept is_path_arg_provider =
    std::is_same_v<std::remove_cvref_t<T>, path_arg> ||
    (type_traits::specialization_of<with_default, std::remove_cvref_t<T>> &&
     std::is_same_v<
        typename std::remove_cvref_t<T>::arg_provider_type,
        path_arg>);

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

template <
    typename CallableType,
    typename ProvidersTupleType,
    typename RequestType,
    typename IndexSeq>
struct rest_handler;

template <
    typename CallableType,
    typename ProvidersTupleType,
    typename RequestType,
    std::size_t... Indexes>
struct rest_handler<CallableType, ProvidersTupleType, RequestType, std::index_sequence<Indexes...>>
{
    // Storage decays function types to function pointers. Introspection
    // (`callable_arg`) uses the original type so a free function and its
    // pointer report the same arguments.
    using callable_storage = std::decay_t<CallableType>;

    callable_storage callable_;
    ProvidersTupleType arg_providers_tuple_;

    auto operator()(RequestType const& request, matcher::context const& context)
        -> decltype(response_from_invoke(
            std::declval<callable_storage&>(),
            get_rest_arg<
                type_traits::callable_arg<CallableType, Indexes>,
                std::tuple_element_t<Indexes, ProvidersTupleType>
            >(
                std::declval<std::tuple_element_t<Indexes, ProvidersTupleType>&>(),
                Indexes,
                request,
                context)...))
    {
        co_return co_await response_from_invoke(
            callable_,
            get_rest_arg<
                type_traits::callable_arg<CallableType, Indexes>,
                std::tuple_element_t<Indexes, ProvidersTupleType>
            >(std::get<Indexes>(arg_providers_tuple_), Indexes, request, context)...
        );
    }

    /// Names of every `path_arg` (and `with_default<path_arg, …>`) provider
    /// declared on this handler, in declaration order. Used by the
    /// registration-time cross-check against the matcher's target template.
    auto path_arg_names() const -> std::vector<std::string>
    {
        return collect_path_arg_names(arg_providers_tuple_);
    }
};

/** rest_handler variant that injects a per-connection context.

    Built when the user callable's first parameter is
    `std::shared_ptr<Context>`. The struct's call operator accepts that
    pointer ahead of the parsed request and forwards it as the first
    argument to the inner callable; the remaining arguments are still
    drawn from the providers tuple at index 1..N.

    The session's @ref boost::taar::session::basic_http calls the
    three-argument `operator()` only when its own `Context` template
    parameter matches @a Context — the same `takes_connection_context`
    concept it uses to detect plain raw-callable handlers picks up this
    overload.
 */
template <
    typename Context,
    typename CallableType,
    typename ProvidersTupleType,
    typename RequestType,
    typename IndexSeq>
struct rest_handler_with_context;

template <
    typename Context,
    typename CallableType,
    typename ProvidersTupleType,
    typename RequestType,
    std::size_t... Indexes>
struct rest_handler_with_context<Context, CallableType, ProvidersTupleType, RequestType, std::index_sequence<Indexes...>>
{
    using callable_storage = std::decay_t<CallableType>;

    callable_storage callable_;
    ProvidersTupleType arg_providers_tuple_;

    // The first argument of the wrapped callable is the connection
    // context, so providers correspond to callable arguments at index
    // Indexes + 1.
    auto operator()(
        std::shared_ptr<Context> const& conn_ctx,
        RequestType const& request,
        matcher::context const& context)
        -> decltype(response_from_invoke(
            std::declval<callable_storage&>(),
            std::declval<std::shared_ptr<Context> const&>(),
            get_rest_arg<
                type_traits::callable_arg<CallableType, Indexes + 1>,
                std::tuple_element_t<Indexes, ProvidersTupleType>
            >(
                std::declval<std::tuple_element_t<Indexes, ProvidersTupleType>&>(),
                Indexes,
                request,
                context)...))
    {
        co_return co_await response_from_invoke(
            callable_,
            conn_ctx,
            get_rest_arg<
                type_traits::callable_arg<CallableType, Indexes + 1>,
                std::tuple_element_t<Indexes, ProvidersTupleType>
            >(std::get<Indexes>(arg_providers_tuple_), Indexes, request, context)...
        );
    }

    auto path_arg_names() const -> std::vector<std::string>
    {
        return collect_path_arg_names(arg_providers_tuple_);
    }
};

template <
    typename MemFnType,
    typename ObjectType,
    typename ProvidersTupleType,
    typename RequestType,
    typename IndexSeq>
struct rest_memfn_handler;

template <
    typename MemFnType,
    typename ObjectType,
    typename ProvidersTupleType,
    typename RequestType,
    std::size_t... Indexes>
struct rest_memfn_handler<MemFnType, ObjectType, ProvidersTupleType, RequestType, std::index_sequence<Indexes...>>
{
    MemFnType memfn_;
    ObjectType object_;
    ProvidersTupleType arg_providers_tuple_;

    auto operator()(RequestType const& request, matcher::context const& context)
        -> decltype(response_from_invoke(
            std::declval<MemFnType&>(),
            std::declval<ObjectType&>(),
            get_rest_arg<
                type_traits::callable_arg<std::remove_reference_t<MemFnType>, Indexes>,
                std::tuple_element_t<Indexes, ProvidersTupleType>
            >(
                std::declval<std::tuple_element_t<Indexes, ProvidersTupleType>&>(),
                Indexes,
                request,
                context)...))
    {
        co_return co_await response_from_invoke(
            memfn_,
            object_,
            get_rest_arg<
                type_traits::callable_arg<std::remove_reference_t<MemFnType>, Indexes>,
                std::tuple_element_t<Indexes, ProvidersTupleType>
            >(std::get<Indexes>(arg_providers_tuple_), Indexes, request, context)...
        );
    }

    auto path_arg_names() const -> std::vector<std::string>
    {
        return collect_path_arg_names(arg_providers_tuple_);
    }
};

template <
    typename Context,
    typename MemFnType,
    typename ObjectType,
    typename ProvidersTupleType,
    typename RequestType,
    typename IndexSeq>
struct rest_memfn_handler_with_context;

template <
    typename Context,
    typename MemFnType,
    typename ObjectType,
    typename ProvidersTupleType,
    typename RequestType,
    std::size_t... Indexes>
struct rest_memfn_handler_with_context<Context, MemFnType, ObjectType, ProvidersTupleType, RequestType, std::index_sequence<Indexes...>>
{
    MemFnType memfn_;
    ObjectType object_;
    ProvidersTupleType arg_providers_tuple_;

    auto operator()(
        std::shared_ptr<Context> const& conn_ctx,
        RequestType const& request,
        matcher::context const& context)
        -> decltype(response_from_invoke(
            std::declval<MemFnType&>(),
            std::declval<ObjectType&>(),
            std::declval<std::shared_ptr<Context> const&>(),
            get_rest_arg<
                type_traits::callable_arg<std::remove_reference_t<MemFnType>, Indexes + 1>,
                std::tuple_element_t<Indexes, ProvidersTupleType>
            >(
                std::declval<std::tuple_element_t<Indexes, ProvidersTupleType>&>(),
                Indexes,
                request,
                context)...))
    {
        co_return co_await response_from_invoke(
            memfn_,
            object_,
            conn_ctx,
            get_rest_arg<
                type_traits::callable_arg<std::remove_reference_t<MemFnType>, Indexes + 1>,
                std::tuple_element_t<Indexes, ProvidersTupleType>
            >(std::get<Indexes>(arg_providers_tuple_), Indexes, request, context)...
        );
    }

    auto path_arg_names() const -> std::vector<std::string>
    {
        return collect_path_arg_names(arg_providers_tuple_);
    }
};

template <
    typename CallableType,
    std::size_t... Indexes,
    typename... ArgProvidersType>
inline auto rest_for_callable(
    CallableType&& callable,
    std::index_sequence<Indexes...>,
    ArgProvidersType... arg_providers)
{
    using noref_fn_type = std::remove_cvref_t<CallableType>;
    using request_type = detail::common_requests_type_t<ArgProvidersType...>;
    using arg_providers_tuple_t = std::tuple<ArgProvidersType...>;

    if constexpr (takes_leading_shared_ptr<noref_fn_type>)
    {
        static_assert(
            type_traits::callable_args_count<noref_fn_type> == sizeof...(ArgProvidersType) + 1,
            "The user callable takes a leading std::shared_ptr<Context>; "
            "the number of REST arg providers must equal the number of "
            "remaining callable arguments.");

        using context_type = leading_context_t<noref_fn_type>;

        return rest_handler_with_context<
            context_type,
            noref_fn_type,
            arg_providers_tuple_t,
            request_type,
            std::index_sequence<Indexes...>
        >{
            std::forward<CallableType>(callable),
            arg_providers_tuple_t {std::move(arg_providers)...}
        };
    }
    else
    {
        static_assert(
            type_traits::callable_args_count<noref_fn_type> == sizeof...(ArgProvidersType),
            "The number of REST arguments providers must be equal to number of "
            "callable arguments.");

        return rest_handler<
            noref_fn_type,
            arg_providers_tuple_t,
            request_type,
            std::index_sequence<Indexes...>
        >{
            std::forward<CallableType>(callable),
            arg_providers_tuple_t {std::move(arg_providers)...}
        };
    }
}

template <
    typename MemFnType,
    typename ObjectType,
    std::size_t... Indexes,
    typename... ArgProvidersType>
inline auto rest_for_memfn(
    MemFnType memfn,
    ObjectType&& object,
    std::index_sequence<Indexes...>,
    ArgProvidersType... arg_providers)
{
    using noref_fn_type = std::remove_cvref_t<MemFnType>;
    using request_type = detail::common_requests_type_t<ArgProvidersType...>;
    using arg_providers_tuple_t = std::tuple<ArgProvidersType...>;
    using object_storage = std::remove_cvref_t<ObjectType>;

    if constexpr (takes_leading_shared_ptr<noref_fn_type>)
    {
        static_assert(
            type_traits::callable_args_count<noref_fn_type> == sizeof...(ArgProvidersType) + 1,
            "The member function takes a leading std::shared_ptr<Context>; "
            "the number of REST arg providers must equal the number of "
            "remaining callable arguments.");

        using context_type = leading_context_t<noref_fn_type>;

        return rest_memfn_handler_with_context<
            context_type,
            MemFnType,
            object_storage,
            arg_providers_tuple_t,
            request_type,
            std::index_sequence<Indexes...>
        >{
            memfn,
            std::forward<ObjectType>(object),
            arg_providers_tuple_t {std::move(arg_providers)...}
        };
    }
    else
    {
        static_assert(
            type_traits::callable_args_count<noref_fn_type> == sizeof...(ArgProvidersType),
            "The number of REST arguments providers must be equal to number of "
            "callable arguments.");

        return rest_memfn_handler<
            MemFnType,
            object_storage,
            arg_providers_tuple_t,
            request_type,
            std::index_sequence<Indexes...>
        >{
            memfn,
            std::forward<ObjectType>(object),
            arg_providers_tuple_t {std::move(arg_providers)...}
        };
    }
}

} // namespace detail

/** Adapt an arbitrary callable into an HTTP request handler.

    `rest` wraps @a callable into a handler suitable for
    @ref boost::taar::session::http::register_request_handler. Each
    @a arg_provider is responsible for producing one argument of @a callable
    from the incoming request:

    @li @ref path_arg pulls a captured path-template placeholder.
    @li @ref query_arg pulls a URL query parameter.
    @li @ref header_arg pulls a header value.
    @li @ref cookie_arg pulls a cookie value.
    @li @ref string_body_arg, @ref json_body_arg, and
        @ref url_encoded_form_data_arg read and decode the request body.

    The number of providers must equal the number of arguments of
    @a callable (enforced with `static_assert`). Each provider's request
    type is unified with the others into the request type passed to the
    handler; the @ref boost::taar::type_traits::super_type machinery picks
    the most specific request type all providers agree on.

    The return value of @a callable is converted to an HTTP response with
    `boost::taar::response_from`, which means user-defined return types
    work as long as they have a `tag_invoke` overload for
    @ref boost::taar::response_from_tag.

    The returned handler exposes a `path_arg_names()` accessor that lists
    every `path_arg` (and `with_default<path_arg, …>`) provider in
    declaration order. The session uses this list at registration time to
    cross-check against the matcher's target template, so typos like
    `path_arg("ud")` against `target == "/users/{id}"` fail at server
    startup rather than at the first request.

    @code
    using boost::taar::matcher::method;
    using boost::taar::matcher::target;
    namespace http  = boost::beast::http;
    namespace taar  = boost::taar;

    session.register_request_handler(
        method == http::verb::get && target == "/users/{id}",
        taar::handler::rest(
            [](int id) { return load_user(id); },
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
        std::index_sequence_for<ArgProvidersType...>{},
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
        std::index_sequence_for<ArgProvidersType...>{},
        std::move(arg_providers)...);
}

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_REST_HPP
