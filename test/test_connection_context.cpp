//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>
#include <type_traits>

namespace {

namespace http = boost::beast::http;
namespace taar = boost::taar;
namespace hdetail = taar::handler::detail;

using taar::matcher::method;
using taar::matcher::target;
using taar::session::basic_http;
using taar::session::no_context;
using taar::handler::contexts;
using taar::handler::path_arg;
using taar::handler::rest;

struct user_context
{
    int counter = 0;
};

struct other_context
{
    int x = 0;
};

bool raw_no_ctx(http::request<http::empty_body> const&, taar::matcher::context const&)
{
    return true;
}

bool raw_ref_ctx(user_context&, http::request<http::empty_body> const&, taar::matcher::context const&)
{
    return true;
}

bool raw_shared_ctx(std::shared_ptr<user_context>, http::request<http::empty_body> const&, taar::matcher::context const&)
{
    return true;
}

// ---- requested_context ------------------------------------------------------

static_assert(std::is_same_v<hdetail::requested_context_t<user_context&>, user_context>);
static_assert(std::is_same_v<hdetail::requested_context_t<user_context const&>, user_context>);
static_assert(std::is_same_v<hdetail::requested_context_t<std::shared_ptr<user_context>>, user_context>);
static_assert(std::is_same_v<hdetail::requested_context_t<std::shared_ptr<user_context> const&>, user_context>);
static_assert(std::is_void_v<hdetail::requested_context_t<int>>);
static_assert(std::is_void_v<hdetail::requested_context_t<int&>>);
static_assert(std::is_void_v<hdetail::requested_context_t<user_context>>);
static_assert(std::is_void_v<hdetail::requested_context_t<user_context&&>>);

// ---- context_index / in_scope ----------------------------------------------

using two = contexts<user_context, other_context>;
static_assert(hdetail::context_index_v<user_context, two> == 0);
static_assert(hdetail::context_index_v<other_context, two> == 1);
static_assert(hdetail::context_index_v<int, two> == hdetail::npos);
static_assert(hdetail::in_scope<user_context&, two>);
static_assert(hdetail::in_scope<other_context const&, two>);
static_assert(!hdetail::in_scope<user_context&, contexts<>>);
static_assert(!hdetail::in_scope<int, two>);

// ---- request_param_index ----------------------------------------------------

static_assert(hdetail::request_param_index<decltype(&raw_no_ctx)> == 0);
static_assert(hdetail::request_param_index<decltype(&raw_ref_ctx)> == 1);
static_assert(hdetail::request_param_index<decltype(&raw_shared_ctx)> == 1);

// ---- rest(): context count is callable args minus providers ---------------

static_assert(std::remove_cvref_t<decltype(rest([](int){ return 0; }, path_arg("x")))>::context_count == 0);
static_assert(std::remove_cvref_t<decltype(rest([](user_context&, int){ return 0; }, path_arg("x")))>::context_count == 1);
static_assert(std::remove_cvref_t<decltype(rest([](user_context&, other_context const&, int){ return 0; }, path_arg("x")))>::context_count == 2);
static_assert(taar::handler::context_aware_handler<decltype(rest([](int){ return 0; }, path_arg("x")))>);
static_assert(!taar::handler::context_aware_handler<decltype(&raw_ref_ctx)>);

// ---- context_arg returns a reference or the shared_ptr -------------------

BOOST_AUTO_TEST_CASE(test_context_arg_reference_and_shared_ptr)
{
    two ctxs {std::make_shared<user_context>(), std::make_shared<other_context>()};
    std::get<0>(ctxs)->counter = 7;

    user_context& by_ref = hdetail::context_arg<user_context&>(ctxs);
    BOOST_TEST(by_ref.counter == 7);
    by_ref.counter = 8;
    BOOST_TEST(std::get<0>(ctxs)->counter == 8);

    std::shared_ptr<user_context> by_ptr = hdetail::context_arg<std::shared_ptr<user_context>>(ctxs);
    BOOST_TEST(by_ptr.get() == std::get<0>(ctxs).get());

    other_context const& other = hdetail::context_arg<other_context const&>(ctxs);
    BOOST_TEST(other.x == 0);
}

// ---- rest handler invoked with a contexts tuple ----------------------------

BOOST_AUTO_TEST_CASE(test_rest_handler_receives_contexts_by_type)
{
    boost::asio::io_context io;
    auto handler = rest(
        [](other_context const& o, user_context& u, int x) { u.counter += x + o.x; return u.counter; },
        path_arg("x"));

    two ctxs {std::make_shared<user_context>(), std::make_shared<other_context>()};
    std::get<1>(ctxs)->x = 10;

    http::request<http::empty_body> req;
    taar::matcher::context mctx;
    mctx.path_args.emplace("x", "5");

    int body = 0;
    boost::asio::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        auto response = co_await handler(ctxs, req, mctx);
        body = std::stoi(response.body());
    }, boost::asio::detached);
    io.run();

    BOOST_TEST(body == 15);
    BOOST_TEST(std::get<0>(ctxs)->counter == 15);
}

// ---- Session construction with and without a Context -----------------------

BOOST_AUTO_TEST_CASE(test_basic_http_default_alias_is_no_context)
{
    static_assert(std::is_same_v<taar::session::http, basic_http<no_context>>);

    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/ping",
        [](http::request<http::empty_body> const&, taar::matcher::context const&)
        -> http::message_generator
        {
            return http::response<http::empty_body>{http::status::ok, 11};
        });
    session.register_request_handler(
        method == http::verb::get && target == "/api/sum/{a}/{b}",
        rest([](int a, int b){ return a + b; }, path_arg("a"), path_arg("b")));
}

BOOST_AUTO_TEST_CASE(test_basic_http_with_user_context_accepts_context_handlers)
{
    basic_http<user_context> session;

    // Raw handlers, by reference and by shared_ptr.
    session.register_request_handler(
        method == http::verb::get && target == "/raw-ref",
        [](user_context& ctx, http::request<http::empty_body> const& req, taar::matcher::context const&)
        -> http::message_generator
        {
            ctx.counter += 1;
            return http::response<http::empty_body>{http::status::ok, req.version()};
        });
    session.register_request_handler(
        method == http::verb::get && target == "/raw-ptr",
        &raw_shared_ctx);

    // rest()-built handlers, by reference and by shared_ptr.
    session.register_request_handler(
        method == http::verb::get && target == "/rest-ref/{x}",
        rest([](user_context& ctx, int x) { ctx.counter += x; return ctx.counter; }, path_arg("x")));
    session.register_request_handler(
        method == http::verb::get && target == "/rest-ptr/{x}",
        rest([](std::shared_ptr<user_context> ctx, int x) { ctx->counter += x; return ctx->counter; }, path_arg("x")));

    // Handlers that do not ask for the context are unaffected.
    session.register_request_handler(
        method == http::verb::get && target == "/plain",
        rest([]{ return std::string{"hello"}; }));
    session.register_request_handler(
        method == http::verb::get && target == "/plain-raw",
        &raw_no_ctx);
}

} // namespace
