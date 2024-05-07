#include <boost/web/matcher/method.h>
#include <boost/web/matcher/version.h>
#include <boost/web/matcher/header.h>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>

#define BOOST_TEST_MODULE test_matcher
#include <boost/test/included/unit_test.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_matcher_method)
{
    namespace http = boost::beast::http;
    using namespace boost::web::matcher;
    using http::verb;

    http::request<http::string_body> req{http::verb::get, "", 10};
    context ctx;

    BOOST_TEST((method == verb::get).evaluate(req, ctx));
    BOOST_TEST(!(verb::put == method).evaluate(req, ctx));
    BOOST_TEST(!(!(method == verb::get)).evaluate(req, ctx));
    BOOST_TEST((!(method == verb::put)).evaluate(req, ctx));
    BOOST_TEST(!(method != verb::get).evaluate(req, ctx));
    BOOST_TEST((verb::put != method).evaluate(req, ctx));
    BOOST_TEST(!(method == verb::get && verb::put == method).evaluate(req, ctx));
    BOOST_TEST((method == verb::get && verb::get == method).evaluate(req, ctx));
    BOOST_TEST((method == verb::get || verb::put == method).evaluate(req, ctx));
    BOOST_TEST((method == verb::put || verb::get == method).evaluate(req, ctx));;
}

BOOST_AUTO_TEST_CASE(test_matcher_version)
{
    namespace http = boost::beast::http;
    using namespace boost::web::matcher;
    using http::verb;

    http::request<http::string_body> req{http::verb::get, "", 10};
    context ctx;

    BOOST_TEST((version == 10).evaluate(req, ctx));
    BOOST_TEST((10 == version).evaluate(req, ctx));
    BOOST_TEST(!(version > 10).evaluate(req, ctx));
    BOOST_TEST(!(10 > version).evaluate(req, ctx));
    BOOST_TEST((version > 9).evaluate(req, ctx));
    BOOST_TEST(!(9 > version).evaluate(req, ctx));
    BOOST_TEST(!(version < 10).evaluate(req, ctx));
    BOOST_TEST(!(10 < version).evaluate(req, ctx));
    BOOST_TEST((version < 11).evaluate(req, ctx));
    BOOST_TEST(!(11 < version).evaluate(req, ctx));
    BOOST_TEST((11 >= version).evaluate(req, ctx));
    BOOST_TEST(!(9 >= version).evaluate(req, ctx));
    BOOST_TEST((10 >= version).evaluate(req, ctx));
    BOOST_TEST((9 <= version).evaluate(req, ctx));
    BOOST_TEST((10 <= version).evaluate(req, ctx));
    BOOST_TEST(!(11 <= version).evaluate(req, ctx));
}

BOOST_AUTO_TEST_CASE(test_matcher_header)
{
    namespace http = boost::beast::http;
    using namespace boost::web::matcher;
    using http::field;

    http::request<http::string_body> req{http::verb::get, "/a/b", 10};
    req.set(field::host, "example.com");
    req.set(field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set("example", "Example Value");
    context ctx;

    BOOST_TEST((header(field::host) == "example.com").evaluate(req, ctx));
    BOOST_TEST((header("host") == "example.com").evaluate(req, ctx));
    BOOST_TEST((header(field::user_agent) == BOOST_BEAST_VERSION_STRING).evaluate(req, ctx));
    BOOST_TEST((header("example") == "Example Value").evaluate(req, ctx));
    BOOST_TEST(!(header("example") == "example value").evaluate(req, ctx));
}

} // namespace
