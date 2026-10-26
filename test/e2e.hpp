//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

// Loopback test helpers: run a taar server on an ephemeral port and talk
// to it with a Beast client over real TCP connections.

#ifndef BOOST_TAAR_TEST_E2E_HPP
#define BOOST_TAAR_TEST_E2E_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <chrono>
#include <concepts>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace e2e {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace taar = boost::taar;

using request = http::request<http::string_body>;
using response = http::response<http::string_body>;

/// A client connection to the test server. One instance = one TCP connection.
class connection
{
public:
    explicit connection(net::any_io_executor ex)
        : socket_ {ex}
    {}

    taar::awaitable<void> connect(unsigned short port)
    {
        auto [ec] = co_await socket_.async_connect(
            {net::ip::address_v4::loopback(), port});
        if (ec) throw boost::system::system_error {ec};
    }

    /// Write one request and read one response on this connection.
    taar::awaitable<response> send(request req)
    {
        if (!req.has_content_length() && !req.chunked())
        {
            req.prepare_payload();
        }
        auto [wec, wsz] = co_await http::async_write(socket_, req);
        if (wec) throw boost::system::system_error {wec};

        response res;
        auto [rec, rsz] = co_await http::async_read(socket_, buffer_, res);
        if (rec) throw boost::system::system_error {rec};
        co_return res;
    }

    void close()
    {
        boost::system::error_code ec;
        socket_.shutdown(net::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

private:
    taar::rebind_executor<net::ip::tcp::socket> socket_;
    boost::beast::flat_buffer buffer_;
};

inline request make_request(
    http::verb verb,
    std::string target,
    std::string body = {},
    std::string content_type = "application/json")
{
    request req {verb, target, 11};
    req.set(http::field::host, "localhost");
    if (!body.empty())
    {
        req.set(http::field::content_type, content_type);
        req.body() = std::move(body);
    }
    req.prepare_payload();
    return req;
}

/** Run @a session behind a server on an ephemeral port and drive @a client.

    The client coroutine receives the port. When it returns, the server is
    cancelled and `io.run()` returns. Exceptions from the client are
    rethrown to the caller so test assertions can be written inside it.
*/
template <typename Session, typename Client>
void run(net::io_context& io, Session& session, Client client)
{
    taar::cancellation_signals signals;
    unsigned short port = 0;
    std::exception_ptr client_error;

    net::co_spawn(
        io,
        taar::server::tcp(
            "127.0.0.1", "0", session, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }),
        net::detached);

    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        try
        {
            // tcp() resolves the bind address asynchronously before it
            // binds; wait until the endpoint callback has published the port.
            net::steady_timer timer {co_await net::this_coro::executor};
            while (port == 0)
            {
                timer.expires_after(std::chrono::milliseconds {1});
                co_await timer.async_wait(taar::use_awaitable);
            }
            co_await client(port);
        }
        catch (...)
        {
            client_error = std::current_exception();
        }
        signals.emit();
    }, net::detached);

    io.run();

    if (client_error) std::rethrow_exception(client_error);
}

/// Sends @a reqs sequentially on one connection and returns the responses.
inline taar::awaitable<std::vector<response>> send_all(
    unsigned short port,
    std::vector<request> reqs)
{
    connection conn {co_await net::this_coro::executor};
    co_await conn.connect(port);
    std::vector<response> out;
    for (auto& r : reqs)
    {
        out.push_back(co_await conn.send(std::move(r)));
    }
    co_return out;
}

/** Sends the requests @a reqs sequentially on one connection.

    Takes the requests as separate arguments rather than a braced list:
    GCC 13 ICEs (build_special_member_call) on a braced initializer list
    of class temporaries inside a `co_await` expression.
*/
template <typename... Requests>
    requires (sizeof...(Requests) > 0 && (std::convertible_to<Requests, request> && ...))
taar::awaitable<std::vector<response>> send_all(unsigned short port, Requests&&... reqs)
{
    std::vector<request> v;
    v.reserve(sizeof...(Requests));
    (v.emplace_back(std::forward<Requests>(reqs)), ...);
    return send_all(port, std::move(v));
}

} // namespace e2e

#endif // BOOST_TAAR_TEST_E2E_HPP
