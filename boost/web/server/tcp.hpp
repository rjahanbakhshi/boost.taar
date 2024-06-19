//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_SERVER_TCP_HPP
#define BOOST_WEB_SERVER_TCP_HPP

#include <boost/web/core/awaitable.hpp>
#include <boost/web/core/cancellation_signals.hpp>
#include <boost/web/core/rebind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <system_error>
#include <string>

namespace boost::web::server {

// tcp server coroutine to be spawned for each tcp server instance.
template <typename SessionHandler> // TODO: SessionHandler concept for callable with correct syntax
[[nodiscard]] awaitable<void> tcp(
    std::string bind_host,
    std::string bind_port,
    const SessionHandler& session_handler,
    cancellation_signals& signals,
    std::function<void(const boost::asio::ip::tcp::endpoint&)> local_endpoint_handler = nullptr)
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
    acceptor.open(query->endpoint().protocol());

    // Allow address reuse
    acceptor.set_option(net::socket_base::reuse_address(true));

    // Bind to the server address
    acceptor.bind(query->endpoint());

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
            const auto executor = socket.get_executor();
            co_spawn(
                executor,
                session_handler(std::move(socket), signals),
                net::bind_cancellation_slot(signals.slot(), detached));
        }
    }
}

} // namespace boost::web::server

#endif // BOOST_WEB_SERVER_TCP_HPP
