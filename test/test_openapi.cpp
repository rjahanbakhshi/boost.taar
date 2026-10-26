//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/handler/constrained.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/rest_param.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/matcher/cookie.hpp>
#include <boost/taar/matcher/header.hpp>
#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/describe.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
namespace openapi = boost::taar::openapi;
namespace matcher = boost::taar::matcher;
using namespace boost::taar::handler;
using matcher::cookie;
using matcher::header;
using matcher::method;
using matcher::target;

// ---- compile-time shape policy --------------------------------------------

template <typename Expr>
using meta_of = typename std::remove_cvref_t<Expr>::metadata_type;

static_assert(openapi::detail::expressible_v<meta_of<decltype(method == http::verb::get && target == "/a")>>);
static_assert(openapi::detail::expressible_v<meta_of<decltype(target == "/a" && method == http::verb::get)>>);
static_assert(openapi::detail::expressible_v<meta_of<decltype((method == http::verb::get || method == http::verb::head) && target == "/a")>>);
static_assert(openapi::detail::expressible_v<meta_of<decltype(method == http::verb::get && target == "/a" && header("X-Key") == "k")>>);
static_assert(openapi::detail::expressible_v<meta_of<decltype(method == http::verb::get && target == "/a" && exist(cookie("sid")))>>);
static_assert(!openapi::detail::expressible_v<meta_of<decltype(target == "/a")>>,
    "a route needs a method");
static_assert(!openapi::detail::expressible_v<meta_of<decltype(method == http::verb::get)>>,
    "a route needs a path");
static_assert(!openapi::detail::expressible_v<meta_of<decltype(method != http::verb::get && target == "/a")>>,
    "negated methods are not expressible");
static_assert(!openapi::detail::expressible_v<meta_of<decltype(method == http::verb::get && !(target == "/a"))>>,
    "negation is not expressible");
static_assert(!openapi::detail::expressible_v<meta_of<decltype(method == http::verb::get && (target == "/a" || target == "/b"))>>,
    "alternation is only allowed between methods");
static_assert(!openapi::detail::expressible_v<meta_of<decltype(method == http::verb::get && target == "/a" && header("X") != "y")>>);
static_assert(!openapi::detail::expressible_v<matcher::any_metadata>,
    "plain callable matchers carry no metadata");

// ---- fixtures -------------------------------------------------------------

struct user_session { std::string user = "alice"; };

struct todo_input
{
    std::string title;
    std::optional<bool> done;
};
BOOST_DESCRIBE_STRUCT(todo_input, (), (title, done))

struct todo
{
    int id;
    std::string title;
    bool done;
};
BOOST_DESCRIBE_STRUCT(todo, (), (id, title, done))

todo create_todo(todo_input in) { return {1, in.title, in.done.value_or(false)}; }
std::vector<todo> list_todos() { return {}; }

json::object get_user(int id, std::optional<bool> verbose, int page)
{
    return {{"id", id}, {"verbose", verbose.value_or(false)}, {"page", page}};
}

std::string create_user(json::value const& body) { return json::serialize(body); }
void delete_user(int, std::string) {}
std::string whoami(user_session const& s, std::string q) { return s.user + ":" + q; }

template <typename Session>
void register_routes(Session& session, openapi::generator& gen, taar::context_store<std::string, user_session>& store)
{
    session.register_request_handler(
        method == http::verb::get && target == "/users/{id}",
        rest(&get_user,
            path_arg("id", "User id"),
            query_arg("verbose"),
            with_default(query_arg("page", "Page number"), 1)),
        gen.describe({.summary = "Fetch a user", .tags = {"users"}, .operation_id = "getUser"}));

    session.register_request_handler(
        method == http::verb::post && target == "/users",
        rest(&create_user, json_body_arg(description {"The user to create"})),
        gen.describe("Create a user"));

    session.register_request_handler(
        method == http::verb::delete_ && target == "/users/{id}" && header("X-Api-Key") == "secret" && exist(cookie("sid")),
        rest(&delete_user, path_arg("id"), header_arg("X-Request-Id", "Trace id")),
        gen.describe({.summary = "Delete a user", .deprecated = true}));

    session.register_request_handler(
        (method == http::verb::get || method == http::verb::head) && target == "/health",
        [](http::request<http::empty_body> const& req, matcher::context const&)
        {
            http::response<http::string_body> res {http::status::ok, req.version()};
            res.body() = "ok";
            res.prepare_payload();
            return res;
        },
        gen.describe({.summary = "Health", .responses = {{"200", "Healthy"}, {"503", "Degraded"}}}));

    session.register_request_handler(
        method == http::verb::get && target == "/files/{*path}",
        [](http::request<http::empty_body> const& req, matcher::context const& ctx)
        {
            http::response<http::string_body> res {http::status::ok, req.version()};
            res.body() = std::string {ctx.path_args.at("path")};
            res.prepare_payload();
            return res;
        },
        gen.describe("Serve a file"));

    session.register_request_handler(
        method == http::verb::get && target == "/me",
        with_session(store, session_key::cookie {"sid"}, session_policy::require,
            rest(&whoami, query_arg("q", "Query"))),
        gen.describe("Who am I"));

    session.register_request_handler(
        method == http::verb::get && target == "/openapi.json",
        gen.handler());
}

// ---- provider introspection -----------------------------------------------

BOOST_AUTO_TEST_CASE(test_rest_params_describe_providers)
{
    auto h = rest(&get_user,
        path_arg("id", "User id"),
        query_arg("verbose"),
        with_default(query_arg("page", "Page number"), 1));
    auto params = h.params();
    BOOST_REQUIRE(params.size() == 3u);

    BOOST_TEST((params[0].in == param_location::path));
    BOOST_TEST(params[0].info.name == "id");
    BOOST_TEST(params[0].info.json_type == "integer");
    BOOST_TEST(params[0].info.description == "User id");
    BOOST_TEST(params[0].info.required);

    BOOST_TEST((params[1].in == param_location::query));
    BOOST_TEST(params[1].info.json_type == "boolean");
    BOOST_TEST(!params[1].info.required);           // std::optional<bool>

    BOOST_TEST((params[2].in == param_location::query));
    BOOST_TEST(params[2].info.description == "Page number");
    BOOST_TEST(!params[2].info.required);           // with_default

    auto body = rest(&create_user, json_body_arg("application/json", "text/json", description {"payload"}));
    auto bp = body.params();
    BOOST_REQUIRE(bp.size() == 1u);
    BOOST_TEST((bp[0].in == param_location::body));
    BOOST_TEST(bp[0].info.description == "payload");
    BOOST_TEST(bp[0].info.json_type == "");         // json::value: any
    BOOST_REQUIRE(bp[0].content_types.size() == 2u);
    BOOST_TEST(bp[0].content_types[0] == "application/json");
    BOOST_TEST(bp[0].content_types[1] == "text/json");

    auto other = rest(&delete_user, path_arg("id"), header_arg(http::field::authorization, "Bearer token"));
    auto op = other.params();
    BOOST_TEST((op[1].in == param_location::header));
    BOOST_TEST(op[1].info.name == "Authorization");
    BOOST_TEST(op[1].info.description == "Bearer token");

    auto text = rest([](std::string) {}, string_body_arg(all_content_types, description {"raw"}));
    BOOST_TEST(text.params()[0].content_types.empty());
    BOOST_TEST(text.params()[0].info.json_type == "string");
    BOOST_TEST(text.params()[0].info.description == "raw");

    auto misc = rest([](std::string, std::size_t) {}, target_arg {}, body_size_arg {});
    BOOST_TEST((misc.params()[0].in == param_location::none));
    BOOST_TEST((misc.params()[1].in == param_location::none));
}

// ---- document --------------------------------------------------------------

BOOST_AUTO_TEST_CASE(test_openapi_document_structure)
{
    taar::session::http session;
    openapi::generator gen {{.title = "Users API", .version = "1.2.3", .description = "Demo"}};
    gen.add_server("http://localhost:8080", "Local");
    taar::context_store<std::string, user_session> store {std::chrono::minutes {5}};
    register_routes(session, gen, store);
    BOOST_TEST(gen.operation_count() == 6u);

    auto doc = gen.document();
    BOOST_TEST(doc.at("openapi").as_string() == "3.1.0");
    BOOST_TEST(doc.at("info").at("title").as_string() == "Users API");
    BOOST_TEST(doc.at("info").at("version").as_string() == "1.2.3");
    BOOST_TEST(doc.at("info").at("description").as_string() == "Demo");
    BOOST_TEST(doc.at("servers").as_array()[0].at("url").as_string() == "http://localhost:8080");

    auto const& paths = doc.at("paths").as_object();
    BOOST_TEST(paths.size() == 5u);
    BOOST_TEST(!paths.contains("/openapi.json"));

    // GET /users/{id}
    auto const& get = paths.at("/users/{id}").at("get");
    BOOST_TEST(get.at("summary").as_string() == "Fetch a user");
    BOOST_TEST(get.at("operationId").as_string() == "getUser");
    BOOST_TEST(get.at("tags").as_array()[0].as_string() == "users");
    auto const& gp = get.at("parameters").as_array();
    BOOST_REQUIRE(gp.size() == 3u);
    BOOST_TEST(gp[0].at("name").as_string() == "id");
    BOOST_TEST(gp[0].at("in").as_string() == "path");
    BOOST_TEST(gp[0].at("required").as_bool());
    BOOST_TEST(gp[0].at("schema").at("type").as_string() == "integer");
    BOOST_TEST(gp[0].at("description").as_string() == "User id");
    BOOST_TEST(gp[1].at("in").as_string() == "query");
    BOOST_TEST(!gp[1].as_object().contains("required"));
    BOOST_TEST(gp[1].at("schema").at("type").as_string() == "boolean");
    BOOST_TEST(gp[2].at("schema").at("type").as_string() == "integer");
    BOOST_TEST(gp[2].at("description").as_string() == "Page number");
    BOOST_TEST(get.at("responses").at("200").at("content").at("application/json").at("schema").at("type").as_string() == "object");

    // POST /users
    auto const& post = paths.at("/users").at("post");
    BOOST_TEST(post.at("summary").as_string() == "Create a user");
    BOOST_TEST(!post.as_object().contains("parameters"));
    auto const& rb = post.at("requestBody");
    BOOST_TEST(rb.at("required").as_bool());
    BOOST_TEST(rb.at("description").as_string() == "The user to create");
    BOOST_TEST(rb.at("content").as_object().contains("application/json"));
    BOOST_TEST(rb.at("content").at("application/json").at("schema").as_object().empty());
    BOOST_TEST(post.at("responses").at("200").at("content").as_object().contains("text/plain"));

    // DELETE /users/{id}: matcher header/cookie constraints become required parameters.
    auto const& del = paths.at("/users/{id}").at("delete");
    BOOST_TEST(del.at("deprecated").as_bool());
    auto const& dp = del.at("parameters").as_array();
    BOOST_REQUIRE(dp.size() == 4u);
    BOOST_TEST(dp[0].at("name").as_string() == "id");
    BOOST_TEST(dp[1].at("name").as_string() == "X-Request-Id");
    BOOST_TEST(dp[1].at("in").as_string() == "header");
    BOOST_TEST(dp[1].at("description").as_string() == "Trace id");
    BOOST_TEST(dp[2].at("name").as_string() == "X-Api-Key");
    BOOST_TEST(dp[2].at("in").as_string() == "header");
    BOOST_TEST(dp[2].at("schema").at("const").as_string() == "secret");
    BOOST_TEST(dp[3].at("name").as_string() == "sid");
    BOOST_TEST(dp[3].at("in").as_string() == "cookie");
    BOOST_TEST(!dp[3].at("schema").as_object().contains("const"));
    BOOST_TEST(del.at("responses").at("200").at("description").as_string() == "No content");

    // GET|HEAD /health: two operations from one registration; explicit responses.
    BOOST_TEST(paths.at("/health").as_object().contains("get"));
    BOOST_TEST(paths.at("/health").as_object().contains("head"));
    auto const& health = paths.at("/health").at("get");
    BOOST_TEST(health.at("responses").at("200").at("description").as_string() == "Healthy");
    BOOST_TEST(health.at("responses").at("503").at("description").as_string() == "Degraded");
    BOOST_TEST(!health.as_object().contains("parameters"));

    // GET /files/{*path}: unbound greedy placeholder is still declared.
    auto const& files = paths.at("/files/{path}").at("get");
    auto const& fp = files.at("parameters").as_array();
    BOOST_REQUIRE(fp.size() == 1u);
    BOOST_TEST(fp[0].at("name").as_string() == "path");
    BOOST_TEST(fp[0].at("in").as_string() == "path");
    BOOST_TEST(fp[0].at("description").as_string() == "Matches the remainder of the path");
    BOOST_TEST(files.at("responses").at("200").at("description").as_string() == "Success");
    BOOST_TEST(!files.at("responses").at("200").as_object().contains("content"));

    // GET /me: with_session passes the inner handler's parameters and result through.
    auto const& me = paths.at("/me").at("get");
    BOOST_TEST(me.at("parameters").as_array()[0].at("name").as_string() == "q");
    BOOST_TEST(me.at("parameters").as_array()[0].at("description").as_string() == "Query");
    BOOST_TEST(me.at("responses").at("200").at("content").as_object().contains("text/plain"));
}

BOOST_AUTO_TEST_CASE(test_openapi_duplicate_operation_throws)
{
    taar::session::http session;
    openapi::generator gen {{.title = "t", .version = "1"}};
    session.register_request_handler(method == http::verb::get && target == "/a", rest([] { return 1; }), gen.describe("a"));
    BOOST_CHECK_THROW(
        session.register_request_handler(method == http::verb::get && target == "/a", rest([] { return 2; }), gen.describe("again")),
        std::logic_error);
    // Same path, different method is fine.
    session.register_request_handler(method == http::verb::post && target == "/a", rest([] { return 3; }), gen.describe("post"));
    BOOST_TEST(gen.operation_count() == 2u);
    BOOST_TEST(gen.document().at("paths").at("/a").at("get").at("responses").at("200").at("content").at("text/plain").at("schema").at("type").as_string() == "integer");
}

BOOST_AUTO_TEST_CASE(test_openapi_multiple_observers_and_root_path)
{
    taar::session::http session;
    openapi::generator gen {{.title = "t", .version = "1"}};
    int seen = 0;
    session.register_request_handler(
        method == http::verb::get && target == "/",
        rest([] { return std::string {"root"}; }),
        gen.describe("Root"),
        [&](auto const&, auto const&) { ++seen; });
    BOOST_TEST(seen == 1);
    BOOST_TEST(gen.document().at("paths").as_object().contains("/"));
}

BOOST_AUTO_TEST_CASE(test_e2e_openapi_document_served)
{
    net::io_context io;
    taar::session::http session;
    openapi::generator gen {{.title = "Users API", .version = "1.0.0"}};
    taar::context_store<std::string, user_session> store {std::chrono::minutes {5}};
    register_routes(session, gen, store);

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/openapi.json"),
            e2e::make_request(http::verb::get, "/users/7?verbose=true")
        );
        BOOST_TEST(res[0].result() == http::status::ok);
        BOOST_TEST(res[0][http::field::content_type] == "application/json");
        BOOST_TEST(json::parse(res[0].body()) == json::value {gen.document()});
        BOOST_TEST(json::parse(res[1].body()).at("id").as_int64() == 7);
    });
}

BOOST_AUTO_TEST_CASE(test_openapi_described_types_render_full_schemas)
{
    taar::session::http session;
    openapi::generator gen {{.title = "t", .version = "1"}};
    gen.set_inline_schemas();
    session.register_request_handler(
        method == http::verb::post && target == "/todos",
        rest(&create_todo, json_body_arg(description {"New todo"})),
        gen.describe("Create"));
    session.register_request_handler(
        method == http::verb::get && target == "/todos",
        rest(&list_todos),
        gen.describe("List"));

    auto doc = gen.document();
    auto const& body = doc.at("paths").at("/todos").at("post").at("requestBody").at("content").at("application/json").at("schema");
    BOOST_TEST(body.at("type").as_string() == "object");
    BOOST_TEST(body.at("properties").at("title").at("type").as_string() == "string");
    BOOST_TEST(body.at("properties").at("done").at("type").as_string() == "boolean");
    BOOST_TEST(body.at("required").as_array().size() == 1u);

    auto const& created = doc.at("paths").at("/todos").at("post").at("responses").at("200").at("content").at("application/json").at("schema");
    BOOST_TEST(created.at("properties").at("id").at("type").as_string() == "integer");
    BOOST_TEST(created.at("required").as_array().size() == 3u);

    auto const& listed = doc.at("paths").at("/todos").at("get").at("responses").at("200").at("content").at("application/json").at("schema");
    BOOST_TEST(listed.at("type").as_string() == "array");
    BOOST_TEST(listed.at("items").at("properties").at("title").at("type").as_string() == "string");
    BOOST_TEST(listed.at("items").at("title").as_string().ends_with("todo"));
    BOOST_TEST(!doc.contains("components"));
}

BOOST_AUTO_TEST_CASE(test_openapi_query_arrays_explode)
{
    taar::session::http session;
    openapi::generator gen {{.title = "t", .version = "1"}};
    session.register_request_handler(
        method == http::verb::get && target == "/search",
        rest([](std::vector<std::string>, std::vector<int>) { return 1; },
            taar::handler::query_list_arg("tag", "Repeated"),
            query_arg("ids", "Comma separated")),
        gen.describe("Search"));
    auto const doc = gen.document();     // keep the document alive while indexing into it
    auto const& params = doc.at("paths").at("/search").at("get").at("parameters").as_array();
    BOOST_REQUIRE(params.size() == 2u);
    BOOST_TEST(params[0].at("name").as_string() == "tag");
    BOOST_TEST(params[0].at("schema").at("type").as_string() == "array");
    BOOST_TEST(params[0].at("schema").at("items").at("type").as_string() == "string");
    BOOST_TEST(!params[0].as_object().contains("explode"));             // form/explode is the default
    BOOST_TEST(params[1].at("schema").at("items").at("type").as_string() == "integer");
    BOOST_TEST(params[1].at("explode").as_bool() == false);             // CSV in one occurrence
}

BOOST_AUTO_TEST_CASE(test_openapi_components_defaults_constraints_and_security)
{
    using taar::handler::constrained;
    taar::session::http session;
    openapi::generator gen {{.title = "t", .version = "1"}};
    gen.add_security_scheme("bearer", openapi::security_scheme::bearer("JWT", "Access token"));
    gen.add_security_scheme("api_key", openapi::security_scheme::api_key_header("X-Api-Key"));
    gen.set_security({"bearer"});
    BOOST_CHECK_THROW(gen.add_security_scheme("bearer", openapi::security_scheme::basic()), std::logic_error);

    session.register_request_handler(
        method == http::verb::get && target == "/todos",
        rest([](int, std::string) { return list_todos(); },
            constrained(with_default(query_arg("page", "Page"), 1), {.minimum = 1}),
            constrained(query_arg("sort"), {.one_of = {"asc", "desc"}, .example = "asc"})),
        gen.describe("List"));
    session.register_request_handler(
        method == http::verb::post && target == "/todos" && header("X-Api-Key") == "k",
        rest(&create_todo, json_body_arg()),
        gen.describe({.summary = "Create", .security = {"api_key"}}));
    session.register_request_handler(
        method == http::verb::delete_ && target == "/todos/{id}",
        rest(&delete_user, path_arg("id"), header_arg(http::field::authorization, "Bearer token")),
        gen.describe("Delete"));
    session.register_request_handler(
        method == http::verb::get && target == "/health",
        rest([] { return 1; }),
        gen.describe({.summary = "Health", .anonymous = true}));

    auto doc = gen.document();
    auto const& paths = doc.at("paths");

    // Defaults and constraint keywords on parameters.
    auto const& lp = paths.at("/todos").at("get").at("parameters").as_array();
    BOOST_REQUIRE(lp.size() == 2u);
    BOOST_TEST(lp[0].at("schema").at("default").as_int64() == 1);
    BOOST_TEST(lp[0].at("schema").at("minimum").as_int64() == 1);
    BOOST_TEST(!lp[0].as_object().contains("required"));
    BOOST_TEST(lp[1].at("schema").at("enum").as_array().size() == 2u);
    BOOST_TEST(lp[1].at("schema").at("examples").as_array()[0].as_string() == "asc");

    // Described types are hoisted into components and referenced.
    auto const& body_schema = paths.at("/todos").at("post").at("requestBody").at("content").at("application/json").at("schema");
    BOOST_REQUIRE(body_schema.as_object().contains("$ref"));
    std::string const ref {body_schema.at("$ref").as_string()};
    BOOST_TEST(ref.starts_with("#/components/schemas/"));
    BOOST_TEST(ref.ends_with("todo_input"));
    auto const& schemas = doc.at("components").at("schemas").as_object();
    auto const& input = schemas.at(ref.substr(std::string {"#/components/schemas/"}.size()));
    BOOST_TEST(input.at("properties").at("title").at("type").as_string() == "string");
    auto const& listed = paths.at("/todos").at("get").at("responses").at("200").at("content").at("application/json").at("schema");
    BOOST_TEST(listed.at("type").as_string() == "array");
    BOOST_TEST(std::string {listed.at("items").at("$ref").as_string()}.ends_with("todo"));
    BOOST_TEST(schemas.size() == 2u);

    // Security: document default, explicit, inferred from the Authorization header, and anonymous.
    BOOST_TEST(doc.at("security").as_array()[0].as_object().contains("bearer"));
    auto const& ss = doc.at("components").at("securitySchemes");
    BOOST_TEST(ss.at("bearer").at("type").as_string() == "http");
    BOOST_TEST(ss.at("bearer").at("scheme").as_string() == "bearer");
    BOOST_TEST(ss.at("bearer").at("bearerFormat").as_string() == "JWT");
    BOOST_TEST(ss.at("bearer").at("description").as_string() == "Access token");
    BOOST_TEST(ss.at("api_key").at("type").as_string() == "apiKey");
    BOOST_TEST(ss.at("api_key").at("in").as_string() == "header");
    BOOST_TEST(ss.at("api_key").at("name").as_string() == "X-Api-Key");

    auto const& post = paths.at("/todos").at("post");
    BOOST_TEST(post.at("security").as_array()[0].as_object().contains("api_key"));
    BOOST_TEST(!post.as_object().contains("parameters"));    // the X-Api-Key constraint is the credential
    auto const& del = paths.at("/todos/{id}").at("delete");
    BOOST_TEST(del.at("security").as_array()[0].as_object().contains("bearer"));
    BOOST_REQUIRE(del.at("parameters").as_array().size() == 1u);
    BOOST_TEST(del.at("parameters").as_array()[0].at("name").as_string() == "id");
    BOOST_TEST(paths.at("/health").at("get").at("security").as_array().empty());
    BOOST_TEST(!paths.at("/todos").at("get").as_object().contains("security"));   // inherits the document's

    // Inline mode keeps schemas in place; an unknown scheme name is a configuration error.
    gen.set_inline_schemas();
    auto inlined = gen.document();
    BOOST_TEST(inlined.at("paths").at("/todos").at("post").at("requestBody").at("content").at("application/json").at("schema").at("type").as_string() == "object");
    BOOST_TEST(!inlined.at("components").as_object().contains("schemas"));
    gen.set_security({"nope"});
    BOOST_CHECK_THROW(gen.document(), std::logic_error);
}

BOOST_AUTO_TEST_CASE(test_openapi_failed_registration_records_nothing)
{
    taar::session::http session;
    openapi::generator gen {{.title = "t", .version = "1"}};
    BOOST_CHECK_THROW(
        session.register_request_handler(
            method == http::verb::get && target == "/users/{id}",
            rest(&get_user, path_arg("idd"), query_arg("verbose"), query_arg("page")),
            gen.describe("Fetch a user")),
        taar::exception);
    BOOST_TEST(gen.operation_count() == 0u);
    // The corrected registration is then accepted under the same path.
    session.register_request_handler(
        method == http::verb::get && target == "/users/{id}",
        rest(&get_user, path_arg("id"), query_arg("verbose"), query_arg("page")),
        gen.describe("Fetch a user"));
    BOOST_TEST(gen.operation_count() == 1u);
}

} // namespace
