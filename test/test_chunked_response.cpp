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
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
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

// Chunked response chaining tests

chunked_response<std::string> inner_chunks()
{
    co_yield std::string{"inner1"};
    co_yield std::string{"inner2"};
}

chunked_response<std::string> outer_with_inner_chunks()
{
    co_yield set_status(http::status::partial_content);
    co_yield set_header(http::field::content_type, "text/plain");
    co_yield std::string{"outer1"};
    co_yield inner_chunks();  // inner1, inner2 (inner's metadata ignored)
    co_yield std::string{"outer2"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_chaining)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_with_inner_chunks();
            gen.set_executor(ioc.get_executor());

            std::vector<std::string> values;

            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            // Check values: outer1, inner1, inner2, outer2
            BOOST_TEST(values.size() == 4u);
            BOOST_TEST(values[0] == "outer1");
            BOOST_TEST(values[1] == "inner1");
            BOOST_TEST(values[2] == "inner2");
            BOOST_TEST(values[3] == "outer2");

            // Check metadata comes from outer only
            BOOST_TEST(gen.status() == http::status::partial_content);
            BOOST_TEST(gen.headers().size() == 1u);
            BOOST_TEST(gen.headers()[0].field == http::field::content_type);
            BOOST_TEST(gen.headers()[0].value == "text/plain");

            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

chunked_response<std::string> empty_inner_chunks()
{
    co_return;
}

chunked_response<std::string> outer_with_empty_inner_chunks()
{
    co_yield std::string{"before"};
    co_yield empty_inner_chunks();
    co_yield std::string{"after"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_chaining_empty_inner)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_with_empty_inner_chunks();
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
            BOOST_TEST(values[0] == "before");
            BOOST_TEST(values[1] == "after");
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

chunked_response<std::string> throwing_inner_chunks()
{
    co_yield std::string{"inner_ok"};
    throw std::runtime_error("inner chunk error");
}

chunked_response<std::string> outer_with_throwing_inner_chunks()
{
    co_yield std::string{"outer_start"};
    co_yield throwing_inner_chunks();
    co_yield std::string{"outer_end"};  // Never reached
}

BOOST_AUTO_TEST_CASE(test_chunked_response_chaining_inner_throws)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_with_throwing_inner_chunks();
            gen.set_executor(ioc.get_executor());

            // Get first value (outer_start)
            auto [ec1, value1] = co_await gen.next();
            BOOST_TEST(value1.has_value());
            BOOST_TEST(*value1 == "outer_start");

            // Get second value (inner_ok from inner)
            auto [ec2, value2] = co_await gen.next();
            BOOST_TEST(value2.has_value());
            BOOST_TEST(*value2 == "inner_ok");

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
                BOOST_TEST(std::string(e.what()) == "inner chunk error");
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

// Test co_await this_coro::executor support

chunked_response<std::string> chunked_with_direct_timer()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield std::string{"first"};
    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait(net::use_awaitable_t<net::io_context::executor_type>{});
    co_yield std::string{"second"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_this_coro_executor)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = chunked_with_direct_timer();
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

chunked_response<std::string> chunked_with_natural_timer()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield std::string{"first"};
    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait();  // No token needed!
    co_yield std::string{"second"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_deferred_operations)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = chunked_with_natural_timer();
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

chunked_response<std::string> cancellation_aware_chunked()
{
    namespace net = boost::asio;

    co_yield std::string{"first"};

    auto cs = co_await net::this_coro::cancellation_state;
    if (cs.cancelled() != net::cancellation_type::none)
    {
        co_return;  // Early exit
    }

    co_yield std::string{"second"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_cancellation_state)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = cancellation_aware_chunked();
            gen.set_executor(ioc.get_executor());
            gen.set_cancellation_slot(signal.slot());

            auto [ec1, v1] = co_await gen.next();
            BOOST_TEST(v1.has_value());
            BOOST_TEST(*v1 == "first");

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

chunked_response<std::string> chunked_with_long_timer()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield std::string{"first"};

    timer.expires_after(std::chrono::hours{1});  // Long wait
    co_await timer.async_wait();  // Should be cancelled

    co_yield std::string{"second"};  // Never reached
}

BOOST_AUTO_TEST_CASE(test_chunked_response_nested_cancellation)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;
    bool completed = false;
    bool got_exception = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = chunked_with_long_timer();
            gen.set_executor(ioc.get_executor());
            gen.set_cancellation_slot(signal.slot());

            auto [ec1, v1] = co_await gen.next();
            BOOST_TEST(*v1 == "first");

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
struct chunked_non_default_constructible
{
    int value;
    explicit chunked_non_default_constructible(int v) : value(v) {}
    chunked_non_default_constructible() = delete;
    chunked_non_default_constructible(chunked_non_default_constructible const&) = default;
    chunked_non_default_constructible(chunked_non_default_constructible&&) = default;
    chunked_non_default_constructible& operator=(chunked_non_default_constructible const&) = default;
    chunked_non_default_constructible& operator=(chunked_non_default_constructible&&) = default;
};

awaitable<chunked_non_default_constructible> get_chunked_ndc_value()
{
    co_return chunked_non_default_constructible{42};
}

chunked_response<int> chunked_with_ndc_await()
{
    auto ndc = co_await get_chunked_ndc_value();
    co_yield ndc.value;
    auto ndc2 = co_await get_chunked_ndc_value();
    co_yield ndc2.value + 1;
}

BOOST_AUTO_TEST_CASE(test_chunked_response_non_default_constructible)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = chunked_with_ndc_await();
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
chunked_response<std::string> chunked_check_default_cancellation()
{
    namespace net = boost::asio;

    auto cs = co_await net::this_coro::cancellation_state;
    // With no cancellation slot set, should return default state (not cancelled)
    if (cs.cancelled() == net::cancellation_type::none)
    {
        co_yield std::string{"first"};
    }
    co_yield std::string{"second"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_no_cancellation_slot)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = chunked_check_default_cancellation();
            gen.set_executor(ioc.get_executor());
            // No cancellation slot set

            auto [ec1, v1] = co_await gen.next();
            BOOST_TEST(v1.has_value());
            BOOST_TEST(*v1 == "first");

            auto [ec2, v2] = co_await gen.next();
            BOOST_TEST(v2.has_value());
            BOOST_TEST(*v2 == "second");

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

BOOST_AUTO_TEST_CASE(test_chunked_response_auto_executor)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = three_values_chunked();
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

BOOST_AUTO_TEST_CASE(test_chunked_response_auto_executor_with_co_await)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = chunked_with_direct_timer();
            // No set_executor() call — executor should be auto-captured from caller

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

BOOST_AUTO_TEST_CASE(test_chunked_response_auto_executor_with_chaining)
{
    boost::asio::io_context ioc;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_with_inner_chunks();
            // No set_executor() call — executor should be auto-captured from caller

            std::vector<std::string> values;

            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 4u);
            BOOST_TEST(values[0] == "outer1");
            BOOST_TEST(values[1] == "inner1");
            BOOST_TEST(values[2] == "inner2");
            BOOST_TEST(values[3] == "outer2");
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// ============================================================================
// Cancellation slot collision regression tests
//
// These tests exercise the exact trigger paths for the use-after-free bug
// where a shared cancellation_slot was reused across multiple co_spawn +
// bind_cancellation_slot operations. Each bind_cancellation_slot replaced the
// previous co_spawn_cancellation_handler, destroying its internal signal while
// outstanding frames still referenced it.
//
// Run under valgrind to verify no invalid memory accesses:
//   valgrind --leak-check=full build/test/boost-taar-test \
//       --run_test=test_chunked_response_flatten_async_inner_with_cancellation_slot
// ============================================================================

// Inner generator that performs async timer waits.
// When flattened inside an outer generator that has a cancellation slot set,
// each co_await triggers await_transform which co_spawns with a cancellation
// signal. Under the old code this co_spawn replaced the flattening co_spawn's
// handler on the shared slot, causing use-after-free.
chunked_response<std::string> inner_with_async_timer_ops()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait();
    co_yield std::string{"inner_a"};

    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait();
    co_yield std::string{"inner_b"};
}

// Outer generator that flattens two async inner generators with regular yields
// in between — maximises the number of cancellation slot interactions.
chunked_response<std::string> outer_flattening_async_inners()
{
    co_yield std::string{"outer_start"};
    co_yield inner_with_async_timer_ops();   // flatten first inner
    co_yield std::string{"outer_middle"};
    co_yield inner_with_async_timer_ops();   // flatten second inner
    co_yield std::string{"outer_end"};
}

// Primary UAF / leak regression test.
// Exercises: flattening co_spawn + inner await_transform co_spawns sharing
// the cancellation slot. Under the old code the slot handlers collide;
// under the fix each co_spawn gets its own cancellation_signal via shared_ptr.
BOOST_AUTO_TEST_CASE(test_chunked_response_flatten_async_inner_with_cancellation_slot)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_flattening_async_inners();
            gen.set_executor(ioc.get_executor());
            gen.set_cancellation_slot(signal.slot());

            std::vector<std::string> values;
            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 7u);
            BOOST_TEST(values[0] == "outer_start");
            BOOST_TEST(values[1] == "inner_a");
            BOOST_TEST(values[2] == "inner_b");
            BOOST_TEST(values[3] == "outer_middle");
            BOOST_TEST(values[4] == "inner_a");
            BOOST_TEST(values[5] == "inner_b");
            BOOST_TEST(values[6] == "outer_end");
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// Inner generator that performs an async op then checks cancellation state.
chunked_response<std::string> inner_async_then_cancel_check()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    // Async op exercises the co_spawn slot collision path during flattening.
    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait();

    co_yield std::string{"inner_after_timer"};

    // Cooperatively check for cancellation.
    auto cs = co_await net::this_coro::cancellation_state;
    if (cs.cancelled() != net::cancellation_type::none)
        co_return;

    co_yield std::string{"inner_not_cancelled"};
}

chunked_response<std::string> outer_cancel_flatten_async()
{
    co_yield std::string{"outer_start"};
    co_yield inner_async_then_cancel_check();
    co_yield std::string{"outer_end"};
}

// Cancellation during flattening with an async inner generator.
// Verifies the per-operation signal chain correctly propagates cancellation
// from the external slot through flattening_cancel_signal_ to the inner
// generator's cancellation_state_.
BOOST_AUTO_TEST_CASE(test_chunked_response_cancel_flatten_with_async_inner)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = outer_cancel_flatten_async();
            gen.set_executor(ioc.get_executor());
            gen.set_cancellation_slot(signal.slot());

            auto [ec1, v1] = co_await gen.next();
            BOOST_TEST(v1.has_value());
            BOOST_TEST(*v1 == "outer_start");

            // Inner's timer completes — this exercises the co_spawn collision path
            auto [ec2, v2] = co_await gen.next();
            BOOST_TEST(v2.has_value());
            BOOST_TEST(*v2 == "inner_after_timer");

            // Emit cancellation — propagates through flattening_cancel_signal_
            // to the inner's cancellation_state_.
            signal.emit(boost::asio::cancellation_type::terminal);

            // Inner sees cancellation and co_returns.
            // Flattening completes, outer resumes and yields "outer_end".
            auto [ec3, v3] = co_await gen.next();
            BOOST_TEST(v3.has_value());
            BOOST_TEST(*v3 == "outer_end");

            auto [ec4, v4] = co_await gen.next();
            BOOST_TEST(!v4.has_value());

            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// Generator with multiple sequential co_await timer operations.
// Each co_await triggers await_transform which creates a co_spawn with a
// cancellation signal. Under the old code each co_spawn replaced the previous
// handler on the shared slot.
chunked_response<std::string> chunked_multiple_sequential_async_ops()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield std::string{"before_first"};

    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait();
    co_yield std::string{"after_first"};

    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait();
    co_yield std::string{"after_second"};

    timer.expires_after(std::chrono::milliseconds{1});
    co_await timer.async_wait();
    co_yield std::string{"after_third"};
}

// Multiple sequential co_awaits with a cancellation slot set.
// Regression test for the secondary issue where each await_transform co_spawn
// replaced the previous one's handler on the shared slot.
BOOST_AUTO_TEST_CASE(test_chunked_response_multiple_co_awaits_with_cancellation_slot)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto gen = chunked_multiple_sequential_async_ops();
            gen.set_executor(ioc.get_executor());
            gen.set_cancellation_slot(signal.slot());

            std::vector<std::string> values;
            while (true)
            {
                auto [ec, value] = co_await gen.next();
                if (!value)
                    break;
                values.push_back(*value);
            }

            BOOST_TEST(values.size() == 4u);
            BOOST_TEST(values[0] == "before_first");
            BOOST_TEST(values[1] == "after_first");
            BOOST_TEST(values[2] == "after_second");
            BOOST_TEST(values[3] == "after_third");
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);
}

// ============================================================================
// Frame leak regression tests
//
// These tests exercise the scenario where a chunked_response is destroyed
// while a co_spawn from await_transform is still in flight. Without the
// alive flag + destructor cancellation, the co_spawn's wrapper frame leaks.
//
// Run under valgrind to verify no leaked blocks:
//   valgrind --leak-check=full build/test/boost-taar-test \
//       --run_test=test_chunked_response_destroy_with_pending_co_spawn
// ============================================================================

// Generator that blocks on a long timer — simulates a pending async operation
// (e.g., reading from a K8s log stream) that outlives the generator.
chunked_response<std::string> chunked_with_long_blocking_op()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield std::string{"ready"};

    // Long wait — will be cancelled when the generator is destroyed.
    timer.expires_after(std::chrono::hours{1});
    co_await timer.async_wait();

    co_yield std::string{"unreachable"};
}

// Primary frame-leak regression test.
// The generator is destroyed while its await_transform co_spawn is pending.
// The destructor must cancel the co_spawn (via active_co_spawn_signal_) and
// the alive flag must prevent the completion handler from accessing the freed
// coroutine frame.
BOOST_AUTO_TEST_CASE(test_chunked_response_destroy_with_pending_co_spawn)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;
    bool completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            {
                auto gen = chunked_with_long_blocking_op();
                gen.set_executor(ioc.get_executor());
                gen.set_cancellation_slot(signal.slot());

                auto [ec1, v1] = co_await gen.next();
                BOOST_TEST(v1.has_value());
                BOOST_TEST(*v1 == "ready");

                // Start second next() — this triggers the long timer via
                // await_transform, posting a co_spawn.
                // Then post gen destruction from inside the coroutine.
                // We use a helper awaitable to start next() and then
                // cancel it by destroying the generator.
            }
            // gen is destroyed here while the second next() was never started.
            // This is the simple case — no pending co_spawn.
            completed = true;
        },
        [](std::exception_ptr ep)
        {
            if (ep) std::rethrow_exception(ep);
        });

    ioc.run();
    BOOST_TEST(completed);

    // Now test the hard case: gen is destroyed while a co_spawn IS pending.
    // We drive gen.next() manually using async_initiate outside of co_await.
    completed = false;
    ioc.restart();

    auto gen = std::make_unique<chunked_response<std::string>>(
        chunked_with_long_blocking_op());
    gen->set_executor(ioc.get_executor());
    gen->set_cancellation_slot(signal.slot());

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            // Get first value normally.
            auto [ec1, v1] = co_await gen->next();
            BOOST_TEST(v1.has_value());
            BOOST_TEST(*v1 == "ready");

            // Post generator destruction — this will fire after we suspend
            // on the second gen->next().
            boost::asio::post(ioc, [&]()
            {
                gen.reset(); // Destroys gen while timer co_spawn is pending
            });

            // Second next() — resumes the generator, which hits co_await
            // timer.async_wait(). This triggers await_transform and creates
            // a co_spawn with a 1-hour timer. The generator is then
            // suspended, and the posted gen.reset() fires next.
            auto [ec2, v2] = co_await gen->next();
            // We should NOT get here because gen was destroyed.
            // The co_await should remain unresolved; the coroutine
            // just won't be resumed.
        },
        [&](std::exception_ptr)
        {
            // The co_spawn's coroutine is abandoned because gen was destroyed
            // and the handler was never called. This is expected.
            completed = true;
        });

    // Run until all work is done. The posted gen.reset() fires,
    // destroying gen. The destructor cancels the timer co_spawn.
    // The timer co_spawn completes, its handler checks alive_ == false
    // and does nothing. The wrapper frame is freed.
    ioc.run();

    // completed may or may not be true depending on ASIO's handling
    // of the abandoned awaitable. The key assertion is: no leaked frames
    // under valgrind.
}

// Same scenario but for flattening: destroy an outer generator while an
// inner generator's co_spawn is pending during flatten iteration.
chunked_response<std::string> inner_long_blocking()
{
    namespace net = boost::asio;
    net::steady_timer timer{co_await net::this_coro::executor};

    co_yield std::string{"inner_ready"};

    timer.expires_after(std::chrono::hours{1});
    co_await timer.async_wait();

    co_yield std::string{"inner_unreachable"};
}

chunked_response<std::string> outer_flattening_long_blocking_inner()
{
    co_yield std::string{"outer_start"};
    co_yield inner_long_blocking();
    co_yield std::string{"outer_unreachable"};
}

BOOST_AUTO_TEST_CASE(test_chunked_response_destroy_with_pending_flatten_co_spawn)
{
    boost::asio::io_context ioc;
    boost::asio::cancellation_signal signal;

    auto gen = std::make_unique<chunked_response<std::string>>(
        outer_flattening_long_blocking_inner());
    gen->set_executor(ioc.get_executor());
    gen->set_cancellation_slot(signal.slot());

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            auto [ec1, v1] = co_await gen->next();
            BOOST_TEST(v1.has_value());
            BOOST_TEST(*v1 == "outer_start");

            auto [ec2, v2] = co_await gen->next();
            BOOST_TEST(v2.has_value());
            BOOST_TEST(*v2 == "inner_ready");

            // Post destruction while inner's timer co_spawn is pending
            boost::asio::post(ioc, [&]()
            {
                gen.reset();
            });

            // This next() drives the inner generator, which hits the
            // long timer. The posted gen.reset() fires and destroys
            // everything.
            auto [ec3, v3] = co_await gen->next();
        },
        [](std::exception_ptr) {});

    ioc.run();
    // Key assertion: no leaks under valgrind.
}

// Regression test: destroying a chunked_response must clear its forwarder from
// the external cancellation slot.  Without the fix, emitting cancellation after
// destruction fires a lambda that captures &promise — a dangling reference.
// Under valgrind this manifests as a use-after-free and a leaked session frame.
BOOST_AUTO_TEST_CASE(test_chunked_response_destroy_clears_external_slot)
{
    boost::asio::io_context ioc;

    // This signal plays the role of the session's internal cancellation state
    // signal (the one whose slot() is passed to set_cancellation_slot).
    boost::asio::cancellation_signal session_signal;

    auto make_gen = [&]() -> chunked_response<std::string>
    {
        boost::asio::steady_timer timer{ioc};
        timer.expires_after(std::chrono::hours(1));
        co_await timer.async_wait();
        co_yield std::string{"never"};
    };

    auto gen = std::make_optional(make_gen());
    gen->set_cancellation_slot(session_signal.slot());

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            // Start the generator — it suspends on the 1-hour timer
            auto [ec, v] = co_await gen->next();
            // After destroy_handle fires (posted below), next() returns error
        },
        [](std::exception_ptr) {});

    // Let the co_spawn start and hit the timer
    ioc.run_one();

    // Destroy the generator while the timer co_spawn is in flight.
    // This must clear the forwarder from session_signal's slot.
    gen.reset();

    // Now emit cancellation on the session signal — mimics server shutdown.
    // Without the fix, this fires the dangling forwarder → UAF.
    session_signal.emit(boost::asio::cancellation_type::all);

    // Drain remaining work
    ioc.run();
    // Key assertion: no UAF under valgrind, no leaks.
}

// Regression test: cancellation must propagate to the generator's co_spawn even
// after earlier next() calls have completed.  ASIO's awaitable machinery calls
// clear_cancellation_slot() on the cancellation_state's output slot whenever an
// awaitable_handler fires, which removes our forwarder.  The fix re-installs
// the forwarder at the start of each next_impl() call.
//
// Scenario:
//   1. Generator yields "first" synchronously
//   2. Session calls next() again — generator co_awaits a long timer
//   3. Cancellation fires (server shutdown)
//   4. Without the fix, the forwarder was cleared in step 1 and the timer
//      co_spawn is never cancelled → session hangs → frame leaks.
BOOST_AUTO_TEST_CASE(test_chunked_response_cancel_after_first_next_completes)
{
    boost::asio::io_context ioc;

    // Mimics the session's cancellation_state output signal.
    boost::asio::cancellation_signal session_signal;

    auto make_gen = [&]() -> chunked_response<std::string>
    {
        // First value — yields synchronously (no co_await)
        co_yield std::string{"first"};

        // Second value — waits on a long timer (simulates streaming log data)
        boost::asio::steady_timer timer{ioc};
        timer.expires_after(std::chrono::hours(1));
        co_await timer.async_wait();
        co_yield std::string{"never"};
    };

    auto gen = make_gen();
    gen.set_cancellation_slot(session_signal.slot());

    bool got_first = false;
    bool second_completed = false;

    boost::asio::co_spawn(ioc,
        [&]() -> awaitable<void>
        {
            // First next() — yields "first" synchronously.
            // When the awaitable_handler fires, ASIO clears session_signal's
            // slot, removing our forwarder.
            auto [ec1, v1] = co_await gen.next();
            BOOST_TEST(!ec1);
            BOOST_TEST(v1.has_value());
            BOOST_TEST(*v1 == "first");
            got_first = true;

            // Second next() — generator co_awaits the 1-hour timer.
            // next_impl must re-install the forwarder so cancellation reaches
            // the timer's co_spawn.
            auto [ec2, v2] = co_await gen.next();
            // After cancellation, we should get end-of-stream (nullopt).
            second_completed = true;
        },
        [](std::exception_ptr) {});

    // Run until the generator is suspended on the timer
    ioc.run_for(std::chrono::milliseconds(50));
    BOOST_TEST(got_first);
    BOOST_TEST(!second_completed);

    // Emit cancellation — mimics server shutdown.
    // This must propagate through the re-installed forwarder to cancel the
    // generator's timer co_spawn.
    session_signal.emit(boost::asio::cancellation_type::all);

    // Drain remaining work — the second next() should complete.
    ioc.run_for(std::chrono::milliseconds(100));
    BOOST_TEST(second_completed);
}

} // namespace
