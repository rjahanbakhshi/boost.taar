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

#include <boost/asio/async_result.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
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
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/type_traits/callable.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/http/chunk_encode.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/system_error.hpp>
#include <boost/url/url_view.hpp>
#include <functional>
#include <type_traits>
#include <vector>
#include <utility>
#include <exception>
#include <cstdint>

namespace boost::taar::session {
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

inline bool is_transport_error(boost::system::error_code const& ec)
{
    namespace asio_err = boost::asio::error;
    namespace http_err = boost::beast::http;
    return ec == asio_err::eof ||
           ec == asio_err::connection_reset ||
           ec == asio_err::operation_aborted ||
           ec == http_err::error::end_of_stream;
}

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
    ::boost::asio::cancellation_slot cancellation_slot = {})
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
    header_response.set(http::field::transfer_encoding, "chunked");

    http::response_serializer<http::empty_body> serializer {header_response};
    auto [header_ec, header_sz] = co_await http::async_write_header(
        stream, serializer, net::as_tuple(net::deferred));
    if (header_ec)
        co_return false;

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
    // rather than a falsely-successful one.
    try
    {
        generator.rethrow_if_exception();
    }
    catch (...)
    {
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
*/
class http
{
private:
    using matcher_type = std::move_only_function<
        bool(
            boost::beast::http::request_header<> const&,
            matcher::context&,
            boost::urls::url_view const&,
            cookies const&)>;

    using request_handler_wrapper_type = std::move_only_function<
        awaitable<bool>(
            matcher::context const&,
            rebind_executor<boost::beast::tcp_stream>&,
            boost::beast::flat_buffer&,
            boost::beast::http::request_parser<boost::beast::http::buffer_body>&,
            cancellation_signals&)>;

    struct matcher_handler_type
    {
        matcher_type matcher;
        request_handler_wrapper_type handler;
    };

    using soft_error_handler_wrapper_type = std::move_only_function<
        awaitable<bool>(
            std::exception_ptr,
            rebind_executor<boost::beast::tcp_stream>&,
            boost::beast::http::request_header<>&,
            cancellation_signals&)>;

    using hard_error_handler_type = std::move_only_function<void(std::exception_ptr)>;

public:
    http()
        : wrapped_soft_error_handler_ {
            [](
                std::exception_ptr eptr,
                rebind_executor<boost::beast::tcp_stream>& stream,
                boost::beast::http::request_header<>& req,
                cancellation_signals&) -> awaitable<bool>
            {
                std::exception_ptr ex;
                std::string error_msg;
                try
                {
                    std::rethrow_exception(eptr);
                }
                catch(boost::system::system_error const& e)
                {
                    if (e.code().category() == error_category())
                    {
                        error_msg = e.code().message();
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
                    response.result(boost::beast::http::status::bad_request);
                    bool keep_alive = response.keep_alive();
                    co_await detail::async_write(stream, std::move(response));
                    co_return keep_alive;
                }

                std::rethrow_exception(eptr);
            }
        }
    {}

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

    http(http const&) = delete;
    http(http&&) = default;
    http& operator=(http const&) = delete;
    http& operator=(http&&) = default;
    ~http() = default;

    /** Drive one accepted connection to completion.

        Called by @ref boost::taar::server::tcp for every accepted socket.
        Owns the socket, parses request headers, dispatches to matching
        handlers, writes responses, honours keep-alive, and terminates on
        cancellation, peer close, or after a non-keep-alive response.
    */
    awaitable<void> operator()(
        rebind_executor<boost::asio::ip::tcp::socket> socket,
        cancellation_signals& signals)
    {
        namespace net = boost::asio;
        namespace http = boost::beast::http;
        namespace this_coro = net::this_coro;
        using boost::beast::tcp_stream;
        using boost::beast::flat_buffer;

        rebind_executor<tcp_stream> stream {std::move(socket)};

        try
        {
            for (auto cs = co_await this_coro::cancellation_state;
                cs.cancelled() == net::cancellation_type::none;
                cs = co_await this_coro::cancellation_state)
            {
                // Read and parse the header and use the target to find the handler.
                flat_buffer buffer;
                http::request_parser<http::buffer_body> header_parser;
                header_parser.header_limit(request_header_limit_);
                header_parser.body_limit(request_body_limit_);

                auto [header_ec, header_sz] = co_await http::async_read_header(
                    stream,
                    buffer,
                    header_parser);

                if (header_ec)
                {
                    if (!detail::is_transport_error(header_ec))
                    {
                        // Protocol error. Send an appropriate error response.
                        http::response<http::empty_body> err_response{
                            detail::select_error_status(header_ec), 11};
                        err_response.keep_alive(false);
                        err_response.prepare_payload();
                        auto [wec, wsz] = co_await http::async_write(stream, err_response);
                        (void)wec;
                        (void)wsz;
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
                        signals))
                    {
                        // Handler instructed to close the session.
                        break;
                    }

                    // Done with this request. Proceeding with the next one.
                    continue;
                }

                // No handler found for this request. Reply with not found error.
                // But before sending the error response, it is necessary to read
                // the full buffer.
                auto keep_alive = req_header.keep_alive();
                auto version = req_header.version();
                if (req_header.has_content_length() || req_header.chunked())
                {
                    http::request_parser<http::string_body> body_parser {std::move(header_parser)};
                    body_parser.body_limit(request_body_limit_);
                    auto [req_ec, req_sz] = co_await async_read(
                        stream,
                        buffer,
                        body_parser);

                    if (req_ec)
                    {
                        if (!detail::is_transport_error(req_ec))
                        {
                            // Protocol error. Send an appropriate error response.
                            http::response<http::empty_body> err_response{
                                detail::select_error_status(req_ec), version};
                            err_response.keep_alive(false);
                            err_response.prepare_payload();
                            auto [wec, wsz] = co_await http::async_write(stream, err_response);
                            (void)wec;
                            (void)wsz;
                        }
                        // Invoke the hard error handler for server-side visibility.
                        throw boost::system::system_error{req_ec};
                    }
                    // Drain the body so the connection stays in keep-alive
                    // state; the body itself is discarded.
                    (void)body_parser.get();
                }

                // Stock not-found response.
                http::response<http::empty_body> response {http::status::not_found, version};
                response.keep_alive(keep_alive);
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

        // Send a TCP shutdown
        boost::system::error_code ec;
        stream.socket().shutdown(net::ip::tcp::socket::shutdown_send, ec); // NOLINT
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
        needs_parsed_target_ |= decltype(operand)::with_parsed_target;
        needs_parsed_cookies_ |= decltype(operand)::with_parsed_cookies;

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
                rebind_executor<boost::beast::tcp_stream>& stream,
                boost::beast::flat_buffer& buffer,
                boost::beast::http::request_parser<boost::beast::http::buffer_body>& header_parser,
                cancellation_signals& signals) mutable
            -> awaitable<bool>
            {
                using request_type = std::remove_cvref_t<type_traits::callable_arg<RequestHandler, 0>>;
                using body_type = request_type::body_type;
                using result_type = type_traits::callable_result<RequestHandler>;

                http::request_parser<body_type> body_parser {std::move(header_parser)};
                body_parser.body_limit(request_body_limit_);
                auto [req_ec, req_sz] = co_await async_read(stream, buffer, body_parser);

                auto version = body_parser.get().version();
                auto keep_alive = body_parser.get().keep_alive();

                if (req_ec)
                {
                    if (!detail::is_transport_error(req_ec))
                    {
                        // Protocol error. Send an appropriate error response.
                        http::response<http::empty_body> err_response{
                            detail::select_error_status(req_ec), version};
                        err_response.keep_alive(false);
                        err_response.prepare_payload();
                        auto [wec, wsz] = co_await http::async_write(stream, err_response);
                        (void)wec;
                        (void)wsz;
                    }
                    // Invoke the hard error handler for server-side visibility.
                    throw boost::system::system_error{req_ec};
                }

                // Get cancellation slot from current coroutine for request-scoped cancellation
                auto cs = co_await boost::asio::this_coro::cancellation_state;
                auto cancellation_slot = cs.slot();

                std::exception_ptr eptr;
                try
                {
                    if constexpr (is_chunked_response<result_type>)
                    {
                        // Sync handler returning chunked_response<T>
                        auto generator = std::invoke(
                            std::move(request_handler),
                            body_parser.get(),
                            context);
                        co_return co_await detail::write_chunked_response(
                            generator, stream, version, keep_alive, cancellation_slot);
                    }
                    else if constexpr (is_async_generator<result_type>)
                    {
                        // Sync handler returning async_generator<T>
                        auto generator = std::invoke(
                            std::move(request_handler),
                            body_parser.get(),
                            context);
                        co_return co_await detail::write_chunked_response(
                            generator, stream, version, keep_alive, cancellation_slot);
                    }
                    else if constexpr (is_awaitable<result_type>)
                    {
                        if constexpr (is_chunked_response<typename result_type::value_type>)
                        {
                            // Async handler returning awaitable<chunked_response<T>>
                            auto generator = co_await std::invoke(
                                std::move(request_handler),
                                body_parser.get(),
                                context);
                            co_return co_await detail::write_chunked_response(
                                generator, stream, version, keep_alive, cancellation_slot);
                        }
                        else if constexpr (is_async_generator<typename result_type::value_type>)
                        {
                            // Async handler returning awaitable<async_generator<T>>
                            auto generator = co_await std::invoke(
                                std::move(request_handler),
                                body_parser.get(),
                                context);
                            co_return co_await detail::write_chunked_response(
                                generator, stream, version, keep_alive, cancellation_slot);
                        }
                        else
                        {
                            // Existing awaitable path
                            auto response = co_await response_from_invoke(
                                std::move(request_handler),
                                body_parser.get(),
                                context);

                            bool keep_alive = response.keep_alive();
                            co_await detail::async_write(stream, std::move(response));
                            co_return keep_alive;
                        }
                    }
                    else
                    {
                        // Existing path: response_from_invoke -> async_write
                        auto response = co_await response_from_invoke(
                            std::move(request_handler),
                            body_parser.get(),
                            context);

                        bool keep_alive = response.keep_alive();
                        co_await detail::async_write(stream, std::move(response));
                        co_return keep_alive;
                    }
                }
                catch (...)
                {
                    eptr = std::current_exception();
                }

                // An exception is thrown within the request handler.
                co_return co_await wrapped_soft_error_handler_(
                    eptr,
                    stream,
                    body_parser.get(),
                    signals);
            }
        );
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
        receives the parsed request header, the underlying `tcp_stream`,
        and the `flat_buffer` holding any bytes already read past the
        header. The session performs no further I/O on the stream and ends
        the session as soon as the handler returns.

        The handler signature must be compatible with:

        @code
        awaitable<void>(
            matcher::context const& context,
            boost::beast::http::request<boost::beast::http::empty_body>&& request,
            rebind_executor<boost::beast::tcp_stream>&& stream,
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
                rebind_executor<boost::beast::tcp_stream>& stream,
                boost::beast::flat_buffer& buffer,
                boost::beast::http::request_parser<boost::beast::http::buffer_body>& header_parser,
                cancellation_signals& signals) mutable
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
            [handler = std::move(handler)](
                std::exception_ptr eptr,
                rebind_executor<boost::beast::tcp_stream>& stream,
                boost::beast::http::request_header<>& request_header,
                cancellation_signals& signals) mutable
            -> awaitable<bool>
            {
                auto response = co_await response_from_invoke(handler, eptr);
                bool keep_alive = response.keep_alive();
                co_await detail::async_write(stream, std::move(response));
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
    std::vector<matcher_handler_type> matcher_handlers_;
    soft_error_handler_wrapper_type wrapped_soft_error_handler_;
    hard_error_handler_type hard_error_handler_ = [](std::exception_ptr){};
    std::uint32_t request_header_limit_ = 8192;
    boost::optional<std::uint64_t> request_body_limit_ = 1 * 1024 * 1024;
    bool needs_parsed_target_ = false;
    bool needs_parsed_cookies_ = false;
};

} // namespace boost::taar::session

#endif // BOOST_TAAR_SESSION_HTTP_HPP
