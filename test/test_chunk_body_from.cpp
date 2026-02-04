//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/chunk_body_from.hpp>
#include <boost/json/value.hpp>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::literals;
using boost::taar::chunk_body_from;
using boost::taar::has_chunk_body_from;

struct user_type {};

BOOST_AUTO_TEST_CASE(test_chunk_body_from_traits)
{
    static_assert(has_chunk_body_from<std::string>);
    static_assert(has_chunk_body_from<std::string_view>);
    static_assert(has_chunk_body_from<char const*>);
    static_assert(has_chunk_body_from<boost::json::value>);
    static_assert(has_chunk_body_from<int>);
    static_assert(has_chunk_body_from<double>);
    static_assert(has_chunk_body_from<std::vector<std::byte>>);
    static_assert(has_chunk_body_from<std::span<std::byte const>>);
    static_assert(!has_chunk_body_from<user_type>);
}

BOOST_AUTO_TEST_CASE(test_chunk_body_from_string)
{
    BOOST_TEST(chunk_body_from("hello"s) == "hello");
    BOOST_TEST(chunk_body_from("world"sv) == "world");
    BOOST_TEST(chunk_body_from("test") == "test");
}

BOOST_AUTO_TEST_CASE(test_chunk_body_from_json)
{
    boost::json::value val = {{"key", "value"}};
    auto result = chunk_body_from(val);
    BOOST_TEST(result == boost::json::serialize(val));
}

BOOST_AUTO_TEST_CASE(test_chunk_body_from_numbers)
{
    BOOST_TEST(chunk_body_from(42) == "42");
    BOOST_TEST(chunk_body_from(13.5) == "13.5");
}

BOOST_AUTO_TEST_CASE(test_chunk_body_from_raw_bytes)
{
    std::vector<std::byte> bytes = {
        std::byte{0x48}, std::byte{0x65}, std::byte{0x6c},
        std::byte{0x6c}, std::byte{0x6f}};
    BOOST_TEST(chunk_body_from(bytes) == "Hello");

    std::span<std::byte const> byte_span {bytes};
    BOOST_TEST(chunk_body_from(byte_span) == "Hello");
}

} // namespace
