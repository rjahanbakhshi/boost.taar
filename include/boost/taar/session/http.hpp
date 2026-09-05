//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_SESSION_HTTP_HPP
#define BOOST_TAAR_SESSION_HTTP_HPP

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/path_arg_check.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/taar/matcher/route_shape.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/taar/core/chunk_body_from.hpp>
#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/is_async_generator.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/is_chunked_response.hpp>
#include <boost/taar/core/cookies.hpp>
#include <boost/taar/core/member_function_of.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/body_reader.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/type_traits/callable.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/http/chunk_encode.hpp>
#include <boost/beast/http/rfc7230.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/system_error.hpp>
#include <boost/url/url_view.hpp>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>
#include <utility>
#include <algorithm>
#include <chrono>
#include <concepts>
#include <exception>
#include <cstdint>

namespace boost::taar::session {

/** Sentinel for `basic_http`'s default `Context` parameter.

    Indicates that the session has no per-connection context: no shared
    object is created on accept, and handlers may not opt into one. The
    default `using http = basic_http<>` selects this case to keep
    pre-existing code working unchanged.
 */
struct no_context {};

namespace detail {

template <typename HandlerType>
concept soft_error_handler = requires(HandlerType handler)
{
    requires std::is_invocable_v<HandlerType, std::exception_ptr>;
    response_from_invoke(std::move(handler), std::declval<std::exception_ptr>());
};

template <typename T>
concept hard_error_handler = requires
{
    requires std::is_invocable_v<T, std::exception_ptr>;
};

/// The contexts tuple a `basic_http<Context>` hands to its handlers:
/// empty for `no_context`, otherwise the single connection context.
template <typename Context>
using contexts_for = std::conditional_t<
    std::is_same_v<Context, no_context>,
    handler::contexts<>,
    handler::contexts<Context>>;

template <typename H, typename Contexts>
using handler_request_t = handler::detail::request_type_of<H>;

template <typename H, typename Contexts>
using handler_result_t = handler::detail::handler_result_t<H, Contexts>;

template <typename StreamType>
auto async_write(
    StreamType& stream,
    ::boost::beast::http::message_generator&& generator)
{
    return ::boost::beast::async_write(
        stream,
        std::move(generator));
}

template <
    typename StreamType,
    bool isRequest,
    typename Body,
    typename Fields>
requires (beast::http::is_mutable_body_writer<Body>::value)
auto async_write(
    StreamType& stream,
    ::boost::beast::http::message<isRequest, Body, Fields>& message)
{
    return ::boost::beast::http::async_write(
        stream,
        message);
}

template <
    typename StreamType,
    bool isRequest,
    typename Body,
    typename Fields>
requires (! beast::http::is_mutable_body_writer<Body>::value)
auto async_write(
    StreamType& stream,
    ::boost::beast::http::message<isRequest, Body, Fields> const& message)
{
    return ::boost::beast::http::async_write(
        stream,
        message);
}

/** Write @a response; for a HEAD request only its header goes on the wire.

    A typed message keeps the Content-Length its body would have had. A
    `message_generator` is written as produced: handlers that build one
    (such as `htdocs`) see the HEAD method themselves.
*/
template <typename StreamType, typename Response>
awaitable<void> write_response(StreamType& stream, Response&& response, bool head_only)
{
    namespace http = ::boost::beast::http;
    using R = std::remove_cvref_t<Response>;
    if constexpr (requires { typename R::body_type; typename R::fields_type; })
    {
        if (head_only)
        {
            http::response_serializer<typename R::body_type, typename R::fields_type> serializer {response};
            co_await http::async_write_header(stream, serializer, use_awaitable_tuple);
            co_return;
        }
    }
    co_await ::boost::taar::session::detail::async_write(stream, std::forward<Response>(response));
}

/// The peer closed (or the operation was cancelled) between requests: not an error.
inline bool is_clean_close(boost::system::error_code const& ec)
{
    namespace asio_err = boost::asio::error;
    namespace http_err = boost::beast::http;
    return ec == asio_err::eof ||
           ec == asio_err::operation_aborted ||
           ec == http_err::error::end_of_stream;
}

/** Align a handler's response with the request: same HTTP version, and
    keep-alive only if the request allows it, the handler did not disable
    it, and the server is not shutting down. No-op for message_generator.
*/
template <typename Response>
void finish_response(Response& response, unsigned version, bool keep_alive, bool shutting_down)
{
    if constexpr (requires { response.version(version); response.keep_alive(keep_alive); })
    {
        // Read the handler's intent before switching the version: a bare
        // HTTP/1.0 message reports keep_alive() == false by default.
        bool const handler_allows = response.keep_alive();
        response.version(version);
        response.keep_alive(keep_alive && handler_allows && !shutting_down);
    }
}

/// True if the request declares a Content-Length above @a limit.
inline bool declares_body_beyond(
    boost::beast::http::request_parser<boost::beast::http::buffer_body> const& parser,
    boost::optional<std::uint64_t> const& limit)
{
    if (!limit) return false;
    auto const length = parser.content_length();
    return length && *length > *limit;
}

/// Keep-alive semantics of a request header (what message::keep_alive() computes).
inline bool header_keep_alive(boost::beast::http::request_header<> const& h)
{
    boost::beast::http::token_list tokens {h[boost::beast::http::field::connection]};
    if (h.version() >= 11) return !tokens.exists("close");
    return tokens.exists("keep-alive");
}

/// Shield the current awaitable thread: cancellation is recorded but neither
/// forwarded to child operations nor turned into exceptions at co_await.
inline awaitable<void> shield_from_cancellation()
{
    namespace net = boost::asio;
    co_await net::this_coro::reset_cancellation_state(
        net::enable_total_cancellation(), net::disable_cancellation());
    co_await net::this_coro::throw_if_cancelled(false);
}

/// Reports whether cancellation was requested while the state was shielded,
/// then restores the default (terminal-only) cancellation filter.
inline awaitable<bool> observe_shutdown()
{
    namespace net = boost::asio;
    auto const cs = co_await net::this_coro::cancellation_state;
    bool const cancelled = cs.cancelled() != net::cancellation_type::none;
    co_await net::this_coro::reset_cancellation_state(net::enable_terminal_cancellation());
    co_await net::this_coro::throw_if_cancelled(true);
    co_return cancelled;
}

inline bool is_transport_error(boost::system::error_code const& ec)
{
    namespace asio_err = boost::asio::error;
    namespace http_err = boost::beast::http;
    return ec == asio_err::eof ||
           ec == asio_err::connection_reset ||
           ec == asio_err::operation_aborted ||
           ec == http_err::error::end_of_stream;
}


/// Hook run on every response header before it is written (CORS, Server, security headers).
using response_filter_type = std::function<void(
    boost::beast::http::request_header<> const&,
    boost::beast::http::response_header<>&)>;

/// Run @a filter on @a response if it is a typed message (message_generator is opaque).
template <typename Response>
void apply_response_filter(
    response_filter_type const& filter,
    boost::beast::http::request_header<> const& request,
    Response& response)
{
    if constexpr (requires { response.base(); })
    {
        if (filter) filter(request, response.base());
    }
}

/// A callable invoked at registration with the route's operand and handler.
template <typename Observer, typename MatcherType, typename Handler>
concept registration_observer = requires(
    Observer&& observer,
    decltype(matcher::operand {std::declval<MatcherType>()}) const& operand,
    Handler const& handler)
{
    std::forward<Observer>(observer)(operand, handler);
};

inline boost::beast::http::status select_error_status(boost::system::error_code const& ec)
{
    namespace http_err = boost::beast::http;
    if (ec == http_err::error::header_limit)
        return http_err::status::request_header_fields_too_large;
    if (ec == http_err::error::body_limit)
        return http_err::status::payload_too_large;
    return http_err::status::bad_request;
}

template <typename T>
inline constexpr std::string_view default_chunk_content_type()
{
    if constexpr (std::same_as<std::remove_cvref_t<T>, boost::json::value>)
        return "application/json";
    else if constexpr (
        std::same_as<std::remove_cvref_t<T>, std::vector<std::byte>> ||
        std::same_as<std::remove_cvref_t<T>, std::span<std::byte const>>)
        return "application/octet-stream";
    else
        return "text/plain";
}

template <typename GeneratorType>
void apply_chunked_metadata(
    GeneratorType&,
    ::boost::beast::http::response<::boost::beast::http::empty_body>&)
{
    // For async_generator: no metadata to apply
}

template <typename T>
void apply_chunked_metadata(
    chunked_response<T>& response,
    ::boost::beast::http::response<::boost::beast::http::empty_body>& header)
{
    namespace http = ::boost::beast::http;

    header.result(response.status());

    for (auto const& h : response.headers())
    {
        if (h.field != http::field::unknown)
            header.set(h.field, h.value);
        else
            header.set(h.name, h.value);
    }
}

template <typename GeneratorType>
constexpr auto chunked_value_type_helper()
{
    if constexpr (is_chunked_response<GeneratorType>)
        return std::type_identity<chunked_response_value_t<GeneratorType>>{};
    else
        return std::type_identity<async_generator_value_t<GeneratorType>>{};
}

template <typename GeneratorType>
using generator_value_t = typename decltype(chunked_value_type_helper<GeneratorType>())::type;

template <typename GeneratorType, typename StreamType>
awaitable<bool> write_chunked_response(
    GeneratorType& generator,
    StreamType& stream,
    unsigned version,
    bool keep_alive,
    ::boost::asio::cancellation_slot cancellation_slot = {},
    std::exception_ptr* mid_stream_error = nullptr,
    response_filter_type const& filter = {},
    ::boost::beast::http::request_header<> const* request = nullptr,
    bool head_only = false)
{
    namespace http = ::boost::beast::http;
    namespace net = ::boost::asio;
    using T = generator_value_t<GeneratorType>;

    generator.set_executor(stream.get_executor());
    if (cancellation_slot.is_connected())
        generator.set_cancellation_slot(cancellation_slot);

    // Get first value BEFORE sending headers so exceptions propagate
    // to the soft error handler before any bytes are on the wire.
    auto [first_ec, first_value] = co_await generator.next();
    (void)first_ec;
    generator.rethrow_if_exception();

    // Build chunked response header. User metadata is applied first so it
    // can override the default content-type, then transfer-encoding is set
    // last to enforce that this response is always chunked-encoded.
    http::response<http::empty_body> header_response {http::status::ok, version};
    header_response.set(http::field::content_type, default_chunk_content_type<T>());
    header_response.keep_alive(keep_alive);
    apply_chunked_metadata(generator, header_response);
    if (filter && request) filter(*request, header_response.base());
    header_response.set(http::field::transfer_encoding, "chunked");

    http::response_serializer<http::empty_body> serializer {header_response};
    auto [header_ec, header_sz] = co_await http::async_write_header(
        stream, serializer, net::as_tuple(net::deferred));
    if (header_ec)
        co_return false;

    if (head_only)
        co_return keep_alive;      // HEAD: the chunked header says it all

    // Write first value if present
    if (first_value)
    {
        auto body = chunk_body_from(std::move(*first_value));
        auto [write_ec, write_sz] = co_await net::async_write(
            stream,
            http::make_chunk(net::buffer(body)),
            net::as_tuple(net::deferred));
        if (write_ec)
            co_return false;

        // Send remaining chunks
        while (true)
        {
            auto [ec, value] = co_await generator.next();
            (void)ec;
            if (!value)
                break;

            auto chunk_body = chunk_body_from(std::move(*value));
            auto [chunk_ec, chunk_sz] = co_await net::async_write(
                stream,
                http::make_chunk(net::buffer(chunk_body)),
                net::as_tuple(net::deferred));
            if (chunk_ec)
                co_return false;
        }
    }

    // If the generator stored an exception (i.e., it threw mid-stream after
    // yielding values), do NOT send the terminating chunk. The chunked
    // protocol has no way to signal failure once headers are on the wire,
    // so we drop the connection: the client sees a truncated response
    // rather than a falsely-successful one. The exception is handed back
    // so the session can report it through the hard error handler.
    try
    {
        generator.rethrow_if_exception();
    }
    catch (...)
    {
        if (mid_stream_error) *mid_stream_error = std::current_exception();
        co_return false;
    }

    // Send last chunk (best effort, ignore errors)
    co_await net::async_write(
        stream,
        http::make_chunk_last(),
        net::as_tuple(net::deferred));

    co_return keep_alive;
}

} // detail

/** Per-connection HTTP session coroutine.

    A single `http` instance is shared across every TCP connection accepted
    by @ref boost::taar::server::tcp. Each accepted socket is dispatched to
    the call operator, which reads requests in a keep-alive loop, walks the
    registered matchers in order, runs the first matching handler, writes
    the response, and continues with the next request on the same
    connection until the client closes, the response opts out of
    keep-alive, or cancellation is requested.

    Register routes with @ref register_request_handler. Each route is a
    matcher (see @ref boost::taar::matcher) paired with a handler. The
    handler can be:

    @li A REST adapter produced by
        @ref boost::taar::handler::rest "taar::handler::rest", or
    @li A static-file handler @ref boost::taar::handler::htdocs, or
    @li A user-written callable with the signature
        `(request, context) -> message_generator` or an awaitable variant.

    Protocol upgrades (WebSocket, HTTP/2 etc.) are supported through
    @ref register_raw_handler, which hands the socket, the parsed header,
    and any post-header bytes to the handler and then closes the session
    cleanly.

    Two error-reporting hooks are exposed:

    @li @ref set_soft_error_handler: invoked when a handler throws.
        Receives the `exception_ptr`, has access to the stream, and is
        expected to write an HTTP response describing the error. The
        default handler turns @ref boost::taar::error codes into 400
        responses.
    @li @ref set_hard_error_handler: invoked when the connection itself
        fails. The default discards the exception silently; a real server
        should at least log.

    `http` is move-constructible but not copyable; share one instance among
    spawned sessions by reference.

    @tparam Stream  The connection stream type. `rebind_executor<beast::tcp_stream>`
                    for plain HTTP (the default); `session::tls_stream` from
                    `<boost/taar/session/https.hpp>` for HTTPS, where the
                    @ref boost::taar::server::tls acceptor performs the
                    handshake before handing the stream over.
    @tparam Context Per-connection state type. When set to anything other
                    than @ref no_context, the session creates one
                    `Context` (via `std::make_shared`) for every accepted
                    TCP connection and keeps it alive across all
                    keep-alive requests on that connection. Handlers opt
                    in through leading parameters of type `Context&`,
                    `Context const&` or `std::shared_ptr<Context>`;
                    handlers without such a parameter are unaffected. The
                    default alias, @ref http, uses @ref no_context and
                    creates nothing per connection.
*/
template <typename Context = no_context, typename Stream = rebind_executor<boost::beast::tcp_stream>>
class basic_http
{
public:
    /// The per-connection context type carried by this session.
    using context_type = Context;

private:
/// @cond
    using matcher_type = std::move_only_function<
        bool(
            boost::beast::http::request_header<> const&,
            matcher::context&,
            boost::urls::url_view const&,
            cookies const&)>;

    using request_handler_wrapper_type = std::move_only_function<
        awaitable<bool>(
            matcher::context const&,
            Stream&,
            boost::beast::flat_buffer&,
            boost::beast::http::request_parser<boost::beast::http::buffer_body>&,
            cancellation_signals&,
            detail::contexts_for<Context> const&)>;

    struct matcher_handler_type
    {
        matcher_type matcher;
        request_handler_wrapper_type handler;
    };

    using soft_error_handler_wrapper_type = std::move_only_function<
        awaitable<bool>(
            std::exception_ptr,
            Stream&,
            boost::beast::http::request_header<>&,
            cancellation_signals&)>;

    using hard_error_handler_type = std::move_only_function<void(std::exception_ptr)>;

/// @endcond
public:
    basic_http()
    {
        // The wrapper outlives moves of this object, so it captures the
        // shared filter rather than `this`.
        wrapped_soft_error_handler_ =
            [filter = response_filter_](
                std::exception_ptr eptr,
                Stream& stream,
                boost::beast::http::request_header<>& req,
                cancellation_signals&) -> awaitable<bool>
            {
                std::exception_ptr ex;
                std::string error_msg;
                auto status = boost::beast::http::status::bad_request;
                try
                {
                    std::rethrow_exception(eptr);
                }
                catch(boost::system::system_error const& e)
                {
                    if (e.code().category() == error_category())
                    {
                        // Constraint failures name the argument and the rule.
                        error_msg = e.code() == taar::error::constraint_violated
                            ? std::string {e.what()}
                            : e.code().message();
                    }
                    else if (e.code() == boost::beast::http::error::body_limit)
                    {
                        // A handler streaming the body hit its own size cap.
                        error_msg = "Request body too large";
                        status = boost::beast::http::status::payload_too_large;
                    }
                    ex = std::current_exception();
                }
                catch (...)
                {
                    ex = std::current_exception();
                }

                if (!error_msg.empty())
                {
                    auto response = response_from(error_msg);
                    response.result(status);
                    detail::finish_response(response, req.version(), detail::header_keep_alive(req), false);
                    detail::apply_response_filter(*filter, req, response);
                    bool keep_alive = response.keep_alive();
                    co_await detail::write_response(stream, std::move(response), req.method() == boost::beast::http::verb::head);
                    co_return keep_alive;
                }

                std::rethrow_exception(eptr);
            };
    }

    /** Answer `405 Method Not Allowed` (with an `Allow` header) instead of
        `404` when no route matches but some route accepts the path under
        another method. Enabled by default.

        Only routes whose matcher is a conjunction of one `target == ...`
        and `method == ...` constraints contribute; opaque or negated
        matchers neither cause a 405 nor appear in `Allow`.
    */
    void set_method_not_allowed(bool enabled)
    {
        method_not_allowed_enabled_ = enabled;
    }

    /** Serve `HEAD` requests through a matching `GET` route when no route
        matches `HEAD` itself. Enabled by default.

        The handler runs with the original HEAD request and only the
        response header is written, so `Content-Length` reflects the GET
        body. Handlers producing a `message_generator` write what they
        produce; a streaming handler's chunked header is sent without
        chunks. `GET` routes list `HEAD` in `Allow`.
    */
    void set_head_from_get(bool enabled)
    {
        head_from_get_ = enabled;
    }

    /** Install a hook that runs on every response header before it is written.

        Applies to handler responses, soft-error responses, the stock 404
        and the header of chunked responses; raw handlers that return a
        `message_generator` are opaque and bypass it. Use it for CORS
        (`taar::cors::response_filter()`), a `Server` header, or security
        headers.
    */
    void set_response_filter(detail::response_filter_type filter)
    {
        *response_filter_ = std::move(filter);
    }

    /// Cap the bytes consumed by a request header. Default 8 KiB.
    void set_request_header_limit(std::uint32_t limit)
    {
        request_header_limit_ = limit;
    }

    /// Cap the bytes consumed by a request body. `boost::none` disables the cap. Default 1 MiB.
    void set_request_body_limit(boost::optional<std::uint64_t> limit)
    {
        request_body_limit_ = limit;
    }

    basic_http(basic_http const&) = delete;
    basic_http(basic_http&&) = default;
    basic_http& operator=(basic_http const&) = delete;
    basic_http& operator=(basic_http&&) = default;
    ~basic_http() = default;

    /** Drive one accepted connection to completion.

        Called by @ref boost::taar::server::tcp for every accepted socket.
        Owns the socket, parses request headers, dispatches to matching
        handlers, writes responses, honours keep-alive, and terminates on
        cancellation, peer close, or after a non-keep-alive response.
    */
    awaitable<void> operator()(
        rebind_executor<boost::asio::ip::tcp::socket> socket,
        cancellation_signals& signals)
        requires std::constructible_from<Stream, rebind_executor<boost::asio::ip::tcp::socket>&&>
    {
        return (*this)(Stream {std::move(socket)}, signals);
    }

    /** Drive one connection on an already established stream.

        Used by @ref boost::taar::server::tls after the handshake, and by
        any transport that builds its own stream (`Stream` is the class
        template parameter, `rebind_executor<beast::tcp_stream>` by
        default). On exit a TLS stream receives `close_notify` before the
        TCP shutdown.
    */
    awaitable<void> operator()(Stream stream, cancellation_signals& signals)
    {
        namespace net = boost::asio;
        namespace http = boost::beast::http;
        namespace this_coro = net::this_coro;
        using boost::beast::flat_buffer;

        // Per-connection context. Created once on accept and carried
        // across every keep-alive request on this connection. For the
        // `no_context` specialisation the tuple is empty.
        detail::contexts_for<Context> conn_ctx;
        if constexpr (!std::is_same_v<Context, no_context>)
        {
            std::get<0>(conn_ctx) = std::make_shared<Context>();
        }

        // One buffer per connection: bytes of a pipelined request that arrive
        // together with the previous one must survive into the next iteration.
        flat_buffer buffer;

        try
        {
            for (auto cs = co_await this_coro::cancellation_state;
                cs.cancelled() == net::cancellation_type::none;
                cs = co_await this_coro::cancellation_state)
            {
                // Read and parse the header and use the target to find the handler.
                http::request_parser<http::buffer_body> header_parser;
                header_parser.header_limit(request_header_limit_);
                // The body limit is applied when the body is read (the
                // matched handler decides whether it buffers or streams),
                // not at header time, so a streaming route can accept a
                // Content-Length beyond the limit.
                header_parser.body_limit(boost::none);

                auto [header_ec, header_sz] = co_await http::async_read_header(
                    stream,
                    buffer,
                    header_parser);

                if (header_ec)
                {
                    if (detail::is_clean_close(header_ec))
                    {
                        // Idle keep-alive connection closed by the peer, or shutdown.
                        break;
                    }
                    if (!detail::is_transport_error(header_ec))
                    {
                        co_await write_protocol_error(stream, header_ec, header_parser.get());
                    }
                    // Invoke the hard error handler for server-side visibility.
                    throw boost::system::system_error{header_ec};
                }

                auto& req_header = header_parser.get();
                matcher::context context;
                boost::urls::url_view parsed_target;
                cookies parsed_cookies;

                if (needs_parsed_target_)
                {
                    parsed_target = boost::urls::url_view(req_header.target());
                }

                if (needs_parsed_cookies_)
                {
                    auto r = req_header.equal_range(boost::beast::http::field::cookie);
                    for (; r.first != r.second; ++r.first)
                    {
                        parse_cookies(r.first->value(), parsed_cookies);
                    }
                }

                auto iter = std::ranges::find_if(matcher_handlers_,
                    [&](auto&& matcher_handler) -> bool
                    {
                        context.path_args.clear();
                        return matcher_handler.matcher(
                            req_header,
                            context,
                            parsed_target,
                            parsed_cookies);
                    });

                if (iter != matcher_handlers_.cend())
                {
                    if (!co_await iter->handler(
                        context,
                        stream,
                        buffer,
                        header_parser,
                        signals,
                        conn_ctx))
                    {
                        // Handler instructed to close the session.
                        break;
                    }

                    // Done with this request. Proceeding with the next one.
                    continue;
                }

                // No handler found. A HEAD request may be served by a GET
                // route (the handler sees HEAD; only the header is written).
                if (head_from_get_ && req_header.method() == http::verb::head)
                {
                    req_header.method(http::verb::get);
                    iter = std::ranges::find_if(matcher_handlers_,
                        [&](auto&& matcher_handler) -> bool
                        {
                            context.path_args.clear();
                            return matcher_handler.matcher(
                                req_header,
                                context,
                                parsed_target,
                                parsed_cookies);
                        });
                    req_header.method(http::verb::head);
                    if (iter != matcher_handlers_.cend())
                    {
                        if (!co_await iter->handler(
                            context,
                            stream,
                            buffer,
                            header_parser,
                            signals,
                            conn_ctx))
                        {
                            break;
                        }
                        continue;
                    }
                }

                // 405 when some route knows this path under another method,
                // otherwise 404. Either way the body must be consumed first.
                auto const allow = method_not_allowed_enabled_
                    ? allowed_methods(req_header, parsed_target, parsed_cookies)
                    : std::string {};
                auto keep_alive = req_header.keep_alive();
                auto version = req_header.version();
                if (req_header.has_content_length() || req_header.chunked())
                {
                    boost::system::error_code req_ec;
                    std::size_t req_sz = 0;
                    if (detail::declares_body_beyond(header_parser, request_body_limit_))
                    {
                        req_ec = http::error::body_limit;
                    }
                    http::request_parser<http::string_body> body_parser {std::move(header_parser)};
                    body_parser.body_limit(request_body_limit_);
                    if (!req_ec)
                    {
                        std::tie(req_ec, req_sz) = co_await async_read(
                            stream,
                            buffer,
                            body_parser);
                    }

                    if (req_ec)
                    {
                        if (!detail::is_transport_error(req_ec))
                        {
                            co_await write_protocol_error(stream, req_ec, body_parser.get().base());
                        }
                        // Invoke the hard error handler for server-side visibility.
                        throw boost::system::system_error{req_ec};
                    }
                    // Drain the body so the connection stays in keep-alive
                    // state; the body itself is discarded.
                    (void)body_parser.get();
                }

                http::response<http::empty_body> response {
                    allow.empty() ? http::status::not_found : http::status::method_not_allowed, version};
                if (!allow.empty())
                {
                    response.set(http::field::allow, allow);
                }
                response.keep_alive(keep_alive);
                detail::apply_response_filter(*response_filter_, req_header, response);
                response.prepare_payload();
                auto [resp_ec, resp_sz] = co_await http::async_write(
                    stream,
                    response);

                if (resp_ec)
                {
                    // Invoke the hard error handler for server-side visibility.
                    throw boost::system::system_error{resp_ec};
                }

                if (!keep_alive)
                {
                    // Keep alive is not requested.
                    break;
                }
            }
        }
        catch (...)
        {
            // Unrecoverable error.
            hard_error_handler_(std::current_exception());
        }

        // Close the TLS layer first, if there is one, then half-close TCP.
        if constexpr (requires { stream.async_shutdown(net::as_tuple(use_awaitable)); })
        {
            boost::beast::get_lowest_layer(stream).expires_after(std::chrono::seconds {5});
            auto [sec] = co_await stream.async_shutdown(net::as_tuple(use_awaitable));
            (void)sec;
        }
        boost::system::error_code ec;
        boost::beast::get_lowest_layer(stream).socket().shutdown(net::ip::tcp::socket::shutdown_send, ec); // NOLINT
    }

    template <typename MatcherType, typename RequestHandler>
    auto register_request_handler(
        MatcherType&&,
        RequestHandler)
    {
        static_assert(
            matcher::is_matcher<std::decay_t<MatcherType>>,
            "Incompatible matcher type");

        static_assert(
            std::is_move_constructible_v<std::decay_t<RequestHandler>>,
            "Http handler target must be move-constructible");
    }

    /** Register a request handler paired with a matcher.

        Routes are evaluated in registration order on every request. The
        first matcher that returns `true` wins, the corresponding handler
        runs, and its response is written back to the client.

        The handler may be:

        @li A @ref boost::taar::handler::rest "rest" adapter,
        @li A @ref boost::taar::handler::htdocs "htdocs" handler,
        @li A raw callable accepting
            `(boost::beast::http::request<Body> const&, taar::matcher::context const&)`
            and returning a `message_generator` (synchronously or as an
            awaitable). The body type drives how the session reads the
            request body.

        @param matcher        A matcher object satisfying the
                              `boost::taar::matcher::is_matcher` concept.
        @param request_handler The handler callable; must be move-constructible.
    */
    template <typename MatcherType, typename RequestHandler> requires(
        matcher::is_matcher<std::decay_t<MatcherType>> &&
        std::is_move_constructible_v<std::decay_t<RequestHandler>>)
    auto register_request_handler(
        MatcherType&& matcher,
        RequestHandler request_handler)
    {
        namespace http = boost::beast::http;

        matcher::operand operand {std::forward<MatcherType>(matcher)};

        // Fail fast on path_arg(name)/target-template mismatches: the
        // check is a no-op when the matcher has no target_constraint or
        // the handler does not expose path_arg_names().
        handler::validate_path_args_against_target(operand, request_handler);

        needs_parsed_target_ |= decltype(operand)::with_parsed_target;
        needs_parsed_cookies_ |= decltype(operand)::with_parsed_cookies;
        record_route_methods(operand);

        matcher_handlers_.emplace_back(
            [this, operand = std::move(operand)](
                http::request_header<> const& request,
                matcher::context& context,
                boost::urls::url_view const& parsed_target,
                cookies const& parsed_cookies)
            {
                return operand(request, context, parsed_target, parsed_cookies);
            },
            [this, request_handler = std::move(request_handler)](
                matcher::context const& context,
                Stream& stream,
                boost::beast::flat_buffer& buffer,
                boost::beast::http::request_parser<boost::beast::http::buffer_body>& header_parser,
                cancellation_signals& signals,
                detail::contexts_for<Context> const& conn_ctx) mutable
            -> awaitable<bool>
            {
                using contexts_type = detail::contexts_for<Context>;
                using request_type = detail::handler_request_t<RequestHandler, contexts_type>;
                using result_type = detail::handler_result_t<RequestHandler, contexts_type>;

                if constexpr (std::is_same_v<std::remove_cvref_t<request_type>, streaming_request>)
                {
                    // Streaming body: hand the handler a reader over the live
                    // parser instead of buffering the body first. The body
                    // limit is the handler's business; a body left unread
                    // closes the connection after the response.
                    static_assert(
                        !is_chunked_response<result_type> && !is_async_generator<result_type> &&
                        !(is_awaitable<result_type> &&
                          (is_chunked_response<awaitable_result_t<result_type>> ||
                           is_async_generator<awaitable_result_t<result_type>>)),
                        "A handler that streams the request body must produce a plain response, "
                        "not a chunked_response or async_generator.");

                    header_parser.body_limit(boost::none);
                    auto version = header_parser.get().version();
                    auto keep_alive = header_parser.get().keep_alive();
                    auto reader = ::boost::taar::detail::make_body_reader(stream, buffer, header_parser);
                    streaming_request request {header_parser.get().base(), reader};

                    bool shutting_down = false;
                    std::exception_ptr eptr;
                    try
                    {
                        co_await detail::shield_from_cancellation();
                        auto response = co_await handler::detail::response_from_invoke_handler(
                            std::move(request_handler), conn_ctx, request, context);
                        shutting_down = co_await detail::observe_shutdown();
                        detail::finish_response(response, version, keep_alive && header_parser.is_done(), shutting_down);
                        detail::apply_response_filter(*response_filter_, header_parser.get(), response);
                        bool const ka = response.keep_alive() && !shutting_down;
                        co_await detail::write_response(stream, std::move(response), header_parser.get().method() == http::verb::head);
                        co_return ka;
                    }
                    catch (...)
                    {
                        eptr = std::current_exception();
                    }
                    shutting_down = co_await detail::observe_shutdown() || shutting_down;
                    if (!header_parser.is_done())
                    {
                        // Unread body: the error response must announce the close.
                        header_parser.get().keep_alive(false);
                    }
                    bool const ka = co_await wrapped_soft_error_handler_(eptr, stream, header_parser.get(), signals);
                    co_return ka && !shutting_down && header_parser.is_done();
                }
                else
                {
                using body_type = request_type::body_type;

                // Reject an oversized Content-Length before reading, as the
                // header-time limit used to; chunked bodies are limited
                // progressively by the parser.
                boost::system::error_code req_ec;
                std::size_t req_sz = 0;
                if (detail::declares_body_beyond(header_parser, request_body_limit_))
                {
                    req_ec = http::error::body_limit;
                }
                http::request_parser<body_type> body_parser {std::move(header_parser)};
                body_parser.body_limit(request_body_limit_);
                if (!req_ec)
                {
                    std::tie(req_ec, req_sz) = co_await async_read(stream, buffer, body_parser);
                }

                auto version = body_parser.get().version();
                auto keep_alive = body_parser.get().keep_alive();

                if (req_ec)
                {
                    if (!detail::is_transport_error(req_ec))
                    {
                        co_await write_protocol_error(stream, req_ec, body_parser.get().base());
                    }
                    // Invoke the hard error handler for server-side visibility.
                    throw boost::system::system_error{req_ec};
                }

                // Get cancellation slot from current coroutine for request-scoped cancellation
                auto cs = co_await boost::asio::this_coro::cancellation_state;
                auto cancellation_slot = cs.slot();

                // Plain (non-streaming) handlers run shielded from cancellation
                // so a shutdown lets the in-flight request finish; the response
                // then closes the connection. Streaming handlers stay cancellable.
                bool shutting_down = false;
                std::exception_ptr eptr;
                std::exception_ptr stream_error;
                try
                {
                    if constexpr (is_chunked_response<result_type>)
                    {
                        // Sync handler returning chunked_response<T>
                        auto generator = handler::detail::invoke_handler(
                            std::move(request_handler),
                            conn_ctx,
                            body_parser.get(),
                            context);
                        bool const ka = co_await detail::write_chunked_response(
                            generator, stream, version, keep_alive, cancellation_slot, &stream_error,
                            *response_filter_, &body_parser.get(),
                            body_parser.get().method() == http::verb::head);
                        if (stream_error) std::rethrow_exception(stream_error);
                        co_return ka;
                    }
                    else if constexpr (is_async_generator<result_type>)
                    {
                        // Sync handler returning async_generator<T>
                        auto generator = handler::detail::invoke_handler(
                            std::move(request_handler),
                            conn_ctx,
                            body_parser.get(),
                            context);
                        bool const ka = co_await detail::write_chunked_response(
                            generator, stream, version, keep_alive, cancellation_slot, &stream_error,
                            *response_filter_, &body_parser.get(),
                            body_parser.get().method() == http::verb::head);
                        if (stream_error) std::rethrow_exception(stream_error);
                        co_return ka;
                    }
                    else if constexpr (is_awaitable<result_type>)
                    {
                        if constexpr (is_chunked_response<typename result_type::value_type>)
                        {
                            // Async handler returning awaitable<chunked_response<T>>
                            auto generator = co_await handler::detail::invoke_handler(
                                std::move(request_handler),
                                conn_ctx,
                                body_parser.get(),
                                context);
                            bool const ka = co_await detail::write_chunked_response(
                                generator, stream, version, keep_alive, cancellation_slot, &stream_error,
                            *response_filter_, &body_parser.get(),
                            body_parser.get().method() == http::verb::head);
                            if (stream_error) std::rethrow_exception(stream_error);
                            co_return ka;
                        }
                        else if constexpr (is_async_generator<typename result_type::value_type>)
                        {
                            // Async handler returning awaitable<async_generator<T>>
                            auto generator = co_await handler::detail::invoke_handler(
                                std::move(request_handler),
                                conn_ctx,
                                body_parser.get(),
                                context);
                            bool const ka = co_await detail::write_chunked_response(
                                generator, stream, version, keep_alive, cancellation_slot, &stream_error,
                            *response_filter_, &body_parser.get(),
                            body_parser.get().method() == http::verb::head);
                            if (stream_error) std::rethrow_exception(stream_error);
                            co_return ka;
                        }
                        else
                        {
                            // Existing awaitable path
                            co_await detail::shield_from_cancellation();
                            auto response = co_await handler::detail::response_from_invoke_handler(
                                std::move(request_handler),
                                conn_ctx,
                                body_parser.get(),
                                context);
                            shutting_down = co_await detail::observe_shutdown();

                            detail::finish_response(response, version, keep_alive, shutting_down);
                            detail::apply_response_filter(*response_filter_, body_parser.get(), response);
                            bool const ka = response.keep_alive() && !shutting_down;
                            co_await detail::write_response(stream, std::move(response), body_parser.get().method() == http::verb::head);
                            co_return ka;
                        }
                    }
                    else
                    {
                        // Existing path: response_from_invoke -> async_write
                        co_await detail::shield_from_cancellation();
                        auto response = co_await handler::detail::response_from_invoke_handler(
                            std::move(request_handler),
                            conn_ctx,
                            body_parser.get(),
                            context);
                        shutting_down = co_await detail::observe_shutdown();

                        detail::finish_response(response, version, keep_alive, shutting_down);
                        detail::apply_response_filter(*response_filter_, body_parser.get(), response);
                        bool const ka = response.keep_alive() && !shutting_down;
                        co_await detail::write_response(stream, std::move(response), body_parser.get().method() == http::verb::head);
                        co_return ka;
                    }
                }
                catch (...)
                {
                    eptr = std::current_exception();
                }
                if (stream_error)
                {
                    // Headers and some chunks are already on the wire: no error
                    // response is possible. Surface it as a hard error instead.
                    std::rethrow_exception(stream_error);
                }
                shutting_down = co_await detail::observe_shutdown() || shutting_down;

                // An exception is thrown within the request handler.
                bool const ka = co_await wrapped_soft_error_handler_(
                    eptr,
                    stream,
                    body_parser.get(),
                    signals);
                co_return ka && !shutting_down;
                }
            }
        );
    }

    /** Register a handler and notify registration observers.

        Each trailing @a observer is invoked once, at registration time,
        with `(matcher::operand const&, RequestHandler const&)`, before the
        route is installed. Documentation generators use this to record
        the route's matcher metadata and the handler's parameters:

        @code
        session.register_request_handler(
            method == http::verb::get && target == "/users/{id}",
            taar::handler::rest(&get_user, taar::handler::path_arg("id")),
            openapi.describe("Fetch a user"));
        @endcode
    */
    template <typename MatcherType, typename RequestHandler, typename FirstObserver, typename... Observers> requires(
        matcher::is_matcher<std::decay_t<MatcherType>> &&
        std::is_move_constructible_v<std::decay_t<RequestHandler>> &&
        detail::registration_observer<FirstObserver, MatcherType, RequestHandler> &&
        (detail::registration_observer<Observers, MatcherType, RequestHandler> && ...))
    auto register_request_handler(
        MatcherType&& matcher,
        RequestHandler request_handler,
        FirstObserver&& first_observer,
        Observers&&... observers)
    {
        matcher::operand operand {std::forward<MatcherType>(matcher)};
        std::forward<FirstObserver>(first_observer)(std::as_const(operand), std::as_const(request_handler));
        (std::forward<Observers>(observers)(std::as_const(operand), std::as_const(request_handler)), ...);
        return register_request_handler(std::move(operand), std::move(request_handler));
    }

    /// Convenience overload that binds a non-const member-function pointer to @a object.
    template <typename MatcherType, typename ObjectType, typename ResultType, typename... ArgsType>
    auto register_request_handler(
        MatcherType&& matcher,
        ResultType(ObjectType::*memfn)(ArgsType...),
        ObjectType* object)
    {
        return register_request_handler(
            std::forward<MatcherType>(matcher),
            [memfn, object](ArgsType&&... args) mutable
            {
                return (object->*memfn)(std::forward<ArgsType>(args)...);
            }
        );
    }

    /// Convenience overload that binds a const member-function pointer to @a object.
    template <typename MatcherType, typename ObjectType, typename ResultType, typename... ArgsType>
    auto register_request_handler(
        MatcherType&& matcher,
        ResultType(ObjectType::*memfn)(ArgsType...) const,
        ObjectType const* object)
    {
        return register_request_handler(
            std::forward<MatcherType>(matcher),
            [memfn, object](ArgsType&&... args)
            {
                return (object->*memfn)(std::forward<ArgsType>(args)...);
            }
        );
    }

    /** Register a "raw" handler that takes ownership of the connection on match.

        Used for protocol upgrades such as WebSocket. On a match the handler
        receives the parsed request header, the underlying stream (a
        `tcp_stream`, or the TLS stream for an `https` session),
        and the `flat_buffer` holding any bytes already read past the
        header. The session performs no further I/O on the stream and ends
        the session as soon as the handler returns.

        The handler signature must be compatible with:

        @code
        awaitable<void>(
            matcher::context const& context,
            boost::beast::http::request<boost::beast::http::empty_body>&& request,
            Stream&& stream,
            boost::beast::flat_buffer&& buffer,
            cancellation_signals& signals)
        @endcode

        @note `boost::beast::flat_buffer` is value-semantic. Passing it
              directly to `asio::async_read_until` / `asio::async_read`
              would read into a copy rather than the buffer itself. Pair
              the buffer with `beast::websocket::stream::async_accept`
              (the natural use case) or drain pipelined bytes into a
              `std::string` and use `boost::asio::dynamic_buffer` for
              further reads.
    */
    template <typename MatcherType, typename RawHandler> requires(
        matcher::is_matcher<std::decay_t<MatcherType>> &&
        std::is_move_constructible_v<std::decay_t<RawHandler>>)
    auto register_raw_handler(
        MatcherType&& matcher,
        RawHandler raw_handler)
    {
        namespace http = boost::beast::http;

        matcher::operand operand {std::forward<MatcherType>(matcher)};
        needs_parsed_target_ |= decltype(operand)::with_parsed_target;
        needs_parsed_cookies_ |= decltype(operand)::with_parsed_cookies;
        record_route_methods(operand);

        matcher_handlers_.emplace_back(
            [this, operand = std::move(operand)](
                http::request_header<> const& request,
                matcher::context& context,
                boost::urls::url_view const& parsed_target,
                cookies const& parsed_cookies)
            {
                return operand(request, context, parsed_target, parsed_cookies);
            },
            [raw_handler = std::move(raw_handler)](
                matcher::context const& context,
                Stream& stream,
                boost::beast::flat_buffer& buffer,
                boost::beast::http::request_parser<boost::beast::http::buffer_body>& header_parser,
                cancellation_signals& signals,
                detail::contexts_for<Context> const& /*conn_ctx*/) mutable
            -> awaitable<bool>
            {
                // Move the parsed header into a request<empty_body>. The raw
                // handler is taking over and owns the protocol state from
                // here on (e.g. websocket::stream::async_accept will read
                // and write its own framing).
                http::request<http::empty_body> request {
                    std::move(header_parser.get())};

                co_await raw_handler(
                    context,
                    std::move(request),
                    std::move(stream),
                    std::move(buffer),
                    signals);

                // Always close the session: the handler now owns the socket,
                // and the session's loop must not read or write again.
                co_return false;
            });
    }

    /** Install a custom soft-error handler.

        The handler is invoked when a registered request handler throws,
        with the captured `std::exception_ptr`. Its return value is fed to
        @ref boost::taar::response_from, written to the client, and
        determines whether the session continues (`keep_alive() == true`)
        or closes.

        Override this when the default 400-with-message behaviour is not
        what you want, e.g. to map specific exceptions to specific status
        codes, return JSON-formatted errors, or include a request id.
    */
    template <detail::soft_error_handler HandlerType>
    void set_soft_error_handler(HandlerType handler)
    {
        wrapped_soft_error_handler_ =
            [filter = response_filter_, handler = std::move(handler)](
                std::exception_ptr eptr,
                Stream& stream,
                boost::beast::http::request_header<>& request_header,
                cancellation_signals& signals) mutable
            -> awaitable<bool>
            {
                auto response = co_await response_from_invoke(handler, eptr);
                detail::finish_response(response, request_header.version(), detail::header_keep_alive(request_header), false);
                detail::apply_response_filter(*filter, request_header, response);
                bool keep_alive = response.keep_alive();
                co_await detail::write_response(stream, std::move(response), request_header.method() == boost::beast::http::verb::head);
                co_return keep_alive;
            };
    }

    /** Install a custom hard-error handler.

        Invoked when the session aborts because of a connection-level
        failure (a transport error, a header that is too large, an I/O
        exception). The handler receives the `std::exception_ptr` and is
        expected to log or otherwise report it; the connection is already
        being torn down by the time the handler runs.

        The default handler silently discards the exception, which is
        almost never the right choice in production; install at least a
        logging hook.
    */
    template <detail::hard_error_handler HandlerType>
    void set_hard_error_handler(HandlerType handler)
    {
        hard_error_handler_ = std::move(handler);
    }

private:
/// @cond
    std::vector<matcher_handler_type> matcher_handlers_;
    soft_error_handler_wrapper_type wrapped_soft_error_handler_;
    hard_error_handler_type hard_error_handler_ = [](std::exception_ptr){};
    /// Shared with the error-handler wrappers so they stay valid across moves of this object.
    std::shared_ptr<detail::response_filter_type> response_filter_ = std::make_shared<detail::response_filter_type>();

    /// Answer a protocol-level failure (malformed request, header or body too large) before closing.
    awaitable<void> write_protocol_error(
        Stream& stream,
        boost::system::error_code ec,
        boost::beast::http::request_header<> const& header)
    {
        namespace http = boost::beast::http;
        unsigned const version = header.version() == 10 ? 10u : 11u;
        http::response<http::empty_body> err_response {detail::select_error_status(ec), version};
        err_response.keep_alive(false);
        err_response.prepare_payload();
        detail::apply_response_filter(*response_filter_, header, err_response);
        auto [wec, wsz] = co_await http::async_write(stream, err_response);
        (void)wec;
        (void)wsz;
    }

    /// Methods a route accepts, with a matcher for its path alone (for 405 and HEAD).
    struct route_methods_type
    {
        std::function<bool(
            boost::beast::http::request_header<> const&,
            matcher::context&,
            boost::urls::url_view const&,
            cookies const&)> target_matches;
        std::vector<std::string> methods;
    };
    std::vector<route_methods_type> route_methods_;
    bool method_not_allowed_enabled_ = true;
    bool head_from_get_ = true;

    template <typename Operand>
    void record_route_methods(Operand const& operand)
    {
        if constexpr (matcher::route_expressible_v<typename Operand::metadata_type>)
        {
            auto summary = matcher::summarize_route(operand);
            auto path_only = matcher::operand {matcher::target == std::string_view {summary.template_str}};
            route_methods_.push_back({
                [path_only = std::move(path_only)](
                    boost::beast::http::request_header<> const& request,
                    matcher::context& context,
                    boost::urls::url_view const& parsed_target,
                    cookies const& parsed_cookies) mutable
                {
                    return path_only(request, context, parsed_target, parsed_cookies);
                },
                std::move(summary.methods)});
            needs_parsed_target_ = true;
        }
    }

    /// `Allow` header value for @a request's path, empty if no route knows the path.
    std::string allowed_methods(
        boost::beast::http::request_header<> const& request,
        boost::urls::url_view const& parsed_target,
        cookies const& parsed_cookies)
    {
        std::vector<std::string> methods;
        matcher::context scratch;
        for (auto& route : route_methods_)
        {
            scratch.path_args.clear();
            if (!route.target_matches(request, scratch, parsed_target, parsed_cookies)) continue;
            for (auto const& m : route.methods)
            {
                if (std::ranges::find(methods, m) == methods.end()) methods.push_back(m);
            }
        }
        if (head_from_get_ &&
            std::ranges::find(methods, "GET") != methods.end() &&
            std::ranges::find(methods, "HEAD") == methods.end())
        {
            methods.push_back("HEAD");
        }
        // The method itself is fine but another constraint (header, cookie,
        // version) rejected the request: that is a 404, not a 405.
        if (std::ranges::find(methods, std::string {request.method_string()}) != methods.end())
        {
            return {};
        }
        std::string allow;
        for (auto const& m : methods)
        {
            if (!allow.empty()) allow += ", ";
            allow += m;
        }
        return allow;
    }
    std::uint32_t request_header_limit_ = 8192;
    boost::optional<std::uint64_t> request_body_limit_ = 1 * 1024 * 1024;
    bool needs_parsed_target_ = false;
    bool needs_parsed_cookies_ = false;
/// @endcond
};

/** Default HTTP session with no per-connection context.

    Equivalent to `basic_http<no_context>`: no object is created on
    accept, and handlers cannot request a connection context. Keyed
    contexts added by `handler::with_session` are still available.
 */
using http = basic_http<>;

} // namespace boost::taar::session

#endif // BOOST_TAAR_SESSION_HTTP_HPP
