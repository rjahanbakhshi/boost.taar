//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_WITH_SESSION_HPP
#define BOOST_TAAR_HANDLER_WITH_SESSION_HPP

#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/is_async_generator.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/is_chunked_response.hpp>
#include <boost/taar/core/is_http_response.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/path_arg_check.hpp>
#include <boost/taar/handler/rest_param.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace boost::taar::handler {
namespace detail {

template <typename Contexts, typename Context>
using with_session_contexts_t = decltype(std::tuple_cat(
    std::declval<Contexts const&>(),
    std::declval<std::tuple<std::shared_ptr<Context>>>()));

template <typename Response, typename Request>
Response unauthorized_response(Request const& request)
{
    Response response {boost::beast::http::status::unauthorized, request.version()};
    response.keep_alive(request.keep_alive());
    response.prepare_payload();
    return response;
}

template <typename T>
chunked_response<T> unauthorized_stream()
{
    co_yield set_status(boost::beast::http::status::unauthorized);
}

/// What the inner handler produces once awaited (or directly, when synchronous).
template <typename R, bool = is_awaitable<R>>
struct produced { using type = R; };
template <typename R>
struct produced<R, true> { using type = typename R::value_type; };
template <typename Inner, typename Contexts>
using produced_t = typename produced<handler_result_t<Inner, Contexts>>::type;

template <typename P>
inline constexpr bool is_stream_v = is_chunked_response<P> || is_async_generator<P>;

template <typename G> struct stream_value;
template <typename T> struct stream_value<chunked_response<T>> { using type = T; };
template <typename T> struct stream_value<async_generator<T>> { using type = T; };

template <typename Inner, typename Contexts>
auto with_session_result()
{
    using P = produced_t<Inner, Contexts>;
    if constexpr (is_stream_v<P>)
    {
        return std::type_identity<awaitable<chunked_response<typename stream_value<P>::type>>> {};
    }
    else
    {
        return std::type_identity<awaitable<handler_response_t<Inner, Contexts>>> {};
    }
}

/// The handler's result: a typed response, or a `chunked_response` for streaming inners.
template <typename Inner, typename Contexts>
using with_session_result_t = typename decltype(with_session_result<Inner, Contexts>())::type;

/** Re-emit @a inner's stream with @a extra headers added to its metadata.

    The inner is driven to its first chunk so that its status and headers
    are known, which are then replayed on this stream ahead of the extra
    headers (an `async_generator` carries none).
*/
template <typename T, typename Gen>
chunked_response<T> relay_stream(Gen inner, std::vector<chunked_set_header> extra)
{
    auto [first_ec, first] = co_await inner.next();
    (void)first_ec;
    if constexpr (is_chunked_response<Gen>)
    {
        co_yield set_status(inner.status());
        for (auto const& h : inner.headers()) co_yield h;
    }
    for (auto& h : extra) co_yield std::move(h);
    if (!first.has_value())
    {
        inner.rethrow_if_exception();
        co_return;
    }
    co_yield std::move(*first);
    for (;;)
    {
        auto [ec, v] = co_await inner.next();
        (void)ec;
        if (!v.has_value())
        {
            inner.rethrow_if_exception();
            co_return;
        }
        co_yield std::move(*v);
    }
}

} // namespace detail

/** Handler decorator that binds a keyed context from a @ref context_store.

    Produced by @ref with_session. On each request it extracts the key
    with the configured extractor, looks the entry up in the store,
    applies the policy when the key is absent or unknown, acquires the
    entry's lock, and invokes the inner handler with the keyed context
    added to the contexts in scope. When a key was minted the extractor
    writes it into the response.

    The inner handler may produce a typed `boost::beast::http::response`
    (the usual case for `rest` and raw handlers) or a stream: a
    `chunked_response<T>` or `async_generator<T>`, directly or through an
    awaitable. A stream is re-emitted as `chunked_response<T>` with the
    minted key's header added. The entry's lock is held only while the
    inner handler runs, that is until it has produced the stream; the
    stream body then runs unlocked, so a long-lived stream does not block
    the client's other requests. A streaming handler that keeps using the
    keyed context must therefore synchronise that access itself (an
    `async_channel` in the context is the usual shape).
    `message_generator` results are not supported under `with_session`.

    If the inner handler throws, the exception propagates to the session's
    soft-error handler as usual. A key minted on that request is not
    communicated to the client; the entry is left for the sweeper.
*/
template <typename Store, typename KeyExtractor, typename Inner>
struct with_session_handler
{
    static constexpr bool context_aware = true;

    using request_type = detail::request_type_of<Inner>;
    using store_type = Store;
    using context_type = typename Store::context_type;

    Store* store_;
    KeyExtractor key_;
    session_policy policy_;
    Inner inner_;

    template <typename Contexts>
    auto operator()(
        Contexts const& ctxs,
        request_type const& request,
        matcher::context const& mctx)
        -> detail::with_session_result_t<Inner, detail::with_session_contexts_t<Contexts, context_type>>
    {
        using extended_contexts = detail::with_session_contexts_t<Contexts, context_type>;
        using produced_type = detail::produced_t<Inner, extended_contexts>;
        constexpr bool streaming = detail::is_stream_v<produced_type>;

        auto key = key_.extract(request);
        typename Store::entry_ptr entry;
        bool minted = false;

        if (key.has_value())
        {
            entry = store_->find(*key);
        }

        if (!entry)
        {
            if (policy_ == session_policy::require)
            {
                if constexpr (streaming)
                {
                    co_return detail::unauthorized_stream<typename detail::stream_value<produced_type>::type>();
                }
                else
                {
                    co_return detail::unauthorized_response<
                        detail::handler_response_t<Inner, extended_contexts>>(request);
                }
            }
            auto created = store_->create();
            key = std::move(created.first);
            entry = std::move(created.second);
            minted = true;
        }

        if constexpr (streaming)
        {
            using chunk_type = typename detail::stream_value<produced_type>::type;
            std::optional<produced_type> stream;
            {
                // Locked only while the handler produces the stream.
                auto guard = co_await store_->lock(entry);
                auto extended = std::tuple_cat(ctxs, std::make_tuple(entry->context()));
                if constexpr (is_awaitable<detail::handler_result_t<Inner, extended_contexts>>)
                {
                    stream.emplace(co_await detail::invoke_handler(inner_, extended, request, mctx));
                }
                else
                {
                    stream.emplace(detail::invoke_handler(inner_, extended, request, mctx));
                }
            }
            std::vector<chunked_set_header> extra;
            if (minted)
            {
                extra.push_back(key_.key_header(*key));
            }
            stream->set_executor(co_await boost::asio::this_coro::executor);
            co_return detail::relay_stream<chunk_type>(std::move(*stream), std::move(extra));
        }
        else
        {
            using response_type = detail::handler_response_t<Inner, extended_contexts>;
            static_assert(
                is_http_response<response_type>,
                "with_session requires the inner handler to produce a typed "
                "boost::beast::http::response, a chunked_response or an "
                "async_generator. message_generator results are not supported "
                "under with_session.");

            auto guard = co_await store_->lock(entry);

            auto extended = std::tuple_cat(ctxs, std::make_tuple(entry->context()));
            response_type response = co_await detail::response_from_invoke_handler(
                inner_, extended, request, mctx);

            if (minted)
            {
                key_.attach(response, *key);
            }
            co_return response;
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

/** Bind a keyed context to a handler.

    @code
    taar::context_store<std::string, cart> carts {std::chrono::minutes {30}};

    session.register_request_handler(
        method == http::verb::post && target == "/cart/items",
        taar::handler::with_session(carts,
            taar::handler::session_key::cookie("sid"),
            taar::handler::session_policy::create_if_missing,
            taar::handler::rest(&add_item, json_body_arg())));   // add_item(cart&, json::value)
    @endcode

    @param store  The @ref context_store that owns the keyed contexts. Must
                  outlive the session.
    @param key    A key extractor from the `session_key` namespace.
    @param policy What to do when the key is absent or unknown.
    @param inner  The handler to run with the keyed context in scope.
*/
template <typename Key, typename Context, typename KeyExtractor, typename Inner>
auto with_session(
    context_store<Key, Context>& store,
    KeyExtractor key,
    session_policy policy,
    Inner inner)
{
    return with_session_handler<context_store<Key, Context>, KeyExtractor, std::decay_t<Inner>>{
        &store, std::move(key), policy, std::move(inner)};
}

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_WITH_SESSION_HPP
