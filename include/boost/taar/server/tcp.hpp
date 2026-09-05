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
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <type_traits>
#include <system_error>
#include <string>

namespace boost::taar::server {

/** Coroutine that binds, listens, and accepts TCP connections.

    Spawn one instance of this coroutine per listen endpoint. It resolves
    @a bind_host / @a bind_port, opens an acceptor with `SO_REUSEADDR`,
    starts listening on `max_listen_connections`, and then loops accepting
    connections until cancellation is requested through @a signals.

    Each accepted socket is forwarded to @a session_handler on its own
    strand, so the `io_context` may be run by several threads. The handler
    is responsible for the full lifetime of the connection, typically by
    invoking a @ref boost::taar::session::http instance.

    The acceptor and every connection register themselves with @a signals,
    and `signals.emit()` may be called from any thread. A cancellation slot
    bound to this coroutine by its spawner also stops it (safely, through
    the acceptor's strand), though it is no longer needed.

    @param bind_host             Host or address to bind. `"0.0.0.0"` and
                                 `"::"` accept on all interfaces.
    @param bind_port             Port string. `"0"` asks the OS to pick a port; use @a local_endpoint_handler
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
namespace detail {

/// Cancellation filter that re-emits the spawner's cancellation through a strand-bound slot.
struct forward_cancellation
{
    std::shared_ptr<cancellation_signals::bound_slot> target;

    boost::asio::cancellation_type_t operator()(boost::asio::cancellation_type_t type) const
    {
        if (type != boost::asio::cancellation_type::none) target->emit(type);
        return boost::asio::cancellation_type::none;
    }
};

/** Run @a loop on a fresh strand registered with @a signals.

    A cancellation bound to this coroutine by its spawner is not applied
    inline (it would run on the emitting thread, racing with the loop's
    operations) but re-emitted through the strand, so both the pool and
    the spawner's own slot stop the loop safely.
*/
template <typename Loop>
awaitable<void> run_on_strand(cancellation_signals& signals, Loop loop)
{
    namespace net = boost::asio;
    auto strand = net::make_strand(co_await net::this_coro::executor);
    auto bound = std::make_shared<cancellation_signals::bound_slot>(signals.slot(strand));
    co_await net::this_coro::reset_cancellation_state(
        forward_cancellation {bound}, net::cancellation_filter<net::cancellation_type::none> {});
    co_await net::co_spawn(strand, std::move(loop), net::bind_cancellation_slot(bound->slot(), use_awaitable));
}

/// Spawn @a session on @a strand with a shutdown slot from @a signals.
template <typename Strand, typename Session>
void spawn_session(Strand const& strand, cancellation_signals& signals, Session&& session)
{
    namespace net = boost::asio;
    auto bound = signals.slot(strand);
    auto const slot = bound.slot();
    net::co_spawn(
        strand,
        std::forward<Session>(session),
        net::bind_cancellation_slot(slot, [bound = std::move(bound)](std::exception_ptr) {}));
}

/** Bind, listen and call `on_socket(socket, strand)` for every accepted
    connection until cancelled. Each socket is moved onto a fresh strand
    made from @a base (the io_context executor, not the acceptor's strand)
    so that connections run in parallel when several threads drive the
    io_context.
*/
template <typename OnSocket>
awaitable<void> accept_loop(
    std::string bind_host,
    std::string bind_port,
    boost::asio::any_io_executor base,
    std::function<void(boost::asio::ip::tcp::endpoint const&)> local_endpoint_handler,
    OnSocket on_socket)
{
    namespace net = boost::asio;
    namespace this_coro = net::this_coro;
    using net::ip::tcp;

    rebind_executor<tcp::resolver> resolver {co_await this_coro::executor};
    auto [ec, query] = co_await resolver.async_resolve(bind_host, bind_port);
    if (ec)
    {
        throw std::system_error {ec};
    }

    rebind_executor<tcp::acceptor> acceptor {co_await this_coro::executor};
    acceptor.open(query.begin()->endpoint().protocol());
    acceptor.set_option(net::socket_base::reuse_address(true));
    acceptor.bind(query.begin()->endpoint());
    acceptor.listen(net::socket_base::max_listen_connections);

    if (local_endpoint_handler)
    {
        local_endpoint_handler(acceptor.local_endpoint());
    }

    for (auto cs = co_await this_coro::cancellation_state;
         cs.cancelled() == net::cancellation_type::none;
         cs = co_await this_coro::cancellation_state)
    {
        auto [aec, socket] = co_await acceptor.async_accept();
        if (aec)
        {
            // A persistent failure (descriptor exhaustion, no buffers) would
            // otherwise spin this loop; wait a little before trying again.
            if (aec == net::error::operation_aborted) continue;
            net::steady_timer backoff {co_await this_coro::executor};
            backoff.expires_after(std::chrono::milliseconds {100});
            auto [tec] = co_await backoff.async_wait(use_awaitable_tuple);
            (void)tec;
            continue;
        }
        {
            auto strand = net::make_strand(base);
            auto const protocol = socket.local_endpoint().protocol();
            rebind_executor<tcp::socket> on_strand {strand, protocol, socket.release()};
            on_socket(std::move(on_strand), strand);
        }
    }
}

} // namespace detail

template <typename SessionHandler> // TODO: SessionHandler concept for callable with correct syntax
[[nodiscard]] awaitable<void> tcp(
    std::string bind_host,
    std::string bind_port,
    SessionHandler&& session_handler,
    cancellation_signals& signals,
    std::function<void(boost::asio::ip::tcp::endpoint const&)> local_endpoint_handler = nullptr)
{
    namespace net = boost::asio;
    static_assert(std::is_lvalue_reference_v<SessionHandler>,
        "server::tcp keeps a reference to the session handler for as long as it "
        "accepts connections: pass an object that outlives the server, not a temporary.");

    auto base = co_await net::this_coro::executor;
    co_await detail::run_on_strand(signals, detail::accept_loop(
        std::move(bind_host),
        std::move(bind_port),
        base,
        std::move(local_endpoint_handler),
        [&](rebind_executor<net::ip::tcp::socket> socket, net::any_io_executor const& strand)
        {
            detail::spawn_session(
                strand, signals,
                std::forward<SessionHandler>(session_handler)(std::move(socket), signals));
        }));
}

} // namespace boost::taar::server

#endif // BOOST_TAAR_SERVER_TCP_HPP
