//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/json/parse.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <string>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
using namespace std::chrono_literals;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::path_arg;
using taar::handler::rest;
using taar::handler::session_policy;
using taar::handler::with_session;
using jsonrpc::rpc_arg;
namespace session_key = taar::handler::session_key;

struct conn_stats { int requests = 0; };
struct calc_session { int last = 0; };

int add(calc_session& s, int a, int b) { s.last = a + b; return s.last; }
int last(conn_stats& c, calc_session const& s) { ++c.requests; return s.last * 100 + c.requests; }

BOOST_AUTO_TEST_CASE(test_e2e_jsonrpc_dual_surface_with_sessions)
{
    net::io_context io;
    taar::context_store<std::string, calc_session> sessions {10min};
    taar::session::basic_http<conn_stats> session;

    // REST surface for the same callable.
    session.register_request_handler(
        method == http::verb::post && target == "/api/add/{a}/{b}",
        with_session(sessions, session_key::header("X-Session"), session_policy::create_if_missing,
            rest(&add, path_arg("a"), path_arg("b"))));

    // JSON-RPC surface: one route, keyed session through the same store.
    jsonrpc::endpoint<conn_stats, calc_session> rpc;
    rpc.method("add", &add, rpc_arg("a"), rpc_arg("b"));
    rpc.method("last", &last);

    session.register_request_handler(
        method == http::verb::post && target == "/rpc",
        with_session(sessions, session_key::header("X-Session"), session_policy::create_if_missing,
            rpc.handler()));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        // Create the session over REST.
        auto r = co_await e2e::send_all(port, {e2e::make_request(http::verb::post, "/api/add/2/3")});
        BOOST_TEST(r[0].body() == "5");
        std::string key {r[0]["X-Session"]};
        BOOST_TEST(key.size() == 32u);

        // Batch over JSON-RPC on another connection, same session.
        auto batch = e2e::make_request(http::verb::post, "/rpc", R"([
            {"jsonrpc":"2.0","id":1,"method":"last"},
            {"jsonrpc":"2.0","id":2,"method":"add","params":{"a":10,"b":20}},
            {"jsonrpc":"2.0","method":"add","params":{"a":0,"b":1}},
            {"jsonrpc":"2.0","id":3,"method":"last"}
        ])");
        batch.set("X-Session", key);
        auto b = co_await e2e::send_all(port, {batch});
        BOOST_TEST(b[0].result() == http::status::ok);
        auto arr = json::parse(b[0].body()).as_array();
        BOOST_REQUIRE(arr.size() == 3u);
        // Envelopes in a batch run in order, on the same connection and session.
        BOOST_TEST(arr[0].at("result").as_int64() == 501);  // last=5, requests=1
        BOOST_TEST(arr[1].at("result").as_int64() == 30);
        BOOST_TEST(arr[2].at("result").as_int64() == 102);  // last=1 (notification ran), requests=2

        // Notification only -> 202, and a GET on the POST-only route -> 405 with Allow.
        auto n = e2e::make_request(http::verb::post, "/rpc", R"({"jsonrpc":"2.0","method":"add","params":{"a":1,"b":1}})");
        n.set("X-Session", key);
        auto g = e2e::make_request(http::verb::get, "/rpc");
        auto c = co_await e2e::send_all(port, {n, g});
        BOOST_TEST(c[0].result() == http::status::accepted);
        BOOST_TEST(c[1].result() == http::status::method_not_allowed);
        BOOST_TEST(c[1][http::field::allow] == "POST");

        BOOST_TEST(sessions.size() == 1u);
    });
}

} // namespace
