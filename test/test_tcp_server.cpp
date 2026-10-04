//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/session/http.hpp>
#include <boost/taar/session/https.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/server/tls.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/test/unit_test.hpp>
#include <type_traits>

BOOST_AUTO_TEST_CASE(test_tcp_server)
{
    namespace net = boost::asio;
    namespace taar = boost::taar;

    taar::session::http http_session;

    taar::cancellation_signals cancellation_signals;
    std::ignore = taar::server::tcp(
        "0.0.0.0",
        "8080",
        http_session,
        cancellation_signals,
        [](net::ip::tcp::endpoint const&){});
}


BOOST_AUTO_TEST_CASE(test_tcp_server_public_stream_types_are_plain_beast_types)
{
    namespace net = boost::asio;
    namespace beast = boost::beast;
    namespace taar = boost::taar;

    static_assert(std::is_same_v<
        taar::session::tls_stream, beast::ssl_stream<beast::tcp_stream>>);
    static_assert(std::is_same_v<
        taar::session::http, taar::session::basic_http<taar::session::no_context, beast::tcp_stream>>);
    static_assert(std::is_same_v<
        taar::session::https, taar::session::basic_http<taar::session::no_context, taar::session::tls_stream>>);
    static_assert(std::is_invocable_r_v<taar::awaitable<void>,
        taar::session::http&, net::ip::tcp::socket, taar::cancellation_signals&>);
    static_assert(std::is_invocable_r_v<taar::awaitable<void>,
        taar::session::https&, taar::session::tls_stream, taar::cancellation_signals&>);
    static_assert(std::is_same_v<taar::awaitable<int>, net::awaitable<int>>);

    // Both server overloads accept an explicit connection executor.
    net::io_context io;
    taar::session::http http_session;
    taar::session::https https_session;
    net::ssl::context tls {net::ssl::context::tls_server};
    taar::cancellation_signals signals;
    std::ignore = taar::server::tcp(io.get_executor(), "127.0.0.1", "0", http_session, signals);
    std::ignore = taar::server::tls(io.get_executor(), "127.0.0.1", "0", tls, https_session, signals);
    BOOST_TEST(true);
}
