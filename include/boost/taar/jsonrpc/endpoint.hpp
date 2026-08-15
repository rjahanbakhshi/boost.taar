//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_JSONRPC_ENDPOINT_HPP
#define BOOST_TAAR_JSONRPC_ENDPOINT_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/core/json_schema.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/jsonrpc/arg.hpp>
#include <boost/taar/jsonrpc/bind.hpp>
#include <boost/taar/jsonrpc/envelope.hpp>
#include <boost/taar/jsonrpc/error.hpp>
#include <boost/taar/jsonrpc/request_scope.hpp>
#include <boost/taar/jsonrpc/response.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/system/system_error.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace boost::taar::jsonrpc {

/// A JSON-RPC error object: `code`, `message`, and optional `data`.
struct error_info
{
    int code;
    std::string message;
    std::optional<boost::json::value> data;
};

/// Signature of the hook that maps exceptions thrown by method callables.
using error_mapper = std::function<error_info(std::exception_ptr)>;

/** The default exception-to-error mapping.

    @li `boost::system::system_error` in the JSON-RPC category: that code,
        the exception's message.
    @li `boost::system::system_error` in Taar's category (argument not found,
        not convertible, ...): `-32602 Invalid params`, `data` carries the
        detail.
    @li Any other `std::exception`: `-32603 Internal error` with the
        exception's `what()` as message.
    @li Anything else: `-32603 Internal error`.
*/
inline error_info default_error_mapper(std::exception_ptr e)
{
    try
    {
        std::rethrow_exception(e);
    }
    catch (boost::system::system_error const& x)
    {
        if (x.code().category() == jsonrpc::error_category())
        {
            return {x.code().value(), x.what(), std::nullopt};
        }
        if (x.code().category() == taar::error_category())
        {
            return {
                static_cast<int>(error::invalid_params),
                jsonrpc::error_category().message(static_cast<int>(error::invalid_params)),
                boost::json::value {x.what()}};
        }
        return {static_cast<int>(error::internal_error), x.what(), std::nullopt};
    }
    catch (std::exception const& x)
    {
        return {static_cast<int>(error::internal_error), x.what(), std::nullopt};
    }
    catch (...)
    {
        return {
            static_cast<int>(error::internal_error),
            jsonrpc::error_category().message(static_cast<int>(error::internal_error)),
            std::nullopt};
    }
}

/// Registration-time description of one method, for generators.
struct method_info
{
    std::string name;
    std::string description;
    std::vector<param_info> params;
};

/// The spec's default message for @a code.
inline std::string message_for(error code)
{
    return jsonrpc::error_category().message(static_cast<int>(code));
}

/** A JSON-RPC 2.0 method registry mounted on one HTTP route.

    Register methods with @ref method, then register @ref handler() on a
    session route. The endpoint parses the request body, dispatches each
    envelope (single or batch) to the matching method, converts results
    and exceptions into response envelopes, and answers notifications with
    `202 Accepted` and no body.

    Method callables follow the same convention as `handler::rest`: leading
    parameters that request a context type in scope are injected, the rest
    come from @ref rpc_arg providers in order. The template arguments name
    the context types the endpoint will see: the session's connection
    context, plus any keyed context added by `handler::with_session` around
    `handler()`.

    @code
    taar::jsonrpc::endpoint<conn_stats> rpc;
    rpc.method("add", "Add two integers", &add, rpc_arg("a"), rpc_arg("b"));
    rpc.method("history", &history);

    session.register_request_handler(
        method == http::verb::post && target == "/rpc", rpc.handler());
    @endcode

    Behaviour notes:
    @li Method names are case-sensitive; registering a name twice throws
        `std::logic_error`.
    @li Unknown members of `params` are ignored. A `params` array binds
        positionally to the method's `rpc_arg` providers in order. Scalar params
        are reported as `-32602`.
    @li All responses, including error envelopes, use HTTP status 200 with
        `Content-Type: application/json`. Notifications (no `id`) produce
        202 with an empty body; so does a batch made only of notifications.
    @li Exceptions thrown by method callables are turned into error
        envelopes by the mapper installed with @ref set_error_mapper
        (default: @ref default_error_mapper). The mapper is also invoked
        for notifications so it can log, but its result is discarded.
*/
template <typename... Context>
class endpoint
{
public:
    using contexts_type = handler::contexts<Context...>;
    /// Contexts a method callable may request: the endpoint's plus the per-request @ref request_scope.
    using call_contexts = handler::contexts<Context..., request_scope>;
    using request_type = boost::beast::http::request<boost::beast::http::string_body>;
    using response_type = boost::beast::http::response<boost::beast::http::string_body>;

    endpoint() = default;
    endpoint(endpoint const&) = delete;
    endpoint& operator=(endpoint const&) = delete;

    /// Register @a callable as method @a name with no description.
    template <typename Callable, typename... Providers>
        requires (!std::is_convertible_v<Callable, std::string_view>)
    endpoint& method(std::string name, Callable&& callable, Providers... providers)
    {
        return method(std::move(name), std::string {}, std::forward<Callable>(callable), std::move(providers)...);
    }

    /// Register @a callable as method @a name with a description for generators.
    template <typename Callable, typename... Providers>
        requires (!std::is_convertible_v<Callable, std::string_view>)
    endpoint& method(std::string name, std::string description, Callable&& callable, Providers... providers)
    {
        if (methods_.contains(name))
        {
            throw std::logic_error {"jsonrpc::endpoint: method already registered: " + name};
        }

        auto bound = detail::bind_method<call, call_contexts>(
            std::forward<Callable>(callable), std::move(providers)...);

        order_.push_back(name);
        methods_.emplace(
            name,
            entry {
                method_info {name, std::move(description), std::move(bound.params)},
                std::move(bound.invoke)});
        return *this;
    }

    /// Replace the exception-to-error mapping. See @ref default_error_mapper.
    void set_error_mapper(error_mapper mapper)
    {
        mapper_ = std::move(mapper);
    }

    /// Map an exception with the installed mapper (falls back to the default if the mapper throws).
    error_info map_exception(std::exception_ptr e) const
    {
        try
        {
            return mapper_(e);
        }
        catch (...)
        {
            return default_error_mapper(e);
        }
    }

    /// Registration-time descriptions of all methods, in registration order.
    std::vector<method_info> methods() const
    {
        std::vector<method_info> out;
        out.reserve(order_.size());
        for (auto const& name : order_)
        {
            out.push_back(methods_.at(name).info);
        }
        return out;
    }

    /// True if a method named @a name is registered.
    bool has_method(std::string const& name) const
    {
        return methods_.contains(name);
    }

    /** Dispatch one parsed envelope.

        @returns The response envelope, or `std::nullopt` for a notification.
    */
    awaitable<std::optional<boost::json::value>> dispatch(
        contexts_type const& ctxs,
        envelope const& env,
        request_type const& request,
        matcher::context const& mctx,
        std::string_view scope_key = {})
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});
        auto it = methods_.find(env.method);
        if (it == methods_.end())
        {
            if (is_notification(env)) co_return std::nullopt;
            co_return make_error_response(id, error::method_not_found, message_for(error::method_not_found));
        }

        auto scoped = begin_request(env, scope_key, co_await boost::asio::this_coro::executor);
        std::exception_ptr failure;
        boost::json::value result;
        try
        {
            result = co_await run_scoped(scoped.scope, it->second.invoke(
                std::tuple_cat(ctxs, std::make_tuple(scoped.scope)), call {env, request}, mctx));
        }
        catch (...)
        {
            failure = std::current_exception();
        }

        std::optional<error_info> info;
        if (failure)
        {
            info = map_exception(failure);      // also for notifications, so the mapper can log
        }
        if (is_notification(env)) co_return std::nullopt;
        if (scoped.scope->cancelled())
        {
            co_return make_error_response(id, request_cancelled_code, "Request cancelled");
        }
        if (info)
        {
            co_return make_error_response(id, info->code, std::move(info->message), std::move(info->data));
        }
        co_return make_response(id, std::move(result));
    }

    /// A registered in-flight request; unregisters on destruction.
    class scoped_request
    {
    public:
        std::shared_ptr<request_scope> scope;

        scoped_request(endpoint* owner, std::string key, std::shared_ptr<request_scope> s)
            : scope {std::move(s)}, owner_ {owner}, key_ {std::move(key)} {}
        scoped_request(scoped_request&& o) noexcept
            : scope {std::move(o.scope)}, owner_ {std::exchange(o.owner_, nullptr)}, key_ {std::move(o.key_)} {}
        scoped_request(scoped_request const&) = delete;
        scoped_request& operator=(scoped_request const&) = delete;
        ~scoped_request() { if (owner_ && !key_.empty()) owner_->end_request(key_, scope.get()); }

    private:
        endpoint* owner_;
        std::string key_;
    };

    /** Register a request as in flight and create its @ref request_scope.

        @param env       The request envelope (notifications get a scope but are not registered).
        @param scope_key Namespace for ids, e.g. the MCP session id, so that
                         equal ids from different clients do not collide.
        @param executor  Executor on which cancellation is emitted.
    */
    scoped_request begin_request(envelope const& env, std::string_view scope_key, boost::asio::any_io_executor executor)
    {
        auto scope = std::make_shared<request_scope>(env.id.value_or(boost::json::value {nullptr}), std::move(executor));
        std::string key;
        if (!is_notification(env))
        {
            key = in_flight_key(scope_key, *env.id);
            std::lock_guard<std::mutex> const g {in_flight_mutex_};
            in_flight_[key].push_back(scope);
        }
        return scoped_request {this, std::move(key), std::move(scope)};
    }

    /// Run @a op under @a scope's cancellation slot, so cancel() interrupts its awaits.
    template <typename T>
    static awaitable<T> run_scoped(std::shared_ptr<request_scope> const& scope, awaitable<T> op)
    {
        auto ex = co_await boost::asio::this_coro::executor;
        co_return co_await boost::asio::co_spawn(ex, std::move(op),
            boost::asio::bind_cancellation_slot(scope->slot(), use_awaitable));
    }

    /** Cancel every in-flight request with @a id under @a scope_key.

        Awaits inside the callable are interrupted; the response is a
        `-32800` error (JSON-RPC) or suppressed (MCP).
        @returns The number of requests cancelled.
    */
    std::size_t cancel(boost::json::value const& id, std::string_view scope_key = {})
    {
        std::vector<std::shared_ptr<request_scope>> targets;
        {
            std::lock_guard<std::mutex> const g {in_flight_mutex_};
            auto it = in_flight_.find(in_flight_key(scope_key, id));
            if (it != in_flight_.end())
            {
                for (auto const& w : it->second) if (auto p = w.lock()) targets.push_back(p);
            }
        }
        for (auto const& t : targets) t->cancel();
        return targets.size();
    }

    /** Handle a parsed JSON body (single object or batch array).

        @returns The JSON to send back, or `std::nullopt` when nothing
                 should be sent (notification, or a batch of notifications).
    */
    awaitable<std::optional<boost::json::value>> handle_json(
        contexts_type const& ctxs,
        boost::json::value const& body,
        request_type const& request,
        matcher::context const& mctx)
    {
        if (auto const* arr = body.if_array())
        {
            if (arr->empty())
            {
                co_return make_error_response(
                    boost::json::value {nullptr}, error::invalid_request, message_for(error::invalid_request));
            }

            boost::json::array responses;
            for (auto const& element : *arr)
            {
                auto env = parse_envelope(element);
                if (env.has_error())
                {
                    responses.push_back(make_error_response(
                        recoverable_id(element), error::invalid_request, message_for(error::invalid_request)));
                    continue;
                }
                auto response = co_await dispatch(ctxs, env.value(), request, mctx);
                if (response.has_value())
                {
                    responses.push_back(std::move(*response));
                }
            }
            if (responses.empty()) co_return std::nullopt;
            co_return boost::json::value {std::move(responses)};
        }

        auto env = parse_envelope(body);
        if (env.has_error())
        {
            co_return make_error_response(
                recoverable_id(body), error::invalid_request, message_for(error::invalid_request));
        }
        co_return co_await dispatch(ctxs, env.value(), request, mctx);
    }

    /// Handle a raw HTTP request: parse the body and wrap the JSON result.
    awaitable<response_type> handle(
        contexts_type const& ctxs,
        request_type const& request,
        matcher::context const& mctx)
    {
        boost::system::error_code ec;
        auto body = boost::json::parse(request.body(), ec);
        std::optional<boost::json::value> out;
        if (ec)
        {
            out = make_error_response(
                boost::json::value {nullptr}, error::parse_error, message_for(error::parse_error));
        }
        else
        {
            out = co_await handle_json(ctxs, body, request, mctx);
        }
        co_return to_http(request, std::move(out));
    }

    /// Wrap an optional JSON result into the HTTP response shape used by this layer.
    static response_type to_http(request_type const& request, std::optional<boost::json::value> out)
    {
        namespace http = boost::beast::http;
        if (!out.has_value())
        {
            response_type response {http::status::accepted, request.version()};
            response.keep_alive(request.keep_alive());
            response.prepare_payload();
            return response;
        }

        response_type response {http::status::ok, request.version()};
        response.set(http::field::content_type, "application/json");
        response.keep_alive(request.keep_alive());
        response.body() = boost::json::serialize(*out);
        response.prepare_payload();
        return response;
    }

    /// The HTTP handler to register on the session route.
    class handler_type
    {
    public:
        static constexpr bool context_aware = true;
        using request_type = endpoint::request_type;

        explicit handler_type(endpoint* self) : self_ {self} {}

        template <typename Contexts>
        awaitable<response_type> operator()(
            Contexts const& ctxs,
            request_type const& request,
            matcher::context const& mctx)
        {
            co_return co_await self_->handle(
                detail::select_contexts<contexts_type>(ctxs), request, mctx);
        }

    private:
    /// @cond
        endpoint* self_;
    /// @endcond
    };

    /// Returns the handler to register. The endpoint must outlive the session.
    handler_type handler()
    {
        return handler_type {this};
    }

    /// The id of an element that failed envelope validation, if it can be recovered.
    static boost::json::value recoverable_id(boost::json::value const& element)
    {
        if (auto const* obj = element.if_object())
        {
            if (auto const* id = obj->if_contains("id"))
            {
                if (id->is_string() || id->is_number() || id->is_null())
                {
                    return *id;
                }
            }
        }
        return boost::json::value {nullptr};
    }

private:
/// @cond
    struct entry
    {
        method_info info;
        std::move_only_function<awaitable<boost::json::value>(
            call_contexts const&, call const&, matcher::context const&)> invoke;
    };

    std::mutex in_flight_mutex_;
    std::unordered_map<std::string, std::vector<std::weak_ptr<request_scope>>> in_flight_;

    static std::string in_flight_key(std::string_view scope_key, boost::json::value const& id)
    {
        return std::string {scope_key} + "\n" + boost::json::serialize(id);
    }

    void end_request(std::string const& key, request_scope const* scope)
    {
        std::lock_guard<std::mutex> const g {in_flight_mutex_};
        auto it = in_flight_.find(key);
        if (it == in_flight_.end()) return;
        std::erase_if(it->second, [&](auto const& w) { auto p = w.lock(); return !p || p.get() == scope; });
        if (it->second.empty()) in_flight_.erase(it);
    }

    std::unordered_map<std::string, entry> methods_;
    std::vector<std::string> order_;
    error_mapper mapper_ = default_error_mapper;
/// @endcond
};

} // namespace boost::taar::jsonrpc

#endif // BOOST_TAAR_JSONRPC_ENDPOINT_HPP
