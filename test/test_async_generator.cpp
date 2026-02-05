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
#include <boost/asio/cancellation_signal.hpp>
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

// Generator chaining tests

async_generator<int> inner_gen()
{
    co_yield 10;
    co_yield 20;
}

async_generator<int> outer_with_inner()
{
    co_yield 1;
    co_yield inner_gen();
    co_yield 100;
}

BOOST_AUTO_TEST_CASE(test_async_generator_chaining)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_with_inner();
            gen.set_executor(ioc.get_executor());

            std::vector<int> values;

            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 4u);
            BOOST_TEST(values[0] == 1);
            BOOST_TEST(values[1] == 10);
            BOOST_TEST(values[2] == 20);
            BOOST_TEST(values[3] == 100);
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

async_generator<int> empty_inner()
{
    co_return;
}

async_generator<int> outer_with_empty_inner()
{
    co_yield 1;
    co_yield empty_inner();
    co_yield 2;
}

BOOST_AUTO_TEST_CASE(test_async_generator_chaining_empty_inner)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_with_empty_inner();
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

async_generator<int> throwing_inner()
{
    co_yield 1;
    throw std::runtime_error("inner error");
}

async_generator<int> outer_with_throwing_inner()
{
    co_yield 0;
    co_yield throwing_inner();
    co_yield 100;  // Never reached
}

BOOST_AUTO_TEST_CASE(test_async_generator_chaining_inner_throws)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_with_throwing_inner();
            gen.set_executor(ioc.get_executor());

            std::vector<int> values;

            // Get first value (0 from outer)
            auto [ec1, value1] = co_await gen.next();
            BOOST_TEST(value1.has_value());
            BOOST_TEST(*value1 == 0);
            values.push_back(*value1);

            // Get second value (1 from inner)
            auto [ec2, value2] = co_await gen.next();
            BOOST_TEST(value2.has_value());
            BOOST_TEST(*value2 == 1);
            values.push_back(*value2);

            // Third call - inner throws, should get nullopt
            auto [ec3, value3] = co_await gen.next();
            BOOST_TEST(!value3.has_value());

            // Exception is available via rethrow_if_exception
            bool caught = false;
            try
            {
                gen.rethrow_if_exception();
            }
            catch (std::runtime_error const& e)
            {
                BOOST_TEST(std::string(e.what()) == "inner error");
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

async_generator<int> outer_multiple_inner()
{
    co_yield inner_gen();  // 10, 20
    co_yield inner_gen();  // 10, 20
}

BOOST_AUTO_TEST_CASE(test_async_generator_multiple_chaining)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_multiple_inner();
            gen.set_executor(ioc.get_executor());

            std::vector<int> values;

            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 4u);
            BOOST_TEST(values[0] == 10);
            BOOST_TEST(values[1] == 20);
            BOOST_TEST(values[2] == 10);
            BOOST_TEST(values[3] == 20);
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// Test co_await this_coro::executor support

async_generator<std::string> generator_with_direct_timer()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield std::string{"first"};
    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait(net::use_awaitable_t<net::io_context::executor_type>{});
    co_yield std::string{"second"};
}

BOOST_AUTO_TEST_CASE(test_async_generator_this_coro_executor)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = generator_with_direct_timer();
            gen.set_executor(ioc.get_executor());

            std::vector<std::string> values;
            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 2u);
            BOOST_TEST(values[0] == "first");
            BOOST_TEST(values[1] == "second");
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// Test natural async syntax with deferred operations (no explicit completion token)

async_generator<std::string> generator_with_natural_timer()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield std::string{"first"};
    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait();  // No token needed!
    co_yield std::string{"second"};
}

BOOST_AUTO_TEST_CASE(test_async_generator_deferred_operations)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = generator_with_natural_timer();
            gen.set_executor(ioc.get_executor());

            std::vector<std::string> values;
            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 2u);
            BOOST_TEST(values[0] == "first");
            BOOST_TEST(values[1] == "second");
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// Cancellation tests

async_generator<int> cancellation_aware_generator()
{
    namespace net = boost::asio;

    co_yield 1;

    auto cs = co_await net::this_coro::cancellation_state;
    if (cs.cancelled() != net::cancellation_type::none)
    {
        co_return;  // Early exit
    }

    co_yield 2;
}

BOOST_AUTO_TEST_CASE(test_async_generator_cancellation_state)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = cancellation_aware_generator();
            gen.set_executor(ioc.get_executor());
            gen.set_cancellation_slot(signal.slot());

            auto [ec1, v1] = co_await gen.next();
            BOOST_TEST(v1.has_value());
            BOOST_TEST(*v1 == 1);

            // Emit cancellation
            signal.emit(boost::asio::cancellation_type::terminal);

            auto [ec2, v2] = co_await gen.next();
            BOOST_TEST(!v2.has_value());  // Generator exited early

            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

async_generator<int> generator_with_long_timer()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield 1;

    timer.expires_after(std::chrono::hours{1});  // Long wait
    co_await timer.async_wait();  // Should be cancelled

    co_yield 2;  // Never reached
}

BOOST_AUTO_TEST_CASE(test_async_generator_nested_cancellation)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;
    bool completed = false;
    bool got_exception = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = generator_with_long_timer();
            gen.set_executor(ioc.get_executor());
            gen.set_cancellation_slot(signal.slot());

            auto [ec1, v1] = co_await gen.next();
            BOOST_TEST(*v1 == 1);

            // Emit cancellation while timer is waiting
            boost::asio::post(ioc, [&]()
            {
                signal.emit(boost::asio::cancellation_type::terminal);
            });

            // The timer should be cancelled and exception thrown
            try
            {
                auto [ec2, v2] = co_await gen.next();
                // Should get nullopt due to operation_aborted exception
                BOOST_TEST(!v2.has_value());
            }
            catch (boost::system::system_error const& e)
            {
                BOOST_TEST(e.code() == boost::asio::error::operation_aborted);
                got_exception = true;
            }

            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
    // Either we got nullopt or an exception - both are valid
    BOOST_TEST((completed || got_exception));
}

// Non-default-constructible type for testing
struct non_default_constructible
{
    int value;
    explicit non_default_constructible(int v) : value(v) {}
    non_default_constructible() = delete;
    non_default_constructible(non_default_constructible const&) = default;
    non_default_constructible(non_default_constructible&&) = default;
    non_default_constructible& operator=(non_default_constructible const&) = default;
    non_default_constructible& operator=(non_default_constructible&&) = default;
};

awaitable<non_default_constructible> get_ndc_value()
{
    co_return non_default_constructible{42};
}

async_generator<int> generator_with_ndc_await()
{
    auto ndc = co_await get_ndc_value();
    co_yield ndc.value;
    auto ndc2 = co_await get_ndc_value();
    co_yield ndc2.value + 1;
}

BOOST_AUTO_TEST_CASE(test_async_generator_non_default_constructible)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = generator_with_ndc_await();
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
            BOOST_TEST(values[0] == 42);
            BOOST_TEST(values[1] == 43);
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// Test that cancellation_state returns a default state when no slot is set
async_generator<int> generator_check_default_cancellation()
{
    namespace net = boost::asio;

    auto cs = co_await net::this_coro::cancellation_state;
    // With no cancellation slot set, should return default state (not cancelled)
    if (cs.cancelled() == net::cancellation_type::none)
    {
        co_yield 1;
    }
    co_yield 2;
}

BOOST_AUTO_TEST_CASE(test_async_generator_no_cancellation_slot)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = generator_check_default_cancellation();
            gen.set_executor(ioc.get_executor());
            // No cancellation slot set

            auto [ec1, v1] = co_await gen.next();
            BOOST_TEST(v1.has_value());
            BOOST_TEST(*v1 == 1);

            auto [ec2, v2] = co_await gen.next();
            BOOST_TEST(v2.has_value());
            BOOST_TEST(*v2 == 2);

            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// Auto-capture executor tests (no set_executor() needed)

BOOST_AUTO_TEST_CASE(test_async_generator_auto_executor)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = three_values();
            // No set_executor() call — executor should be auto-captured from caller

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

BOOST_AUTO_TEST_CASE(test_async_generator_auto_executor_with_co_await)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = generator_with_timer();
            // No set_executor() call — executor should be auto-captured from caller

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

BOOST_AUTO_TEST_CASE(test_async_generator_auto_executor_with_chaining)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_with_inner();
            // No set_executor() call — executor should be auto-captured from caller

            std::vector<int> values;

            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 4u);
            BOOST_TEST(values[0] == 1);
            BOOST_TEST(values[1] == 10);
            BOOST_TEST(values[2] == 20);
            BOOST_TEST(values[3] == 100);
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
