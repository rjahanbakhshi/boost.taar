//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include <boost/taar/core/http_date.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>

namespace {

namespace taar = boost::taar;
using namespace std::chrono;

sys_seconds const rfc_example = sys_days {1994y / November / 6} + 8h + 49min + 37s;

BOOST_AUTO_TEST_CASE(test_http_date_formats_imf_fixdate)
{
    BOOST_TEST(taar::format_http_date(rfc_example) == "Sun, 06 Nov 1994 08:49:37 GMT");
    BOOST_TEST(taar::format_http_date(sys_days {2000y / January / 1}) == "Sat, 01 Jan 2000 00:00:00 GMT");
    // Sub-second precision is truncated.
    BOOST_TEST(taar::format_http_date(rfc_example + 999ms) == "Sun, 06 Nov 1994 08:49:37 GMT");
}

BOOST_AUTO_TEST_CASE(test_http_date_parses_the_three_forms)
{
    BOOST_TEST((taar::parse_http_date("Sun, 06 Nov 1994 08:49:37 GMT") == rfc_example));
    BOOST_TEST((taar::parse_http_date("Sunday, 06-Nov-94 08:49:37 GMT") == rfc_example));
    BOOST_TEST((taar::parse_http_date("Sun Nov  6 08:49:37 1994") == rfc_example));
    BOOST_TEST((taar::parse_http_date("Sun Nov 16 08:49:37 1994") == rfc_example + days {10}));
    // Round trip.
    auto const now = floor<seconds>(system_clock::now());
    BOOST_TEST((taar::parse_http_date(taar::format_http_date(now)) == now));
}

BOOST_AUTO_TEST_CASE(test_http_date_rejects_malformed_values)
{
    BOOST_TEST(!taar::parse_http_date(""));
    BOOST_TEST(!taar::parse_http_date("yesterday"));
    BOOST_TEST(!taar::parse_http_date("Sun, 06 Nov 1994 08:49:37 UTC"));
    BOOST_TEST(!taar::parse_http_date("Sun, 31 Feb 1994 08:49:37 GMT"));
    BOOST_TEST(!taar::parse_http_date("Sun, 06 Foo 1994 08:49:37 GMT"));
    BOOST_TEST(!taar::parse_http_date("Sun, 06 Nov 1994 24:49:37 GMT"));
    BOOST_TEST(!taar::parse_http_date("Xyz, 06 Nov 1994 08:49:37 GMT"));
    BOOST_TEST(!taar::parse_http_date("Sun, 06 Nov 1994 08:49:37 GMT "));
    BOOST_TEST(!taar::parse_http_date("Funday, 06-Nov-94 08:49:37 GMT"));
    BOOST_TEST(!taar::parse_http_date("1994-11-06T08:49:37Z"));
}

BOOST_AUTO_TEST_CASE(test_http_date_rfc850_two_digit_year_is_not_far_future)
{
    auto const year_now = static_cast<int>(year_month_day {floor<days>(system_clock::now())}.year());
    auto const parsed = taar::parse_http_date("Sunday, 06-Nov-94 08:49:37 GMT");
    BOOST_TEST_REQUIRE(parsed.has_value());
    auto const y = static_cast<int>(year_month_day {floor<days>(*parsed)}.year());
    BOOST_TEST(y <= year_now + 50);
    BOOST_TEST(y % 100 == 94);
}

} // namespace
