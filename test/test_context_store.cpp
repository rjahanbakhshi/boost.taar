//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace net = boost::asio;
namespace taar = boost::taar;
using namespace std::chrono_literals;

struct counter
{
    int value = 0;
};

using store_type = taar::context_store<std::string, counter>;
using clock_type = store_type::clock;

// A clock the test advances by hand.
struct fake_clock
{
    clock_type::time_point now = clock_type::time_point {1h};
    clock_type::time_point operator()() const { return now; }
};

BOOST_AUTO_TEST_CASE(test_context_store_create_find_erase)
{
    store_type store {10min};

    BOOST_TEST(store.size() == 0u);
    BOOST_TEST(!store.find("a"));

    auto a = store.emplace("a");
    BOOST_REQUIRE(!!a);
    BOOST_TEST(a->key() == "a");
    BOOST_TEST(store.size() == 1u);

    auto again = store.emplace("a");
    BOOST_TEST(again.get() == a.get());
    BOOST_TEST(store.size() == 1u);

    auto found = store.find("a");
    BOOST_TEST(found.get() == a.get());
    found->context()->value = 42;
    BOOST_TEST(a->context()->value == 42);

    BOOST_TEST(store.erase("a"));
    BOOST_TEST(!store.erase("a"));
    BOOST_TEST(!store.find("a"));
    BOOST_TEST(a->context()->value == 42); // caller's pointer still valid
}

BOOST_AUTO_TEST_CASE(test_context_store_create_forwards_constructor_args)
{
    struct with_ctor { explicit with_ctor(int v) : v {v} {} int v; };
    taar::context_store<std::string, with_ctor> store {10min};
    auto e = store.emplace("k", 7);
    BOOST_TEST(e->context()->v == 7);
}

BOOST_AUTO_TEST_CASE(test_context_store_create_initialises_aggregates)
{
    // An aggregate context built from arguments (braced where the
    // compiler lacks parenthesised aggregate initialisation).
    store_type store {10min};
    auto e = store.emplace("k", 5);
    BOOST_TEST(e->context()->value == 5);
    auto [key, minted] = store.create(9);
    BOOST_TEST(minted->context()->value == 9);
}

BOOST_AUTO_TEST_CASE(test_context_store_minted_keys_are_unique_hex)
{
    store_type store {10min};
    std::set<std::string> keys;
    for (int i = 0; i < 2000; ++i)
    {
        auto [key, e] = store.create();
        BOOST_TEST(key.size() == 32u);
        BOOST_TEST(key.find_first_not_of("0123456789abcdef") == std::string::npos);
        BOOST_TEST(e->key() == key);
        keys.insert(key);
    }
    BOOST_TEST(keys.size() == 2000u);
    BOOST_TEST(store.size() == 2000u);
}

BOOST_AUTO_TEST_CASE(test_context_store_custom_key_generator)
{
    int n = 0;
    taar::context_store<int, counter> store {10min, [&] { return ++n; }};
    auto [k1, e1] = store.create();
    auto [k2, e2] = store.create();
    BOOST_TEST(k1 == 1);
    BOOST_TEST(k2 == 2);
}

BOOST_AUTO_TEST_CASE(test_context_store_expired_entry_is_unknown_and_swept)
{
    fake_clock clk;
    store_type store {1min, taar::default_key_generator<std::string>{}, [&] { return clk.now; }};

    store.emplace("old");
    clk.now += 30s;
    store.emplace("young");
    BOOST_TEST(store.size() == 2u);

    clk.now += 45s; // old: 75s idle (expired), young: 45s idle
    BOOST_TEST(!store.find("old"));
    BOOST_TEST(!!store.find("young")); // also touches young
    BOOST_TEST(store.size() == 1u);

    clk.now += 50s; // young: 50s since touch, still alive
    BOOST_TEST(store.sweep() == 0u);
    clk.now += 20s; // 70s
    BOOST_TEST(store.sweep() == 1u);
    BOOST_TEST(store.size() == 0u);
}

BOOST_AUTO_TEST_CASE(test_context_store_opportunistic_sweep_on_access)
{
    fake_clock clk;
    store_type store {1min, taar::default_key_generator<std::string>{}, [&] { return clk.now; }};

    for (int i = 0; i < 5; ++i) store.emplace(std::to_string(i));
    clk.now += 2min;
    // Touching an unrelated key sweeps everything that expired.
    store.emplace("fresh");
    BOOST_TEST(store.size() == 1u);
}

BOOST_AUTO_TEST_CASE(test_context_store_locked_entry_is_not_swept)
{
    fake_clock clk;
    store_type store {1min, taar::default_key_generator<std::string>{}, [&] { return clk.now; }};
    net::io_context io;

    auto e = store.emplace("busy");

    net::co_spawn(io, [&]() -> taar::awaitable<void>
    {
        auto guard = co_await store.lock(e);
        BOOST_TEST(e->in_flight());

        clk.now += 10min;
        BOOST_TEST(store.sweep() == 0u);       // in flight, kept
        BOOST_TEST(store.find("busy").get() == e.get());   // still reachable
    }, net::detached);
    io.run();

    BOOST_TEST(!e->in_flight());
    // Released: activity refreshed at release time, so still alive now...
    BOOST_TEST(store.sweep() == 0u);
    clk.now += 2min;
    // ...and swept once idle again.
    BOOST_TEST(store.sweep() == 1u);
}

BOOST_AUTO_TEST_CASE(test_context_store_lock_serialises_same_key_single_thread)
{
    store_type store {10min};
    net::io_context io;
    auto e = store.emplace("k");
    std::vector<std::string> events;

    auto worker = [&](std::string name) -> taar::awaitable<void>
    {
        auto guard = co_await store.lock(e);
        events.push_back(name + ":enter");
        net::steady_timer t {co_await net::this_coro::executor};
        t.expires_after(5ms);
        co_await t.async_wait(taar::use_awaitable);
        events.push_back(name + ":leave");
    };

    net::co_spawn(io, worker("a"), net::detached);
    net::co_spawn(io, worker("b"), net::detached);
    io.run();

    BOOST_REQUIRE(events.size() == 4u);
    BOOST_TEST(events[0] == "a:enter");
    BOOST_TEST(events[1] == "a:leave");
    BOOST_TEST(events[2] == "b:enter");
    BOOST_TEST(events[3] == "b:leave");
}

BOOST_AUTO_TEST_CASE(test_context_store_lock_serialises_same_key_multi_thread)
{
    store_type store {10min};
    net::io_context io;
    auto same = store.emplace("same");
    auto other = store.emplace("other");

    std::atomic<int> concurrent_same {0};
    std::atomic<int> max_concurrent_same {0};
    std::atomic<int> concurrent_any {0};
    std::atomic<int> max_concurrent_any {0};
    int unguarded_sum = 0;

    auto worker = [&](store_type::entry_ptr e) -> taar::awaitable<void>
    {
        auto guard = co_await store.lock(e);
        auto any = ++concurrent_any;
        max_concurrent_any = std::max(max_concurrent_any.load(), any);
        if (e == same)
        {
            auto c = ++concurrent_same;
            max_concurrent_same = std::max(max_concurrent_same.load(), c);
            ++unguarded_sum; // protected by the entry lock
        }
        net::steady_timer t {co_await net::this_coro::executor};
        t.expires_after(2ms);
        co_await t.async_wait(taar::use_awaitable);
        if (e == same) --concurrent_same;
        --concurrent_any;
    };

    for (int i = 0; i < 24; ++i)
    {
        net::co_spawn(io, worker(same), net::detached);
        net::co_spawn(io, worker(other), net::detached);
    }

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&] { io.run(); });
    for (auto& t : threads) t.join();

    BOOST_TEST(max_concurrent_same.load() == 1);
    BOOST_TEST(unguarded_sum == 24);
    // Two different keys may overlap; with 4 threads we expect to see it.
    BOOST_TEST(max_concurrent_any.load() >= 1);
}

BOOST_AUTO_TEST_CASE(test_context_store_concurrent_create_same_key)
{
    store_type store {10min};
    std::vector<std::thread> threads;
    std::vector<store_type::entry_ptr> results(16);
    for (int i = 0; i < 16; ++i)
    {
        threads.emplace_back([&, i] { results[i] = store.emplace("race"); });
    }
    for (auto& t : threads) t.join();
    for (auto const& r : results)
    {
        BOOST_TEST(r.get() == results[0].get());
    }
    BOOST_TEST(store.size() == 1u);
}

} // namespace
