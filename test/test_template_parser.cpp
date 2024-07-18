//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/matcher/template_parser.hpp>
#include <boost/test/unit_test.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_matcher_template_parser_invalid)
{
    using namespace boost::taar::matcher;

    BOOST_TEST((parse_template("") == error::no_absolute_template));
    BOOST_TEST((parse_template("blah") == error::no_absolute_template));
    BOOST_TEST((parse_template("blah/blah") == error::no_absolute_template));
    BOOST_TEST((parse_template("/blah/{b") == error::invalid_template));
    BOOST_TEST((parse_template("/blah/{") == error::invalid_template));
    BOOST_TEST((parse_template("/blah{") == error::invalid_template));
    BOOST_TEST((parse_template("/bl{ah") == error::invalid_template));
    BOOST_TEST((parse_template("{blah}/blah") == error::no_absolute_template));
    BOOST_TEST((parse_template("/blah}") == error::invalid_template));
    BOOST_TEST((parse_template("/bl}ah") == error::invalid_template));
    BOOST_TEST((parse_template("/blah/{bl/ah}") == error::invalid_template));
    BOOST_TEST((parse_template("/blah/b{la}h") == error::invalid_template));
    BOOST_TEST((parse_template("/blah/{blah") == error::invalid_template));
}

BOOST_AUTO_TEST_CASE(test_matcher_template_parser)
{
    using namespace boost::taar::matcher;

    auto result = parse_template("/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 0);

    result = parse_template("//");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 0);

    result = parse_template("///");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 0);

    result = parse_template("/first");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "first");

    result = parse_template("/first/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "first");

    result = parse_template("/first//");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "first");

    result = parse_template("//first/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "first");

    result = parse_template("/first/second");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "second");

    result = parse_template("/first/second/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "second");

    result = parse_template("/{first}");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "{first}");

    result = parse_template("/first/{second}");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "{second}");

    result = parse_template("/first/{second}/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "{second}");

    result = parse_template("/{first}/second/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "{first}");
    BOOST_TEST(result.value()[1] == "second");

    result = parse_template("/first/{second}/third");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 3);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "{second}");
    BOOST_TEST(result.value()[2] == "third");
}

} // namespace
