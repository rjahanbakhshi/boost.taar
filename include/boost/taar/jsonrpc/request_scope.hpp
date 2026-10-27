//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_JSONRPC_REQUEST_SCOPE_HPP
#define BOOST_TAAR_JSONRPC_REQUEST_SCOPE_HPP

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/post.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace boost::taar::jsonrpc {

/** Per-request state a JSON-RPC method or MCP tool can ask for by declaring
    a `request_scope const&` (or `std::shared_ptr<request_scope>`) parameter.

    It carries the request id, a cancellation slot that the endpoint fires
    when the request is cancelled (asynchronous operations the callable
    awaits are interrupted; a synchronous callable can poll @ref cancelled),
    and a sink for server-to-client notifications such as MCP progress.

    @code
    mcp.tool("export", "Long running", [](jsonrpc::request_scope const& scope, int items)
        -> taar::awaitable<std::string>
    {
        for (int i = 0; i < items && !scope.cancelled(); ++i)
        {
            co_await do_one(i);
            scope.report_progress(i + 1, items);
        }
        co_return "done";
    }, mcp_arg("items"));
    @endcode
*/
class request_scope : public std::enable_shared_from_this<request_scope>
{
public:
    request_scope(boost::json::value id, boost::asio::any_io_executor executor)
        : id_ {std::move(id)}
        , executor_ {std::move(executor)}
    {}

    /// The request's `id` (`null` for notifications).
    boost::json::value const& id() const noexcept { return id_; }

    /// MCP `_meta.progressToken` of the request, when the client sent one.
    std::optional<boost::json::value> progress_token;

    /// Where notifications go (set by the endpoint); empty on transports without a stream.
    std::function<void(boost::json::value)> notify;

    /** The slot the endpoint binds to the method's coroutine.

        Not for callables: the coroutine's own cancellation state already
        interrupts every `co_await` inside it when the request is
        cancelled, and assigning this slot would replace that binding.
    */
    boost::asio::cancellation_slot slot() { return signal_.slot(); }

    /// True once @ref cancel was called.
    bool cancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }

    /// Cancel the request: marks it and emits the signal on the request's executor.
    void cancel(boost::asio::cancellation_type type = boost::asio::cancellation_type::all)
    {
        cancelled_.store(true, std::memory_order_release);
        boost::asio::post(executor_, [self = shared_from_this(), type]
        {
            self->signal_.emit(type);
        });
    }

    /// Send a JSON-RPC notification to the client, if the transport supports it.
    void send_notification(std::string_view method, boost::json::value params = {}) const
    {
        if (!notify) return;
        boost::json::object msg;
        msg["jsonrpc"] = "2.0";
        msg["method"] = method;
        if (!params.is_null()) msg["params"] = std::move(params);
        notify(std::move(msg));
    }

    /** Send an MCP `notifications/progress` for this request.

        No-op when the client sent no `progressToken` or the transport has
        no stream.
    */
    void report_progress(double progress, std::optional<double> total = std::nullopt, std::string message = {}) const
    {
        if (!progress_token) return;
        boost::json::object params;
        params["progressToken"] = *progress_token;
        params["progress"] = progress;
        if (total) params["total"] = *total;
        if (!message.empty()) params["message"] = std::move(message);
        send_notification("notifications/progress", std::move(params));
    }

private:
    boost::json::value id_;
    boost::asio::any_io_executor executor_;
    boost::asio::cancellation_signal signal_;
    std::atomic<bool> cancelled_ {false};
};

/// JSON-RPC error code reported when a request was cancelled before it produced a result.
inline constexpr int request_cancelled_code = -32800;

} // namespace boost::taar::jsonrpc

#endif // BOOST_TAAR_JSONRPC_REQUEST_SCOPE_HPP
