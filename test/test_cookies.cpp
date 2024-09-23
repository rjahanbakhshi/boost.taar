//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/cookies.hpp>
#include <boost/test/unit_test.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_cookies_parser)
{
    using namespace boost::taar;

    cookies c1;
    BOOST_TEST((parse_cookies("", c1) == error::success));
    BOOST_TEST(c1.size() == 0);

    BOOST_TEST((parse_cookies("session=10", c1) == error::success));
    BOOST_TEST(c1.size() == 1);
    BOOST_TEST(c1.contains("session"));
    BOOST_TEST(c1.at("session") == "10");

    BOOST_TEST((parse_cookies("session=20", c1) == error::success));
    BOOST_TEST(c1.size() == 1);
    BOOST_TEST(c1.contains("session"));
    BOOST_TEST(c1.at("session") == "10");

    BOOST_TEST((parse_cookies("session=20;", c1) == error::success));
    BOOST_TEST(c1.size() == 1);
    BOOST_TEST(c1.contains("session"));
    BOOST_TEST(c1.at("session") == "10");

    BOOST_TEST((parse_cookies("session=", c1) == error::success));
    BOOST_TEST(c1.size() == 1);
    BOOST_TEST(c1.contains("session"));
    BOOST_TEST(c1.at("session") == "10");

    BOOST_TEST((parse_cookies("next=", c1) == error::success));
    BOOST_TEST(c1.size() == 2);
    BOOST_TEST(c1.contains("next"));
    BOOST_TEST(c1.at("next") == "");

    BOOST_TEST((parse_cookies("next=13;", c1) == error::success));
    BOOST_TEST(c1.size() == 2);
    BOOST_TEST(c1.contains("next"));
    BOOST_TEST(c1.at("next") == "");

    BOOST_TEST((parse_cookies("bad", c1) == error::invalid_cookie_format));
    BOOST_TEST(c1.size() == 2);

    BOOST_TEST((parse_cookies("bad;", c1) == error::invalid_cookie_format));
    BOOST_TEST(c1.size() == 2);

    BOOST_TEST((parse_cookies("bad=va lue", c1) == error::invalid_cookie_format));
    BOOST_TEST(c1.size() == 2);

    BOOST_TEST((parse_cookies("good=val=ue;", c1) == error::success));
    BOOST_TEST(c1.size() == 3);
    BOOST_TEST(c1.at("good") == "val=ue");

    BOOST_TEST((parse_cookies("good2=\"val ue\"", c1) == error::success));
    BOOST_TEST(c1.size() == 4);
    BOOST_TEST(c1.at("good2") == "val ue");

    BOOST_TEST((parse_cookies(R"(good3="val;ue";good4=val)", c1) == error::success));
    BOOST_TEST(c1.size() == 6);
    BOOST_TEST(c1.at("good3") == "val;ue");
    BOOST_TEST(c1.at("good4") == "val");

    BOOST_TEST((parse_cookies("nosp1=17;nosp2=19", c1) == error::success));
    BOOST_TEST(c1.size() == 8);
    BOOST_TEST(c1.contains("nosp1"));
    BOOST_TEST(c1.at("nosp1") == "17");
    BOOST_TEST(c1.contains("nosp2"));
    BOOST_TEST(c1.at("nosp2") == "19");

    BOOST_TEST((parse_cookies("W=Z", c1) == error::success));
    BOOST_TEST(c1.size() == 9);
    BOOST_TEST(c1.contains("W"));
    BOOST_TEST(c1.at("W") == "Z");
}

} // namespace
