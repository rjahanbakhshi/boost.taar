//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// The session adds `Date` (RFC 9110 6.6.1) to every response it writes
// unless the handler or the filter set one, or it is disabled.

#include "e2e.hpp"
#include "joining_thread.hpp"
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/http_date.hpp>
#include <boost/taar/core/interceptor.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace taar = boost::taar;
using namespace std::chrono_literals;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;

/// True if @a value is an IMF-fixdate within a minute of now.
bool is_current_date(std::string_view value)
{
    auto const parsed = taar::parse_http_date(value);
    if (!parsed) return false;
    if (taar::format_http_date(*parsed) != value) return false;     // IMF-fixdate, not an obsolete form
    auto const now = std::chrono::system_clock::now();
    auto const diff = now > *parsed ? now - *parsed : *parsed - now;
    return diff < 1min;
}

taar::session::http make_session()
{
    taar::session::http session;
    session.register_request_handler(method == http::verb::get && target == "/ping",
        rest([] { return std::string {"pong"}; }));
    session.register_request_handler(method == http::verb::get && target == "/num/{n}",
        rest([](int n) { return n; }, taar::handler::path_arg("n")));
    session.register_request_handler(method == http::verb::get && target == "/stream",
        rest([]() -> taar::chunked_response<std::string> { co_yield std::string {"a"}; co_yield std::string {"b"}; }));
    session.register_request_handler(method == http::verb::get && target == "/not-modified",
        [](http::request<http::empty_body> const&, taar::matcher::context const&)
        {
            return http::response<http::empty_body> {http::status::not_modified, 11};
        });
    session.register_request_handler(method == http::verb::get && target == "/dated",
        [](http::request<http::empty_body> const&, taar::matcher::context const&)
        {
            http::response<http::string_body> res {http::status::ok, 11};
            res.set(http::field::date, "Sun, 06 Nov 1994 08:49:37 GMT");
            res.body() = "old";
            res.prepare_payload();
            return res;
        });
    session.add_interceptor(target == "/private",
        [](http::request_header<> const&, taar::matcher::context&) -> taar::interceptor_result
        {
            return taar::reject(http::status::unauthorized);
        });
    return session;
}

} // namespace

BOOST_AUTO_TEST_CASE(test_date_on_every_kind_of_response)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto head = e2e::make_request(http::verb::head, "/ping");
        auto res = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/ping"),
            e2e::make_request(http::verb::get, "/missing"),             // 404
            e2e::make_request(http::verb::post, "/ping"),               // 405
            e2e::make_request(http::verb::get, "/num/abc"),             // soft error 400
            e2e::make_request(http::verb::get, "/not-modified"),        // 304
            e2e::make_request(http::verb::get, "/private"),             // interceptor 401
            e2e::make_request(http::verb::get, "/stream"));             // chunked
        BOOST_TEST_REQUIRE(res.size() == 7u);
        BOOST_TEST(res[0].result() == http::status::ok);
        BOOST_TEST(res[1].result() == http::status::not_found);
        BOOST_TEST(res[2].result() == http::status::method_not_allowed);
        BOOST_TEST(res[3].result() == http::status::bad_request);
        BOOST_TEST(res[4].result() == http::status::not_modified);
        BOOST_TEST(res[5].result() == http::status::unauthorized);
        BOOST_TEST(res[6].chunked());
        for (auto const& r : res)
        {
            BOOST_TEST(is_current_date(r[http::field::date]), r.result_int() << ": " << r[http::field::date]);
        }

        // HEAD served from GET: header only, with Date.
        taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
        co_await socket.async_connect({net::ip::address_v4::loopback(), port});
        co_await http::async_write(socket, head);
        http::response_parser<http::empty_body> parser;
        parser.skip(true);
        beast::flat_buffer buffer;
        co_await http::async_read_header(socket, buffer, parser);
        BOOST_TEST(parser.get().result() == http::status::ok);
        BOOST_TEST(is_current_date(parser.get()[http::field::date]));
    });
}

BOOST_AUTO_TEST_CASE(test_date_on_protocol_error_responses)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
        co_await socket.async_connect({net::ip::address_v4::loopback(), port});
        std::string const garbage = "NOT A REQUEST\r\n\r\n";
        co_await net::async_write(socket, net::buffer(garbage));
        e2e::response res;
        beast::flat_buffer buffer;
        co_await http::async_read(socket, buffer, res);
        BOOST_TEST(res.result() == http::status::bad_request);
        BOOST_TEST(is_current_date(res[http::field::date]));
    });
}

BOOST_AUTO_TEST_CASE(test_date_set_by_handler_or_filter_is_kept)
{
    net::io_context io;
    auto session = make_session();
    session.set_response_filter([](http::request_header<> const& req, http::response_header<>& res)
    {
        if (req.target() == "/ping") res.set(http::field::date, "Mon, 07 Nov 1994 08:49:37 GMT");
    });
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/dated"),
            e2e::make_request(http::verb::get, "/ping"));
        BOOST_TEST(res[0][http::field::date] == "Sun, 06 Nov 1994 08:49:37 GMT");
        BOOST_TEST(res[1][http::field::date] == "Mon, 07 Nov 1994 08:49:37 GMT");
        // Exactly one Date each.
        BOOST_TEST(res[0].count(http::field::date) == 1u);
        BOOST_TEST(res[1].count(http::field::date) == 1u);
    });
}

BOOST_AUTO_TEST_CASE(test_date_can_be_disabled)
{
    net::io_context io;
    auto session = make_session();
    session.set_date_header(false);
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/ping"),
            e2e::make_request(http::verb::get, "/missing"),
            e2e::make_request(http::verb::get, "/stream"));
        for (auto const& r : res)
        {
            BOOST_TEST(r.count(http::field::date) == 0u);
        }
    });
}

BOOST_AUTO_TEST_CASE(test_date_cache_is_current_and_thread_safe)
{
    BOOST_TEST(is_current_date(taar::detail::cached_http_date()));

    std::atomic<int> bad {0};
    {
        std::vector<test_util::joining_thread> threads;
        for (int t = 0; t < 4; ++t)
        {
            threads.emplace_back([&]
            {
                for (int i = 0; i < 20000; ++i)
                {
                    std::string const copy {taar::detail::cached_http_date()};
                    if (!taar::parse_http_date(copy)) ++bad;
                }
            });
        }
    }
    BOOST_TEST(bad.load() == 0);
}
