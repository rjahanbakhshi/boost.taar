//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/cors.hpp>
#include <boost/taar/core/interceptor.hpp>
#include <boost/taar/core/request_attributes.hpp>
#include <boost/taar/handler/guard.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/json/parse.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
namespace mcp = boost::taar::mcp;
namespace openapi = boost::taar::openapi;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::attribute_arg;
using taar::handler::guard;
using taar::handler::rest;

struct principal { std::string user; };
struct request_id { int value = 0; };

/// Rejects requests without a bearer token; stores the token's user otherwise.
taar::interceptor_result require_bearer(http::request_header<> const& req, taar::matcher::context& ctx)
{
    auto const auth = req[http::field::authorization];
    if (!auth.starts_with("Bearer "))
    {
        return taar::reject(http::status::unauthorized);
    }
    ctx.attributes.set(principal {std::string {auth.substr(7)}});
    return taar::proceed();
}

/// Write @a raw on a new connection and read until the server closes it.
taar::awaitable<std::string> raw_exchange(unsigned short port, std::string raw)
{
    taar::rebind_executor<net::ip::tcp::socket> socket {co_await net::this_coro::executor};
    auto [cec] = co_await socket.async_connect({net::ip::address_v4::loopback(), port});
    if (cec) throw boost::system::system_error {cec};
    auto [wec, wn] = co_await net::async_write(socket, net::buffer(raw));
    if (wec) throw boost::system::system_error {wec};
    std::string out;
    auto [rec, rn] = co_await net::async_read(socket, net::dynamic_buffer(out));
    (void)rec;
    co_return out;
}

e2e::request with_bearer(e2e::request req, std::string const& user)
{
    req.set(http::field::authorization, "Bearer " + user);
    return req;
}

BOOST_AUTO_TEST_CASE(test_request_attributes_bag)
{
    taar::request_attributes bag;
    BOOST_TEST(bag.empty());
    BOOST_TEST(!bag.contains<principal>());
    BOOST_TEST(bag.find<principal>() == nullptr);

    bag.set(principal {"alice"});
    bag.set(request_id {7});
    BOOST_TEST(!bag.empty());
    BOOST_TEST(bag.contains<principal>());
    BOOST_TEST(bag.find<principal>()->user == "alice");
    BOOST_TEST(bag.find<request_id>()->value == 7);

    // One value per type: a second set replaces the first.
    bag.set(principal {"bob"});
    BOOST_TEST(bag.find<principal>()->user == "bob");

    // Mutable access, and const access through a copy.
    bag.find<request_id>()->value = 8;
    taar::request_attributes const copy = bag;
    BOOST_TEST(copy.find<request_id>()->value == 8);
    BOOST_TEST(!copy.contains<std::string>());
}

BOOST_AUTO_TEST_CASE(test_attribute_arg_provider)
{
    http::request_header<> header;
    taar::matcher::context ctx;
    attribute_arg<principal> const arg;

    BOOST_TEST(arg.name().find("principal") != std::string::npos);
    auto absent = arg(header, ctx);
    BOOST_TEST(absent.has_error());
    BOOST_TEST(absent.error().is(taar::error::argument_not_found));

    ctx.attributes.set(principal {"alice"});
    auto present = arg(header, ctx);
    BOOST_TEST(present.has_value());
    BOOST_TEST(present->user == "alice");

    // Not an input parameter for OpenAPI.
    static_assert(taar::handler::detail::location_of<attribute_arg<principal>>() ==
        taar::handler::param_location::none);
}

BOOST_AUTO_TEST_CASE(test_attribute_arg_optional_when_absent)
{
    net::io_context io;
    taar::session::http session;

    session.register_request_handler(
        method == http::verb::get && target == "/me",
        rest([](std::optional<principal> p) { return p ? p->user : std::string {"anonymous"}; },
            attribute_arg<principal>()));
    session.register_request_handler(
        method == http::verb::get && target == "/required",
        rest([](principal const& p) { return p.user; }, attribute_arg<principal>()));

    jsonrpc::endpoint<> rpc;
    rpc.method("whoami", [](std::optional<principal> p) { return p ? p->user : std::string {"anonymous"}; },
        attribute_arg<principal>());
    session.register_request_handler(method == http::verb::post && target == "/rpc", rpc.handler());

    mcp::endpoint<mcp::stateless> tools {{.name = "s", .version = "1"}};
    tools.tool("whoami", "Caller", [](std::optional<principal> p, std::string greeting)
    {
        return greeting + " " + (p ? p->user : std::string {"anonymous"});
    }, attribute_arg<principal>(), mcp::mcp_arg("greeting"));
    session.register_request_handler(target == "/mcp", tools.handler());

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto list = e2e::make_request(http::verb::post, "/mcp", R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
        auto call = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"whoami","arguments":{"greeting":"hi"}}})");
        auto r = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/me"),
            e2e::make_request(http::verb::get, "/required"),
            e2e::make_request(http::verb::post, "/rpc", R"({"jsonrpc":"2.0","id":1,"method":"whoami"})"),
            list,
            call);
        BOOST_TEST(r[0].body() == "anonymous");
        // A required attribute that nobody set is an argument failure.
        BOOST_TEST(r[1].result() == http::status::bad_request);
        BOOST_TEST(json::parse(r[2].body()).at("result").as_string() == "anonymous");

        // The attribute is not part of the tool's input schema.
        auto schema = json::parse(r[3].body()).at("result").at("tools").as_array()[0].at("inputSchema");
        BOOST_TEST(schema.at("properties").as_object().size() == 1u);
        BOOST_TEST(schema.at("properties").as_object().contains("greeting"));
        BOOST_TEST(json::parse(r[4].body()).at("result").at("structuredContent").at("result").as_string() == "hi anonymous");
    });
}

BOOST_AUTO_TEST_CASE(test_interceptor_reject_and_principal_on_every_surface)
{
    net::io_context io;
    taar::session::http session;
    taar::cors cors;
    session.set_response_filter(cors.response_filter());
    session.add_interceptor(&require_bearer);

    session.register_request_handler(
        method == http::verb::get && target == "/me",
        rest([](principal const& p) { return p.user; }, attribute_arg<principal>()));

    jsonrpc::endpoint<> rpc;
    rpc.method("whoami", [](principal const& p) { return p.user; }, attribute_arg<principal>());
    session.register_request_handler(method == http::verb::post && target == "/rpc", rpc.handler());

    mcp::endpoint<mcp::stateless> tools {{.name = "s", .version = "1"}};
    tools.tool("whoami", "Caller", [](principal const& p) { return p.user; }, attribute_arg<principal>());
    session.register_request_handler(target == "/mcp", tools.handler());

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto anonymous = e2e::make_request(http::verb::get, "/me");
        anonymous.set(http::field::origin, "http://app.example");
        auto r = co_await e2e::send_all(port,
            anonymous,
            with_bearer(e2e::make_request(http::verb::get, "/me"), "alice"),
            with_bearer(e2e::make_request(http::verb::post, "/rpc", R"({"jsonrpc":"2.0","id":1,"method":"whoami"})"), "bob"),
            with_bearer(e2e::make_request(http::verb::post, "/mcp",
                R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"whoami"}})"), "carol"),
            e2e::make_request(http::verb::post, "/rpc", R"({"jsonrpc":"2.0","id":3,"method":"whoami"})"));

        BOOST_TEST(r[0].result() == http::status::unauthorized);
        BOOST_TEST(r[0].body() == "Unauthorized");
        BOOST_TEST(r[0][http::field::content_type] == "text/plain");
        // The response filter ran on the rejection: a browser can read it.
        BOOST_TEST(r[0][http::field::access_control_allow_origin] == "*");
        BOOST_TEST(r[0].keep_alive());

        BOOST_TEST(r[1].body() == "alice");
        BOOST_TEST(json::parse(r[2].body()).at("result").as_string() == "bob");
        BOOST_TEST(json::parse(r[3].body()).at("result").at("structuredContent").at("result").as_string() == "carol");
        BOOST_TEST(r[4].result() == http::status::unauthorized);
    });
}

BOOST_AUTO_TEST_CASE(test_interceptor_matcher_scope)
{
    net::io_context io;
    taar::session::http session;
    taar::cors cors;
    session.set_response_filter(cors.response_filter());
    session.register_request_handler(method == http::verb::options, cors.preflight_handler());
    session.add_interceptor(!(method == http::verb::options) && !(target == "/health"), &require_bearer);

    // The scope's target template must not leak path arguments into the request.
    std::vector<std::size_t> path_arg_counts;
    session.add_interceptor(target == "/items/{id}",
        [&](http::request_header<> const&, taar::matcher::context& ctx)
        {
            path_arg_counts.push_back(ctx.path_args.size());
            return taar::proceed();
        });

    session.register_request_handler(method == http::verb::get && target == "/health", rest([] { return std::string {"ok"}; }));
    session.register_request_handler(
        method == http::verb::get && target == "/items/{id}",
        rest([](std::string id, principal const& p) { return id + ":" + p.user; },
            taar::handler::path_arg("id"), attribute_arg<principal>()));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto preflight = e2e::make_request(http::verb::options, "/items/1");
        preflight.set(http::field::origin, "http://app.example");
        preflight.set(http::field::access_control_request_method, "GET");
        auto r = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/health"),
            preflight,
            e2e::make_request(http::verb::get, "/items/1"),
            with_bearer(e2e::make_request(http::verb::get, "/items/7"), "alice"));
        BOOST_TEST(r[0].body() == "ok");
        BOOST_TEST(r[1].result() == http::status::no_content);
        BOOST_TEST(r[2].result() == http::status::unauthorized);
        BOOST_TEST(r[3].body() == "7:alice");
    });
    // The preflight and the authorized GET reached the scoped interceptor
    // (the 401 short-circuited); it saw no path arguments from its own template.
    BOOST_TEST(path_arg_counts.size() == 2u);
    BOOST_TEST(path_arg_counts.at(0) == 0u);
    BOOST_TEST(path_arg_counts.at(1) == 0u);
}

BOOST_AUTO_TEST_CASE(test_interceptor_order_and_short_circuit)
{
    net::io_context io;
    taar::session::http session;
    int second_runs = 0;

    session.add_interceptor([](http::request_header<> const& req, taar::matcher::context& ctx)
    {
        if (req.target() == "/deny") return taar::reject(http::status::forbidden, "denied by first");
        ctx.attributes.set(request_id {41});
        return taar::proceed();
    });
    session.add_interceptor([&](http::request_header<> const&, taar::matcher::context& ctx)
    {
        ++second_runs;
        auto* id = ctx.attributes.find<request_id>();
        if (!id) return taar::reject(http::status::internal_server_error);
        ++id->value;
        return taar::proceed();
    });
    session.register_request_handler(method == http::verb::get && target == "/{*path}",
        rest([](request_id id) { return id.value; }, attribute_arg<request_id>()));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/ok"),
            e2e::make_request(http::verb::get, "/deny"));
        BOOST_TEST(r[0].body() == "42");
        BOOST_TEST(r[1].result() == http::status::forbidden);
        BOOST_TEST(r[1].body() == "denied by first");
    });
    BOOST_TEST(second_runs == 1);
}

BOOST_AUTO_TEST_CASE(test_interceptor_rejection_does_not_read_body)
{
    net::io_context io;
    taar::session::http session;
    session.set_request_body_limit(1024);
    session.add_interceptor(&require_bearer);
    session.register_request_handler(
        method == http::verb::post && target == "/upload",
        rest([](std::string const& body) { return body.size(); }, taar::handler::string_body_arg()));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto ex = co_await net::this_coro::executor;
        taar::rebind_executor<net::ip::tcp::socket> socket {ex};
        auto [cec] = co_await socket.async_connect({net::ip::address_v4::loopback(), port});
        BOOST_TEST(!cec);

        // A body far beyond the limit is declared, and none of it is sent:
        // reading it would either hang or answer 413.
        std::string const head =
            "POST /upload HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Type: text/plain\r\nContent-Length: 10000000\r\n\r\n";
        auto [wec, wn] = co_await net::async_write(socket, net::buffer(head));
        BOOST_TEST(!wec);

        auto const start = std::chrono::steady_clock::now();
        boost::beast::flat_buffer buffer;
        e2e::response res;
        auto [rec, rn] = co_await http::async_read(socket, buffer, res);
        BOOST_TEST(!rec);
        BOOST_TEST(res.result() == http::status::unauthorized);
        BOOST_TEST(!res.keep_alive());
        BOOST_TEST((std::chrono::steady_clock::now() - start < std::chrono::seconds {2}));

        // The server closes the connection after the rejection.
        char byte = 0;
        auto [eec, en] = co_await net::async_read(socket, net::buffer(&byte, 1));
        BOOST_TEST(eec == net::error::eof);
    });
}

struct banned : std::runtime_error { using runtime_error::runtime_error; };

BOOST_AUTO_TEST_CASE(test_interceptor_async_and_throwing)
{
    net::io_context io;
    taar::session::http session;
    session.set_soft_error_handler([](taar::failure const& f)
    {
        http::response<http::string_body> res {http::status::internal_server_error, 11};
        if (auto const* e = f.as<banned>())
        {
            res.result(http::status::forbidden);
            res.body() = e->what();
        }
        res.prepare_payload();
        return res;
    });

    session.add_interceptor(
        [](http::request_header<> const& req, taar::matcher::context& ctx) -> taar::awaitable<taar::interceptor_result>
        {
            net::steady_timer timer {co_await net::this_coro::executor};
            timer.expires_after(std::chrono::milliseconds {5});
            co_await timer.async_wait(taar::use_awaitable);
            if (req.target() == "/slow-deny") co_return taar::reject(http::status::too_many_requests);
            ctx.attributes.set(request_id {5});
            co_return taar::proceed();
        });
    session.add_interceptor([](http::request_header<> const& req, taar::matcher::context&)
    {
        if (req.target() == "/banned") throw banned {"you are banned"};
        return taar::proceed();
    });
    session.register_request_handler(method == http::verb::get && target == "/{*path}",
        rest([](request_id id) { return id.value; }, attribute_arg<request_id>()));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/ok"),
            e2e::make_request(http::verb::get, "/slow-deny"),
            e2e::make_request(http::verb::get, "/banned"),
            e2e::make_request(http::verb::get, "/ok"));
        BOOST_TEST(r[0].body() == "5");
        BOOST_TEST(r[1].result() == http::status::too_many_requests);
        BOOST_TEST(r[2].result() == http::status::forbidden);
        BOOST_TEST(r[2].body() == "you are banned");
        // The connection survived the failure.
        BOOST_TEST(r[3].body() == "5");
    });
}

BOOST_AUTO_TEST_CASE(test_interceptor_runs_before_405_head_and_raw_handlers)
{
    net::io_context io;
    taar::session::http session;
    std::vector<std::string> seen_methods;
    session.add_interceptor([&](http::request_header<> const& req, taar::matcher::context&)
    {
        seen_methods.emplace_back(req.method_string());
        if (req.count("X-Deny")) return taar::reject(http::status::unauthorized);
        return taar::proceed();
    });
    session.register_request_handler(method == http::verb::get && target == "/thing", rest([] { return std::string {"thing"}; }));
    bool raw_ran = false;
    session.register_raw_handler(target == "/raw",
        [&](taar::matcher::context const&, http::request<http::empty_body>&&, boost::beast::tcp_stream&&,
            boost::beast::flat_buffer&&, taar::cancellation_signals&) -> taar::awaitable<void>
        {
            raw_ran = true;
            co_return;
        });

    auto denied = [](http::verb verb, std::string path)
    {
        auto req = e2e::make_request(verb, std::move(path));
        req.set("X-Deny", "1");
        return req;
    };

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto r = co_await e2e::send_all(port,
            denied(http::verb::put, "/thing"),
            denied(http::verb::get, "/raw"),
            e2e::make_request(http::verb::put, "/thing"));
        BOOST_TEST(r[0].result() == http::status::unauthorized);
        BOOST_TEST(r[1].result() == http::status::unauthorized);
        BOOST_TEST(r[2].result() == http::status::method_not_allowed);

        // A HEAD answer is header-only, so read it raw.
        auto head = co_await raw_exchange(port,
            "HEAD /thing HTTP/1.1\r\nHost: x\r\nX-Deny: 1\r\nConnection: close\r\n\r\n");
        BOOST_TEST(head.starts_with("HTTP/1.1 401 Unauthorized\r\n"));
        auto allowed = co_await raw_exchange(port,
            "HEAD /thing HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        BOOST_TEST(allowed.starts_with("HTTP/1.1 200 OK\r\n"));
    });
    BOOST_TEST(!raw_ran);
    // HEAD reached the interceptor as HEAD, before HEAD-from-GET rewrote it.
    BOOST_TEST(seen_methods.size() == 5u);
    BOOST_TEST(seen_methods.at(3) == "HEAD");
}

struct conn_stats { int intercepted = 0; };

BOOST_AUTO_TEST_CASE(test_interceptor_connection_context_injection)
{
    net::io_context io;
    taar::session::basic_http<conn_stats> session;
    session.add_interceptor([](conn_stats& c, http::request_header<> const&, taar::matcher::context&)
    {
        ++c.intercepted;
        return taar::proceed();
    });
    session.add_interceptor(target == "/count",
        [](std::shared_ptr<conn_stats> const& c, http::request_header<> const&, taar::matcher::context&)
        {
            ++c->intercepted;
            return taar::proceed();
        });
    session.register_request_handler(method == http::verb::get && target == "/{*path}",
        rest([](conn_stats const& c) { return c.intercepted; }));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto a = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/other"),
            e2e::make_request(http::verb::get, "/count"));
        BOOST_TEST(a[0].body() == "1");
        BOOST_TEST(a[1].body() == "3");
        // A new connection has a fresh context.
        auto b = co_await e2e::send_all(port, e2e::make_request(http::verb::get, "/other"));
        BOOST_TEST(b[0].body() == "1");
    });
}

taar::interceptor_result require_admin(http::request_header<> const& req, taar::matcher::context& ctx)
{
    if (req["X-Role"] != "admin") return taar::reject(http::status::forbidden, "admins only");
    ctx.attributes.set(principal {"admin"});
    return taar::proceed();
}

e2e::request as_admin(e2e::request req)
{
    req.set("X-Role", "admin");
    return req;
}

BOOST_AUTO_TEST_CASE(test_guard_typed_and_streaming_inner)
{
    net::io_context io;
    taar::session::http session;
    taar::cors cors;
    session.set_response_filter(cors.response_filter());

    session.register_request_handler(
        method == http::verb::get && target == "/secret/{id}",
        guard(&require_admin,
            rest([](std::string id, principal const& p) { return id + ":" + p.user; },
                taar::handler::path_arg("id"), attribute_arg<principal>())));
    session.register_request_handler(
        method == http::verb::get && target == "/stream",
        guard(&require_admin,
            rest([]() -> taar::chunked_response<std::string>
            {
                co_yield taar::set_header("X-Stream", "1");
                co_yield std::string {"a"};
                co_yield std::string {"b"};
            })));
    session.register_request_handler(
        method == http::verb::get && target == "/gen",
        guard(
            [](http::request_header<> const& req, taar::matcher::context& ctx) -> taar::awaitable<taar::interceptor_result>
            {
                net::steady_timer timer {co_await net::this_coro::executor};
                timer.expires_after(std::chrono::milliseconds {2});
                co_await timer.async_wait(taar::use_awaitable);
                co_return require_admin(req, ctx);
            },
            rest([]() -> taar::async_generator<std::string>
            {
                co_yield std::string {"x"};
                co_yield std::string {"y"};
            })));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto denied = e2e::make_request(http::verb::get, "/secret/3");
        denied.set(http::field::origin, "http://app.example");
        auto r = co_await e2e::send_all(port,
            denied,
            as_admin(e2e::make_request(http::verb::get, "/secret/3")),
            e2e::make_request(http::verb::get, "/stream"),
            as_admin(e2e::make_request(http::verb::get, "/stream")),
            e2e::make_request(http::verb::get, "/gen"),
            as_admin(e2e::make_request(http::verb::get, "/gen")));

        BOOST_TEST(r[0].result() == http::status::forbidden);
        BOOST_TEST(r[0].body() == "admins only");
        BOOST_TEST(r[0][http::field::access_control_allow_origin] == "*");
        BOOST_TEST(r[1].body() == "3:admin");

        BOOST_TEST(r[2].result() == http::status::forbidden);
        BOOST_TEST(r[2].body() == "admins only");
        BOOST_TEST(r[2][http::field::content_type] == "text/plain");
        BOOST_TEST(r[3].result() == http::status::ok);
        BOOST_TEST(r[3].body() == "ab");
        BOOST_TEST(r[3]["X-Stream"] == "1");

        BOOST_TEST(r[4].result() == http::status::forbidden);
        BOOST_TEST(r[5].body() == "xy");
    });
}

BOOST_AUTO_TEST_CASE(test_guard_forwards_params_to_openapi)
{
    taar::session::http session;
    openapi::generator gen {{.title = "t", .version = "1"}};

    auto handler = guard(&require_admin,
        rest([](int id, std::string q, principal const& p) { return std::to_string(id) + q + p.user; },
            taar::handler::path_arg("id", "Item id"), taar::handler::query_arg("q"), attribute_arg<principal>()));
    BOOST_TEST(handler.path_arg_names() == std::vector<std::string> {"id"});
    BOOST_TEST(handler.params().size() == 3u);
    static_assert(std::is_same_v<decltype(handler)::inner_type::request_type, decltype(handler)::request_type>);

    session.register_request_handler(method == http::verb::get && target == "/items/{id}", std::move(handler), gen.describe("Item"));
    auto const op = gen.document().at("paths").at("/items/{id}").at("get");
    auto const& params = op.at("parameters").as_array();
    BOOST_TEST(params.size() == 2u);
    BOOST_TEST(params[0].at("name").as_string() == "id");
    BOOST_TEST(params[0].at("description").as_string() == "Item id");
    BOOST_TEST(params[1].at("name").as_string() == "q");
}

struct cart { int items = 0; };

BOOST_AUTO_TEST_CASE(test_guard_composes_with_with_session)
{
    using taar::handler::session_policy;
    using taar::handler::with_session;
    namespace session_key = taar::handler::session_key;

    net::io_context io;
    taar::session::http session;
    taar::context_store<std::string, cart> carts {std::chrono::minutes {10}};

    // Guard inside with_session: the check sees the keyed context.
    session.register_request_handler(
        method == http::verb::post && target == "/add",
        with_session(carts, session_key::header("X-Session"), session_policy::create_if_missing,
            guard(
                [](cart const& c, http::request_header<> const&, taar::matcher::context&)
                {
                    if (c.items >= 2) return taar::reject(http::status::conflict, "cart full");
                    return taar::proceed();
                },
                rest([](cart& c) { return ++c.items; }))));

    // Guard outside with_session: the check runs before the entry is locked.
    session.register_request_handler(
        method == http::verb::get && target == "/cart",
        guard(&require_admin,
            with_session(carts, session_key::header("X-Session"), session_policy::require,
                rest([](cart const& c, principal const& p) { return p.user + ":" + std::to_string(c.items); },
                    attribute_arg<principal>()))));

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto first = co_await e2e::send_all(port, e2e::make_request(http::verb::post, "/add"));
        BOOST_TEST(first[0].body() == "1");
        std::string const key {first[0]["X-Session"]};
        BOOST_TEST(!key.empty());

        auto keyed = [&](e2e::request req)
        {
            req.set("X-Session", key);
            return req;
        };
        auto r = co_await e2e::send_all(port,
            keyed(e2e::make_request(http::verb::post, "/add")),
            keyed(e2e::make_request(http::verb::post, "/add")),
            keyed(e2e::make_request(http::verb::get, "/cart")),
            as_admin(keyed(e2e::make_request(http::verb::get, "/cart"))));
        BOOST_TEST(r[0].body() == "2");
        BOOST_TEST(r[1].result() == http::status::conflict);
        BOOST_TEST(r[1].body() == "cart full");
        BOOST_TEST(r[2].result() == http::status::forbidden);
        BOOST_TEST(r[3].body() == "admin:2");
    });
}

BOOST_AUTO_TEST_CASE(test_guard_with_const_matcher_context)
{
    // A handler called directly with a const context: the check's attribute
    // still reaches the inner handler, without writing through a const_cast.
    auto handler = guard(&require_admin,
        rest([](principal const& p) { return p.user; }, attribute_arg<principal>()));
    using request_type = decltype(handler)::request_type;

    auto call = [&](bool admin)
    {
        request_type req {http::verb::get, "/", 11};
        if (admin) req.set("X-Role", "admin");
        taar::matcher::context const ctx {};
        net::io_context io;
        std::optional<http::response<http::string_body>> out;
        net::co_spawn(io, [&]() -> taar::awaitable<void>
        {
            out = co_await handler(taar::handler::contexts<> {}, req, ctx);
        }, net::detached);
        io.run();
        BOOST_TEST(ctx.attributes.contains<principal>() == admin);
        return *out;
    };

    auto const ok = call(true);
    BOOST_TEST(ok.result() == http::status::ok);
    BOOST_TEST(ok.body() == "admin");
    auto const denied = call(false);
    BOOST_TEST(denied.result() == http::status::forbidden);
    BOOST_TEST(denied.body() == "admins only");
}

} // namespace
