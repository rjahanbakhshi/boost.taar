//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/session/http.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/test/unit_test.hpp>

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
        [](const net::ip::tcp::endpoint&){});
}

