//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/beast/http/empty_body.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/core/response_builder.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/test/unit_test.hpp>

namespace {

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

struct object_type
{
    http::message_generator fn1_const(
        const http::request<boost::beast::http::empty_body>& request,
        const boost::taar::matcher::context&) const
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
        const http::request<boost::beast::http::empty_body>& request,
        const boost::taar::matcher::context& context)
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
            catch (const std::exception& e)
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
        method == http::verb::get && target == "/special/{*}",
        [](
            const http::request<http::empty_body>& request,
            const taar::matcher::context& context) -> http::message_generator
        {
            http::response<http::string_body> res {
                boost::beast::http::status::ok,
                request.version()};
            res.set(boost::beast::http::field::content_type, "text/html");
            res.keep_alive(request.keep_alive());
            res.body() = "Special path is: " + context.path_args.at("*");
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
        taar::handler::rest([](const std::string& value)
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

    const object_type const_object;
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
    const http::request<boost::beast::http::empty_body>& request,
    const boost::taar::matcher::context&)
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
        const http::request<boost::beast::http::empty_body>& request,
        const boost::taar::matcher::context&)
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

} // namespace
