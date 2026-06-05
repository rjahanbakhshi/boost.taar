//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>
#include <type_traits>

namespace {

namespace http = boost::beast::http;
namespace taar = boost::taar;

using taar::matcher::method;
using taar::matcher::target;
using taar::session::basic_http;
using taar::session::no_context;
using taar::handler::path_arg;
using taar::handler::rest;

struct user_context
{
    int counter = 0;
    std::string last_path;
};

struct other_context
{
    int x = 0;
};

// ---- Raw-handler probes used by the detection traits -----------------------

bool raw_no_ctx(
    http::request<http::empty_body> const&,
    taar::matcher::context const&)
{
    return true;
}

bool raw_with_user_ctx(
    std::shared_ptr<user_context>,
    http::request<http::empty_body> const&,
    taar::matcher::context const&)
{
    return true;
}

bool raw_with_other_ctx(
    std::shared_ptr<other_context>,
    http::request<http::empty_body> const&,
    taar::matcher::context const&)
{
    return true;
}

// Zero-arg callable — exercises the std::tuple_element<0, tuple<>>
// short-circuit; the detection trait must say "no context" without
// triggering a hard error.
auto zero_arg_handler = []{ return std::string{"ok"}; };

// ---- Type-trait checks -----------------------------------------------------

namespace detail = taar::session::detail;

static_assert(!detail::advertises_connection_context<decltype(zero_arg_handler)>);
static_assert(!detail::advertises_connection_context<decltype(&raw_no_ctx)>);
static_assert( detail::advertises_connection_context<decltype(&raw_with_user_ctx)>);
static_assert( detail::advertises_connection_context<decltype(&raw_with_other_ctx)>);

static_assert(!detail::takes_connection_context<decltype(zero_arg_handler), user_context>);
static_assert(!detail::takes_connection_context<decltype(&raw_no_ctx), user_context>);
static_assert( detail::takes_connection_context<decltype(&raw_with_user_ctx), user_context>);
static_assert(!detail::takes_connection_context<decltype(&raw_with_user_ctx), other_context>);
static_assert( detail::takes_connection_context<decltype(&raw_with_other_ctx), other_context>);

// ---- rest factory dispatch -------------------------------------------------

// A regular callable without a leading shared_ptr — rest() must produce
// the plain rest_handler, not the with-context variant.
static_assert(!std::is_same_v<
    std::remove_cvref_t<decltype(rest([](int){ return 0; }, path_arg("x")))>::callable_storage,
    void>); // sanity: callable_storage exists on rest_handler

// Detection of leading shared_ptr (sanity of the rest-local concept).
namespace rdetail = taar::handler::detail;

static_assert( rdetail::takes_leading_shared_ptr<
    decltype(+[](std::shared_ptr<user_context>, int){ return 0; })>);
static_assert(!rdetail::takes_leading_shared_ptr<
    decltype(+[](int){ return 0; })>);
static_assert(!rdetail::takes_leading_shared_ptr<decltype(zero_arg_handler)>);

// A context-taking handler routes through rest_handler_with_context: its
// operator() takes shared_ptr<user_context> ahead of the request, so
// the session's takes_connection_context says yes.
auto ctx_handler = rest(
    [](std::shared_ptr<user_context> ctx, int x){ return ctx->counter + x; },
    path_arg("x"));

static_assert(detail::takes_connection_context<decltype(ctx_handler), user_context>);
static_assert(!detail::takes_connection_context<decltype(ctx_handler), other_context>);

// The connection-context type the rest factory deduced from the
// callable is the element type of the leading shared_ptr.
static_assert(std::is_same_v<
    rdetail::leading_context_t<
        decltype(+[](std::shared_ptr<user_context>, int){ return 0; })>,
    user_context>);

// ---- Session construction with and without a Context -----------------------

BOOST_AUTO_TEST_CASE(test_basic_http_default_alias_is_no_context)
{
    static_assert(std::is_same_v<
        taar::session::http,
        taar::session::basic_http<no_context>>);

    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/ping",
        [](http::request<http::empty_body> const&, taar::matcher::context const&)
        -> http::message_generator
        {
            return http::response<http::empty_body>{http::status::ok, 11};
        });
}

BOOST_AUTO_TEST_CASE(test_basic_http_with_user_context_accepts_context_handler)
{
    basic_http<user_context> session;

    // Raw handler that opts in.
    session.register_request_handler(
        method == http::verb::get && target == "/raw",
        [](std::shared_ptr<user_context> ctx,
           http::request<http::empty_body> const& req,
           taar::matcher::context const&)
        -> http::message_generator
        {
            ctx->counter += 1;
            return http::response<http::empty_body>{http::status::ok, req.version()};
        });

    // rest()-built handler that opts in.
    session.register_request_handler(
        method == http::verb::get && target == "/rest/{x}",
        rest(
            [](std::shared_ptr<user_context> ctx, int x)
            {
                ctx->counter += x;
                return ctx->counter;
            },
            path_arg("x")));

    // Plain handler that does *not* opt in: still allowed.
    session.register_request_handler(
        method == http::verb::get && target == "/plain",
        rest([]{ return std::string{"hello"}; }));
}

BOOST_AUTO_TEST_CASE(test_basic_http_no_context_accepts_legacy_handlers)
{
    // A no_context session keeps accepting the pre-Phase-0 handler
    // shape unchanged.
    basic_http<no_context> session;
    session.register_request_handler(
        method == http::verb::get && target == "/api/sum/{a}/{b}",
        rest(
            [](int a, int b){ return a + b; },
            path_arg("a"),
            path_arg("b")));
}

// ---- SFINAE probe for the session/handler context-type match check --------
//
// register_request_handler emits a static_assert when a handler advertises
// a shared_ptr<T> whose T does not match the session's Context. We can't
// test the failing case directly without breaking compilation, but we can
// confirm the positive case (matching types) lets the registration go
// through and that the negative case is rejected at the right level by
// reading the advertises/takes traits independently.

static_assert(detail::advertises_connection_context<decltype(&raw_with_other_ctx)>);
static_assert(!detail::takes_connection_context<decltype(&raw_with_other_ctx), user_context>);
// → register_request_handler<basic_http<user_context>, raw_with_other_ctx>
//   would fire the static_assert with the diagnostic message; the trait
//   pair above is exactly what its `if constexpr` reads.

} // namespace
