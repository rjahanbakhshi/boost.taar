//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_SERVER_TCP_HPP
#define BOOST_TAAR_SERVER_TCP_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <system_error>
#include <string>

namespace boost::taar::server {

/** Coroutine that binds, listens, and accepts TCP connections.

    Spawn one instance of this coroutine per listen endpoint. It resolves
    @a bind_host / @a bind_port, opens an acceptor with `SO_REUSEADDR`,
    starts listening on `max_listen_connections`, and then loops accepting
    connections until cancellation is requested through @a signals.

    Each accepted socket is forwarded to @a session_handler on the executor
    of the new socket. The handler is responsible for the full lifetime of
    the connection — typically by invoking a
    @ref boost::taar::session::http instance.

    @param bind_host             Host or address to bind. `"0.0.0.0"` and
                                 `"::"` accept on all interfaces.
    @param bind_port             Port string. The empty string asks the OS
                                 to pick a port; use @a local_endpoint_handler
                                 to learn the chosen port.
    @param session_handler       A callable invoked as
                                 @c session_handler(socket, signals). Usually
                                 a @ref boost::taar::session::http instance
                                 by reference.
    @param signals               Cancellation pool. The same pool is shared
                                 with spawned sessions so a single
                                 @ref cancellation_signals::emit shuts the
                                 server down.
    @param local_endpoint_handler Optional callback invoked with the
                                 resolved local endpoint after `bind()`.

    @throws std::system_error if name resolution fails.
*/
template <typename SessionHandler> // TODO: SessionHandler concept for callable with correct syntax
[[nodiscard]] awaitable<void> tcp(
    std::string bind_host,
    std::string bind_port,
    SessionHandler&& session_handler,
    cancellation_signals& signals,
    std::function<void(boost::asio::ip::tcp::endpoint const&)> local_endpoint_handler = nullptr)
{
    namespace net = boost::asio;
    namespace this_coro = net::this_coro;
    using net::ip::tcp;
    using net::co_spawn;
    using net::detached;

    rebind_executor<tcp::resolver> resolver {co_await this_coro::executor};
    auto [ec, query] = co_await resolver.async_resolve(bind_host, bind_port);
    if (ec)
    {
        throw std::system_error {ec};
    }

    rebind_executor<tcp::acceptor> acceptor {co_await this_coro::executor};

    // Open the acceptor
    acceptor.open(query.begin()->endpoint().protocol());

    // Allow address reuse
    acceptor.set_option(net::socket_base::reuse_address(true));

    // Bind to the server address
    acceptor.bind(query.begin()->endpoint());

    // Start listening for connections
    acceptor.listen(net::socket_base::max_listen_connections);

    if (local_endpoint_handler)
    {
        local_endpoint_handler(acceptor.local_endpoint());
    }

    for (auto cs = co_await this_coro::cancellation_state;
         cs.cancelled() == net::cancellation_type::none;
         cs = co_await this_coro::cancellation_state)
    {
        auto [ec, socket] = co_await acceptor.async_accept();
        if (!ec)
        {
            auto const executor = socket.get_executor();
            co_spawn(
                executor,
                std::forward<SessionHandler>(session_handler)(std::move(socket), signals),
                net::bind_cancellation_slot(signals.slot(), detached));
        }
    }
}

} // namespace boost::taar::server

#endif // BOOST_TAAR_SERVER_TCP_HPP
