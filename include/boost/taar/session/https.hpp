//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_SESSION_HTTPS_HPP
#define BOOST_TAAR_SESSION_HTTPS_HPP

#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

// Including this header pulls in Boost.Asio SSL; link OpenSSL (SSL and
// Crypto) into programs that use it.

namespace boost::taar::session {

/// The stream an HTTPS session runs on: TLS over Beast's `tcp_stream`.
using tls_stream = boost::beast::ssl_stream<rebind_executor<boost::beast::tcp_stream>>;

/** HTTPS session: @ref basic_http over a TLS stream.

    Register handlers exactly as on `basic_http`; the only difference is the
    acceptor, which must be @ref boost::taar::server::tls so the handshake
    happens before the session starts.

    @code
    boost::asio::ssl::context tls {boost::asio::ssl::context::tls_server};
    tls.use_certificate_chain_file("cert.pem");
    tls.use_private_key_file("key.pem", boost::asio::ssl::context::pem);

    taar::session::https session;
    session.register_request_handler(method == http::verb::get && target == "/hello",
        taar::handler::rest([] { return "hello over TLS"; }));

    boost::asio::co_spawn(io, taar::server::tls("0.0.0.0", "8443", tls, session, signals),
        boost::asio::detached);
    @endcode
*/
template <typename Context = no_context>
using basic_https = basic_http<Context, tls_stream>;

/// HTTPS session without a per-connection context.
using https = basic_https<>;

} // namespace boost::taar::session

#endif // BOOST_TAAR_SESSION_HTTPS_HPP
