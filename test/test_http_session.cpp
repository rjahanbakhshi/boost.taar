//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/core/response_builder.hpp>
#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/test/unit_test.hpp>
#include <string>

namespace {

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

struct object_type
{
    http::message_generator fn1_const(
        http::request<boost::beast::http::empty_body> const& request,
        boost::taar::matcher::context const&) const
    {
        http::response<http::string_body> res {
            boost::beast::http::status::ok,
            request.version()};
        res.set(boost::beast::http::field::content_type, "text/html");
        res.keep_alive(request.keep_alive());
        res.body() = "Hello";
        res.prepare_payload();
        return res;
    }

    http::message_generator fn1(
        http::request<boost::beast::http::empty_body> const& request,
        boost::taar::matcher::context const& context)
    {
        return fn1_const(request, context);
    }
};

void on_hard_error(int, std::exception_ptr)
{}

BOOST_AUTO_TEST_CASE(test_http_session)
{
    taar::session::http http_session;

    http_session.set_hard_error_handler(
        [](std::exception_ptr eptr)
        {
            try
            {
                std::rethrow_exception(eptr);
            }
            catch (std::exception const& e)
            {
                std::cerr << e.what() << '\n';
            }
            catch (...)
            {
                std::cerr << "Unknown error!\n";
            }
        });

    http_session.set_hard_error_handler(std::bind_front(on_hard_error, 10));

    http_session.set_soft_error_handler(
        [](std::exception_ptr)
        {
            return boost::json::value{{"Error", "Oops!"}};
        }
    );

    http_session.register_request_handler(
        method == http::verb::get && target == "/special/{*path}",
        [](
            http::request<http::empty_body> const& request,
            taar::matcher::context const& context) -> http::message_generator
        {
            http::response<http::string_body> res {
                boost::beast::http::status::ok,
                request.version()};
            res.set(boost::beast::http::field::content_type, "text/html");
            res.keep_alive(request.keep_alive());
            res.body() = "Special path is: " + context.path_args.at("*path");
            res.prepare_payload();
            return res;
        }
    );

    http_session.register_request_handler(
        method == http::verb::get && target == "/api/version",
        taar::handler::rest([]{ return "1.0"; })
    );

    http_session.register_request_handler(
        method == http::verb::get && target == "/api/throw",
        taar::handler::rest([]{ throw std::runtime_error{"Error"}; })
    );

    http_session.register_request_handler(
        method == http::verb::get && target == "/api/sum/{a}/{b}",
        taar::handler::rest([](int a, int b)
        {
            return boost::json::value {
                {"a", a},
                {"b", b},
                {"result", a + b}
            };
        },
        taar::handler::path_arg("a"),
        taar::handler::path_arg("b"))
    );

    http_session.register_request_handler(
        method == http::verb::post && target == "/api/store/",
        taar::handler::rest([](std::string const& value)
        {
        },
        taar::handler::query_arg("value"))
    );

    object_type object;
    http_session.register_request_handler(
        method == http::verb::post && target == "/api/custom",
        &object_type::fn1,
        &object
    );
    http_session.register_request_handler(
        method == http::verb::post && target == "/api/custom",
        &object_type::fn1_const,
        &object
    );

    object_type const const_object;
    http_session.register_request_handler(
        method == http::verb::post && target == "/api/custom",
        &object_type::fn1_const,
        &const_object
    );
}

BOOST_AUTO_TEST_CASE(test_http_session_soft_error_handler)
{
    using taar::awaitable;
    using taar::response_builder;
    taar::session::http http_session;

    //http_session.set_soft_error_handler([](std::exception_ptr){});
    //http_session.set_soft_error_handler([](std::exception_ptr) -> awaitable<void> { return {}; });

    //http_session.set_soft_error_handler([](std::exception_ptr) -> int { return {}; });
    //http_session.set_soft_error_handler([](std::exception_ptr) -> awaitable<int> { return {}; });

    //http_session.set_soft_error_handler([](std::exception_ptr) -> response_builder { return {}; });
    //http_session.set_soft_error_handler([](std::exception_ptr) -> awaitable<response_builder> { return {}; });
}

taar::awaitable<int> awaitable_handler(
    http::request<boost::beast::http::empty_body> const& request,
    boost::taar::matcher::context const&)
{
    co_return 10;
}

BOOST_AUTO_TEST_CASE(test_http_session_awaitable_handler)
{
    taar::session::http http_session;
    http_session.register_request_handler(
        method == http::verb::post,
        &awaitable_handler);
}

struct move_only_handler
{
    move_only_handler(int i) : value {i}
    {}

    move_only_handler(move_only_handler const&) = delete;
    move_only_handler(move_only_handler&&) = default;
    move_only_handler& operator=(move_only_handler const&) = delete;
    move_only_handler& operator=(move_only_handler&&) = default;

    int operator()(
        http::request<boost::beast::http::empty_body> const& request,
        boost::taar::matcher::context const&)
    {
        return value;
    }
    int value;
};

BOOST_AUTO_TEST_CASE(test_http_session_move_only_handler)
{
    taar::session::http http_session;
    http_session.register_request_handler(
        method == http::verb::post,
        move_only_handler{13});
}

struct move_only_handler_const
{
    move_only_handler_const(int i) : value {i}
    {}

    move_only_handler_const(move_only_handler_const const&) = delete;
    move_only_handler_const(move_only_handler_const&&) = default;
    move_only_handler_const& operator=(move_only_handler_const const&) = delete;
    move_only_handler_const& operator=(move_only_handler_const&&) = default;

    int operator()(
        http::request<boost::beast::http::empty_body> const& request,
        boost::taar::matcher::context const&) const
    {
        return value;
    }
    int value;
};

BOOST_AUTO_TEST_CASE(test_http_session_move_only_handler_const)
{
    taar::session::http http_session;
    move_only_handler_const fn{13};
    http_session.register_request_handler(
        method == http::verb::post,
        std::move(fn));
}

taar::async_generator<std::string> chunked_handler(
    http::request<boost::beast::http::empty_body> const& request,
    boost::taar::matcher::context const&)
{
    co_yield "first chunk";
    co_yield "second chunk";
}

taar::awaitable<taar::async_generator<std::string>> async_chunked_handler(
    http::request<boost::beast::http::empty_body> const& request,
    boost::taar::matcher::context const&)
{
    co_return chunked_handler(request, {});
}

BOOST_AUTO_TEST_CASE(test_http_session_chunked_handler)
{
    taar::session::http http_session;

    // Sync handler returning async_generator
    http_session.register_request_handler(
        method == http::verb::get && target == "/api/stream",
        &chunked_handler);

    // Lambda returning async_generator
    http_session.register_request_handler(
        method == http::verb::get && target == "/api/stream2",
        [](
            http::request<http::empty_body> const& request,
            taar::matcher::context const& context) -> taar::async_generator<std::string>
        {
            co_yield "chunk1";
            co_yield "chunk2";
        });

    // Async handler returning awaitable<async_generator>
    http_session.register_request_handler(
        method == http::verb::get && target == "/api/stream3",
        &async_chunked_handler);
}

BOOST_AUTO_TEST_CASE(test_http_session_request_header_limit)
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using net::ip::tcp;

    net::io_context io;
    taar::session::http http_session;
    http_session.set_request_header_limit(128);

    bool hard_error_called = false;
    http_session.set_hard_error_handler([&](std::exception_ptr) {
        hard_error_called = true;
    });

    // Register a handler so the request would otherwise match
    http_session.register_request_handler(
        method == http::verb::get,
        [](http::request<http::empty_body> const&, taar::matcher::context const&)
        { return std::string{"ok"}; });

    http::status client_response_status{};

    net::co_spawn(io, [&]() -> taar::awaitable<void> {
        auto executor = co_await net::this_coro::executor;

        taar::rebind_executor<tcp::acceptor> acceptor{executor};
        acceptor.open(tcp::v4());
        acceptor.set_option(net::socket_base::reuse_address(true));
        acceptor.bind({tcp::v4(), 0});
        acceptor.listen();
        auto const server_port = acceptor.local_endpoint().port();

        net::co_spawn(executor, [&, server_port]() -> taar::awaitable<void> {
            taar::rebind_executor<tcp::socket> client{co_await net::this_coro::executor};
            co_await client.async_connect({net::ip::address_v4::loopback(), server_port});

            // Send a request with a header far exceeding the 128-byte limit
            http::request<http::empty_body> req{http::verb::get, "/", 11};
            req.set(http::field::host, "localhost");
            req.set("X-Big-Header", std::string(300, 'A'));
            co_await http::async_write(client, req);

            boost::beast::flat_buffer buf;
            http::response<http::empty_body> response;
            auto [rec, rsz] = co_await http::async_read(client, buf, response);
            if (!rec)
                client_response_status = response.result();
        }, net::detached);

        auto [accept_ec, server_socket] = co_await acceptor.async_accept();
        if (!accept_ec)
        {
            taar::cancellation_signals signals;
            co_await http_session(std::move(server_socket), signals);
        }
    }, net::detached);

    io.run();

    BOOST_TEST(client_response_status == http::status::request_header_fields_too_large);
    BOOST_TEST(hard_error_called);
}

BOOST_AUTO_TEST_CASE(test_http_session_request_body_limit_no_match)
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using net::ip::tcp;

    net::io_context io;
    taar::session::http http_session;
    http_session.set_request_body_limit(16);
    // No handlers registered → no-match path

    bool hard_error_called = false;
    http_session.set_hard_error_handler([&](std::exception_ptr) {
        hard_error_called = true;
    });

    http::status client_response_status{};

    net::co_spawn(io, [&]() -> taar::awaitable<void> {
        auto executor = co_await net::this_coro::executor;

        taar::rebind_executor<tcp::acceptor> acceptor{executor};
        acceptor.open(tcp::v4());
        acceptor.set_option(net::socket_base::reuse_address(true));
        acceptor.bind({tcp::v4(), 0});
        acceptor.listen();
        auto const server_port = acceptor.local_endpoint().port();

        net::co_spawn(executor, [&, server_port]() -> taar::awaitable<void> {
            taar::rebind_executor<tcp::socket> client{co_await net::this_coro::executor};
            co_await client.async_connect({net::ip::address_v4::loopback(), server_port});

            // POST with a body exceeding the 16-byte limit
            http::request<http::string_body> req{http::verb::post, "/unmatched", 11};
            req.set(http::field::host, "localhost");
            req.body() = std::string(100, 'X');
            req.prepare_payload();
            co_await http::async_write(client, req);

            boost::beast::flat_buffer buf;
            http::response<http::empty_body> response;
            auto [rec, rsz] = co_await http::async_read(client, buf, response);
            if (!rec)
                client_response_status = response.result();
        }, net::detached);

        auto [accept_ec, server_socket] = co_await acceptor.async_accept();
        if (!accept_ec)
        {
            taar::cancellation_signals signals;
            co_await http_session(std::move(server_socket), signals);
        }
    }, net::detached);

    io.run();

    BOOST_TEST(client_response_status == http::status::payload_too_large);
    BOOST_TEST(hard_error_called);
}

BOOST_AUTO_TEST_CASE(test_http_session_request_body_limit_matched)
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using net::ip::tcp;

    net::io_context io;
    taar::session::http http_session;
    http_session.set_request_body_limit(16);

    bool hard_error_called = false;
    http_session.set_hard_error_handler([&](std::exception_ptr) {
        hard_error_called = true;
    });

    // Register a POST handler that accepts a string body
    http_session.register_request_handler(
        method == http::verb::post,
        [](http::request<http::string_body> const&, taar::matcher::context const&)
        { return std::string{"ok"}; });

    http::status client_response_status{};

    net::co_spawn(io, [&]() -> taar::awaitable<void> {
        auto executor = co_await net::this_coro::executor;

        taar::rebind_executor<tcp::acceptor> acceptor{executor};
        acceptor.open(tcp::v4());
        acceptor.set_option(net::socket_base::reuse_address(true));
        acceptor.bind({tcp::v4(), 0});
        acceptor.listen();
        auto const server_port = acceptor.local_endpoint().port();

        net::co_spawn(executor, [&, server_port]() -> taar::awaitable<void> {
            taar::rebind_executor<tcp::socket> client{co_await net::this_coro::executor};
            co_await client.async_connect({net::ip::address_v4::loopback(), server_port});

            // POST with a body exceeding the 16-byte limit
            http::request<http::string_body> req{http::verb::post, "/", 11};
            req.set(http::field::host, "localhost");
            req.body() = std::string(100, 'Y');
            req.prepare_payload();
            co_await http::async_write(client, req);

            boost::beast::flat_buffer buf;
            http::response<http::empty_body> response;
            auto [rec, rsz] = co_await http::async_read(client, buf, response);
            if (!rec)
                client_response_status = response.result();
        }, net::detached);

        auto [accept_ec, server_socket] = co_await acceptor.async_accept();
        if (!accept_ec)
        {
            taar::cancellation_signals signals;
            co_await http_session(std::move(server_socket), signals);
        }
    }, net::detached);

    io.run();

    BOOST_TEST(client_response_status == http::status::payload_too_large);
    BOOST_TEST(hard_error_called);
}

} // namespace
