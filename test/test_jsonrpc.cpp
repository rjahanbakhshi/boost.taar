//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/jsonrpc/envelope.hpp>
#include <boost/taar/jsonrpc/error.hpp>
#include <boost/taar/jsonrpc/response.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>
#include <boost/test/unit_test.hpp>
#include <string>

namespace {

namespace jsonrpc = boost::taar::jsonrpc;
namespace json    = boost::json;

// ---- error category --------------------------------------------------------

BOOST_AUTO_TEST_CASE(test_jsonrpc_error_category_name)
{
    BOOST_TEST(std::string{jsonrpc::error_category().name()} == "jsonrpc");
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_error_codes_have_spec_values)
{
    BOOST_TEST(static_cast<int>(jsonrpc::error::parse_error)      == -32700);
    BOOST_TEST(static_cast<int>(jsonrpc::error::invalid_request)  == -32600);
    BOOST_TEST(static_cast<int>(jsonrpc::error::method_not_found) == -32601);
    BOOST_TEST(static_cast<int>(jsonrpc::error::invalid_params)   == -32602);
    BOOST_TEST(static_cast<int>(jsonrpc::error::internal_error)   == -32603);
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_error_messages)
{
    auto const& cat = jsonrpc::error_category();
    BOOST_TEST(cat.message(static_cast<int>(jsonrpc::error::parse_error))      == "Parse error.");
    BOOST_TEST(cat.message(static_cast<int>(jsonrpc::error::invalid_request))  == "Invalid Request.");
    BOOST_TEST(cat.message(static_cast<int>(jsonrpc::error::method_not_found)) == "Method not found.");
    BOOST_TEST(cat.message(static_cast<int>(jsonrpc::error::invalid_params))   == "Invalid params.");
    BOOST_TEST(cat.message(static_cast<int>(jsonrpc::error::internal_error))   == "Internal error.");
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_error_converts_to_error_code)
{
    // The is_error_code_enum + make_error_code wiring lets the enum
    // implicitly become a boost::system::error_code.
    boost::system::error_code ec = jsonrpc::error::method_not_found;
    BOOST_TEST(ec.value() == -32601);
    BOOST_TEST((ec.category() == jsonrpc::error_category()));
    BOOST_TEST((ec == jsonrpc::error::method_not_found));
}

// ---- parse_envelope: positive cases ---------------------------------------

BOOST_AUTO_TEST_CASE(test_parse_envelope_full_request)
{
    auto value = json::parse(R"({
        "jsonrpc": "2.0",
        "id": 7,
        "method": "add",
        "params": {"a": 2, "b": 3}
    })");

    auto result = jsonrpc::parse_envelope(value);
    BOOST_REQUIRE(result.has_value());
    auto const& env = result.value();
    BOOST_TEST(env.jsonrpc == "2.0");
    BOOST_TEST(env.method == "add");
    BOOST_REQUIRE(env.id.has_value());
    BOOST_TEST(env.id->as_int64() == 7);
    BOOST_REQUIRE(env.params.has_value());
    BOOST_TEST(env.params->as_object().at("a").as_int64() == 2);
    BOOST_TEST(!jsonrpc::is_notification(env));
}

BOOST_AUTO_TEST_CASE(test_parse_envelope_notification_has_no_id)
{
    auto value = json::parse(R"({
        "jsonrpc": "2.0",
        "method": "ping"
    })");

    auto result = jsonrpc::parse_envelope(value);
    BOOST_REQUIRE(result.has_value());
    auto const& env = result.value();
    BOOST_TEST(env.method == "ping");
    BOOST_TEST(!env.id.has_value());
    BOOST_TEST(!env.params.has_value());
    BOOST_TEST(jsonrpc::is_notification(env));
}

BOOST_AUTO_TEST_CASE(test_parse_envelope_id_can_be_string_or_null)
{
    {
        auto value = json::parse(R"({"jsonrpc":"2.0","id":"abc","method":"m"})");
        auto result = jsonrpc::parse_envelope(value);
        BOOST_REQUIRE(result.has_value());
        BOOST_TEST(result.value().id->as_string() == "abc");
    }
    {
        // Explicit null id is *not* a notification -- id is present and
        // its value is null.
        auto value = json::parse(R"({"jsonrpc":"2.0","id":null,"method":"m"})");
        auto result = jsonrpc::parse_envelope(value);
        BOOST_REQUIRE(result.has_value());
        BOOST_REQUIRE(result.value().id.has_value());
        BOOST_TEST(result.value().id->is_null());
        BOOST_TEST(!jsonrpc::is_notification(result.value()));
    }
}

BOOST_AUTO_TEST_CASE(test_parse_envelope_params_can_be_array)
{
    auto value = json::parse(R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "sum",
        "params": [1, 2, 3]
    })");

    auto result = jsonrpc::parse_envelope(value);
    BOOST_REQUIRE(result.has_value());
    BOOST_REQUIRE(result.value().params.has_value());
    BOOST_TEST(result.value().params->as_array().size() == 3u);
}

// ---- parse_envelope: negative cases ---------------------------------------

BOOST_AUTO_TEST_CASE(test_parse_envelope_rejects_non_object)
{
    auto result = jsonrpc::parse_envelope(json::value{"not an object"});
    BOOST_REQUIRE(!result.has_value());
    BOOST_TEST((result.error() == jsonrpc::error::invalid_request));
}

BOOST_AUTO_TEST_CASE(test_parse_envelope_rejects_missing_jsonrpc_field)
{
    auto value = json::parse(R"({"id":1,"method":"m"})");
    auto result = jsonrpc::parse_envelope(value);
    BOOST_REQUIRE(!result.has_value());
    BOOST_TEST((result.error() == jsonrpc::error::invalid_request));
}

BOOST_AUTO_TEST_CASE(test_parse_envelope_rejects_wrong_jsonrpc_version)
{
    auto value = json::parse(R"({"jsonrpc":"1.0","id":1,"method":"m"})");
    auto result = jsonrpc::parse_envelope(value);
    BOOST_REQUIRE(!result.has_value());
    BOOST_TEST((result.error() == jsonrpc::error::invalid_request));
}

BOOST_AUTO_TEST_CASE(test_parse_envelope_rejects_non_string_jsonrpc)
{
    auto value = json::parse(R"({"jsonrpc":2.0,"id":1,"method":"m"})");
    auto result = jsonrpc::parse_envelope(value);
    BOOST_REQUIRE(!result.has_value());
    BOOST_TEST((result.error() == jsonrpc::error::invalid_request));
}

BOOST_AUTO_TEST_CASE(test_parse_envelope_rejects_missing_method)
{
    auto value = json::parse(R"({"jsonrpc":"2.0","id":1})");
    auto result = jsonrpc::parse_envelope(value);
    BOOST_REQUIRE(!result.has_value());
    BOOST_TEST((result.error() == jsonrpc::error::invalid_request));
}

BOOST_AUTO_TEST_CASE(test_parse_envelope_rejects_non_string_method)
{
    auto value = json::parse(R"({"jsonrpc":"2.0","id":1,"method":42})");
    auto result = jsonrpc::parse_envelope(value);
    BOOST_REQUIRE(!result.has_value());
    BOOST_TEST((result.error() == jsonrpc::error::invalid_request));
}

// ---- response builders ----------------------------------------------------

BOOST_AUTO_TEST_CASE(test_make_response_shape)
{
    auto resp = jsonrpc::make_response(json::value{7}, json::value{42});
    auto const& obj = resp.as_object();
    BOOST_TEST(obj.at("jsonrpc").as_string() == "2.0");
    BOOST_TEST(obj.at("id").as_int64() == 7);
    BOOST_TEST(obj.at("result").as_int64() == 42);
}

BOOST_AUTO_TEST_CASE(test_make_response_preserves_string_id)
{
    auto resp = jsonrpc::make_response(json::value{"req-1"}, json::value{"ok"});
    BOOST_TEST(resp.as_object().at("id").as_string() == "req-1");
}

BOOST_AUTO_TEST_CASE(test_make_error_response_with_default_message_from_category)
{
    auto const default_msg = jsonrpc::error_category().message(
        static_cast<int>(jsonrpc::error::method_not_found));

    auto resp = jsonrpc::make_error_response(
        json::value{1}, jsonrpc::error::method_not_found, default_msg);

    auto const& err = resp.as_object().at("error").as_object();
    BOOST_TEST(err.at("code").as_int64() == -32601);
    BOOST_TEST(err.at("message").as_string() == "Method not found.");
    BOOST_TEST(!err.contains("data"));
}

BOOST_AUTO_TEST_CASE(test_make_error_response_custom_message_and_data)
{
    auto resp = jsonrpc::make_error_response(
        json::value{1},
        jsonrpc::error::invalid_params,
        std::string{"argument 'b' is missing"},
        json::value{json::object{{"missing", "b"}}});

    auto const& err = resp.as_object().at("error").as_object();
    BOOST_TEST(err.at("code").as_int64() == -32602);
    BOOST_TEST(err.at("message").as_string() == "argument 'b' is missing");
    BOOST_TEST(err.at("data").as_object().at("missing").as_string() == "b");
}

BOOST_AUTO_TEST_CASE(test_make_error_response_accepts_null_id)
{
    // Per spec, errors emitted before id extraction use null.
    auto resp = jsonrpc::make_error_response(
        json::value{nullptr},
        jsonrpc::error::parse_error,
        "Invalid JSON");
    BOOST_TEST(resp.as_object().at("id").is_null());
    BOOST_TEST(resp.as_object().at("error").as_object().at("code").as_int64() == -32700);
}

BOOST_AUTO_TEST_CASE(test_make_error_response_with_application_code)
{
    // Application server errors live in -32000..-32099; the int overload
    // lets callers use them.
    auto resp = jsonrpc::make_error_response(
        json::value{1}, -32050, "Rate-limited");
    BOOST_TEST(resp.as_object().at("error").as_object().at("code").as_int64() == -32050);
}

} // namespace
