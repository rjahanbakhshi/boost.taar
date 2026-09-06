//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/error.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/json/value.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>

namespace {

namespace taar = boost::taar;
namespace http = boost::beast::http;
using taar::failure;
using taar::error;

// failure is a usable E for boost::system::result: no implicit conversions
// from codes or to/from the value types.
static_assert(!std::is_convertible_v<boost::system::error_code, failure>);
static_assert(!std::is_convertible_v<error, failure>);
static_assert(!std::is_convertible_v<failure, boost::json::value>);
static_assert(!std::is_convertible_v<failure, std::string>);
static_assert(std::is_constructible_v<taar::result<int>, failure>);
static_assert(std::is_constructible_v<taar::result<std::string>, std::string>);
static_assert(std::is_constructible_v<taar::result<boost::json::value>, boost::json::value>);
static_assert(std::is_constructible_v<taar::result<boost::json::value>, failure>);
static_assert(std::is_nothrow_copy_constructible_v<taar::exception>);

struct user_error {};

BOOST_AUTO_TEST_CASE(test_failure_construction)
{
    failure none;
    BOOST_TEST(!none.code());
    BOOST_TEST(none.message().empty());
    BOOST_TEST(none.data().is_null());
    BOOST_TEST(none.cause() == nullptr);
    BOOST_TEST(!none.exception());

    failure f {error::argument_not_found};
    BOOST_TEST((f.code() == error::argument_not_found));
    BOOST_TEST(f.message().empty());
    BOOST_TEST(f.where().line() > 0u);

    failure g {boost::system::errc::make_error_code(boost::system::errc::timed_out), "waiting for roots"};
    BOOST_TEST((g.code() == boost::system::errc::timed_out));
    BOOST_TEST(g.message() == "waiting for roots");

    f.with_message("query parameter 'page'").with_data({{"argument", "page"}});
    BOOST_TEST(f.message() == "query parameter 'page'");
    BOOST_TEST(f.data().at("argument").as_string() == "page");
}

BOOST_AUTO_TEST_CASE(test_failure_chain)
{
    failure inner {error::invalid_number_format, "value 'abc'"};
    failure outer {error::invalid_argument_format, "argument page (position 1)"};
    outer.with_cause(inner);

    BOOST_REQUIRE(outer.cause() != nullptr);
    BOOST_TEST(outer.cause()->message() == "value 'abc'");
    BOOST_TEST(&outer.root() == outer.cause());
    BOOST_TEST(&inner.root() == &inner);

    BOOST_TEST(inner.chain() == "value 'abc': Invalid number format [Taar error:11]");
    BOOST_TEST(
        outer.chain() ==
        "argument page (position 1): Invalid argument format. [Taar error:5]"
        " (caused by: value 'abc': Invalid number format [Taar error:11])");

    // Three levels; a level without message prints only the code text.
    failure top {error::constraint_violated};
    top.with_cause(outer);
    BOOST_TEST(
        top.chain() ==
        "Argument constraint violated [Taar error:13]"
        " (caused by: argument page (position 1): Invalid argument format. [Taar error:5]"
        " (caused by: value 'abc': Invalid number format [Taar error:11]))");

    // Same text as system_error::what() for a single level.
    boost::system::system_error se {error::constraint_violated, "id must be >= 1"};
    BOOST_TEST(failure(error::constraint_violated, "id must be >= 1").chain() == std::string {se.what()});

    // Copies share the cause.
    failure copy = outer;
    BOOST_TEST(copy.cause() == outer.cause());
}

BOOST_AUTO_TEST_CASE(test_failure_is_has)
{
    failure f {error::invalid_argument_format};
    f.with_cause(failure {error::invalid_number_format});

    BOOST_TEST(f.is(error::invalid_argument_format));
    BOOST_TEST(!f.is(error::invalid_number_format));
    BOOST_TEST(f.has(error::invalid_argument_format));
    BOOST_TEST(f.has(error::invalid_number_format));
    BOOST_TEST(!f.has(error::argument_not_found));
    BOOST_TEST(f.root().is(error::invalid_number_format));
}

BOOST_AUTO_TEST_CASE(test_failure_from_exception)
{
    // taar::exception: its failure, not wrapped.
    {
        failure f {error::argument_ambiguous, "query parameter 'tag'"};
        f.with_data({{"argument", "tag"}});
        auto const got = failure::from_exception(std::make_exception_ptr(taar::exception {f}));
        BOOST_TEST(got.is(error::argument_ambiguous));
        BOOST_TEST(got.message() == "query parameter 'tag'");
        BOOST_TEST(got.data().at("argument").as_string() == "tag");
        BOOST_TEST(got.cause() == nullptr);
        BOOST_TEST(!got.exception());
    }
    // boost::system::system_error: its code, the prefix as message, the exception kept.
    {
        auto eptr = std::make_exception_ptr(boost::system::system_error {http::error::body_limit, "reading body"});
        auto const got = failure::from_exception(eptr);
        BOOST_TEST((got.code() == http::error::body_limit));
        BOOST_TEST(got.message() == "reading body");
        BOOST_TEST((got.exception() == eptr));

        auto const bare = failure::from_exception(
            std::make_exception_ptr(boost::system::system_error {error::late_chunk_metadata}));
        BOOST_TEST(bare.is(error::late_chunk_metadata));
        BOOST_TEST(bare.message().empty());
    }
    // std::system_error.
    {
        auto const got = failure::from_exception(
            std::make_exception_ptr(std::system_error {std::make_error_code(std::errc::timed_out), "connect"}));
        BOOST_TEST((got.code() == boost::system::errc::timed_out));
        BOOST_TEST(got.message() == "connect");
    }
    // std::exception: unknown_exception with what() as message.
    {
        auto eptr = std::make_exception_ptr(std::runtime_error {"boom"});
        auto const got = failure::from_exception(eptr);
        BOOST_TEST(got.is(error::unknown_exception));
        BOOST_TEST(got.message() == "boom");
        BOOST_TEST((got.exception() == eptr));
        BOOST_TEST(got.chain() == "boom: Unknown exception [Taar error:14]");
    }
    // Non-std throw.
    {
        auto eptr = std::make_exception_ptr(user_error {});
        auto const got = failure::from_exception(eptr);
        BOOST_TEST(got.is(error::unknown_exception));
        BOOST_TEST(got.message() == "unknown exception");
        BOOST_TEST((got.exception() == eptr));
    }
    // Null.
    BOOST_TEST(!failure::from_exception(nullptr).code());
}

BOOST_AUTO_TEST_CASE(test_result_value_throws_exception)
{
    taar::result<int> ok {42};
    BOOST_TEST(ok.value() == 42);

    failure f {error::invalid_content_type, "media type 'text/plain'"};
    taar::result<int> bad {f};
    BOOST_TEST(bad.has_error());
    BOOST_TEST(bad.error().is(error::invalid_content_type));

    try
    {
        (void)bad.value();
        BOOST_FAIL("value() did not throw");
    }
    catch (taar::exception const& e)
    {
        BOOST_TEST(e.error().is(error::invalid_content_type));
        BOOST_TEST(e.error().message() == "media type 'text/plain'");
        BOOST_TEST(std::string {e.what()} == f.chain());
    }

    taar::result<std::string> text {failure {error::argument_not_found}};
    BOOST_CHECK_THROW((void)text.value(), taar::exception);
    BOOST_CHECK_THROW((void)text.value(), std::exception);
}

BOOST_AUTO_TEST_CASE(test_failure_rethrow)
{
    // No foreign exception: throws taar::exception with the same failure.
    try
    {
        failure {error::argument_ambiguous, "header 'x'"}.rethrow();
    }
    catch (taar::exception const& e)
    {
        BOOST_TEST(e.error().is(error::argument_ambiguous));
        BOOST_TEST(e.error().message() == "header 'x'");
    }

    // A wrapped user exception comes back as itself, even when nested.
    failure outer {error::invalid_argument_format, "argument x (position 1)"};
    outer.with_cause(failure::from_exception(std::make_exception_ptr(user_error {})));
    BOOST_CHECK_THROW(outer.rethrow(), user_error);

    auto const wrapped = failure::from_exception(std::make_exception_ptr(std::out_of_range {"index"}));
    BOOST_CHECK_THROW(wrapped.rethrow(), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(test_http_status_for)
{
    BOOST_TEST((taar::http_status_for(failure {error::argument_not_found}) == http::status::bad_request));
    BOOST_TEST((taar::http_status_for(failure {error::invalid_argument_format}) == http::status::bad_request));
    BOOST_TEST((taar::http_status_for(failure {error::constraint_violated}) == http::status::bad_request));
    BOOST_TEST((taar::http_status_for(failure {error::invalid_content_type}) == http::status::bad_request));
    BOOST_TEST((taar::http_status_for(failure {http::error::body_limit}) == http::status::payload_too_large));
    BOOST_TEST((taar::http_status_for(failure {http::error::header_limit}) ==
        http::status::request_header_fields_too_large));
    BOOST_TEST((taar::http_status_for(failure {error::unknown_exception}) == http::status::internal_server_error));
    BOOST_TEST((taar::http_status_for(failure {error::late_chunk_metadata}) == http::status::internal_server_error));
    BOOST_TEST((taar::http_status_for(
        failure::from_exception(std::make_exception_ptr(std::runtime_error {"x"}))) ==
        http::status::internal_server_error));
    // Only the outermost code counts.
    failure wrapped {error::unknown_exception};
    wrapped.with_cause(failure {error::argument_not_found});
    BOOST_TEST((taar::http_status_for(wrapped) == http::status::internal_server_error));
}

} // namespace
