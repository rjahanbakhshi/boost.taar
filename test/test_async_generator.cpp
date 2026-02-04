//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/is_async_generator.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using boost::taar::async_generator;
using boost::taar::is_async_generator;
using boost::taar::awaitable;

BOOST_AUTO_TEST_CASE(test_is_async_generator_trait)
{
    static_assert(is_async_generator<async_generator<int>>);
    static_assert(is_async_generator<async_generator<std::string>>);
    static_assert(!is_async_generator<int>);
    static_assert(!is_async_generator<std::string>);
    static_assert(!is_async_generator<awaitable<int>>);
}

async_generator<int> three_values()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

BOOST_AUTO_TEST_CASE(test_async_generator_basic)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = three_values();
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
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

async_generator<std::string> empty_generator()
{
    co_return;
}

BOOST_AUTO_TEST_CASE(test_async_generator_empty)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = empty_generator();
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

awaitable<void> async_delay(std::chrono::milliseconds ms)
{
    auto executor = co_await boost::asio::this_coro::executor;
    boost::asio::steady_timer timer{executor};
    timer.expires_after(ms);
    co_await timer.async_wait(
        boost::asio::use_awaitable_t<boost::asio::io_context::executor_type>{});
}

async_generator<int> generator_with_timer()
{
    co_yield 1;
    co_await async_delay(std::chrono::milliseconds{1});
    co_yield 2;
}

BOOST_AUTO_TEST_CASE(test_async_generator_with_timer)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = generator_with_timer();
            gen.set_executor(ioc.get_executor());

            std::vector<int> values;

            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 2u);
            BOOST_TEST(values[0] == 1);
            BOOST_TEST(values[1] == 2);
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

async_generator<int> throwing_generator()
{
    co_yield 1;
    throw std::runtime_error("generator error");
}

BOOST_AUTO_TEST_CASE(test_async_generator_exception)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = throwing_generator();
            gen.set_executor(ioc.get_executor());

            auto [ec1, value1] = co_await gen.next();
            BOOST_TEST(value1.has_value());
            BOOST_TEST(*value1 == 1);

            // The next call returns nullopt because the generator threw
            auto [ec2, value2] = co_await gen.next();
            BOOST_TEST(!value2.has_value());

            // The exception is available via rethrow_if_exception
            bool caught = false;
            try
            {
                gen.rethrow_if_exception();
            }
            catch (std::runtime_error const& e)
            {
                BOOST_TEST(std::string(e.what()) == "generator error");
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
