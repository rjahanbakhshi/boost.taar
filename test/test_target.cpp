//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#include <boost/url/url_view.hpp>
#include <boost/web/matcher/target.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>
#include <boost/test/unit_test.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_matcher_empty_target)
{
    namespace http = boost::beast::http;
    using namespace boost::web::matcher;

    http::request<http::string_body> req{http::verb::get, "/", 10};
    auto parsed_target = boost::urls::url_view(req.target());
    context ctx;

    BOOST_TEST(!(target == "/first")(req, ctx, parsed_target));

    ctx.path_args.clear();
    BOOST_TEST((target == "/{*}")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 1);
    BOOST_TEST(ctx.path_args.contains("*"));
    BOOST_TEST(ctx.path_args.at("*") == "");
}

BOOST_AUTO_TEST_CASE(test_matcher_target)
{
    namespace http = boost::beast::http;
    using namespace boost::web::matcher;

    http::request<http::string_body> req{http::verb::get, "/first/second/third", 10};
    auto parsed_target = boost::urls::url_view(req.target());
    context ctx;

    BOOST_TEST(!(target == "/first/second")(req, ctx, parsed_target));
    BOOST_TEST(!(target == "/first/second/fourth")(req, ctx, parsed_target));
    BOOST_TEST(!(target == "/first/second/third/fourth")(req, ctx, parsed_target));
    BOOST_TEST(!(target == "/{a}")(req, ctx, parsed_target));
    BOOST_TEST(!(target == "/{a}/second")(req, ctx, parsed_target));
    BOOST_TEST(!(target == "/{a}/first/second")(req, ctx, parsed_target));
    BOOST_TEST(!(target == "/{a}/first/second/third")(req, ctx, parsed_target));
    BOOST_TEST(!(target == "/{a}/{b}/")(req, ctx, parsed_target));
    BOOST_TEST(!(target == "/{a}/{b}/{c}/{d}")(req, ctx, parsed_target));

    ctx.path_args.clear();
    BOOST_TEST((target == "/first/second/third")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.empty());

    ctx.path_args.clear();
    BOOST_TEST((target == "/{a}/second/third")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 1);
    BOOST_TEST(ctx.path_args.contains("a"));
    BOOST_TEST(ctx.path_args.at("a") == "first");

    ctx.path_args.clear();
    BOOST_TEST((target == "/first/{a}/third")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 1);
    BOOST_TEST(ctx.path_args.contains("a"));
    BOOST_TEST(ctx.path_args.at("a") == "second");

    ctx.path_args.clear();
    BOOST_TEST((target == "/first/second/{a}")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 1);
    BOOST_TEST(ctx.path_args.contains("a"));
    BOOST_TEST(ctx.path_args.at("a") == "third");

    ctx.path_args.clear();
    BOOST_TEST((target == "/{a}/{ab}/third")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 2);
    BOOST_TEST(ctx.path_args.contains("a"));
    BOOST_TEST(ctx.path_args.contains("ab"));
    BOOST_TEST(ctx.path_args.at("a") == "first");
    BOOST_TEST(ctx.path_args.at("ab") == "second");

    ctx.path_args.clear();
    BOOST_TEST((target == "/{a}/second/{ab}/")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 2);
    BOOST_TEST(ctx.path_args.contains("a"));
    BOOST_TEST(ctx.path_args.contains("ab"));
    BOOST_TEST(ctx.path_args.at("a") == "first");
    BOOST_TEST(ctx.path_args.at("ab") == "third");

    ctx.path_args.clear();
    BOOST_TEST((target == "/{a}/{ab}/{abc}")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 3);
    BOOST_TEST(ctx.path_args.contains("a"));
    BOOST_TEST(ctx.path_args.contains("ab"));
    BOOST_TEST(ctx.path_args.contains("abc"));
    BOOST_TEST(ctx.path_args.at("a") == "first");
    BOOST_TEST(ctx.path_args.at("ab") == "second");
    BOOST_TEST(ctx.path_args.at("abc") == "third");

    ctx.path_args.clear();
    BOOST_TEST((target == "/{*}")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 1);
    BOOST_TEST(ctx.path_args.contains("*"));
    BOOST_TEST(ctx.path_args.at("*") == "first/second/third");

    ctx.path_args.clear();
    BOOST_TEST((target == "/first/{*}")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 1);
    BOOST_TEST(ctx.path_args.contains("*"));
    BOOST_TEST(ctx.path_args.at("*") == "second/third");

    ctx.path_args.clear();
    BOOST_TEST((target == "/first/second/{*}")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 1);
    BOOST_TEST(ctx.path_args.contains("*"));
    BOOST_TEST(ctx.path_args.at("*") == "third");

    ctx.path_args.clear();
    BOOST_TEST((target == "/first/second/third/{*}")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 1);
    BOOST_TEST(ctx.path_args.contains("*"));
    BOOST_TEST(ctx.path_args.at("*") == "");

    ctx.path_args.clear();
    BOOST_TEST((target == "/{a}/{b}/{c}/{*}")(req, ctx, parsed_target));
    BOOST_TEST(ctx.path_args.size() == 4);
    BOOST_TEST(ctx.path_args.contains("a"));
    BOOST_TEST(ctx.path_args.contains("b"));
    BOOST_TEST(ctx.path_args.contains("c"));
    BOOST_TEST(ctx.path_args.at("a") == "first");
    BOOST_TEST(ctx.path_args.at("b") == "second");
    BOOST_TEST(ctx.path_args.at("c") == "third");
    BOOST_TEST(ctx.path_args.at("*") == "");
}

} // namespace
