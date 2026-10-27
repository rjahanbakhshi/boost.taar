//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include "e2e.hpp"
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <string>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace taar = boost::taar;
using namespace std::chrono_literals;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::path_arg;
using taar::handler::rest;
using taar::handler::session_policy;
using taar::handler::with_session;
namespace session_key = taar::handler::session_key;

struct conn_stats { int requests = 0; };
struct cart { int items = 0; };

BOOST_AUTO_TEST_CASE(test_e2e_connection_context_survives_keep_alive)
{
    net::io_context io;
    taar::session::basic_http<conn_stats> session;
    session.register_request_handler(
        method == http::verb::get && target == "/count",
        rest([](conn_stats& c) { return ++c.requests; }));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto a = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/count"),
            e2e::make_request(http::verb::get, "/count"),
            e2e::make_request(http::verb::get, "/count"));
        BOOST_TEST(a[0].body() == "1");
        BOOST_TEST(a[1].body() == "2");
        BOOST_TEST(a[2].body() == "3");

        auto b = co_await e2e::send_all(port, e2e::make_request(http::verb::get, "/count"));
        BOOST_TEST(b[0].body() == "1");
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_cookie_session_shared_across_connections)
{
    net::io_context io;
    taar::context_store<std::string, cart> carts {10min};
    taar::session::basic_http<conn_stats> session;

    session.register_request_handler(
        method == http::verb::post && target == "/cart/{n}",
        with_session(carts, session_key::cookie("sid"), session_policy::create_if_missing,
            rest([](cart& c, int n) { c.items += n; return c.items; }, path_arg("n"))));

    session.register_request_handler(
        method == http::verb::get && target == "/cart",
        with_session(carts, session_key::cookie("sid"), session_policy::require,
            rest([](conn_stats& s, cart const& c) { ++s.requests; return c.items; })));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        // Connection A creates the session.
        auto a = co_await e2e::send_all(port, e2e::make_request(http::verb::post, "/cart/2"));
        BOOST_TEST(a[0].result() == http::status::ok);
        BOOST_TEST(a[0].body() == "2");
        std::string set_cookie {a[0][http::field::set_cookie]};
        BOOST_TEST(set_cookie.starts_with("sid="));
        auto key = set_cookie.substr(4, set_cookie.find(';') - 4);

        // Connection B presents the cookie: same cart, fresh connection stats.
        auto b_get = e2e::make_request(http::verb::get, "/cart");
        b_get.set(http::field::cookie, "sid=" + key);
        auto b_post = e2e::make_request(http::verb::post, "/cart/3");
        b_post.set(http::field::cookie, "sid=" + key);
        auto b = co_await e2e::send_all(port, b_get, b_post);
        BOOST_TEST(b[0].body() == "2");
        BOOST_TEST((b[0].find(http::field::set_cookie) == b[0].end()));
        BOOST_TEST(b[1].body() == "5");

        // No cookie on a require route -> 401.
        auto c = co_await e2e::send_all(port, e2e::make_request(http::verb::get, "/cart"));
        BOOST_TEST(c[0].result() == http::status::unauthorized);

        // Forged cookie on a require route -> 401 as well.
        auto d_req = e2e::make_request(http::verb::get, "/cart");
        d_req.set(http::field::cookie, "sid=forged");
        auto d = co_await e2e::send_all(port, d_req);
        BOOST_TEST(d[0].result() == http::status::unauthorized);
        BOOST_TEST(carts.size() == 1u);
    });
}

} // namespace
