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

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/is_http_response.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/path_arg_check.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <memory>
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

} // namespace detail

/** Handler decorator that binds a keyed context from a @ref context_store.

    Produced by @ref with_session. On each request it extracts the key
    with the configured extractor, looks the entry up in the store,
    applies the policy when the key is absent or unknown, acquires the
    entry's lock, and invokes the inner handler with the keyed context
    added to the contexts in scope. When a key was minted the extractor
    writes it into the response.

    The inner handler must produce a typed `boost::beast::http::response`
    (the usual case for `rest` and raw handlers); streaming handlers and
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
        -> awaitable<detail::handler_response_t<
            Inner, detail::with_session_contexts_t<Contexts, context_type>>>
    {
        using response_type = detail::handler_response_t<
            Inner, detail::with_session_contexts_t<Contexts, context_type>>;

        static_assert(
            is_http_response<response_type>,
            "with_session requires the inner handler to produce a typed "
            "boost::beast::http::response so the session key can be attached. "
            "Streaming handlers and message_generator results are not "
            "supported under with_session.");

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
                co_return detail::unauthorized_response<response_type>(request);
            }
            auto created = store_->create();
            key = std::move(created.first);
            entry = std::move(created.second);
            minted = true;
        }

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

    auto path_arg_names() const -> std::vector<std::string>
        requires has_path_arg_names<Inner>
    {
        return inner_.path_arg_names();
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
    @param key    A key extractor from @ref session_key.
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
