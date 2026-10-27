//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include <boost/taar/core/byte_range.hpp>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace {

namespace taar = boost::taar;
using taar::byte_range;
using taar::range_status;

BOOST_AUTO_TEST_CASE(test_byte_range_single_forms)
{
    auto r = taar::parse_range("bytes=0-499", 10000);
    BOOST_TEST((r.status == range_status::satisfiable));
    BOOST_TEST((r.ranges == std::vector<byte_range> {{0, 499}}));
    BOOST_TEST(r.ranges[0].length() == 500u);

    r = taar::parse_range("bytes=9500-", 10000);
    BOOST_TEST((r.ranges == std::vector<byte_range> {{9500, 9999}}));

    r = taar::parse_range("bytes=-500", 10000);
    BOOST_TEST((r.ranges == std::vector<byte_range> {{9500, 9999}}));

    // Clipping: past the end, suffix larger than the file.
    r = taar::parse_range("bytes=9000-20000", 10000);
    BOOST_TEST((r.ranges == std::vector<byte_range> {{9000, 9999}}));
    r = taar::parse_range("bytes=-20000", 10000);
    BOOST_TEST((r.ranges == std::vector<byte_range> {{0, 9999}}));

    // Unit and whitespace tolerance.
    r = taar::parse_range(" Bytes = 1-2 , 4-5 ", 10);
    BOOST_TEST((r.ranges == std::vector<byte_range> {{1, 2}, {4, 5}}));

    // Saturating numbers.
    r = taar::parse_range("bytes=0-99999999999999999999999", 10);
    BOOST_TEST((r.ranges == std::vector<byte_range> {{0, 9}}));
}

BOOST_AUTO_TEST_CASE(test_byte_range_unsatisfiable_and_ignored)
{
    BOOST_TEST((taar::parse_range("bytes=10000-", 10000).status == range_status::unsatisfiable));
    BOOST_TEST((taar::parse_range("bytes=-0", 10000).status == range_status::unsatisfiable));
    BOOST_TEST((taar::parse_range("bytes=0-0", 0).status == range_status::unsatisfiable));
    BOOST_TEST((taar::parse_range("bytes=-5", 0).status == range_status::unsatisfiable));
    // One satisfiable spec is enough.
    auto r = taar::parse_range("bytes=20000-30000, 0-1", 10000);
    BOOST_TEST((r.status == range_status::satisfiable));
    BOOST_TEST((r.ranges == std::vector<byte_range> {{0, 1}}));

    for (auto const* bad : {"", "bytes", "bytes=", "bytes=,", "items=0-1", "bytes=5-1", "bytes=a-b",
                            "bytes=1", "bytes=--1", "bytes=1-2-3", "bytes 0-1", "bytes=0x1-2"})
    {
        BOOST_TEST_INFO(bad);
        BOOST_TEST((taar::parse_range(bad, 10000).status == range_status::ignored));
    }
}

BOOST_AUTO_TEST_CASE(test_byte_range_limit_and_coalescing)
{
    BOOST_TEST((taar::parse_range("bytes=0-0,2-2,4-4", 100, 3).status == range_status::satisfiable));
    BOOST_TEST((taar::parse_range("bytes=0-0,2-2,4-4,6-6", 100, 3).status == range_status::ignored));
    BOOST_TEST((taar::parse_range("bytes=0-0", 100, 0).status == range_status::ignored));

    auto const merged = taar::coalesce_ranges({{50, 59}, {0, 9}, {5, 14}, {15, 19}, {30, 39}});
    BOOST_TEST((merged == std::vector<byte_range> {{0, 19}, {30, 39}, {50, 59}}));
    BOOST_TEST((taar::coalesce_ranges({{0, 9}, {2, 3}}) == std::vector<byte_range> {{0, 9}}));
    BOOST_TEST(taar::coalesce_ranges({}).empty());
}

} // namespace
