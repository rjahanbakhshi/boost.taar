//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/async_channel.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>
#include <thread>

namespace {

namespace net = boost::asio;
namespace taar = boost::taar;

using channel = taar::async_channel<std::string>;

std::optional<std::optional<std::string>> receive_once(net::io_context& io, channel& ch)
{
    std::optional<std::optional<std::string>> got;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        got = co_await ch.async_receive(taar::use_awaitable);
    }, net::detached);
    io.restart();
    io.run();
    return got;
}

BOOST_AUTO_TEST_CASE(test_async_channel_push_then_receive_in_order)
{
    net::io_context io;
    channel ch;
    BOOST_TEST(ch.push("a"));
    BOOST_TEST(ch.push("b"));
    BOOST_TEST(ch.size() == 2u);
    auto a = receive_once(io, ch);
    auto b = receive_once(io, ch);
    BOOST_REQUIRE(a.has_value() && a->has_value());
    BOOST_REQUIRE(b.has_value() && b->has_value());
    BOOST_TEST(**a == "a");
    BOOST_TEST(**b == "b");
    BOOST_TEST(ch.size() == 0u);
}

BOOST_AUTO_TEST_CASE(test_async_channel_receive_waits_for_push)
{
    net::io_context io;
    channel ch;
    std::optional<std::optional<std::string>> got;
    bool pushed_first = false;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        got = co_await ch.async_receive(taar::use_awaitable);
        BOOST_TEST(pushed_first);
    }, net::detached);
    // Queued behind the coroutine's start: the receive is already waiting
    // when this runs.
    net::post(io, [&]
    {
        BOOST_TEST(!got.has_value());
        pushed_first = true;
        BOOST_TEST(ch.push("late"));
    });
    io.run();
    BOOST_REQUIRE(got.has_value() && got->has_value());
    BOOST_TEST(**got == "late");
}

BOOST_AUTO_TEST_CASE(test_async_channel_close_wakes_waiter_and_rejects_push)
{
    net::io_context io;
    channel ch;
    std::optional<std::optional<std::string>> got;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        got = co_await ch.async_receive(taar::use_awaitable);
    }, net::detached);
    net::post(io, [&] { ch.close(); });
    io.run();
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST(!got->has_value());
    BOOST_TEST(ch.closed());
    BOOST_TEST(!ch.push("x"));
}

BOOST_AUTO_TEST_CASE(test_async_channel_drains_queued_values_after_close)
{
    net::io_context io;
    channel ch;
    ch.push("kept");
    ch.close();
    auto first = receive_once(io, ch);
    auto second = receive_once(io, ch);
    BOOST_REQUIRE(first.has_value() && first->has_value());
    BOOST_TEST(**first == "kept");
    BOOST_REQUIRE(second.has_value());
    BOOST_TEST(!second->has_value());
}

BOOST_AUTO_TEST_CASE(test_async_channel_cancellation_completes_with_nullopt)
{
    net::io_context io;
    channel ch;
    net::cancellation_signal cancel;
    std::optional<std::optional<std::string>> got;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        got = co_await ch.async_receive(taar::use_awaitable);
    }, net::bind_cancellation_slot(cancel.slot(), net::detached));
    net::post(io, [&] { cancel.emit(net::cancellation_type::terminal); });
    io.run();
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST(!got->has_value());

    // The cancelled waiter is gone; a later push is queued, not lost.
    BOOST_TEST(ch.push("after"));
    BOOST_TEST(ch.size() == 1u);
}

BOOST_AUTO_TEST_CASE(test_async_channel_push_from_another_thread)
{
    net::io_context io;
    channel ch;
    std::optional<std::optional<std::string>> got;
    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        got = co_await ch.async_receive(taar::use_awaitable);
    }, net::detached);
    auto work = net::make_work_guard(io);
    std::thread producer {[&]
    {
        ch.push("from-thread");
        work.reset();
    }};
    io.run();
    producer.join();
    BOOST_REQUIRE(got.has_value() && got->has_value());
    BOOST_TEST(**got == "from-thread");
}

} // namespace
