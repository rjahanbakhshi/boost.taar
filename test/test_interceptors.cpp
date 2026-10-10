//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/core/request_attributes.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/json/parse.hpp>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace jsonrpc = boost::taar::jsonrpc;
namespace mcp = boost::taar::mcp;

using taar::matcher::method;
using taar::matcher::target;
using taar::handler::attribute_arg;
using taar::handler::rest;

struct principal { std::string user; };
struct request_id { int value = 0; };

BOOST_AUTO_TEST_CASE(test_request_attributes_bag)
{
    taar::request_attributes bag;
    BOOST_TEST(bag.empty());
    BOOST_TEST(!bag.contains<principal>());
    BOOST_TEST(bag.find<principal>() == nullptr);

    bag.set(principal {"alice"});
    bag.set(request_id {7});
    BOOST_TEST(!bag.empty());
    BOOST_TEST(bag.contains<principal>());
    BOOST_TEST(bag.find<principal>()->user == "alice");
    BOOST_TEST(bag.find<request_id>()->value == 7);

    // One value per type: a second set replaces the first.
    bag.set(principal {"bob"});
    BOOST_TEST(bag.find<principal>()->user == "bob");

    // Mutable access, and const access through a copy.
    bag.find<request_id>()->value = 8;
    taar::request_attributes const copy = bag;
    BOOST_TEST(copy.find<request_id>()->value == 8);
    BOOST_TEST(!copy.contains<std::string>());
}

BOOST_AUTO_TEST_CASE(test_attribute_arg_provider)
{
    http::request_header<> header;
    taar::matcher::context ctx;
    attribute_arg<principal> const arg;

    BOOST_TEST(arg.name().find("principal") != std::string::npos);
    auto absent = arg(header, ctx);
    BOOST_TEST(absent.has_error());
    BOOST_TEST(absent.error().is(taar::error::argument_not_found));

    ctx.attributes.set(principal {"alice"});
    auto present = arg(header, ctx);
    BOOST_TEST(present.has_value());
    BOOST_TEST(present->user == "alice");

    // Not an input parameter for OpenAPI.
    static_assert(taar::handler::detail::location_of<attribute_arg<principal>>() ==
        taar::handler::param_location::none);
}

BOOST_AUTO_TEST_CASE(test_attribute_arg_optional_when_absent)
{
    net::io_context io;
    taar::session::http session;

    session.register_request_handler(
        method == http::verb::get && target == "/me",
        rest([](std::optional<principal> p) { return p ? p->user : std::string {"anonymous"}; },
            attribute_arg<principal>()));
    session.register_request_handler(
        method == http::verb::get && target == "/required",
        rest([](principal const& p) { return p.user; }, attribute_arg<principal>()));

    jsonrpc::endpoint<> rpc;
    rpc.method("whoami", [](std::optional<principal> p) { return p ? p->user : std::string {"anonymous"}; },
        attribute_arg<principal>());
    session.register_request_handler(method == http::verb::post && target == "/rpc", rpc.handler());

    mcp::endpoint<mcp::stateless> tools {{.name = "s", .version = "1"}};
    tools.tool("whoami", "Caller", [](std::optional<principal> p, std::string greeting)
    {
        return greeting + " " + (p ? p->user : std::string {"anonymous"});
    }, attribute_arg<principal>(), mcp::mcp_arg("greeting"));
    session.register_request_handler(target == "/mcp", tools.handler());

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto list = e2e::make_request(http::verb::post, "/mcp", R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
        auto call = e2e::make_request(http::verb::post, "/mcp",
            R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"whoami","arguments":{"greeting":"hi"}}})");
        auto r = co_await e2e::send_all(port, {
            e2e::make_request(http::verb::get, "/me"),
            e2e::make_request(http::verb::get, "/required"),
            e2e::make_request(http::verb::post, "/rpc", R"({"jsonrpc":"2.0","id":1,"method":"whoami"})"),
            list,
            call});
        BOOST_TEST(r[0].body() == "anonymous");
        // A required attribute that nobody set is an argument failure.
        BOOST_TEST(r[1].result() == http::status::bad_request);
        BOOST_TEST(json::parse(r[2].body()).at("result").as_string() == "anonymous");

        // The attribute is not part of the tool's input schema.
        auto schema = json::parse(r[3].body()).at("result").at("tools").as_array()[0].at("inputSchema");
        BOOST_TEST(schema.at("properties").as_object().size() == 1u);
        BOOST_TEST(schema.at("properties").as_object().contains("greeting"));
        BOOST_TEST(json::parse(r[4].body()).at("result").at("structuredContent").at("result").as_string() == "hi anonymous");
    });
}

} // namespace
