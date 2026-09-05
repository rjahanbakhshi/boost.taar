//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MCP_ENDPOINT_HPP
#define BOOST_TAAR_MCP_ENDPOINT_HPP

#include <boost/taar/core/async_channel.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/core/json_schema.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/jsonrpc/bind.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/jsonrpc/envelope.hpp>
#include <boost/taar/jsonrpc/error.hpp>
#include <boost/taar/jsonrpc/response.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/mcp/content.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/type_traits/callable.hpp>
#include <boost/describe/members.hpp>
#include <boost/describe/enumerators.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/system/system_error.hpp>
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#if defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
#include <unistd.h>
#endif

namespace boost::taar::mcp {

/// Session type for an endpoint without per-session state (no `Mcp-Session-Id`).
struct stateless {};

/// MCP logging levels (RFC 5424 names), least to most severe.
enum class log_level { debug, info, notice, warning, error, critical, alert, emergency };

inline constexpr std::array<std::string_view, 8> log_level_names {
    "debug", "info", "notice", "warning", "error", "critical", "alert", "emergency"};

inline std::string_view to_string(log_level level) noexcept
{
    return log_level_names[static_cast<std::size_t>(level)];
}

inline std::optional<log_level> parse_log_level(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < log_level_names.size(); ++i)
    {
        if (log_level_names[i] == name) return static_cast<log_level>(i);
    }
    return std::nullopt;
}

/// Static information the server reports in `initialize`, plus session options.
struct server_info
{
    std::string name;
    std::string version;
    std::string instructions {};
    std::chrono::steady_clock::duration session_idle_timeout = std::chrono::minutes {30};
    /// Items per page for `*/list` methods; 0 returns everything in one page.
    std::size_t page_size = 0;
    /// Events kept per session for `Last-Event-ID` stream resumption; also
    /// bounds how many undelivered events a session queues for a stream
    /// that has not been opened yet (oldest dropped first).
    std::size_t stream_history = 100;
    /// Least severe `log()` message delivered before the client sets a level.
    log_level default_log_level = log_level::debug;
    /// Interval between `: keepalive` comments on an idle event stream, so a
    /// dropped connection is noticed and freed for a reconnect; zero disables.
    std::chrono::steady_clock::duration stream_keepalive = std::chrono::seconds {15};
};

/// The client's self-description from `initialize`.
struct client_info
{
    std::string name;
    std::string version;
};

/// What the client declared in `initialize` that the server may ask of it.
struct client_capabilities
{
    bool roots = false;                 ///< Answers `roots/list`.
    bool roots_list_changed = false;    ///< Sends `notifications/roots/list_changed`.
    bool sampling = false;              ///< Answers `sampling/createMessage`.
    bool elicitation = false;           ///< Answers `elicitation/create`.
};

/** The client answered a server-to-client request with a JSON-RPC error.
    Thrown by @ref endpoint::request and the helpers built on it.
*/
class client_error : public std::runtime_error
{
public:
    client_error(int code, std::string message, boost::json::value data)
        : std::runtime_error {std::move(message)}
        , code_ {code}
        , data_ {std::move(data)}
    {}

    int code() const noexcept { return code_; }
    boost::json::value const& data() const noexcept { return data_; }

private:
    int code_;
    boost::json::value data_;
};

/** Identity of the current MCP session, injectable into stateful tools and
    resources by declaring a `session_info const&` parameter. Lets a tool
    address its own client: `endpoint.notify(info.id, "notifications/...")`
    or `co_await endpoint.elicit(info.id, ...)`, after checking
    `info.capabilities`.
*/
struct session_info
{
    std::string id;
    client_info client {};
    client_capabilities capabilities {};
};

/// Parsed `initialize` parameters handed to the @ref endpoint::on_initialize hook.
struct initialize_params
{
    std::string protocol_version;
    boost::json::value capabilities;
    client_info client;
};

/// Protocol revisions this implementation can speak, newest first.
inline constexpr std::array<std::string_view, 3> supported_protocol_versions {
    "2025-11-25", "2025-06-18", "2025-03-26"};

inline constexpr std::string_view latest_protocol_version = supported_protocol_versions[0];

inline bool is_supported_protocol_version(std::string_view v) noexcept
{
    for (auto s : supported_protocol_versions)
    {
        if (s == v) return true;
    }
    return false;
}

/// JSON-RPC error code MCP assigns to an unknown resource URI.
inline constexpr int resource_not_found_code = -32002;

/// What a `completion/complete` request asks for.
struct completion_request
{
    std::string ref_type;           ///< "ref/prompt" or "ref/resource".
    std::string ref;                ///< Prompt name or resource URI template.
    std::string argument;           ///< The argument being completed.
    std::string value;              ///< What the user typed so far.
    boost::json::object context;    ///< `params.context.arguments`: other arguments already filled in.
};

/// A completer's answer. At most 100 values are sent; the rest sets `hasMore`.
struct completion_result
{
    std::vector<std::string> values;
    std::optional<std::size_t> total {};
    bool has_more = false;
};

/// Behavioural hints a tool may advertise (`tools/list` `annotations`).
struct tool_annotations
{
    std::string title {};
    std::optional<bool> read_only_hint {};
    std::optional<bool> destructive_hint {};
    std::optional<bool> idempotent_hint {};
    std::optional<bool> open_world_hint {};
};

/** Everything `tool()` can say about a tool besides its callable.

    `output_schema` left unset derives a schema from the callable's return
    type (a described struct or string-keyed map as is; scalars, strings,
    enums and sequences under a `result` property, matching how results
    are wrapped); an empty object disables it.
*/
struct tool_spec
{
    std::string name;
    std::string description {};
    std::string title {};
    std::optional<tool_annotations> annotations {};
    std::optional<boost::json::object> output_schema {};
};

/// Registration-time description of a tool, for `tools/list`.
struct tool_info
{
    std::string name;
    std::string description;
    std::vector<param_info> params;
    std::string title {};
    std::optional<tool_annotations> annotations {};
    boost::json::object output_schema {};
};

/// Registration-time description of a prompt, for `prompts/list`.
struct prompt_info
{
    std::string name;
    std::string description;
    std::vector<param_info> params;
};

/// Registration-time description of a resource, for `resources/list`.
struct resource_info
{
    std::string uri;            ///< Fixed URI or URI template.
    std::string mime_type;
    std::string description;
    bool is_template = false;
    std::vector<param_info> params;
};

/// Maps an exception from a tool callable to the `isError` text, or `nullopt` for a protocol error.
using tool_error_handler = std::function<std::optional<std::string>(std::exception_ptr)>;

/** Default tool error mapping: argument and JSON-RPC-category failures stay
    JSON-RPC errors; everything else becomes an `isError: true` result with
    the exception's message as text.
*/
inline std::optional<std::string> default_tool_error_handler(std::exception_ptr e)
{
    try
    {
        std::rethrow_exception(e);
    }
    catch (boost::system::system_error const& x)
    {
        if (x.code().category() == taar::error_category() ||
            x.code().category() == jsonrpc::error_category())
        {
            return std::nullopt;
        }
        return std::string {x.what()};
    }
    catch (std::exception const& x)
    {
        return std::string {x.what()};
    }
    catch (...)
    {
        return std::string {"unknown error"};
    }
}

/// What an MCP prompt callable's argument providers receive (same shape as @ref tool_call).
using prompt_call = tool_call;

namespace detail {

/// The `outputSchema` implied by a tool callable's return type, or empty.
template <typename Callable>
boost::json::object output_schema_for()
{
    using R0 = std::remove_cvref_t<type_traits::callable_result<std::remove_cvref_t<Callable>>>;
    using R = std::remove_cvref_t<awaitable_result_t<R0>>;
    if constexpr (
        std::is_void_v<R> || is_optional_v<R> ||
        std::same_as<R, boost::json::value> || std::same_as<R, boost::json::object> || std::same_as<R, boost::json::array> ||
        std::same_as<R, tool_result> || std::same_as<R, text> || std::same_as<R, image> ||
        std::same_as<R, audio> || std::same_as<R, embedded_resource>)
    {
        return {};
    }
    else if constexpr (boost::describe::has_describe_members<R>::value || taar::detail::string_map_like<R>)
    {
        return json_schema_for<R>();
    }
    else if constexpr (
        std::is_arithmetic_v<R> || type_traits::string_like<R> ||
        boost::describe::has_describe_enumerators<R>::value || taar::detail::sequence_like<R>)
    {
        boost::json::object schema;
        schema["type"] = "object";
        schema["properties"] = boost::json::object {{"result", json_schema_for<R>()}};
        schema["required"] = boost::json::array {"result"};
        return schema;
    }
    else
    {
        return {};
    }
}

/// One server-to-client message with its stream event id.
struct sse_event
{
    std::uint64_t id;
    std::string json;
};

/// Per-session bookkeeping stored next to the user's Session object.
template <typename Session>
struct session_slot
{
    Session session {};
    session_info info {};
    std::shared_ptr<async_channel<sse_event>> mailbox = std::make_shared<async_channel<sse_event>>();
    std::mutex subscriptions_mutex;
    std::set<std::string> subscriptions;       ///< Resource URIs subscribed by this session.
    std::mutex history_mutex;
    std::deque<sse_event> history;             ///< Recent events, for resumption.
    std::uint64_t next_event_id = 1;
    std::size_t history_limit = 100;
    std::atomic<int> log_level {0};            ///< Least severe level delivered by `log()`.

    /** Queue a message for the client, assigning the next event id.

        The undelivered queue is bounded by the same `history_limit`: a
        client that keeps a session alive without ever opening its stream
        costs at most that many messages, and receives the newest ones
        when it finally connects.
    */
    bool push(std::string json)
    {
        // Id assignment and enqueueing share the lock so that concurrent
        // producers cannot interleave ids out of order in the queue.
        std::lock_guard<std::mutex> const g {history_mutex};
        sse_event event {next_event_id++, std::move(json)};
        history.push_back(event);
        while (history.size() > history_limit) history.pop_front();
        bool const queued = mailbox->push(std::move(event));
        mailbox->trim(history_limit);
        return queued;
    }

    /// Highest event id a stream has taken from the mailbox (handed to a client).
    std::shared_ptr<std::atomic<std::uint64_t>> last_taken = std::make_shared<std::atomic<std::uint64_t>>(0);

    /** Events a resuming client missed: newer than @a last_event_id and
        already taken by an earlier stream. Events still queued in the
        mailbox are not replayed, they follow in order anyway.
    */
    std::vector<sse_event> replay_after(std::uint64_t last_event_id)
    {
        auto const taken = last_taken->load();
        std::lock_guard<std::mutex> const g {history_mutex};
        std::vector<sse_event> out;
        for (auto const& e : history) if (e.id > last_event_id && e.id <= taken) out.push_back(e);
        return out;
    }
    std::shared_ptr<std::atomic<bool>> stream_attached = std::make_shared<std::atomic<bool>>(false);

    using reply_channel = async_channel<boost::json::value>;
    std::mutex pending_mutex;
    std::map<std::string, std::shared_ptr<reply_channel>> pending;  ///< Server requests awaiting a reply.
    std::uint64_t next_request_id = 1;

    /// Allocate an id and a channel for a server-to-client request.
    std::pair<boost::json::value, std::shared_ptr<reply_channel>> begin_request()
    {
        std::lock_guard<std::mutex> const g {pending_mutex};
        boost::json::value id {next_request_id++};
        auto channel = std::make_shared<reply_channel>();
        pending[boost::json::serialize(id)] = channel;
        return {std::move(id), std::move(channel)};
    }

    void end_request(boost::json::value const& id)
    {
        std::lock_guard<std::mutex> const g {pending_mutex};
        pending.erase(boost::json::serialize(id));
    }

    /// Hand a client's response to the request waiting for it; false if none is.
    bool deliver_response(boost::json::value const& response)
    {
        auto const* obj = response.if_object();
        auto const* id = obj ? obj->if_contains("id") : nullptr;
        if (!id) return false;
        std::shared_ptr<reply_channel> channel;
        {
            std::lock_guard<std::mutex> const g {pending_mutex};
            auto it = pending.find(boost::json::serialize(*id));
            if (it == pending.end()) return false;
            channel = std::move(it->second);
            pending.erase(it);
        }
        return channel->push(response);
    }

    ~session_slot()
    {
        // Ends any open event stream and fails pending requests once the session is gone.
        mailbox->close();
        for (auto& [_, channel] : pending) channel->close();
    }
};

template <typename Session>
struct session_store
{
    using type = context_store<std::string, session_slot<Session>>;
    type store;
    explicit session_store(std::chrono::steady_clock::duration idle) : store {idle} {}

    static std::shared_ptr<Session> session_of(typename type::entry_ptr const& e)
    {
        return std::shared_ptr<Session> {e->context(), &e->context()->session};
    }

    static std::shared_ptr<session_info> info_of(typename type::entry_ptr const& e)
    {
        return std::shared_ptr<session_info> {e->context(), &e->context()->info};
    }
};

template <>
struct session_store<stateless>
{
    explicit session_store(std::chrono::steady_clock::duration) {}
};

template <typename Session>
struct session_handle
{
    using type = typename session_store<Session>::type::entry_ptr;
};

template <>
struct session_handle<stateless>
{
    using type = std::nullptr_t;
};

template <typename Session>
using session_handle_t = typename session_handle<Session>::type;

inline std::string text_of(boost::json::value const& v)
{
    if (v.is_string()) return std::string {v.as_string()};
    if (v.is_null()) return {};
    return boost::json::serialize(v);
}

} // namespace detail

/** An MCP (Model Context Protocol) server endpoint over Streamable HTTP.

    Built on @ref jsonrpc::endpoint. Register tools and resources, then
    register @ref handler() on a session route for `POST` (and `DELETE` in
    stateful mode). The endpoint implements the protocol's lifecycle
    (`initialize`, `notifications/initialized`, `ping`), discovery
    (`tools/list`, `resources/list`, `resources/templates/list`) and the
    `tools/call` / `resources/read` dispatch, plus the HTTP-level rules of
    the transport (`Mcp-Session-Id`, `MCP-Protocol-Version`, `Origin`).
    Custom JSON-RPC methods can be added through @ref rpc().

    @tparam Session  Per-session state type. @ref stateless (the default)
                     disables sessions: no `Mcp-Session-Id` header is issued
                     or required. Any other type enables stateful mode: a
                     session is created on `initialize`, identified by
                     `Mcp-Session-Id`, serialised (one call at a time), and
                     removed by `DELETE` or after the idle timeout.
    @tparam Context  Context types forwarded from the HTTP session (the
                     connection context, keyed contexts from wrappers).
                     Tool and resource callables may request any of these
                     and, in stateful mode, the `Session` type and
                     @ref session_info (the session's id).

    @code
    taar::mcp::endpoint<calc_session, conn_stats> mcp {
        {.name = "calc-server", .version = "1.0.0"}};
    mcp.tool("add", "Add two integers", &add, mcp_arg("a"), mcp_arg("b"));
    mcp.resource("calc://history/last", "text/plain", "Most recent result", &last);
    session.register_request_handler(target == "/mcp", mcp.handler());
    @endcode

    Behaviour notes:
    @li Protocol version: the client's requested version is echoed when
        supported, otherwise the newest supported version is offered.
        A request carrying an unsupported `MCP-Protocol-Version` header is
        rejected with 400.
    @li JSON-RPC batches are not accepted on this transport (-32600).
    @li A JSON-RPC response or notification posted by the client is
        acknowledged with 202 and no body.
    @li Tool callables that throw produce an `isError: true` result; see
        @ref set_tool_error_handler. Argument errors and unknown tools are
        JSON-RPC errors (-32602).
    @li `GET` opens the server-to-client event stream (stateful mode only)
        when @ref stream_handler is registered for it; otherwise 405.
        Messages queued with @ref notify or @ref broadcast are delivered
        as SSE `message` events on that stream.
    @li The same endpoint can serve the stdio transport with
        @ref serve_stdio: newline-delimited JSON-RPC on stdin/stdout, one
        implicit session, notifications written to stdout.
*/
template <typename Session = stateless, typename... Context>
class endpoint
{
public:
    static constexpr bool stateful = !std::is_same_v<Session, stateless>;

    using incoming_contexts = handler::contexts<Context...>;
    using contexts_type = std::conditional_t<stateful,
        handler::contexts<Context..., Session, session_info>,
        handler::contexts<Context...>>;
    using rpc_type = std::conditional_t<stateful,
        jsonrpc::endpoint<Context..., Session, session_info>,
        jsonrpc::endpoint<Context...>>;
    /// Handle to a live session: the store entry in stateful mode, `nullptr` otherwise.
    using session_handle = detail::session_handle_t<Session>;
    /// Contexts a tool or resource callable may request: the session ones plus the per-request @ref jsonrpc::request_scope.
    using call_contexts = std::conditional_t<stateful,
        handler::contexts<Context..., Session, session_info, jsonrpc::request_scope>,
        handler::contexts<Context..., jsonrpc::request_scope>>;
    using request_type = boost::beast::http::request<boost::beast::http::string_body>;
    using response_type = boost::beast::http::response<boost::beast::http::string_body>;

    explicit endpoint(server_info info)
        : info_ {std::move(info)}
        , sessions_ {info_.session_idle_timeout}
    {}

    endpoint(endpoint const&) = delete;
    endpoint& operator=(endpoint const&) = delete;

    /** Register a tool. Leading context parameters are injected; the rest
        come from @ref mcp_arg providers. The callable may return a plain
        value (rendered as text plus `structuredContent`) or one of the
        typed results in `<boost/taar/mcp/content.hpp>`: @ref tool_result,
        @ref image, @ref audio, @ref embedded_resource or @ref text.
    */
    template <typename Callable, typename... Providers>
    endpoint& tool(std::string name, std::string description, Callable&& callable, Providers... providers)
    {
        return tool(tool_spec {std::move(name), std::move(description)},
            std::forward<Callable>(callable), std::move(providers)...);
    }

    /// Register a tool with a full @ref tool_spec (title, annotations, output schema).
    template <typename Callable, typename... Providers>
    endpoint& tool(tool_spec spec, Callable&& callable, Providers... providers)
    {
        if (tools_.contains(spec.name))
        {
            throw std::logic_error {"mcp::endpoint: tool already registered: " + spec.name};
        }
        auto bound = jsonrpc::detail::bind_method<tool_call, call_contexts>(
            std::forward<Callable>(callable), std::move(providers)...);
        tool_info info {spec.name, std::move(spec.description), std::move(bound.params)};
        info.title = std::move(spec.title);
        info.annotations = std::move(spec.annotations);
        info.output_schema = spec.output_schema
            ? std::move(*spec.output_schema)
            : detail::output_schema_for<Callable>();
        tool_order_.push_back(spec.name);
        tools_.emplace(spec.name, tool_entry {std::move(info), std::move(bound.invoke)});
        return *this;
    }

    /** Register a completer for a prompt argument (`completion/complete`
        with `ref/prompt`). @a completer is called with a
        @ref completion_request, or just the typed value as a
        `std::string_view`, and returns a @ref completion_result or a
        `std::vector<std::string>`.
    */
    template <typename Completer>
    endpoint& complete_prompt(std::string prompt, std::string argument, Completer completer)
    {
        if (!prompts_.contains(prompt))
        {
            throw std::logic_error {"mcp::endpoint: no such prompt to complete: " + prompt};
        }
        completers_[completion_key("ref/prompt", prompt, argument)] = wrap_completer(std::move(completer));
        return *this;
    }

    /// Register a completer for a resource template argument (`completion/complete` with `ref/resource`).
    template <typename Completer>
    endpoint& complete_resource(std::string uri_template, std::string argument, Completer completer)
    {
        bool const known = std::ranges::any_of(resources_, [&](auto const& r)
        {
            return r.info.is_template && r.info.uri == uri_template;
        });
        if (!known)
        {
            throw std::logic_error {"mcp::endpoint: no such resource template to complete: " + uri_template};
        }
        completers_[completion_key("ref/resource", uri_template, argument)] = wrap_completer(std::move(completer));
        return *this;
    }

    /// Register a resource by fixed URI or URI template (`{name}` placeholders bound with @ref uri_arg).
    template <typename Callable, typename... Providers>
    endpoint& resource(
        std::string uri,
        std::string mime_type,
        std::string description,
        Callable&& callable,
        Providers... providers)
    {
        for (auto const& r : resources_)
        {
            if (r.info.uri == uri)
            {
                throw std::logic_error {"mcp::endpoint: resource already registered: " + uri};
            }
        }
        auto bound = jsonrpc::detail::bind_method<resource_call, call_contexts>(
            std::forward<Callable>(callable), std::move(providers)...);
        resources_.push_back(resource_entry {
            resource_info {uri, std::move(mime_type), std::move(description), is_uri_template(uri), std::move(bound.params)},
            std::move(bound.invoke)});
        return *this;
    }

    /** Register a prompt. Arguments are bound with @ref mcp_arg from
        `params.arguments` (prompt arguments are strings). The callable may
        return a `std::string` (one user text message), a `boost::json::array`
        of messages, or an object with `messages` (and optional
        `description`).
    */
    template <typename Callable, typename... Providers>
    endpoint& prompt(std::string name, std::string description, Callable&& callable, Providers... providers)
    {
        if (prompts_.contains(name))
        {
            throw std::logic_error {"mcp::endpoint: prompt already registered: " + name};
        }
        auto bound = jsonrpc::detail::bind_method<prompt_call, call_contexts>(
            std::forward<Callable>(callable), std::move(providers)...);
        prompt_order_.push_back(name);
        prompts_.emplace(name, prompt_entry {
            prompt_info {name, std::move(description), std::move(bound.params)},
            std::move(bound.invoke)});
        return *this;
    }

    std::vector<prompt_info> prompts() const
    {
        std::vector<prompt_info> out;
        for (auto const& name : prompt_order_) out.push_back(prompts_.at(name).info);
        return out;
    }

    /** Tell every session subscribed to @a uri that the resource changed
        (`notifications/resources/updated`). Returns the number notified.
    */
    std::size_t resource_updated(std::string const& uri) requires stateful
    {
        auto const text = notification("notifications/resources/updated", boost::json::object {{"uri", uri}});
        std::size_t n = 0;
        for (auto const& key : sessions_.store.keys())
        {
            auto e = sessions_.store.peek(key);
            if (!e) continue;
            bool subscribed = false;
            {
                std::lock_guard<std::mutex> const g {e->context()->subscriptions_mutex};
                subscribed = e->context()->subscriptions.contains(uri);
            }
            if (subscribed && e->context()->push(text)) ++n;
        }
        return n;
    }

    /// Broadcast `notifications/tools/list_changed` to every session.
    std::size_t tools_changed() requires stateful { return broadcast("notifications/tools/list_changed"); }
    /// Broadcast `notifications/resources/list_changed` to every session.
    std::size_t resources_changed() requires stateful { return broadcast("notifications/resources/list_changed"); }
    /// Broadcast `notifications/prompts/list_changed` to every session.
    std::size_t prompts_changed() requires stateful { return broadcast("notifications/prompts/list_changed"); }

    /** Hook run on every `initialize`.

        In stateful mode the callable is `(Session&, initialize_params const&)`
        and runs on the freshly created session; if it throws, the session
        is discarded and the client receives an error. In stateless mode it
        is `(initialize_params const&)`.
    */
    template <typename F>
    endpoint& on_initialize(F f)
    {
        if constexpr (stateful)
        {
            static_assert(std::is_invocable_v<F&, Session&, initialize_params const&>,
                "on_initialize for a stateful endpoint takes (Session&, initialize_params const&).");
            on_initialize_ = [f = std::move(f)](contexts_type const& ctxs, initialize_params const& p) mutable
            {
                f(*std::get<sizeof...(Context)>(ctxs), p);
            };
        }
        else
        {
            static_assert(std::is_invocable_v<F&, initialize_params const&>,
                "on_initialize for a stateless endpoint takes (initialize_params const&).");
            on_initialize_ = [f = std::move(f)](contexts_type const&, initialize_params const& p) mutable
            {
                f(p);
            };
        }
        return *this;
    }

    /** Send a JSON-RPC request to one session's client and await its reply (stateful mode).

        The request travels over the session's `GET` stream (or stdout on
        stdio); the client answers by posting a JSON-RPC response to the
        endpoint. Returns the `result`. Throws @ref client_error when the
        client answers with an error, `std::out_of_range` if the session is
        unknown, `boost::system::system_error` with `errc::timed_out` after
        @a timeout (zero means wait indefinitely) or `errc::operation_canceled`
        if the session ends first. Cancelling the awaiting coroutine (for
        instance through `request_scope`) abandons the request.

        Usable from a tool: it does not need the session lock, which the
        calling tool already holds.
    */
    awaitable<boost::json::value> request(
        std::string const& session_id,
        std::string method,
        boost::json::value params = {},
        std::chrono::steady_clock::duration timeout = {})
        requires stateful
    {
        namespace net = boost::asio;
        auto entry = sessions_.store.peek(session_id);
        if (!entry) throw std::out_of_range {"unknown MCP session: " + session_id};
        auto slot = entry->context();
        auto [id, channel] = slot->begin_request();
        struct cleanup
        {
            std::shared_ptr<detail::session_slot<Session>> slot;
            boost::json::value id;
            ~cleanup() { slot->end_request(id); }
        } const guard {slot, id};

        boost::json::object msg;
        msg["jsonrpc"] = "2.0";
        msg["id"] = id;
        msg["method"] = std::move(method);
        if (!params.is_null()) msg["params"] = std::move(params);
        slot->push(boost::json::serialize(msg));

        auto ex = co_await net::this_coro::executor;
        net::steady_timer timer {ex};
        auto timed_out = std::make_shared<std::atomic<bool>>(false);
        if (timeout > std::chrono::steady_clock::duration::zero())
        {
            timer.expires_after(timeout);
            timer.async_wait([channel, timed_out](boost::system::error_code const& ec)
            {
                if (!ec)
                {
                    timed_out->store(true);
                    channel->close();
                }
            });
        }
        auto reply = co_await channel->async_receive(
            net::use_awaitable_t<net::any_io_executor> {});
        timer.cancel();
        if (!reply)
        {
            throw boost::system::system_error {
                timed_out->load() ? boost::system::errc::timed_out : boost::system::errc::operation_canceled,
                boost::system::generic_category(),
                "MCP request " + std::string {msg.at("method").as_string()} + " to session " + session_id};
        }
        auto const& obj = reply->as_object();
        if (auto const* err = obj.if_contains("error"))
        {
            int code = 0;
            std::string message = "client error";
            boost::json::value data;
            if (auto const* eo = err->if_object())
            {
                if (auto const* c = eo->if_contains("code"); c && c->is_int64()) code = static_cast<int>(c->as_int64());
                if (auto const* m = eo->if_contains("message"); m && m->is_string()) message = std::string {m->as_string()};
                if (auto const* d = eo->if_contains("data")) data = *d;
            }
            throw client_error {code, std::move(message), std::move(data)};
        }
        if (auto const* result = obj.if_contains("result")) co_return *result;
        co_return boost::json::value {nullptr};
    }

    /// `sampling/createMessage`: ask the client's model for a completion. @a params is the protocol's request object.
    awaitable<boost::json::value> create_message(
        std::string const& session_id,
        boost::json::object params,
        std::chrono::steady_clock::duration timeout = {})
        requires stateful
    {
        return request(session_id, "sampling/createMessage", std::move(params), timeout);
    }

    /** `elicitation/create`: ask the user for structured input.

        @a requested_schema is a flat JSON Schema object describing the
        fields. The result is the protocol's `{action, content}` object,
        where `action` is `accept`, `decline` or `cancel`.
    */
    awaitable<boost::json::value> elicit(
        std::string const& session_id,
        std::string message,
        boost::json::object requested_schema,
        std::chrono::steady_clock::duration timeout = {})
        requires stateful
    {
        boost::json::object params;
        params["message"] = std::move(message);
        params["requestedSchema"] = std::move(requested_schema);
        return request(session_id, "elicitation/create", std::move(params), timeout);
    }

    /// `roots/list`: the client's filesystem roots, as the protocol's `{roots: [...]}` object.
    awaitable<boost::json::value> list_roots(
        std::string const& session_id,
        std::chrono::steady_clock::duration timeout = {})
        requires stateful
    {
        return request(session_id, "roots/list", {}, timeout);
    }

    /** Handle a notification sent by the client, such as
        `notifications/roots/list_changed`. Runs under the session lock
        with the session contexts; the second argument is `params`.
    */
    void on_client_notification(
        std::string method,
        std::function<void(contexts_type const&, boost::json::value const&)> handler)
    {
        client_notifications_[std::move(method)] = std::move(handler);
    }

    /// Replace how tool exceptions are reported. See @ref default_tool_error_handler.
    void set_tool_error_handler(tool_error_handler h)
    {
        tool_error_ = std::move(h);
    }

    /// Install an `Origin` validator. Requests whose `Origin` fails it get 403.
    void set_origin_validator(std::function<bool(std::string_view)> v)
    {
        origin_validator_ = std::move(v);
    }

    /// The underlying JSON-RPC endpoint, for custom methods and the error mapper.
    rpc_type& rpc() noexcept { return rpc_; }
    rpc_type const& rpc() const noexcept { return rpc_; }

    server_info const& info() const noexcept { return info_; }

    std::vector<tool_info> tools() const
    {
        std::vector<tool_info> out;
        for (auto const& name : tool_order_) out.push_back(tools_.at(name).info);
        return out;
    }

    std::vector<resource_info> resources() const
    {
        std::vector<resource_info> out;
        for (auto const& r : resources_) out.push_back(r.info);
        return out;
    }

    /// Number of live sessions (stateful mode).
    std::size_t session_count() const requires stateful
    {
        return sessions_.store.size();
    }

    /// Remove idle sessions now (stateful mode). Also runs opportunistically on access.
    std::size_t sweep_sessions() requires stateful
    {
        return sessions_.store.sweep();
    }

    /** Queue a JSON-RPC notification for one session's event stream (stateful mode).

        Delivered when the client has a `GET` stream open; otherwise held
        until it opens one or the session ends. Does not refresh the
        session's idle timer.

        @returns false if the session is unknown or expired.
    */
    bool notify(std::string const& session_id, std::string_view method, boost::json::value params = {})
        requires stateful
    {
        auto e = sessions_.store.peek(session_id);
        if (!e) return false;
        return e->context()->push(notification(method, std::move(params)));
    }

    /// Queue a notification for every live session (stateful mode). Returns the number queued.
    /** Send a `notifications/message` log entry to one session (stateful mode).

        Dropped, returning false, when @a level is below the level the
        client set with `logging/setLevel` (initially
        `server_info::default_log_level`) or the session is unknown.
    */
    bool log(std::string const& session_id, log_level level, boost::json::value data, std::string logger = {})
        requires stateful
    {
        auto entry = sessions_.store.peek(session_id);
        if (!entry) return false;
        if (static_cast<int>(level) < entry->context()->log_level.load()) return false;
        return entry->context()->push(notification("notifications/message", log_params(level, std::move(data), std::move(logger))));
    }

    /// `log()` to every session; returns how many accepted the entry.
    std::size_t log_all(log_level level, boost::json::value data, std::string logger = {})
        requires stateful
    {
        std::size_t n = 0;
        for (auto const& key : sessions_.store.keys())
        {
            if (log(key, level, data, logger)) ++n;
        }
        return n;
    }

    std::size_t broadcast(std::string_view method, boost::json::value params = {})
        requires stateful
    {
        auto const text = notification(method, std::move(params));
        std::size_t n = 0;
        for (auto const& key : sessions_.store.keys())
        {
            if (auto e = sessions_.store.peek(key); e && e->context()->push(text)) ++n;
        }
        return n;
    }

    /** The HTTP handler for the `GET` event stream (stateful mode).

        Register it on the same route as @ref handler for `GET`:
        @code
        session.register_request_handler(method == http::verb::get && target == "/mcp", mcp.stream_handler());
        session.register_request_handler(target == "/mcp", mcp.handler());
        @endcode
        The response is `text/event-stream`; one stream per session (a
        second `GET` while one is open is answered 409). The stream ends
        when the session is deleted or expires.
    */
    auto stream_handler() requires stateful
    {
        return [this](
            boost::beast::http::request<boost::beast::http::empty_body> const& request,
            matcher::context const&) -> chunked_response<std::string>
        {
            namespace http = boost::beast::http;
            if (origin_validator_)
            {
                auto origin = request.find(http::field::origin);
                if (origin != request.end() && !origin_validator_(origin->value()))
                {
                    return plain_stream(http::status::forbidden, "Origin not allowed");
                }
            }
            auto sid = request.find("Mcp-Session-Id");
            if (sid == request.end())
            {
                return plain_stream(http::status::bad_request, "Missing Mcp-Session-Id");
            }
            auto entry = sessions_.store.find(std::string {sid->value()});
            if (!entry)
            {
                return plain_stream(http::status::not_found, "Unknown or expired session");
            }
            auto& slot = *entry->context();
            if (slot.stream_attached->exchange(true))
            {
                return plain_stream(http::status::conflict, "A stream is already open for this session");
            }
            std::vector<detail::sse_event> replay;
            if (auto last = request.find("Last-Event-ID"); last != request.end())
            {
                char* end = nullptr;
                std::string const text {last->value()};
                auto const id = std::strtoull(text.c_str(), &end, 10);
                if (!text.empty() && *end == '\0') replay = slot.replay_after(id);
            }
            return event_stream(slot.mailbox, slot.stream_attached, slot.last_taken, std::move(replay), info_.stream_keepalive);
        };
    }

#if defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
    /** Serve the MCP stdio transport on @a in_fd / @a out_fd.

        Takes ownership of both descriptors (they are closed when the
        coroutine ends). Reads newline-delimited JSON-RPC messages, answers on @a out_fd and
        writes server notifications (from @ref notify / @ref broadcast) there
        as well. One implicit session is created on `initialize` in stateful
        mode; the session is removed when the input reaches end of file.
        On end of input the coroutine waits for requests still running,
        flushes their replies, removes the session and returns. Nothing but
        protocol messages is written to @a out_fd, so a program
        using it must log to stderr. Idle expiry applies to the stdio
        session like any other; a stdio-only server should give
        @ref server_info::session_idle_timeout a generous value.

        @param incoming Contexts for the `Context...` types, normally one
                        fresh object each (the default).
    */
    awaitable<void> serve_stdio(int in_fd, int out_fd, incoming_contexts incoming)
    {
        namespace net = boost::asio;
        namespace http = boost::beast::http;

        auto ex = co_await net::this_coro::executor;

        // Everything the spawned coroutines touch lives here, so a request
        // still running when stdin closes never outlives its inputs.
        struct shared_state
        {
            net::posix::stream_descriptor out;
            std::shared_ptr<async_channel<std::string>> outbox = std::make_shared<async_channel<std::string>>();
            request_type const request {http::verb::post, "/", 11};
            matcher::context const mctx {};
            std::atomic<std::size_t> in_flight {0};
            std::atomic<bool> eof {false};
            async_channel<bool> drained;
            async_channel<bool> writer_done;

            shared_state(net::any_io_executor const& executor, int fd) : out {executor, fd} {}
        };
        auto state = std::make_shared<shared_state>(ex, out_fd);
        net::posix::stream_descriptor in {ex, in_fd};

        // Single writer: everything leaving the process goes through outbox.
        net::co_spawn(ex, [state]() -> awaitable<void>
        {
            for (;;)
            {
                auto line = co_await state->outbox->async_receive(use_awaitable);
                if (!line) break;
                auto [ec, n] = co_await net::async_write(state->out, net::buffer(*line), use_awaitable_tuple);
                if (ec) break;
            }
            state->writer_done.push(true);
        }, net::detached);

        auto send = [state](boost::json::value const& v)
        {
            state->outbox->push(boost::json::serialize(v) + "\n");
        };
        auto finished_one = [state]
        {
            if (state->in_flight.fetch_sub(1) == 1 && state->eof.load()) state->drained.push(true);
        };

        session_handle session {};
        net::streambuf buffer;

        for (;;)
        {
            auto [ec, n] = co_await net::async_read_until(in, buffer, '\n', use_awaitable_tuple);
            if (ec) break;
            std::string line {net::buffers_begin(buffer.data()), net::buffers_begin(buffer.data()) + static_cast<std::ptrdiff_t>(n)};
            buffer.consume(n);
            if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;

            boost::system::error_code pec;
            auto body = boost::json::parse(line, pec);
            if (pec)
            {
                send(error_envelope(boost::json::value {nullptr}, jsonrpc::error::parse_error));
                continue;
            }
            if (body.is_array())
            {
                send(error_envelope(boost::json::value {nullptr},
                    static_cast<int>(jsonrpc::error::invalid_request),
                    "Batch requests are not supported by this transport"));
                continue;
            }
            if (auto const* obj = body.if_object();
                obj != nullptr && !obj->contains("method") && (obj->contains("result") || obj->contains("error")))
            {
                // A response from the client to a server request.
                if constexpr (stateful)
                {
                    if (session) session->context()->deliver_response(body);
                }
                continue;
            }
            auto env = jsonrpc::parse_envelope(body);
            if (env.has_error())
            {
                send(error_envelope(rpc_type::recoverable_id(body), jsonrpc::error::invalid_request));
                continue;
            }

            if (env->method == "initialize")
            {
                auto outcome = co_await initialize(incoming, env.value());
                if constexpr (stateful)
                {
                    if (outcome.session)
                    {
                        if (session) sessions_.store.erase(session->key());
                        session = outcome.session;
                        // Forward this session's notifications to stdout.
                        net::co_spawn(ex, [mailbox = session->context()->mailbox, state]() -> awaitable<void>
                        {
                            for (;;)
                            {
                                auto msg = co_await mailbox->async_receive(use_awaitable);
                                if (!msg) break;
                                state->outbox->push(msg->json + "\n");
                            }
                        }, net::detached);
                    }
                }
                if (outcome.reply) send(*outcome.reply);
                continue;
            }

            if constexpr (stateful)
            {
                if (!session)
                {
                    if (!jsonrpc::is_notification(env.value()))
                    {
                        send(error_envelope(env->id.value_or(boost::json::value {nullptr}),
                            static_cast<int>(jsonrpc::error::invalid_request), "Server not initialized"));
                    }
                    continue;
                }
            }
            if (env->method == "notifications/cancelled")
            {
                std::string key;
                if constexpr (stateful) key = session->key();
                cancel_from(env.value(), key);
                continue;
            }
            // Requests run concurrently with reading (the session lock still
            // serialises them) so a cancellation can arrive mid-call.
            ++state->in_flight;
            net::co_spawn(ex,
                [this, incoming, env = std::move(env.value()), session, state, send, finished_one]() -> awaitable<void>
                {
                    auto reply = co_await run(incoming, env, session, state->request, state->mctx);
                    if (reply) send(*reply);
                    finished_one();
                },
                net::detached);
        }

        // stdin is closed: let the requests already running finish and
        // flush their replies before the session and the writer go away.
        state->eof = true;
        if (state->in_flight.load() > 0)
        {
            co_await state->drained.async_receive(use_awaitable);
        }
        if constexpr (stateful)
        {
            if (session) sessions_.store.erase(session->key());
        }
        state->outbox->close();
        co_await state->writer_done.async_receive(use_awaitable);
    }

    /// Serve the stdio transport on duplicates of the process's stdin/stdout with fresh contexts.
    awaitable<void> serve_stdio()
    {
        return serve_stdio(::dup(STDIN_FILENO), ::dup(STDOUT_FILENO), default_incoming());
    }

    /// One freshly constructed object for each `Context...` type.
    static incoming_contexts default_incoming()
    {
        return incoming_contexts {std::make_shared<Context>()...};
    }
#endif

    /// The HTTP handler for the MCP route (`POST`, and `DELETE` in stateful mode).
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
                jsonrpc::detail::select_contexts<incoming_contexts>(ctxs), request, mctx);
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

    /// Handle one HTTP request addressed to the MCP route.
    awaitable<response_type> handle(
        incoming_contexts const& incoming,
        request_type const& request,
        matcher::context const& mctx)
    {
        namespace http = boost::beast::http;

        if (origin_validator_)
        {
            auto origin = request.find(http::field::origin);
            if (origin != request.end() && !origin_validator_(origin->value()))
            {
                co_return plain(request, http::status::forbidden, "Origin not allowed");
            }
        }

        switch (request.method())
        {
        case http::verb::post:
            co_return co_await handle_post(incoming, request, mctx);

        case http::verb::delete_:
            if constexpr (stateful)
            {
                auto sid = request.find("Mcp-Session-Id");
                if (sid == request.end())
                {
                    co_return plain(request, http::status::bad_request, "Missing Mcp-Session-Id");
                }
                if (!sessions_.store.erase(std::string {sid->value()}))
                {
                    co_return plain(request, http::status::not_found, "Unknown or expired session");
                }
                co_return plain(request, http::status::ok, "");
            }
            else
            {
                co_return method_not_allowed(request);
            }

        default:
            co_return method_not_allowed(request);
        }
    }

private:
/// @cond
    struct tool_entry
    {
        tool_info info;
        std::move_only_function<awaitable<boost::json::value>(
            call_contexts const&, tool_call const&, matcher::context const&)> invoke;
    };

    struct resource_entry
    {
        resource_info info;
        std::move_only_function<awaitable<boost::json::value>(
            call_contexts const&, resource_call const&, matcher::context const&)> invoke;
    };

    struct prompt_entry
    {
        prompt_info info;
        std::move_only_function<awaitable<boost::json::value>(
            call_contexts const&, prompt_call const&, matcher::context const&)> invoke;
    };

    /// Slice @a items for `params.cursor`; sets `nextCursor` when more remain. Throws invalid_params on a bad cursor.
    boost::json::object paginate(boost::json::array items, std::string_view key, jsonrpc::envelope const& env) const
    {
        std::size_t start = 0;
        if (env.params)
        {
            if (auto const* obj = env.params->if_object())
            {
                if (auto const* c = obj->if_contains("cursor"))
                {
                    if (!c->is_string()) throw boost::system::system_error {jsonrpc::error::invalid_params, "cursor must be a string"};
                    auto const text = std::string {c->as_string()};
                    char* end = nullptr;
                    auto const parsed = std::strtoull(text.c_str(), &end, 10);
                    if (text.empty() || *end != '\0' || parsed > items.size())
                    {
                        throw boost::system::system_error {jsonrpc::error::invalid_params, "invalid cursor"};
                    }
                    start = static_cast<std::size_t>(parsed);
                }
            }
        }
        boost::json::object result;
        if (info_.page_size == 0 || items.size() - start <= info_.page_size)
        {
            boost::json::array page;
            for (std::size_t i = start; i < items.size(); ++i) page.push_back(std::move(items[i]));
            result[key] = std::move(page);
            return result;
        }
        boost::json::array page;
        for (std::size_t i = start; i < start + info_.page_size; ++i) page.push_back(std::move(items[i]));
        result[key] = std::move(page);
        result["nextCursor"] = std::to_string(start + info_.page_size);
        return result;
    }

    /// The session id the contexts belong to (empty in stateless mode).
    static std::string scope_key_of(contexts_type const& ctxs)
    {
        if constexpr (stateful) return std::get<sizeof...(Context) + 1>(ctxs)->id;
        else return {};
    }

    /// Notification sink for a request: the session's stream, if any.
    std::function<void(boost::json::value)> notification_sink(contexts_type const& ctxs)
    {
        if constexpr (stateful)
        {
            auto entry = sessions_.store.peek(scope_key_of(ctxs));
            if (!entry) return {};
            return [slot = entry->context()](boost::json::value msg)
            {
                slot->push(boost::json::serialize(msg));
            };
        }
        else
        {
            return {};
        }
    }

    /// Register the request, attach progress token and sink, and append the scope to the contexts.
    typename rpc_type::scoped_request begin_scope(
        contexts_type const& ctxs,
        jsonrpc::envelope const& env,
        boost::asio::any_io_executor executor)
    {
        auto scoped = rpc_.begin_request(env, scope_key_of(ctxs), std::move(executor));
        if (env.params)
        {
            if (auto const* obj = env.params->if_object())
            {
                if (auto const* meta = obj->if_contains("_meta"); meta && meta->is_object())
                {
                    if (auto const* token = meta->as_object().if_contains("progressToken"))
                    {
                        scoped.scope->progress_token = *token;
                    }
                }
            }
        }
        scoped.scope->notify = notification_sink(ctxs);
        return scoped;
    }

    /// Cancel an in-flight request named by a `notifications/cancelled` envelope.
    void cancel_from(jsonrpc::envelope const& env, std::string_view scope_key)
    {
        if (!env.params) return;
        auto const* obj = env.params->if_object();
        if (obj == nullptr) return;
        if (auto const* id = obj->if_contains("requestId"))
        {
            rpc_.cancel(*id, scope_key);
        }
    }

    static response_type plain(request_type const& request, boost::beast::http::status status, std::string body)
    {
        namespace http = boost::beast::http;
        response_type response {status, request.version()};
        if (!body.empty())
        {
            response.set(http::field::content_type, "text/plain");
            response.body() = std::move(body);
        }
        response.keep_alive(request.keep_alive());
        response.prepare_payload();
        return response;
    }

    static response_type method_not_allowed(request_type const& request)
    {
        namespace http = boost::beast::http;
        auto response = plain(request, http::status::method_not_allowed, "");
        response.set(http::field::allow, stateful ? "POST, DELETE" : "POST");
        return response;
    }

    static boost::json::value error_envelope(boost::json::value id, int code, std::string message)
    {
        return jsonrpc::make_error_response(std::move(id), code, std::move(message));
    }

    static boost::json::value error_envelope(boost::json::value id, jsonrpc::error code)
    {
        return jsonrpc::make_error_response(std::move(id), code, jsonrpc::message_for(code));
    }

    awaitable<response_type> handle_post(
        incoming_contexts const& incoming,
        request_type const& request,
        matcher::context const& mctx)
    {
        namespace http = boost::beast::http;

        auto version_header = request.find("MCP-Protocol-Version");
        if (version_header != request.end() && !is_supported_protocol_version(version_header->value()))
        {
            co_return plain(request, http::status::bad_request, "Unsupported MCP-Protocol-Version");
        }

        boost::system::error_code ec;
        auto body = boost::json::parse(request.body(), ec);
        if (ec)
        {
            co_return rpc_type::to_http(request,
                error_envelope(boost::json::value {nullptr}, jsonrpc::error::parse_error));
        }

        if (body.is_array())
        {
            co_return rpc_type::to_http(request, error_envelope(
                boost::json::value {nullptr},
                static_cast<int>(jsonrpc::error::invalid_request),
                "Batch requests are not supported by this transport"));
        }

        if (auto const* obj = body.if_object();
            obj != nullptr && !obj->contains("method") && (obj->contains("result") || obj->contains("error")))
        {
            // A JSON-RPC response from the client to a server request. Routed
            // without the session lock: the requesting tool may hold it.
            if constexpr (stateful)
            {
                if (auto h = request.find("Mcp-Session-Id"); h != request.end())
                {
                    if (auto entry = sessions_.store.peek(std::string {h->value()}))
                    {
                        entry->context()->deliver_response(body);
                    }
                }
            }
            co_return rpc_type::to_http(request, std::nullopt);
        }

        auto env = jsonrpc::parse_envelope(body);
        if (env.has_error())
        {
            co_return rpc_type::to_http(request,
                error_envelope(rpc_type::recoverable_id(body), jsonrpc::error::invalid_request));
        }

        if (env->method == "notifications/cancelled")
        {
            // Must not wait for the session lock: the request to cancel holds it.
            std::string_view sid;
            if constexpr (stateful)
            {
                auto h = request.find("Mcp-Session-Id");
                if (h != request.end()) sid = h->value();
            }
            cancel_from(env.value(), sid);
            co_return rpc_type::to_http(request, std::nullopt);
        }

        if (env->method == "initialize")
        {
            auto outcome = co_await initialize(incoming, env.value());
            auto response = rpc_type::to_http(request, std::move(outcome.reply));
            if constexpr (stateful)
            {
                if (outcome.session) response.set("Mcp-Session-Id", outcome.session->key());
            }
            co_return response;
        }

        session_handle session {};
        if constexpr (stateful)
        {
            auto sid = request.find("Mcp-Session-Id");
            if (sid == request.end())
            {
                co_return plain(request, http::status::bad_request, "Missing Mcp-Session-Id");
            }
            session = sessions_.store.find(std::string {sid->value()});
            if (!session)
            {
                co_return plain(request, http::status::not_found, "Unknown or expired session");
            }
        }
        auto out = co_await run(incoming, env.value(), session, request, mctx);
        co_return rpc_type::to_http(request, std::move(out));
    }

    /// Contexts for a call on @a session: the incoming ones plus the session and its info.
    contexts_type session_contexts(incoming_contexts const& incoming, session_handle const& session) const
    {
        if constexpr (stateful)
        {
            return std::tuple_cat(incoming,
                std::make_tuple(sessions_.session_of(session), sessions_.info_of(session)));
        }
        else
        {
            return incoming;
        }
    }

    /// Lock the session (stateful) and dispatch one envelope.
    awaitable<std::optional<boost::json::value>> run(
        incoming_contexts const& incoming,
        jsonrpc::envelope const& env,
        session_handle session,
        request_type const& request,
        matcher::context const& mctx)
    {
        if constexpr (stateful)
        {
            auto guard = co_await sessions_.store.lock(session);
            co_return co_await dispatch(session_contexts(incoming, session), env, request, mctx);
        }
        else
        {
            co_return co_await dispatch(incoming, env, request, mctx);
        }
    }

    struct initialize_outcome
    {
        std::optional<boost::json::value> reply;
        session_handle session {};
    };

    static initialize_params parse_initialize(jsonrpc::envelope const& env)
    {
        initialize_params p;
        p.protocol_version = std::string {latest_protocol_version};
        if (env.params.has_value())
        {
            if (auto const* obj = env.params->if_object())
            {
                if (auto const* v = obj->if_contains("protocolVersion"); v && v->is_string())
                {
                    p.protocol_version = std::string {v->as_string()};
                }
                if (auto const* c = obj->if_contains("capabilities"))
                {
                    p.capabilities = *c;
                }
                if (auto const* ci = obj->if_contains("clientInfo"); ci && ci->is_object())
                {
                    if (auto const* n = ci->as_object().if_contains("name"); n && n->is_string())
                    {
                        p.client.name = std::string {n->as_string()};
                    }
                    if (auto const* v = ci->as_object().if_contains("version"); v && v->is_string())
                    {
                        p.client.version = std::string {v->as_string()};
                    }
                }
            }
        }
        return p;
    }

    static client_capabilities parse_client_capabilities(boost::json::value const& v)
    {
        client_capabilities caps;
        auto const* obj = v.if_object();
        if (!obj) return caps;
        if (auto const* roots = obj->if_contains("roots"); roots && roots->is_object())
        {
            caps.roots = true;
            if (auto const* lc = roots->as_object().if_contains("listChanged"); lc && lc->is_bool())
            {
                caps.roots_list_changed = lc->as_bool();
            }
        }
        caps.sampling = obj->contains("sampling") && obj->at("sampling").is_object();
        caps.elicitation = obj->contains("elicitation") && obj->at("elicitation").is_object();
        return caps;
    }

    boost::json::object initialize_result(std::string_view negotiated) const
    {
        boost::json::object capabilities;
        capabilities["tools"] = boost::json::object {{"listChanged", true}};
        if constexpr (stateful)
        {
            capabilities["logging"] = boost::json::object {};
        }
        if (!completers_.empty())
        {
            capabilities["completions"] = boost::json::object {};
        }
        capabilities["resources"] = boost::json::object {{"subscribe", stateful}, {"listChanged", true}};
        if (!prompts_.empty())
        {
            capabilities["prompts"] = boost::json::object {{"listChanged", true}};
        }

        boost::json::object server;
        server["name"] = info_.name;
        server["version"] = info_.version;

        boost::json::object result;
        result["protocolVersion"] = negotiated;
        result["capabilities"] = std::move(capabilities);
        result["serverInfo"] = std::move(server);
        if (!info_.instructions.empty())
        {
            result["instructions"] = info_.instructions;
        }
        return result;
    }

    /// Run `initialize`: negotiate the version, create the session (stateful), run the hook.
    awaitable<initialize_outcome> initialize(incoming_contexts const& incoming, jsonrpc::envelope const& env)
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});
        auto params = parse_initialize(env);
        auto const negotiated = is_supported_protocol_version(params.protocol_version)
            ? std::string_view {params.protocol_version}
            : latest_protocol_version;

        initialize_outcome outcome;
        std::exception_ptr failure;

        if constexpr (stateful)
        {
            auto [sid, entry] = sessions_.store.create();
            entry->context()->info.id = sid;
            entry->context()->info.client = params.client;
            entry->context()->info.capabilities = parse_client_capabilities(params.capabilities);
            entry->context()->history_limit = info_.stream_history;
            entry->context()->log_level = static_cast<int>(info_.default_log_level);
            {
                auto guard = co_await sessions_.store.lock(entry);
                if (on_initialize_)
                {
                    try
                    {
                        on_initialize_(session_contexts(incoming, entry), params);
                    }
                    catch (...)
                    {
                        failure = std::current_exception();
                    }
                }
            }
            if (failure)
            {
                sessions_.store.erase(sid);
            }
            else
            {
                outcome.session = entry;
            }
        }
        else if (on_initialize_)
        {
            try
            {
                on_initialize_(incoming, params);
            }
            catch (...)
            {
                failure = std::current_exception();
            }
        }

        if (failure)
        {
            auto info = rpc_.map_exception(failure);
            outcome.reply = jsonrpc::make_error_response(
                id, info.code, std::move(info.message), std::move(info.data));
        }
        else if (!jsonrpc::is_notification(env))
        {
            outcome.reply = jsonrpc::make_response(id, initialize_result(negotiated));
        }
        co_return outcome;
    }

    awaitable<std::optional<boost::json::value>> dispatch(
        contexts_type const& ctxs,
        jsonrpc::envelope const& env,
        request_type const& request,
        matcher::context const& mctx)
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});
        std::optional<boost::json::value> out;
        try
        {
            out = co_await dispatch_impl(ctxs, env, request, mctx);
        }
        catch (boost::system::system_error const& e)
        {
            if (e.code().category() != jsonrpc::error_category()) throw;
            if (jsonrpc::is_notification(env)) co_return std::nullopt;
            co_return error_envelope(id, e.code().value(), e.what());
        }
        co_return out;
    }

    awaitable<std::optional<boost::json::value>> dispatch_impl(
        contexts_type const& ctxs,
        jsonrpc::envelope const& env,
        request_type const& request,
        matcher::context const& mctx)
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});
        std::optional<boost::json::value> out;

        if (env.method == "notifications/cancelled")
        {
            cancel_from(env, scope_key_of(ctxs));
            co_return std::nullopt;
        }
        else if (env.method.starts_with("notifications/"))
        {
            if (auto it = client_notifications_.find(env.method); it != client_notifications_.end())
            {
                it->second(ctxs, env.params.value_or(boost::json::value {nullptr}));
            }
            co_return std::nullopt;
        }
        else if (env.method == "ping")
        {
            out = jsonrpc::make_response(id, boost::json::object {});
        }
        else if (env.method == "logging/setLevel")
        {
            auto const* params = env.params ? env.params->if_object() : nullptr;
            auto const* level = params ? params->if_contains("level") : nullptr;
            auto const parsed = level && level->is_string() ? parse_log_level(level->as_string()) : std::nullopt;
            if (!parsed)
            {
                co_return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params),
                    "logging/setLevel requires params.level: one of debug, info, notice, warning, error, critical, alert, emergency");
            }
            if constexpr (stateful)
            {
                if (auto entry = sessions_.store.peek(scope_key_of(ctxs)))
                {
                    entry->context()->log_level = static_cast<int>(*parsed);
                }
                out = jsonrpc::make_response(id, boost::json::object {});
            }
            else
            {
                // No session, no stream, nothing to log to: not offered.
                co_return error_envelope(id, jsonrpc::error::method_not_found);
            }
        }
        else if (env.method == "completion/complete")
        {
            out = complete(env);
        }
        else if (env.method == "tools/list")
        {
            out = jsonrpc::make_response(id, list_tools(env));
        }
        else if (env.method == "prompts/list")
        {
            out = jsonrpc::make_response(id, list_prompts(env));
        }
        else if (env.method == "prompts/get")
        {
            out = co_await get_prompt(ctxs, env, request, mctx);
        }
        else if (env.method == "resources/subscribe" || env.method == "resources/unsubscribe")
        {
            out = subscribe(ctxs, env, env.method == "resources/subscribe");
        }
        else if (env.method == "tools/call")
        {
            out = co_await call_tool(ctxs, env, request, mctx);
        }
        else if (env.method == "resources/list")
        {
            out = jsonrpc::make_response(id, list_resources(false, env));
        }
        else if (env.method == "resources/templates/list")
        {
            out = jsonrpc::make_response(id, list_resources(true, env));
        }
        else if (env.method == "resources/read")
        {
            out = co_await read_resource(ctxs, env, request, mctx);
        }
        else
        {
            co_return co_await rpc_.dispatch(ctxs, env, request, mctx, scope_key_of(ctxs));
        }

        if (jsonrpc::is_notification(env)) co_return std::nullopt;
        co_return out;
    }

    boost::json::object list_tools(jsonrpc::envelope const& env) const
    {
        boost::json::array tools;
        for (auto const& name : tool_order_)
        {
            auto const& info = tools_.at(name).info;
            boost::json::object t;
            t["name"] = info.name;
            if (!info.title.empty()) t["title"] = info.title;
            t["description"] = info.description;
            t["inputSchema"] = object_schema(info.params);
            if (!info.output_schema.empty()) t["outputSchema"] = info.output_schema;
            if (info.annotations)
            {
                boost::json::object a;
                if (!info.annotations->title.empty()) a["title"] = info.annotations->title;
                if (info.annotations->read_only_hint) a["readOnlyHint"] = *info.annotations->read_only_hint;
                if (info.annotations->destructive_hint) a["destructiveHint"] = *info.annotations->destructive_hint;
                if (info.annotations->idempotent_hint) a["idempotentHint"] = *info.annotations->idempotent_hint;
                if (info.annotations->open_world_hint) a["openWorldHint"] = *info.annotations->open_world_hint;
                t["annotations"] = std::move(a);
            }
            tools.push_back(std::move(t));
        }
        return paginate(std::move(tools), "tools", env);
    }

    boost::json::object list_prompts(jsonrpc::envelope const& env) const
    {
        boost::json::array prompts;
        for (auto const& name : prompt_order_)
        {
            auto const& info = prompts_.at(name).info;
            boost::json::object p;
            p["name"] = info.name;
            if (!info.description.empty()) p["description"] = info.description;
            boost::json::array args;
            for (auto const& a : info.params)
            {
                boost::json::object arg;
                arg["name"] = a.name;
                if (!a.description.empty()) arg["description"] = a.description;
                arg["required"] = a.required;
                args.push_back(std::move(arg));
            }
            p["arguments"] = std::move(args);
            prompts.push_back(std::move(p));
        }
        return paginate(std::move(prompts), "prompts", env);
    }

    boost::json::object list_resources(bool templates, jsonrpc::envelope const& env) const
    {
        boost::json::array items;
        for (auto const& r : resources_)
        {
            if (r.info.is_template != templates) continue;
            boost::json::object item;
            item[templates ? "uriTemplate" : "uri"] = r.info.uri;
            item["name"] = r.info.uri;
            item["mimeType"] = r.info.mime_type;
            if (!r.info.description.empty()) item["description"] = r.info.description;
            items.push_back(std::move(item));
        }
        return paginate(std::move(items), templates ? "resourceTemplates" : "resources", env);
    }

    /// Shape a prompt callable's result as `{description?, messages}`.
    static boost::json::object prompt_result(boost::json::value const& v, std::string const& description)
    {
        boost::json::object result;
        if (!description.empty()) result["description"] = description;
        if (auto const* obj = v.if_object(); obj && obj->contains("messages"))
        {
            result = *obj;
            if (!description.empty() && !result.contains("description")) result["description"] = description;
            return result;
        }
        if (v.is_array())
        {
            result["messages"] = v;
            return result;
        }
        boost::json::object content;
        content["type"] = "text";
        content["text"] = detail::text_of(v);
        boost::json::object message;
        message["role"] = "user";
        message["content"] = std::move(content);
        result["messages"] = boost::json::array {std::move(message)};
        return result;
    }

    awaitable<std::optional<boost::json::value>> get_prompt(
        contexts_type const& ctxs,
        jsonrpc::envelope const& env,
        request_type const& request,
        matcher::context const& mctx)
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});
        boost::json::object const* params = env.params ? env.params->if_object() : nullptr;
        boost::json::value const* name = params ? params->if_contains("name") : nullptr;
        if (name == nullptr || !name->is_string())
        {
            co_return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params), "prompts/get requires params.name");
        }
        auto it = prompts_.find(std::string {name->as_string()});
        if (it == prompts_.end())
        {
            co_return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params),
                "Unknown prompt: " + std::string {name->as_string()});
        }
        boost::json::object const empty;
        boost::json::object const* arguments = &empty;
        if (auto const* a = params->if_contains("arguments"))
        {
            if (!a->is_object())
            {
                co_return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params), "params.arguments must be an object");
            }
            arguments = &a->as_object();
        }

        auto scoped = begin_scope(ctxs, env, co_await boost::asio::this_coro::executor);
        std::exception_ptr failure;
        boost::json::value value;
        try
        {
            value = co_await rpc_type::run_scoped(scoped.scope, it->second.invoke(
                std::tuple_cat(ctxs, std::make_tuple(scoped.scope)), prompt_call {env, *arguments, request}, mctx));
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (scoped.scope->cancelled()) co_return std::nullopt;
        if (failure)
        {
            auto info = rpc_.map_exception(failure);
            co_return jsonrpc::make_error_response(id, info.code, std::move(info.message), std::move(info.data));
        }
        co_return jsonrpc::make_response(id, prompt_result(value, it->second.info.description));
    }

    /// `resources/subscribe` and `resources/unsubscribe` (stateful only).
    boost::json::value subscribe(contexts_type const& ctxs, jsonrpc::envelope const& env, bool on)
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});
        if constexpr (!stateful)
        {
            (void)ctxs; (void)on;
            return error_envelope(id, jsonrpc::error::method_not_found);
        }
        else
        {
            boost::json::object const* params = env.params ? env.params->if_object() : nullptr;
            boost::json::value const* uri = params ? params->if_contains("uri") : nullptr;
            if (uri == nullptr || !uri->is_string())
            {
                return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params), "resources/subscribe requires params.uri");
            }
            auto entry = sessions_.store.peek(scope_key_of(ctxs));
            if (!entry) return error_envelope(id, jsonrpc::error::invalid_request);
            {
                std::lock_guard<std::mutex> const g {entry->context()->subscriptions_mutex};
                if (on) entry->context()->subscriptions.insert(std::string {uri->as_string()});
                else entry->context()->subscriptions.erase(std::string {uri->as_string()});
            }
            return jsonrpc::make_response(id, boost::json::object {});
        }
    }

    static boost::json::object tool_result(boost::json::value const& v)
    {
        // Already in protocol shape (mcp::tool_result or hand-built): pass through.
        if (auto const* obj = v.if_object(); obj && obj->contains("content") && obj->at("content").is_array())
        {
            boost::json::object out = *obj;
            if (!out.contains("isError")) out["isError"] = false;
            return out;
        }
        // A single typed content item.
        if (auto const* obj = v.if_object(); obj && obj->contains("type") && obj->at("type").is_string() &&
            (obj->at("type").as_string() == "image" || obj->at("type").as_string() == "audio" ||
             obj->at("type").as_string() == "resource" || obj->at("type").as_string() == "text"))
        {
            boost::json::object out;
            out["content"] = boost::json::array {v};
            out["isError"] = false;
            return out;
        }
        boost::json::array content;
        if (!v.is_null())
        {
            boost::json::object text;
            text["type"] = "text";
            text["text"] = detail::text_of(v);
            content.push_back(std::move(text));
        }
        boost::json::object result;
        result["content"] = std::move(content);
        if (v.is_object())
        {
            result["structuredContent"] = v;
        }
        else if (!v.is_null())
        {
            boost::json::object wrapped;
            wrapped["result"] = v;
            result["structuredContent"] = std::move(wrapped);
        }
        result["isError"] = false;
        return result;
    }

    awaitable<std::optional<boost::json::value>> call_tool(
        contexts_type const& ctxs,
        jsonrpc::envelope const& env,
        request_type const& request,
        matcher::context const& mctx)
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});

        boost::json::object const* params = env.params ? env.params->if_object() : nullptr;
        boost::json::value const* name = params ? params->if_contains("name") : nullptr;
        if (name == nullptr || !name->is_string())
        {
            co_return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params), "tools/call requires params.name");
        }

        auto it = tools_.find(std::string {name->as_string()});
        if (it == tools_.end())
        {
            co_return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params),
                "Unknown tool: " + std::string {name->as_string()});
        }

        boost::json::object const empty;
        boost::json::object const* arguments = &empty;
        if (auto const* a = params->if_contains("arguments"))
        {
            if (!a->is_object())
            {
                co_return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params), "params.arguments must be an object");
            }
            arguments = &a->as_object();
        }

        auto scoped = begin_scope(ctxs, env, co_await boost::asio::this_coro::executor);
        std::exception_ptr failure;
        boost::json::value value;
        try
        {
            value = co_await rpc_type::run_scoped(scoped.scope, it->second.invoke(
                std::tuple_cat(ctxs, std::make_tuple(scoped.scope)), tool_call {env, *arguments, request}, mctx));
        }
        catch (...)
        {
            failure = std::current_exception();
        }

        if (scoped.scope->cancelled())
        {
            co_return std::nullopt;     // the client asked us to stop: no response
        }
        if (failure)
        {
            if (auto text = map_tool_error(failure))
            {
                boost::json::object content_item;
                content_item["type"] = "text";
                content_item["text"] = *text;
                boost::json::object result;
                result["content"] = boost::json::array {std::move(content_item)};
                result["isError"] = true;
                co_return jsonrpc::make_response(id, std::move(result));
            }
            auto info = rpc_.map_exception(failure);
            co_return jsonrpc::make_error_response(id, info.code, std::move(info.message), std::move(info.data));
        }

        co_return jsonrpc::make_response(id, tool_result(value));
    }

    std::optional<std::string> map_tool_error(std::exception_ptr e) const
    {
        try
        {
            return tool_error_(e);
        }
        catch (...)
        {
            return default_tool_error_handler(e);
        }
    }

    awaitable<std::optional<boost::json::value>> read_resource(
        contexts_type const& ctxs,
        jsonrpc::envelope const& env,
        request_type const& request,
        matcher::context const& mctx)
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});

        boost::json::object const* params = env.params ? env.params->if_object() : nullptr;
        boost::json::value const* uri_value = params ? params->if_contains("uri") : nullptr;
        if (uri_value == nullptr || !uri_value->is_string())
        {
            co_return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params), "resources/read requires params.uri");
        }
        std::string const uri {uri_value->as_string()};

        resource_entry* match = nullptr;
        std::unordered_map<std::string, std::string> args;
        for (auto& r : resources_)
        {
            if (!r.info.is_template && r.info.uri == uri)
            {
                match = &r;
                break;
            }
        }
        if (match == nullptr)
        {
            for (auto& r : resources_)
            {
                if (!r.info.is_template) continue;
                if (auto captured = match_uri_template(r.info.uri, uri))
                {
                    match = &r;
                    args = std::move(*captured);
                    break;
                }
            }
        }
        if (match == nullptr)
        {
            co_return error_envelope(id, resource_not_found_code, "Resource not found: " + uri);
        }

        auto scoped = begin_scope(ctxs, env, co_await boost::asio::this_coro::executor);
        std::exception_ptr failure;
        boost::json::value value;
        try
        {
            value = co_await rpc_type::run_scoped(scoped.scope, match->invoke(
                std::tuple_cat(ctxs, std::make_tuple(scoped.scope)), resource_call {env, uri, args, request}, mctx));
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (scoped.scope->cancelled())
        {
            co_return std::nullopt;
        }
        if (failure)
        {
            auto info = rpc_.map_exception(failure);
            co_return jsonrpc::make_error_response(id, info.code, std::move(info.message), std::move(info.data));
        }

        boost::json::object item;
        item["uri"] = uri;
        item["mimeType"] = match->info.mime_type;
        if (auto const* obj = value.if_object(); obj && obj->contains("blob"))
        {
            // mcp::blob: binary contents, with its own mime type when given.
            item["blob"] = obj->at("blob");
            if (auto const* mt = obj->if_contains("mimeType"); mt && mt->is_string() && !mt->as_string().empty())
            {
                item["mimeType"] = *mt;
            }
        }
        else
        {
            item["text"] = detail::text_of(value);
        }
        boost::json::object result;
        result["contents"] = boost::json::array {std::move(item)};
        co_return jsonrpc::make_response(id, std::move(result));
    }

    static boost::json::object log_params(log_level level, boost::json::value data, std::string logger)
    {
        boost::json::object params;
        params["level"] = to_string(level);
        if (!logger.empty()) params["logger"] = std::move(logger);
        params["data"] = std::move(data);
        return params;
    }

    using completer_fn = std::function<completion_result(completion_request const&)>;

    static std::string completion_key(std::string_view ref_type, std::string_view ref, std::string_view argument)
    {
        return std::string {ref_type} + "\n" + std::string {ref} + "\n" + std::string {argument};
    }

    template <typename Completer>
    static completer_fn wrap_completer(Completer completer)
    {
        return [c = std::move(completer)](completion_request const& request) mutable -> completion_result
        {
            auto to_result = [](auto&& value) -> completion_result
            {
                using V = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<V, completion_result>) return value;
                else return completion_result {.values = std::vector<std::string>(value.begin(), value.end())};
            };
            if constexpr (std::invocable<Completer&, completion_request const&>)
            {
                return to_result(c(request));
            }
            else
            {
                static_assert(std::invocable<Completer&, std::string_view>,
                    "A completer takes a completion_request const& or a std::string_view (the typed value).");
                return to_result(c(std::string_view {request.value}));
            }
        };
    }

    /// Answer `completion/complete`.
    boost::json::value complete(jsonrpc::envelope const& env) const
    {
        auto const id = env.id.value_or(boost::json::value {nullptr});
        auto invalid = [&](char const* what)
        {
            return error_envelope(id, static_cast<int>(jsonrpc::error::invalid_params), what);
        };
        auto const* params = env.params ? env.params->if_object() : nullptr;
        auto const* ref = params ? params->if_contains("ref") : nullptr;
        auto const* argument = params ? params->if_contains("argument") : nullptr;
        if (!ref || !ref->is_object() || !argument || !argument->is_object())
        {
            return invalid("completion/complete requires params.ref and params.argument");
        }
        completion_request request;
        auto const& r = ref->as_object();
        if (auto const* t = r.if_contains("type"); t && t->is_string()) request.ref_type = std::string {t->as_string()};
        char const* ref_field = nullptr;
        if (request.ref_type == "ref/prompt") ref_field = "name";
        else if (request.ref_type == "ref/resource") ref_field = "uri";
        else return invalid("params.ref.type must be ref/prompt or ref/resource");
        if (auto const* n = r.if_contains(ref_field); n && n->is_string()) request.ref = std::string {n->as_string()};
        if (request.ref.empty()) return invalid("params.ref is missing its name or uri");
        auto const& a = argument->as_object();
        if (auto const* n = a.if_contains("name"); n && n->is_string()) request.argument = std::string {n->as_string()};
        if (auto const* v = a.if_contains("value"); v && v->is_string()) request.value = std::string {v->as_string()};
        if (request.argument.empty()) return invalid("params.argument.name is required");
        if (auto const* ctx = params->if_contains("context"); ctx && ctx->is_object())
        {
            if (auto const* args = ctx->as_object().if_contains("arguments"); args && args->is_object())
            {
                request.context = args->as_object();
            }
        }
        if (request.ref_type == "ref/prompt" && !prompts_.contains(request.ref))
        {
            return invalid("Unknown prompt");
        }
        if (request.ref_type == "ref/resource" && !std::ranges::any_of(resources_,
            [&](auto const& res) { return res.info.is_template && res.info.uri == request.ref; }))
        {
            return invalid("Unknown resource template");
        }

        completion_result result;
        if (auto it = completers_.find(completion_key(request.ref_type, request.ref, request.argument)); it != completers_.end())
        {
            result = it->second(request);
        }
        constexpr std::size_t max_values = 100;
        if (result.values.size() > max_values)
        {
            if (!result.total) result.total = result.values.size();
            result.values.resize(max_values);
            result.has_more = true;
        }
        boost::json::array values;
        for (auto const& v : result.values) values.emplace_back(v);
        boost::json::object completion;
        completion["values"] = std::move(values);
        if (result.total) completion["total"] = static_cast<std::uint64_t>(*result.total);
        completion["hasMore"] = result.has_more;
        return jsonrpc::make_response(id, boost::json::object {{"completion", std::move(completion)}});
    }

    static std::string notification(std::string_view method, boost::json::value params)
    {
        boost::json::object msg;
        msg["jsonrpc"] = "2.0";
        msg["method"] = method;
        if (!params.is_null()) msg["params"] = std::move(params);
        return boost::json::serialize(msg);
    }

    static chunked_response<std::string> plain_stream(boost::beast::http::status status, std::string text)
    {
        co_yield set_status(status);
        co_yield set_header(boost::beast::http::field::content_type, "text/plain");
        co_yield std::move(text);
    }

    struct attached_guard
    {
        std::shared_ptr<std::atomic<bool>> flag;
        ~attached_guard() { if (flag) flag->store(false); }
    };

    static std::string sse_frame(detail::sse_event const& e)
    {
        return "id: " + std::to_string(e.id) + "\nevent: message\ndata: " + e.json + "\n\n";
    }

    /// The next event, `nullopt` + `timed_out` on keepalive expiry, `nullopt` alone when closed.
    struct received
    {
        std::optional<detail::sse_event> event;
        bool timed_out = false;
    };

    static awaitable<received> receive_or_keepalive(
        std::shared_ptr<async_channel<detail::sse_event>> mailbox,
        std::chrono::steady_clock::duration keepalive)
    {
        namespace net = boost::asio;
        if (keepalive <= std::chrono::steady_clock::duration::zero())
        {
            co_return received {co_await mailbox->async_receive(use_awaitable), false};
        }
        using namespace net::experimental::awaitable_operators;
        net::steady_timer timer {co_await net::this_coro::executor};
        timer.expires_after(keepalive);
        auto outcome = co_await (mailbox->async_receive(use_awaitable) || timer.async_wait(use_awaitable));
        if (outcome.index() == 0)
        {
            co_return received {std::get<0>(std::move(outcome)), false};
        }
        co_return received {std::nullopt, true};
    }

    static chunked_response<std::string> event_stream(
        std::shared_ptr<async_channel<detail::sse_event>> mailbox,
        std::shared_ptr<std::atomic<bool>> attached,
        std::shared_ptr<std::atomic<std::uint64_t>> last_taken,
        std::vector<detail::sse_event> replay,
        std::chrono::steady_clock::duration keepalive)
    {
        attached_guard const guard {std::move(attached)};
        co_yield set_header(boost::beast::http::field::content_type, "text/event-stream");
        co_yield set_header(boost::beast::http::field::cache_control, "no-cache");
        co_yield std::string {": connected\n\n"};
        // Resumption: replay what an earlier stream took but the client
        // missed; whatever is still queued follows in order.
        for (auto const& e : replay)
        {
            co_yield sse_frame(e);
        }
        for (;;)
        {
            auto got = co_await receive_or_keepalive(mailbox, keepalive);
            if (got.timed_out)
            {
                // Writing it is what notices a dropped peer, which then
                // frees the session for a reconnect.
                co_yield std::string {": keepalive\n\n"};
                continue;
            }
            if (!got.event) break;
            last_taken->store(got.event->id);
            co_yield sse_frame(*got.event);
        }
    }

    server_info info_;
    detail::session_store<Session> sessions_;
    rpc_type rpc_;
    std::unordered_map<std::string, tool_entry> tools_;
    std::vector<std::string> tool_order_;
    std::vector<resource_entry> resources_;
    std::unordered_map<std::string, prompt_entry> prompts_;
    std::unordered_map<std::string, completer_fn> completers_;
    std::vector<std::string> prompt_order_;
    std::move_only_function<void(contexts_type const&, initialize_params const&)> on_initialize_;
    std::unordered_map<std::string, std::function<void(contexts_type const&, boost::json::value const&)>> client_notifications_;
    tool_error_handler tool_error_ = default_tool_error_handler;
    std::function<bool(std::string_view)> origin_validator_;
/// @endcond
};

} // namespace boost::taar::mcp

#endif // BOOST_TAAR_MCP_ENDPOINT_HPP
