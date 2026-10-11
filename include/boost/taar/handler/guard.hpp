//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_GUARD_HPP
#define BOOST_TAAR_HANDLER_GUARD_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/interceptor.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/is_chunked_response.hpp>
#include <boost/taar/core/is_http_response.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/path_arg_check.hpp>
#include <boost/taar/handler/rest_param.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/string_body.hpp>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace boost::taar::handler {
namespace detail {

/** A rejection as the response type the guarded handler produces.

    The status and headers are kept; the text body is kept when the body
    type accepts a `std::string`, and dropped otherwise.
*/
template <typename Response>
Response rejection_as(boost::beast::http::response<boost::beast::http::string_body>&& rejection)
{
    namespace http = boost::beast::http;
    if constexpr (std::is_same_v<Response, http::response<http::string_body>>)
    {
        return std::move(rejection);
    }
    else if constexpr (std::is_same_v<Response, http::message_generator>)
    {
        return http::message_generator {std::move(rejection)};
    }
    else
    {
        static_assert(is_http_response<Response>,
            "guard requires the inner handler to produce a boost::beast::http::response, "
            "a message_generator, a chunked_response or an async_generator.");
        Response response {std::move(rejection.base())};
        if constexpr (std::is_assignable_v<typename Response::body_type::value_type&, std::string&&>)
        {
            response.body() = std::move(rejection.body());
        }
        response.prepare_payload();
        return response;
    }
}

/** A one-shot stream carrying a rejection: its status, its headers, and
    its text body when the chunk type is `std::string`.
*/
template <typename T>
chunked_response<T> rejection_stream(boost::beast::http::response<boost::beast::http::string_body> rejection)
{
    namespace http = boost::beast::http;
    co_yield set_status(rejection.result());
    for (auto const& f : rejection.base())
    {
        // The stream is chunked and its connection handling is the session's.
        if (f.name() == http::field::content_length || f.name() == http::field::connection)
        {
            continue;
        }
        co_yield chunked_set_header {f.name(), std::string {f.name_string()}, std::string {f.value()}};
    }
    if constexpr (std::is_same_v<T, std::string>)
    {
        if (!rejection.body().empty())
        {
            co_yield std::move(rejection.body());
        }
    }
}

} // namespace detail

/** Handler decorator that runs a check before the inner handler.

    Produced by @ref guard. The check has the interceptor signature
    (leading context parameters injected by type, then
    `boost::beast::http::request_header<> const&` and
    `matcher::context&`) and returns @ref boost::taar::interceptor_result,
    directly or through an awaitable. On `std::nullopt` the inner handler
    runs with the same contexts, request and matcher context, so
    attributes the check stored are visible to it; on a response that
    response is the route's answer.

    The inner handler may produce a typed `boost::beast::http::response`,
    a `message_generator`, or a stream (`chunked_response<T>` or
    `async_generator<T>`, directly or through an awaitable). A rejection
    is converted to the inner's response type: for a typed response with
    a body that does not accept a `std::string`, only the status and
    headers are kept; for a stream it becomes a one-shot
    `chunked_response<T>` with the rejection's status and headers (and its
    text when `T` is `std::string`).

    The check runs when the route's handler is invoked, after the request
    body has been read. Use a session interceptor
    (`basic_http::add_interceptor(matcher, fn)`) when the decision must
    be taken before the body is read.

    `params()`, `path_arg_names()` and `inner_type` forward to the inner
    handler, so OpenAPI and the registration checks see through the
    guard. It composes with `with_session` in either order; inside
    `with_session` the check can request the keyed context.

    A check that throws propagates to the session's soft error handler,
    like a handler exception.
*/
template <typename Check, typename Inner>
struct guard_handler
{
    static constexpr bool context_aware = true;

    using request_type = detail::request_type_of<Inner>;

    Check check_;
    Inner inner_;

    template <typename Contexts>
    auto operator()(
        Contexts const& ctxs,
        request_type const& request,
        matcher::context const& mctx)
        -> detail::with_session_result_t<Inner, Contexts>
    {
        using produced_type = detail::produced_t<Inner, Contexts>;

        // The session owns a mutable context per request and lends it to
        // the handler as const; the check stores attributes into it, so
        // that the inner handler (and a stream it produces) sees them.
        auto& context = const_cast<matcher::context&>(mctx);
        auto rejection = co_await detail::invoke_interceptor(check_, ctxs, request, context);

        if constexpr (detail::is_stream_v<produced_type>)
        {
            using chunk_type = typename detail::stream_value<produced_type>::type;
            if (rejection)
            {
                co_return detail::rejection_stream<chunk_type>(std::move(*rejection));
            }
            std::optional<produced_type> stream;
            if constexpr (is_awaitable<detail::handler_result_t<Inner, Contexts>>)
            {
                stream.emplace(co_await detail::invoke_handler(inner_, ctxs, request, mctx));
            }
            else
            {
                stream.emplace(detail::invoke_handler(inner_, ctxs, request, mctx));
            }
            if constexpr (is_chunked_response<produced_type>)
            {
                co_return std::move(*stream);
            }
            else
            {
                stream->set_executor(co_await boost::asio::this_coro::executor);
                co_return detail::relay_stream<chunk_type>(std::move(*stream), {});
            }
        }
        else
        {
            using response_type = detail::handler_response_t<Inner, Contexts>;
            if (rejection)
            {
                co_return detail::rejection_as<response_type>(std::move(*rejection));
            }
            co_return co_await detail::response_from_invoke_handler(inner_, ctxs, request, mctx);
        }
    }

    auto path_arg_names() const -> std::vector<std::string>
        requires has_path_arg_names<Inner>
    {
        return inner_.path_arg_names();
    }

    /// The wrapped handler, for documentation generators that introspect it.
    using inner_type = Inner;

    auto params() const -> std::vector<rest_param_info>
        requires has_rest_params<Inner>
    {
        return inner_.params();
    }
};

/** Run a check before a route's handler.

    @code
    auto require_admin = [](http::request_header<> const& req, taar::matcher::context& ctx)
    {
        if (req[http::field::authorization] != "Bearer admin-token")
            return taar::reject(http::status::forbidden);
        ctx.attributes.set(principal {"admin"});
        return taar::proceed();
    };

    session.register_request_handler(
        method == http::verb::delete_ && target == "/tasks/{id}",
        taar::handler::guard(require_admin,
            taar::handler::rest(&delete_task, path_arg("id"), attribute_arg<principal>())));
    @endcode

    @param check A callable with the interceptor signature returning
                 @ref boost::taar::interceptor_result or an awaitable of
                 it. It must not be a generic lambda (its parameters are
                 inspected to inject contexts).
    @param inner The handler to run when the check lets the request
                 proceed.
*/
template <typename Check, typename Inner>
auto guard(Check check, Inner inner)
{
    return guard_handler<std::decay_t<Check>, std::decay_t<Inner>> {std::move(check), std::move(inner)};
}

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_GUARD_HPP
