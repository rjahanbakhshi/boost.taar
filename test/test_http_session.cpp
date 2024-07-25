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
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
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
    taar::session::http http_session;

    http_session.set_soft_error_handler(
        [this](std::exception_ptr eptr)
        {
            try
            {
                std::rethrow_exception(eptr);
            }
            catch (...)
            {
                http::response<http::string_body> response {
                    boost::beast::http::status::internal_server_error,
                    11};
                return response;
            }
        }
    );
}

} // namespace
