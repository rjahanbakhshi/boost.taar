//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/web/matcher/target_parser.hpp>
#include <boost/test/unit_test.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_matcher_target_parser_invalid)
{
    using namespace boost::web::matcher;

    BOOST_TEST((parse_target("") == error::no_absolute_target_path));
    BOOST_TEST((parse_target("blah") == error::no_absolute_target_path));
    BOOST_TEST((parse_target("blah/blah") == error::no_absolute_target_path));
    BOOST_TEST((parse_target("/blah/{blah}") == error::invalid_target_path));
    BOOST_TEST((parse_target("/blah/{b") == error::invalid_target_path));
    BOOST_TEST((parse_target("/blah/{") == error::invalid_target_path));
    BOOST_TEST((parse_target("/blah{") == error::invalid_target_path));
    BOOST_TEST((parse_target("/bl{ah") == error::invalid_target_path));
    BOOST_TEST((parse_target("{blah}/blah") == error::no_absolute_target_path));
    BOOST_TEST((parse_target("/blah}") == error::invalid_target_path));
    BOOST_TEST((parse_target("/bl}ah") == error::invalid_target_path));
    BOOST_TEST((parse_target("/blah/{bl/ah}", true) == error::invalid_target_path));
    BOOST_TEST((parse_target("/blah/b{la}h", true) == error::invalid_target_path));
    BOOST_TEST((parse_target("/blah/{blah", true) == error::invalid_target_path));
}

BOOST_AUTO_TEST_CASE(test_matcher_target_parser_no_templates)
{
    using namespace boost::web::matcher;

    auto result = parse_target("/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 0);

    result = parse_target("//");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 0);

    result = parse_target("///");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 0);

    result = parse_target("/first");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "first");

    result = parse_target("/first/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "first");

    result = parse_target("/first//");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "first");

    result = parse_target("//first/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "first");

    result = parse_target("/first/second");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "second");

    result = parse_target("/first/second/");
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "second");
}

BOOST_AUTO_TEST_CASE(test_matcher_target_parser_templated)
{
    using namespace boost::web::matcher;

    auto result = parse_target("/{first}", true);
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 1);
    BOOST_TEST(result.value()[0] == "{first}");

    result = parse_target("/first/{second}", true);
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "{second}");

    result = parse_target("/first/{second}/", true);
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "{second}");

    result = parse_target("/{first}/second/", true);
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 2);
    BOOST_TEST(result.value()[0] == "{first}");
    BOOST_TEST(result.value()[1] == "second");

    result = parse_target("/first/{second}/third", true);
    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value().size() == 3);
    BOOST_TEST(result.value()[0] == "first");
    BOOST_TEST(result.value()[1] == "{second}");
    BOOST_TEST(result.value()[2] == "third");
}

} // namespace
