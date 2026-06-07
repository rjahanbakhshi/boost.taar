//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <string>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace taar = boost::taar;
using namespace std::chrono_literals;

using taar::handler::contexts;
using taar::handler::path_arg;
using taar::handler::rest;
using taar::handler::session_policy;
using taar::handler::with_session;
namespace session_key = taar::handler::session_key;

struct conn_ctx { int requests = 0; };
struct sess_ctx { int counter = 0; };

using store_type = taar::context_store<std::string, sess_ctx>;
using response_type = http::response<http::string_body>;

// Runs a handler coroutine to completion on a fresh io_context.
template <typename Handler, typename Contexts>
response_type call(Handler& handler, Contexts const& ctxs, http::request<http::empty_body> const& req)
{
    net::io_context io;
    std::optional<response_type> out;
    std::exception_ptr error;
    taar::matcher::context mctx;
    mctx.path_args.emplace("x", "1");
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        try { out = co_await handler(ctxs, req, mctx); }
        catch (...) { error = std::current_exception(); }
    }, net::detached);
    io.run();
    if (error) std::rethrow_exception(error);
    return *out;
}

http::request<http::empty_body> get_with_cookie(std::string const& cookie = {})
{
    http::request<http::empty_body> req {http::verb::get, "/x/1", 11};
    if (!cookie.empty()) req.set(http::field::cookie, cookie);
    return req;
}

std::string set_cookie_value(response_type const& res, std::string const& name)
{
    auto it = res.find(http::field::set_cookie);
    if (it == res.end()) return {};
    std::string v {it->value()};
    auto const prefix = name + "=";
    if (!v.starts_with(prefix)) return {};
    auto end = v.find(';');
    return v.substr(prefix.size(), end == std::string::npos ? std::string::npos : end - prefix.size());
}

BOOST_AUTO_TEST_CASE(test_with_session_create_if_missing_mints_key_and_sets_cookie)
{
    store_type store {10min};
    auto handler = with_session(store, session_key::cookie("sid"), session_policy::create_if_missing,
        rest([](sess_ctx& s, int x) { s.counter += x; return s.counter; }, path_arg("x")));

    auto first = call(handler, contexts<>{}, get_with_cookie());
    BOOST_TEST(first.result() == http::status::ok);
    BOOST_TEST(first.body() == "1");
    auto key = set_cookie_value(first, "sid");
    BOOST_TEST(key.size() == 32u);
    BOOST_TEST(std::string {first[http::field::set_cookie]}.find("HttpOnly") != std::string::npos);
    BOOST_TEST(store.size() == 1u);

    // Round trip: same key, same context, no new Set-Cookie.
    auto second = call(handler, contexts<>{}, get_with_cookie("sid=" + key));
    BOOST_TEST(second.body() == "2");
    BOOST_TEST((second.find(http::field::set_cookie) == second.end()));
    BOOST_TEST(store.size() == 1u);
}

BOOST_AUTO_TEST_CASE(test_with_session_unknown_key_is_replaced_not_adopted)
{
    store_type store {10min};
    auto handler = with_session(store, session_key::cookie("sid"), session_policy::create_if_missing,
        rest([](sess_ctx& s) { return ++s.counter; }));

    auto res = call(handler, contexts<>{}, get_with_cookie("sid=forged"));
    auto key = set_cookie_value(res, "sid");
    BOOST_TEST(!key.empty());
    BOOST_TEST(key != "forged");
    BOOST_TEST(!store.find("forged"));
    BOOST_TEST(!!store.find(key));
}

BOOST_AUTO_TEST_CASE(test_with_session_require_rejects_missing_and_unknown)
{
    store_type store {10min};
    bool ran = false;
    auto handler = with_session(store, session_key::cookie("sid"), session_policy::require,
        rest([&](sess_ctx& s) { ran = true; return ++s.counter; }));

    auto no_cookie = call(handler, contexts<>{}, get_with_cookie());
    BOOST_TEST(no_cookie.result() == http::status::unauthorized);
    BOOST_TEST(!ran);
    BOOST_TEST(store.size() == 0u);

    auto unknown = call(handler, contexts<>{}, get_with_cookie("sid=nope"));
    BOOST_TEST(unknown.result() == http::status::unauthorized);
    BOOST_TEST(!ran);

    auto e = store.emplace("known");
    auto ok = call(handler, contexts<>{}, get_with_cookie("sid=known"));
    BOOST_TEST(ok.result() == http::status::ok);
    BOOST_TEST(ran);
    BOOST_TEST(e->context()->counter == 1);
}

BOOST_AUTO_TEST_CASE(test_with_session_header_extractor)
{
    store_type store {10min};
    auto handler = with_session(store, session_key::header("X-Session"), session_policy::create_if_missing,
        rest([](sess_ctx& s) { return ++s.counter; }));

    http::request<http::empty_body> req {http::verb::get, "/x/1", 11};
    auto first = call(handler, contexts<>{}, req);
    std::string key {first["X-Session"]};
    BOOST_TEST(key.size() == 32u);

    req.set("X-Session", key);
    auto second = call(handler, contexts<>{}, req);
    BOOST_TEST(second.body() == "2");
    BOOST_TEST((second.find("X-Session") == second.end()));
}

BOOST_AUTO_TEST_CASE(test_with_session_inner_sees_connection_and_keyed_context)
{
    store_type store {10min};
    auto handler = with_session(store, session_key::cookie("sid"), session_policy::create_if_missing,
        rest([](conn_ctx& c, sess_ctx& s, int x) { ++c.requests; s.counter += x; return c.requests * 100 + s.counter; },
             path_arg("x")));

    contexts<conn_ctx> ctxs {std::make_shared<conn_ctx>()};
    auto first = call(handler, ctxs, get_with_cookie());
    BOOST_TEST(first.body() == "101");
    auto key = set_cookie_value(first, "sid");

    // Same connection context, same session: both advance.
    auto second = call(handler, ctxs, get_with_cookie("sid=" + key));
    BOOST_TEST(second.body() == "202");

    // New connection context, same session: connection counter restarts.
    contexts<conn_ctx> other {std::make_shared<conn_ctx>()};
    auto third = call(handler, other, get_with_cookie("sid=" + key));
    BOOST_TEST(third.body() == "103");
}

BOOST_AUTO_TEST_CASE(test_with_session_wraps_raw_handler)
{
    store_type store {10min};
    auto handler = with_session(store, session_key::cookie("sid"), session_policy::create_if_missing,
        [](sess_ctx& s, http::request<http::empty_body> const&, taar::matcher::context const&)
        {
            return std::to_string(++s.counter);
        });

    auto first = call(handler, contexts<>{}, get_with_cookie());
    BOOST_TEST(first.body() == "1");
    auto key = set_cookie_value(first, "sid");
    auto second = call(handler, contexts<>{}, get_with_cookie("sid=" + key));
    BOOST_TEST(second.body() == "2");
}

BOOST_AUTO_TEST_CASE(test_with_session_expired_key_behaves_as_unknown)
{
    store_type::clock::time_point now {1h};
    store_type store {1min, taar::default_key_generator<std::string>{}, [&] { return now; }};
    auto handler = with_session(store, session_key::cookie("sid"), session_policy::require,
        rest([](sess_ctx& s) { return ++s.counter; }));

    auto [key, e] = store.create();
    BOOST_TEST(call(handler, contexts<>{}, get_with_cookie("sid=" + key)).result() == http::status::ok);
    now += 2min;
    BOOST_TEST(call(handler, contexts<>{}, get_with_cookie("sid=" + key)).result() == http::status::unauthorized);
}

BOOST_AUTO_TEST_CASE(test_with_session_inner_exception_propagates_and_entry_remains)
{
    store_type store {10min};
    auto handler = with_session(store, session_key::cookie("sid"), session_policy::create_if_missing,
        rest([](sess_ctx&) -> int { throw std::runtime_error {"boom"}; }));

    BOOST_CHECK_THROW(call(handler, contexts<>{}, get_with_cookie()), std::runtime_error);
    BOOST_TEST(store.size() == 1u); // minted entry left for the sweeper
}

BOOST_AUTO_TEST_CASE(test_with_session_exposes_inner_path_arg_names)
{
    store_type store {10min};
    auto handler = with_session(store, session_key::cookie("sid"), session_policy::require,
        rest([](sess_ctx&, int) { return 0; }, path_arg("x")));
    auto names = handler.path_arg_names();
    BOOST_REQUIRE(names.size() == 1u);
    BOOST_TEST(names[0] == "x");
}

} // namespace
