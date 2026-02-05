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

} // namespace
