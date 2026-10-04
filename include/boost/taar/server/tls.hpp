//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_SERVER_TLS_HPP
#define BOOST_TAAR_SERVER_TLS_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/https.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <chrono>
#include <type_traits>
#include <functional>
#include <string>
#include <utility>

namespace boost::taar::server {

/** Coroutine that accepts TCP connections and runs a TLS handshake on each
    before handing the encrypted stream to @a session_handler.

    Same binding and cancellation behaviour as @ref tcp. A connection whose
    handshake fails or takes longer than @a handshake_timeout is dropped
    without involving the session. The session receives a
    @ref boost::taar::session::tls_stream, so pair this acceptor with
    @ref boost::taar::session::https (or `basic_https<Context>`).

    @param connection_executor The executor every connection strand is made
                              from, normally the `io_context` (or thread
                              pool) executor itself; see @ref tcp.
    @param bind_host          Host or address to bind.
    @param bind_port          Port string; `"0"` lets the OS pick.
    @param tls                The TLS context holding certificate, key and
                              options. Must outlive the server.
    @param session_handler    Invoked as @c session_handler(tls_stream, signals).
    @param signals            Cancellation pool shared with the sessions; see @ref tcp for threading.
    @param local_endpoint_handler Optional callback with the bound endpoint.
    @param handshake_timeout  Upper bound for the TLS handshake.

    @throws boost::taar::exception carrying the system error code if
            name resolution, binding or listening fails.
*/
template <typename SessionHandler>
[[nodiscard]] awaitable<void> tls(
    boost::asio::any_io_executor connection_executor,
    std::string bind_host,
    std::string bind_port,
    boost::asio::ssl::context& tls,
    SessionHandler&& session_handler,
    cancellation_signals& signals,
    std::function<void(boost::asio::ip::tcp::endpoint const&)> local_endpoint_handler = nullptr,
    std::chrono::steady_clock::duration handshake_timeout = std::chrono::seconds {30})
{
    namespace net = boost::asio;
    static_assert(std::is_lvalue_reference_v<SessionHandler>,
        "server::tls keeps a reference to the session handler for as long as it "
        "accepts connections: pass an object that outlives the server, not a temporary.");

    co_await detail::run_on_strand(signals, detail::accept_loop(
        std::move(bind_host),
        std::move(bind_port),
        std::move(connection_executor),
        std::move(local_endpoint_handler),
        [&](net::ip::tcp::socket socket, net::any_io_executor const& strand)
        {
            detail::spawn_session(
                strand, signals,
                [&tls, &session_handler, &signals, handshake_timeout, socket = std::move(socket)]() mutable
                    -> awaitable<void>
                {
                    session::tls_stream stream {
                        boost::beast::tcp_stream {std::move(socket)}, tls};
                    boost::beast::get_lowest_layer(stream).expires_after(handshake_timeout);
                    auto [ec] = co_await stream.async_handshake(
                        net::ssl::stream_base::server, use_awaitable_tuple);
                    if (ec)
                    {
                        co_return;
                    }
                    boost::beast::get_lowest_layer(stream).expires_never();
                    co_await session_handler(std::move(stream), signals);
                });
        }));
}

/** Coroutine that accepts TLS connections, making connection strands
    from the executor it is spawned on.

    Equivalent to the overload taking a `connection_executor`, with
    `co_await this_coro::executor` in its place. Spawn it on the
    `io_context` (or thread pool) executor itself; spawned on a strand,
    every connection would be serialised through that strand, so use the
    overload taking an explicit `connection_executor` instead.

    @throws boost::taar::exception carrying the system error code if
            name resolution, binding or listening fails.
*/
template <typename SessionHandler>
[[nodiscard]] awaitable<void> tls(
    std::string bind_host,
    std::string bind_port,
    boost::asio::ssl::context& tls,
    SessionHandler&& session_handler,
    cancellation_signals& signals,
    std::function<void(boost::asio::ip::tcp::endpoint const&)> local_endpoint_handler = nullptr,
    std::chrono::steady_clock::duration handshake_timeout = std::chrono::seconds {30})
{
    static_assert(std::is_lvalue_reference_v<SessionHandler>,
        "server::tls keeps a reference to the session handler for as long as it "
        "accepts connections: pass an object that outlives the server, not a temporary.");

    co_await server::tls(
        co_await boost::asio::this_coro::executor,
        std::move(bind_host),
        std::move(bind_port),
        tls,
        session_handler,
        signals,
        std::move(local_endpoint_handler),
        handshake_timeout);
}

} // namespace boost::taar::server

#endif // BOOST_TAAR_SERVER_TLS_HPP
