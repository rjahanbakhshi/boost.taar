//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_TEST_JOINING_THREAD_HPP
#define BOOST_TAAR_TEST_JOINING_THREAD_HPP

#include <thread>
#include <utility>

namespace test_util {

#if defined(__cpp_lib_jthread)

using joining_thread = std::jthread;

#else

/// A thread joined on destruction: std::jthread minus stop tokens, for
/// standard libraries that gate std::jthread (libc++ behind
/// -fexperimental-library).
class joining_thread
{
public:
    template <typename F>
    explicit joining_thread(F&& f)
        : thread_ {std::forward<F>(f)}
    {}

    joining_thread(joining_thread&&) noexcept = default;

    joining_thread& operator=(joining_thread&& other) noexcept
    {
        join_if_joinable();
        thread_ = std::move(other.thread_);
        return *this;
    }

    ~joining_thread() { join_if_joinable(); }

private:
    void join_if_joinable()
    {
        if (thread_.joinable()) thread_.join();
    }

    std::thread thread_;
};

#endif

} // namespace test_util

#endif // BOOST_TAAR_TEST_JOINING_THREAD_HPP
