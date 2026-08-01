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
#include <boost/taar/core/rebind_executor.hpp>
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

    @param bind_host          Host or address to bind.
    @param bind_port          Port string; `"0"` lets the OS pick.
    @param tls                The TLS context holding certificate, key and
                              options. Must outlive the server.
    @param session_handler    Invoked as @c session_handler(tls_stream, signals).
    @param signals            Cancellation pool shared with the sessions.
    @param local_endpoint_handler Optional callback with the bound endpoint.
    @param handshake_timeout  Upper bound for the TLS handshake.

    @throws std::system_error if name resolution fails.
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
    namespace net = boost::asio;

    co_await detail::accept_loop(
        std::move(bind_host),
        std::move(bind_port),
        signals,
        std::move(local_endpoint_handler),
        [&](rebind_executor<net::ip::tcp::socket> socket)
        {
            auto const executor = socket.get_executor();
            net::co_spawn(
                executor,
                [&tls, &session_handler, &signals, handshake_timeout, socket = std::move(socket)]() mutable
                    -> awaitable<void>
                {
                    session::tls_stream stream {
                        rebind_executor<boost::beast::tcp_stream> {std::move(socket)}, tls};
                    boost::beast::get_lowest_layer(stream).expires_after(handshake_timeout);
                    auto [ec] = co_await stream.async_handshake(
                        net::ssl::stream_base::server, net::as_tuple(use_awaitable));
                    if (ec)
                    {
                        co_return;
                    }
                    boost::beast::get_lowest_layer(stream).expires_never();
                    co_await session_handler(std::move(stream), signals);
                },
                net::bind_cancellation_slot(signals.slot(), net::detached));
        });
}

} // namespace boost::taar::server

#endif // BOOST_TAAR_SERVER_TLS_HPP
