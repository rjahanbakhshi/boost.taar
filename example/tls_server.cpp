//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// HTTPS. The session is session::https (basic_http over a TLS stream) and
// the acceptor is server::tls, which performs the handshake; handlers are
// registered exactly as for plain HTTP.
//
//   openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 365 -subj /CN=localhost
//   ./tls_server 8443 cert.pem key.pem
//   curl -k https://localhost:8443/hello
//   curl -k 'https://localhost:8443/sum/2/3'
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tls.hpp>
#include <boost/taar/session/https.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/json/value.hpp>
#include <iostream>

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::rest;
    using taar::handler::path_arg;

    if (argc != 4)
    {
        std::cerr << "Usage: tls_server <port> <cert.pem> <key.pem>\n";
        return 1;
    }

    net::ssl::context tls {net::ssl::context::tls_server};
    tls.set_options(net::ssl::context::default_workarounds | net::ssl::context::no_sslv2 | net::ssl::context::no_sslv3);
    tls.use_certificate_chain_file(argv[2]);
    tls.use_private_key_file(argv[3], net::ssl::context::pem);

    taar::session::https session;
    session.register_request_handler(
        method == http::verb::get && target == "/hello",
        rest([] { return "hello over TLS"; }));
    session.register_request_handler(
        method == http::verb::get && target == "/sum/{a}/{b}",
        rest([](int a, int b) { return boost::json::value {{"sum", a + b}}; }, path_arg("a"), path_arg("b")));

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tls("0.0.0.0", argv[1], tls, session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on https://" << ep << '\n'; }),
        taar::ignore_and_rethrow);

    io.run();
}
