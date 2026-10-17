//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/entity_tag.hpp>
#include <boost/test/unit_test.hpp>
#include <string_view>

namespace {

namespace taar = boost::taar;

BOOST_AUTO_TEST_CASE(test_entity_tag_parsing)
{
    std::string_view text = R"( W/"abc", "def")";
    auto a = taar::parse_entity_tag(text);
    BOOST_TEST_REQUIRE(a.has_value());
    BOOST_TEST(a->weak);
    BOOST_TEST(a->opaque == "abc");
    BOOST_TEST(text == R"(, "def")");

    for (std::string_view bad : {"", "abc", R"("abc)", R"(w/"abc")", "\"a b\""})
    {
        auto copy = bad;
        BOOST_TEST(!taar::parse_entity_tag(copy));
    }
    std::string_view empty_tag = R"("")";
    BOOST_TEST(taar::parse_entity_tag(empty_tag).has_value());
}

BOOST_AUTO_TEST_CASE(test_entity_tag_strong_and_weak_comparison)
{
    // RFC 9110 8.8.3.2 comparison table.
    BOOST_TEST(!taar::etag_list_matches(R"(W/"1")", R"(W/"1")", false));
    BOOST_TEST(taar::etag_list_matches(R"(W/"1")", R"(W/"1")", true));
    BOOST_TEST(!taar::etag_list_matches(R"(W/"1")", R"(W/"2")", true));
    BOOST_TEST(!taar::etag_list_matches(R"(W/"1")", R"("1")", false));
    BOOST_TEST(taar::etag_list_matches(R"(W/"1")", R"("1")", true));
    BOOST_TEST(taar::etag_list_matches(R"("1")", R"("1")", false));

    BOOST_TEST(taar::etag_list_matches("*", R"("x")", false));
    BOOST_TEST(taar::etag_list_matches(R"("a", "b" ,W/"c")", R"("c")", true));
    BOOST_TEST(taar::etag_list_matches(R"(,"a",,"b")", R"("b")", false));
    BOOST_TEST(!taar::etag_list_matches(R"("a", "b")", R"("c")", true));
    BOOST_TEST(!taar::etag_list_matches(R"("a" junk)", R"("a")", true));
    BOOST_TEST(!taar::etag_list_matches("", R"("a")", true));
}

} // namespace
