//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/is_chunked_response.hpp>
#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using boost::taar::chunked_response;
using boost::taar::is_chunked_response;
using boost::taar::awaitable;
using boost::taar::set_status;
using boost::taar::set_header;
using boost::taar::error;
using boost::taar::make_error_code;
namespace http = boost::beast::http;

BOOST_AUTO_TEST_CASE(test_is_chunked_response_trait)
{
    static_assert(is_chunked_response<chunked_response<int>>);
    static_assert(is_chunked_response<chunked_response<std::string>>);
    static_assert(!is_chunked_response<int>);
    static_assert(!is_chunked_response<std::string>);
    static_assert(!is_chunked_response<awaitable<int>>);
    static_assert(!is_chunked_response<boost::taar::async_generator<int>>);
}

chunked_response<int> three_values_chunked()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

BOOST_AUTO_TEST_CASE(test_chunked_response_basic)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = three_values_chunked();
            gen.set_executor(ioc.get_executor());

            std::vector<int> values;

            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 3u);
            BOOST_TEST(values[0] == 1);
            BOOST_TEST(values[1] == 2);
            BOOST_TEST(values[2] == 3);

            // Default metadata
            BOOST_TEST(gen.status() == http::status::ok);
            BOOST_TEST(gen.headers().empty());
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

chunked_response<std::string> with_metadata()
{
    co_yield set_status(http::status::partial_content);
    co_yield set_header(http::field::content_type, "application/pdf");
    co_yield set_header("X-Custom", "custom-value");
    co_yield std::string{"chunk1"};
    co_yield std::string{"chunk2"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_metadata)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = with_metadata();
            gen.set_executor(ioc.get_executor());

            // First next() runs through all metadata yields and stops at first data yield
            auto [ec1, value1] = co_await gen.next();
            BOOST_TEST(value1.has_value());
            BOOST_TEST(*value1 == "chunk1");

            // Metadata should be available after first next()
            BOOST_TEST(gen.status() == http::status::partial_content);
            BOOST_TEST(gen.headers().size() == 2u);
            BOOST_TEST(gen.headers()[0].field == http::field::content_type);
            BOOST_TEST(gen.headers()[0].value == "application/pdf");
            BOOST_TEST(gen.headers()[1].field == http::field::unknown);
            BOOST_TEST(gen.headers()[1].name == "X-Custom");
            BOOST_TEST(gen.headers()[1].value == "custom-value");

            auto [ec2, value2] = co_await gen.next();
            BOOST_TEST(value2.has_value());
            BOOST_TEST(*value2 == "chunk2");

            auto [ec3, value3] = co_await gen.next();
            BOOST_TEST(!value3.has_value());

            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

chunked_response<std::string> empty_chunked()
{
    co_return;
}

BOOST_AUTO_TEST_CASE(test_chunked_response_empty)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = empty_chunked();
            gen.set_executor(ioc.get_executor());

            auto [ec, value] = co_await gen.next();
            BOOST_TEST(!value.has_value());
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

chunked_response<std::string> metadata_only_then_empty()
{
    co_yield set_status(http::status::no_content);
}

BOOST_AUTO_TEST_CASE(test_chunked_response_metadata_only)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = metadata_only_then_empty();
            gen.set_executor(ioc.get_executor());

            auto [ec, value] = co_await gen.next();
            BOOST_TEST(!value.has_value());

            BOOST_TEST(gen.status() == http::status::no_content);
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

chunked_response<int> throwing_chunked()
{
    co_yield 1;
    throw std::runtime_error("chunked error");
}

chunked_response<std::string> late_metadata()
{
    co_yield std::string{"chunk1"};
    co_yield set_status(http::status::not_found);
    co_yield std::string{"chunk2"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_late_metadata)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = late_metadata();
            gen.set_executor(ioc.get_executor());

            auto [ec1, value1] = co_await gen.next();
            BOOST_TEST(value1.has_value());
            BOOST_TEST(*value1 == "chunk1");

            auto [ec2, value2] = co_await gen.next();
            BOOST_TEST(!value2.has_value());

            bool caught = false;
            try
            {
                gen.rethrow_if_exception();
            }
            catch (boost::system::system_error const& e)
            {
                BOOST_TEST(e.code() == make_error_code(error::late_chunk_metadata));
                caught = true;
            }
            BOOST_TEST(caught);

            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

BOOST_AUTO_TEST_CASE(test_chunked_response_exception)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = throwing_chunked();
            gen.set_executor(ioc.get_executor());

            auto [ec1, value1] = co_await gen.next();
            BOOST_TEST(value1.has_value());
            BOOST_TEST(*value1 == 1);

            auto [ec2, value2] = co_await gen.next();
            BOOST_TEST(!value2.has_value());

            bool caught = false;
            try
            {
                gen.rethrow_if_exception();
            }
            catch (std::runtime_error const& e)
            {
                BOOST_TEST(std::string(e.what()) == "chunked error");
                caught = true;
            }
            BOOST_TEST(caught);

            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

} // namespace
