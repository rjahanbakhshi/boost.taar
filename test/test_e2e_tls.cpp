//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tls.hpp>
#include <boost/taar/session/https.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <exception>
#include <string>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;
using taar::handler::path_arg;
using namespace std::chrono_literals;

// Self-signed test certificate for CN=localhost. Test material only.
constexpr char const* test_cert = R"(-----BEGIN CERTIFICATE-----
MIIDPzCCAiegAwIBAgIUbeCWGwn/GpBstyvZngUFcC57UkwwDQYJKoZIhvcNAQEL
BQAwLjESMBAGA1UEAwwJbG9jYWxob3N0MRgwFgYDVQQKDA9ib29zdC50YWFyIHRl
c3QwIBcNMjYxMDI1MTQ0ODI5WhgPMjEyNjEwMDExNDQ4MjlaMC4xEjAQBgNVBAMM
CWxvY2FsaG9zdDEYMBYGA1UECgwPYm9vc3QudGFhciB0ZXN0MIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlsn8qnfWyKFE8bf74UzzS4ZT+3Slg0WVLN32
f3/+UxspS+UKnuQW/WzJvFCOohXvEI10QN4V4Ml0TCFowle8IEb2vp76pPA6V6/3
Z6KVDsRaPIyc7wJH58XnzIodAVYvu7ODuKcsJmxzScQ4BcSck6XG5zJIDNQp81iO
oy+5F8PJ+tYZ9ucUnPWgWcoAR98krmm97gpSNxlCn1NsUzEQ0g4Mm0jGw7QSiV+7
+NAJNv3/axaJPRxnF7XDl7ixgdMMMTHX/MUbeJ7sfiM98adbKobaVX5NcfujUkSD
s918Vr3E4Jkp6191Ng8MMaULY7QXcXoKl/UY4L6+xDVI7KwvxQIDAQABo1MwUTAd
BgNVHQ4EFgQUxBKkmeq2QoXrh64M+3ziymRBqP0wHwYDVR0jBBgwFoAUxBKkmeq2
QoXrh64M+3ziymRBqP0wDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOC
AQEAG4SmSU+KaYfXg/Dp88iXoTqn+xyM9Pzcdi/XXmOLPoGonCrxZPpNynd7YmWA
aKw+8vhrAXOYfo0RkOorLd/QlVv/fCnnmNGfeb8Orbuuf1KkcJL1V++zLFonf1CQ
Zl8YEMoDZXot3QYT3mXY+6gjdQDi8nAK2rVgl9he7wsJoNlK1zzRzru1oP/1PPm4
0B7d4lrjsbQcIOHONgYV9k38LCJHCljBx+ikRu1WobAL0/nzQREr1+biyp7DE36r
CLIG2JLiQW/VEqGGERaRzY9xBdGW6qIWt3BwDT/dgasRPKk/kwZdlAgtLNik0lkO
VLTQzqoZud7ztvpJ6lAEoet90A==
-----END CERTIFICATE-----
)";

constexpr char const* test_key = R"(-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCWyfyqd9bIoUTx
t/vhTPNLhlP7dKWDRZUs3fZ/f/5TGylL5Qqe5Bb9bMm8UI6iFe8QjXRA3hXgyXRM
IWjCV7wgRva+nvqk8DpXr/dnopUOxFo8jJzvAkfnxefMih0BVi+7s4O4pywmbHNJ
xDgFxJyTpcbnMkgM1CnzWI6jL7kXw8n61hn25xSc9aBZygBH3ySuab3uClI3GUKf
U2xTMRDSDgybSMbDtBKJX7v40Ak2/f9rFok9HGcXtcOXuLGB0wwxMdf8xRt4nux+
Iz3xp1sqhtpVfk1x+6NSRIOz3XxWvcTgmSnrX3U2DwwxpQtjtBdxegqX9Rjgvr7E
NUjsrC/FAgMBAAECggEAPNpyUheAAkL3MHfYOoIUE9A3t1zJaRNdUxiW4Li2WjK1
dVAPe7VAQ/Ex7wIxtd8VNkBoAL+/45JZkVnQTdIbdYk+DgrZy1O5r+aveU8HScjb
Q+FZeimY/EQFuqmocAAozl7xk0VAD5en5Ua/dzB0Ii/+eBJ2OlsrPO5pzhi14Y0/
DeRuFs7HXPMyPEcsCL/z6Tp29KhadIK2clAgvVTY/sAuS+905uhlsu86T1j34ri1
f50op6tSpWytJy7Jd3kG83RjiNFH68IguUxtEqN/0DAtgEQiFrpVkXAktILw+Wsa
9fHiPpBzMRaRgoZE974TC0QB2IFDPXkPE2MwdphGdwKBgQDM/04xvyt/v9R6e4m8
zm0/FcSfsbun0nSkXZzizdsno9XBKbqKKULYlVvC9EnAcfwn7VBOCaKJPXvufgxE
/de1MfuyxmSZV+YBwcenhrRYBKiHs4q6v+OzWiSrNUYCoW2HZHA0gkeHvrxqKkp9
IdpwAeN9I+hbIa8+8/FeQd/4pwKBgQC8TgvOOg6ZJkKQQxW5sSEgmBDA9MA6/Kwj
q2Y89rUCtB/66V8wEe8OBpwlhFrZ+tAZKVYf0gCCv+GBFhfCgf4nAfd/NlwCkL+w
DTOkjvXBvmwWUTsGtPlLiB6f7YYBLzKlsM59gpTsEmor+ISGVHGKu9rRm/S9QVgn
1wTQjMt1swKBgCAyNODBSp6szT1Ta8WYkjXnQFNogbWQ6zrpbPt+I15tb+P/nsmW
Q2ZqxteusGqmPFk0/3jSMJHnczB44YpwZ9GQgRyJ5kDjOj+qZKYeFPMCAEoX28EK
5ENF9B5jppRBMEekca79iyh5v3PZIovWpk0TLtx3qEWv26tCevU32WpfAoGBAJfm
kYlrtgj/HyUGqMjhxWpooOM2zLcIysf/ZPr6vOwQZ9fdWCcL4H69AEsXaL58oFAb
01jZ7AWNShj6vHHwsRfAc2mQbUq4f5lCHQ27MS54MPMT1fzEEnJGvQWf8sCT1VRp
uzjuFLoW5MTRtf4ysmEKxyrdluDwjG+e+UhOu3YXAoGBAJ1iC46bUmnfSeOZs99W
5Z3MqRS5Zw60pc0kB33Wnp5DDhp8aq0pyWfiESjZBzTXCiNqZSaLwBJxAm3gaipi
aBEPRRHCjZXlnv2YkeTn67XQliOvB9M1B2ZWL9OH82/331lAzYhTniwMwe8y4/D/
E40K+NEm38c2d+c7XshqWwDk
-----END PRIVATE KEY-----
)";

net::ssl::context server_tls()
{
    net::ssl::context tls {net::ssl::context::tls_server};
    tls.use_certificate_chain(net::buffer(std::string_view {test_cert}));
    tls.use_private_key(net::buffer(std::string_view {test_key}), net::ssl::context::pem);
    return tls;
}

using tls_client = net::ssl::stream<taar::rebind_executor<net::ip::tcp::socket>>;

/// Like e2e::run, but behind server::tls.
template <typename Session, typename Client>
void run_tls(net::io_context& io, net::ssl::context& tls, Session& session, Client client)
{
    taar::cancellation_signals signals;
    unsigned short port = 0;
    std::exception_ptr client_error;

    net::co_spawn(io,
        taar::server::tls("127.0.0.1", "0", tls, session, signals,
            [&](net::ip::tcp::endpoint const& ep) { port = ep.port(); }, 2s),
        net::bind_cancellation_slot(signals.slot(), net::detached));

    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        try
        {
            net::steady_timer timer {co_await net::this_coro::executor};
            while (port == 0)
            {
                timer.expires_after(1ms);
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

taar::awaitable<tls_client> connect_tls(net::ssl::context& ctx, unsigned short port)
{
    tls_client stream {co_await net::this_coro::executor, ctx};
    co_await beast::get_lowest_layer(stream).async_connect({net::ip::address_v4::loopback(), port});
    co_await stream.async_handshake(net::ssl::stream_base::client);
    co_return stream;
}

taar::awaitable<e2e::response> send(tls_client& stream, beast::flat_buffer& buffer, e2e::request req)
{
    co_await http::async_write(stream, req);
    e2e::response res;
    co_await http::async_read(stream, buffer, res);
    co_return res;
}

taar::session::https make_session()
{
    taar::session::https session;
    session.register_request_handler(method == http::verb::get && target == "/hello", rest([] { return "hello over TLS"; }));
    session.register_request_handler(method == http::verb::get && target == "/sum/{a}/{b}",
        rest([](int a, int b) { return a + b; }, path_arg("a"), path_arg("b")));
    session.register_request_handler(method == http::verb::get && target == "/stream",
        rest([]() -> taar::chunked_response<std::string> { co_yield std::string {"a"}; co_yield std::string {"b"}; }));
    return session;
}

BOOST_AUTO_TEST_CASE(test_e2e_tls_keep_alive_requests_and_streaming)
{
    net::io_context io;
    auto tls = server_tls();
    auto session = make_session();
    run_tls(io, tls, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        net::ssl::context client_ctx {net::ssl::context::tls_client};
        client_ctx.set_verify_mode(net::ssl::verify_none);
        auto stream = co_await connect_tls(client_ctx, port);
        beast::flat_buffer buffer;

        auto a = co_await send(stream, buffer, e2e::make_request(http::verb::get, "/hello"));
        BOOST_TEST(a.result() == http::status::ok);
        BOOST_TEST(a.body() == "hello over TLS");
        auto b = co_await send(stream, buffer, e2e::make_request(http::verb::get, "/sum/2/3"));
        BOOST_TEST(b.body() == "5");
        auto c = co_await send(stream, buffer, e2e::make_request(http::verb::get, "/stream"));
        BOOST_TEST(c.body() == "ab");
        BOOST_TEST(c.chunked());
        auto d = co_await send(stream, buffer, e2e::make_request(http::verb::get, "/sum/x/3"));
        BOOST_TEST(d.result() == http::status::bad_request);
        auto e = co_await send(stream, buffer, e2e::make_request(http::verb::get, "/missing"));
        BOOST_TEST(e.result() == http::status::not_found);

        // The server sends close_notify when the connection ends.
        auto close = e2e::make_request(http::verb::get, "/hello");
        close.set(http::field::connection, "close");
        auto f = co_await send(stream, buffer, close);
        BOOST_TEST(f.body() == "hello over TLS");
        auto [sec] = co_await stream.async_shutdown(net::as_tuple(taar::use_awaitable));
        BOOST_TEST((!sec || sec == net::ssl::error::stream_truncated || sec == net::error::eof));
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_tls_rejects_plaintext_and_keeps_serving)
{
    net::io_context io;
    auto tls = server_tls();
    auto session = make_session();
    run_tls(io, tls, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        // Plain HTTP on the TLS port: the handshake fails, the connection is dropped.
        taar::rebind_executor<net::ip::tcp::socket> plain {co_await net::this_coro::executor};
        co_await plain.async_connect({net::ip::address_v4::loopback(), port});
        auto req = e2e::make_request(http::verb::get, "/hello");
        co_await http::async_write(plain, req);
        char buf[256];
        auto [ec, n] = co_await plain.async_read_some(net::buffer(buf));
        BOOST_TEST((ec == net::error::eof || ec == net::error::connection_reset || n > 0));

        // TLS clients are unaffected.
        net::ssl::context client_ctx {net::ssl::context::tls_client};
        client_ctx.set_verify_mode(net::ssl::verify_none);
        auto stream = co_await connect_tls(client_ctx, port);
        beast::flat_buffer buffer;
        auto a = co_await send(stream, buffer, e2e::make_request(http::verb::get, "/hello"));
        BOOST_TEST(a.body() == "hello over TLS");
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_tls_client_can_verify_the_server_certificate)
{
    net::io_context io;
    auto tls = server_tls();
    auto session = make_session();
    run_tls(io, tls, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        net::ssl::context client_ctx {net::ssl::context::tls_client};
        client_ctx.add_certificate_authority(net::buffer(std::string_view {test_cert}));
        client_ctx.set_verify_mode(net::ssl::verify_peer);
        tls_client stream {co_await net::this_coro::executor, client_ctx};
        stream.set_verify_callback(net::ssl::host_name_verification("localhost"));
        co_await beast::get_lowest_layer(stream).async_connect({net::ip::address_v4::loopback(), port});
        co_await stream.async_handshake(net::ssl::stream_base::client);
        beast::flat_buffer buffer;
        auto a = co_await send(stream, buffer, e2e::make_request(http::verb::get, "/hello"));
        BOOST_TEST(a.body() == "hello over TLS");
    });
}

} // namespace
