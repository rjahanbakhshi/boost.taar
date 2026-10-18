//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/core/http_date.hpp>
#include <boost/taar/handler/htdocs.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace net = boost::asio;
namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::htdocs;
using taar::handler::htdocs_options;

/// A fresh document root under the system temporary directory, removed afterwards.
struct docroot
{
    fs::path dir = fs::temp_directory_path() /
        ("taar-htdocs-" + std::to_string(std::random_device {}()) + "-" + std::to_string(std::random_device {}()));

    docroot() { fs::create_directories(dir); }
    ~docroot()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    fs::path write(std::string const& relative, std::string const& content) const
    {
        auto const p = dir / relative;
        fs::create_directories(p.parent_path());
        std::ofstream {p, std::ios::binary | std::ios::trunc} << content;
        return p;
    }

    htdocs_options options() const
    {
        htdocs_options o;
        o.root = dir;
        return o;
    }
};

struct reply
{
    http::response_header<> head;
    std::string body;

    http::status status() const { return head.result(); }
    std::string operator[](http::field f) const { return std::string {head[f]}; }
    std::string operator[](std::string_view f) const { return std::string {head[f]}; }
    bool has(http::field f) const { return head.find(f) != head.end(); }
};

std::string drain(taar::segmented_body::value_type const& body)
{
    http::response_header<> header;
    taar::segmented_body::writer writer {header, body};
    boost::system::error_code ec;
    writer.init(ec);
    std::string out;
    while (auto chunk = writer.get(ec))
    {
        out.append(static_cast<char const*>(chunk->first.data()), chunk->first.size());
    }
    BOOST_TEST(!ec);
    return out;
}

/// Call @a handler directly; @a args fills the route's path arguments.
reply call(
    htdocs const& handler,
    std::string target_text,
    std::vector<std::pair<std::string, std::string>> headers = {},
    http::verb verb = http::verb::get,
    std::unordered_map<std::string, std::string> args = {})
{
    htdocs::request_type req {verb, target_text, 11};
    for (auto const& [name, value] : headers) req.set(name, value);
    taar::matcher::context context;
    context.path_args = std::move(args);
    auto res = handler(req, context);
    reply out;
    out.body = drain(res.body());
    out.head = std::move(res.base());
    return out;
}

/// Shorthand for a GET through a `/{*path}` route.
reply get(htdocs const& handler, std::string const& path, std::vector<std::pair<std::string, std::string>> headers = {})
{
    auto const q = path.find('?');
    std::string capture = path.substr(1, q == std::string::npos ? std::string::npos : q - 1);
    return call(handler, path, std::move(headers), http::verb::get, {{"path", capture}});
}

// ---------------------------------------------------------------------------
// Defects of the original handler

BOOST_AUTO_TEST_CASE(test_htdocs_serves_the_greedy_capture_not_the_target)
{
    docroot root;
    root.write("app.css", "body{}");
    root.write("static/app.css", "wrong file");
    htdocs handler {root.dir.string()};
    // Mounted at /static/{*path}: the capture is "app.css".
    auto r = call(handler, "/static/app.css", {}, http::verb::get, {{"path", "app.css"}});
    BOOST_TEST(static_cast<int>(r.status()) == 200);
    BOOST_TEST(r.body == "body{}");
    BOOST_TEST(r[http::field::content_type] == "text/css; charset=utf-8");
}

BOOST_AUTO_TEST_CASE(test_htdocs_ignores_the_query_string)
{
    docroot root;
    root.write("app.css", "body{}");
    htdocs handler {root.options()};
    auto r = call(handler, "/app.css?v=3");     // no path args: the target path
    BOOST_TEST(static_cast<int>(r.status()) == 200);
    BOOST_TEST(r.body == "body{}");
}

BOOST_AUTO_TEST_CASE(test_htdocs_percent_decodes_and_allows_dots_inside_names)
{
    docroot root;
    root.write("a..b.txt", "dots");
    root.write("with space.txt", "space");
    htdocs handler {root.options()};
    BOOST_TEST(call(handler, "/a..b.txt").body == "dots");
    BOOST_TEST(call(handler, "/with%20space.txt").body == "space");
}

BOOST_AUTO_TEST_CASE(test_htdocs_rejects_traversal_and_encoded_separators)
{
    docroot root;
    root.write("pub/file.txt", "ok");
    root.write("secret.txt", "secret");
    htdocs_options o = root.options();
    o.root = root.dir / "pub";
    htdocs handler {o};
    for (auto const* t : {"/../secret.txt", "/%2e%2e/secret.txt", "/%2E%2E/secret.txt", "/sub/./file.txt",
                          "/sub%2f..%2f..%2fsecret.txt", "/a%2Fb", "/file.txt%00.png", "/..%5csecret.txt",
                          "/a\\b"})
    {
        BOOST_TEST_INFO(t);
        auto r = call(handler, t);
        BOOST_TEST(static_cast<int>(r.status()) == 400);
        BOOST_TEST(r.body.find("secret") == std::string::npos);
    }
    // Same through a greedy capture (already decoded by the matcher).
    auto r = call(handler, "/x", {}, http::verb::get, {{"path", "../secret.txt"}});
    BOOST_TEST(static_cast<int>(r.status()) == 400);
    BOOST_TEST(call(handler, "/file.txt").body == "ok");
}

BOOST_AUTO_TEST_CASE(test_htdocs_refuses_symlinks_out_of_the_root)
{
    docroot root;
    root.write("pub/inside.txt", "inside");
    root.write("outside.txt", "outside");
    std::error_code ec;
    fs::create_symlink(root.dir / "outside.txt", root.dir / "pub/out.txt", ec);
    if (ec)
    {
        BOOST_TEST_MESSAGE("symlinks unavailable, skipped: " << ec.message());
        return;
    }
    fs::create_symlink(root.dir / "pub/inside.txt", root.dir / "pub/in.txt");
    fs::create_directory_symlink(root.dir, root.dir / "pub/up");

    htdocs_options o = root.options();
    o.root = root.dir / "pub";
    htdocs handler {o};
    BOOST_TEST(static_cast<int>(call(handler, "/out.txt").status()) == 404);
    BOOST_TEST(static_cast<int>(call(handler, "/up/outside.txt").status()) == 404);
    BOOST_TEST(call(handler, "/in.txt").body == "inside");      // links inside the root are fine

    o.follow_symlinks_outside_root = true;
    htdocs lenient {o};
    BOOST_TEST(call(lenient, "/out.txt").body == "outside");
}

BOOST_AUTO_TEST_CASE(test_htdocs_error_bodies_are_plain_and_never_reflect_the_target)
{
    docroot root;
    htdocs handler {root.options()};
    auto r = call(handler, "/%3Cscript%3Ealert(1)%3C/script%3E");
    BOOST_TEST(static_cast<int>(r.status()) == 404);
    BOOST_TEST(r[http::field::content_type] == "text/plain; charset=utf-8");
    BOOST_TEST(r.body == "Not Found\n");
    BOOST_TEST(r[http::field::content_length] == "10");

    auto bad = call(handler, "/<script>");      // not a valid origin-form target
    BOOST_TEST(static_cast<int>(bad.status()) == 400);
    BOOST_TEST(bad.body == "Bad Request\n");
}

BOOST_AUTO_TEST_CASE(test_htdocs_redirects_directories_and_serves_index_files)
{
    docroot root;
    root.write("docs/index.htm", "htm index");
    root.write("docs/index.html", "html index");
    root.write("empty/.keep", "");
    root.write("file.txt", "file");
    htdocs_options o = root.options();
    o.index_files = {"index.html", "index.htm"};
    htdocs handler {o};

    auto r = call(handler, "/docs?x=1");
    BOOST_TEST(static_cast<int>(r.status()) == 301);
    BOOST_TEST(r[http::field::location] == "/docs/?x=1");
    // Mounted: the Location keeps the full request path.
    r = call(handler, "/static/docs", {}, http::verb::get, {{"path", "docs"}});
    BOOST_TEST(r[http::field::location] == "/static/docs/");

    BOOST_TEST(call(handler, "/docs/").body == "html index");
    o.index_files = {"index.htm", "index.html"};
    BOOST_TEST(call(htdocs {o}, "/docs/").body == "htm index");
    BOOST_TEST(static_cast<int>(call(handler, "/empty/").status()) == 404);      // no listing
    BOOST_TEST(static_cast<int>(call(handler, "/file.txt/").status()) == 404);
    BOOST_TEST(static_cast<int>(call(handler, "/missing").status()) == 404);
}

BOOST_AUTO_TEST_CASE(test_htdocs_answers_other_methods_with_405)
{
    docroot root;
    root.write("a.txt", "a");
    htdocs handler {root.options()};
    for (auto verb : {http::verb::post, http::verb::put, http::verb::delete_, http::verb::options})
    {
        auto r = call(handler, "/a.txt", {}, verb);
        BOOST_TEST(static_cast<int>(r.status()) == 405);
        BOOST_TEST(r[http::field::allow] == "GET, HEAD");
    }
}

BOOST_AUTO_TEST_CASE(test_htdocs_legacy_constructor_and_option_validation)
{
    docroot root;
    root.write("home.html", "home");
    htdocs legacy {root.dir.string(), "home.html"};
    BOOST_TEST(call(legacy, "/").body == "home");
    BOOST_TEST(legacy.options().index_files == std::vector<std::string> {"home.html"});
    htdocs no_index {root.dir.string(), ""};
    BOOST_TEST(static_cast<int>(call(no_index, "/").status()) == 404);

    htdocs_options o = root.options();
    o.index_files = {"../index.html"};
    BOOST_CHECK_THROW(htdocs {o}, taar::exception);
    o.index_files = {"a/index.html"};
    BOOST_CHECK_THROW(htdocs {o}, taar::exception);
    o.index_files = {};
    o.fallback_document = "../index.html";
    BOOST_CHECK_THROW(htdocs {o}, taar::exception);
}

// ---------------------------------------------------------------------------
// Resolution details

BOOST_AUTO_TEST_CASE(test_htdocs_hides_dot_files_unless_asked)
{
    docroot root;
    root.write(".env", "SECRET=1");
    root.write(".git/config", "[core]");
    root.write(".well-known/security.txt", "Contact: x");
    htdocs handler {root.options()};
    BOOST_TEST(static_cast<int>(call(handler, "/.env").status()) == 404);
    BOOST_TEST(static_cast<int>(call(handler, "/.git/config").status()) == 404);
    BOOST_TEST(call(handler, "/.well-known/security.txt").body == "Contact: x");

    htdocs_options o = root.options();
    o.serve_hidden = true;
    BOOST_TEST(call(htdocs {o}, "/.env").body == "SECRET=1");
}

BOOST_AUTO_TEST_CASE(test_htdocs_path_arg_selection)
{
    docroot root;
    root.write("f.txt", "file");
    htdocs_options o = root.options();
    o.path_arg = "rest";
    htdocs named {o};
    BOOST_TEST((named.path_arg_names() == std::vector<std::string> {"rest"}));
    BOOST_TEST(call(named, "/u/7/files/f.txt", {}, http::verb::get, {{"id", "7"}, {"rest", "f.txt"}}).body == "file");
    // The anonymous greedy capture.
    htdocs plain {root.options()};
    BOOST_TEST(plain.path_arg_names().empty());
    BOOST_TEST(call(plain, "/x/f.txt", {}, http::verb::get, {{"*", "f.txt"}}).body == "file");

    // Registration checks the name against the route template.
    taar::session::http session;
    BOOST_CHECK_THROW(
        session.register_request_handler(method == http::verb::get && target == "/{*path}", named),
        taar::exception);
    BOOST_CHECK_NO_THROW(
        session.register_request_handler(method == http::verb::get && target == "/u/{id}/files/{*rest}", named));
}

BOOST_AUTO_TEST_CASE(test_htdocs_spa_fallback)
{
    docroot root;
    root.write("index.html", "<app>");
    root.write("assets/app.js", "js");
    htdocs_options o = root.options();
    o.fallback_document = "/index.html";
    htdocs handler {o};

    auto r = get(handler, "/settings/profile");
    BOOST_TEST(static_cast<int>(r.status()) == 200);
    BOOST_TEST(r.body == "<app>");
    BOOST_TEST(r[http::field::cache_control] == "no-cache");
    BOOST_TEST(r[http::field::content_type] == "text/html; charset=utf-8");
    BOOST_TEST(static_cast<int>(get(handler, "/assets/missing.js").status()) == 404);
    BOOST_TEST(get(handler, "/assets/app.js").body == "js");
    BOOST_TEST(static_cast<int>(get(handler, "/.hidden").status()) == 404);

    o.fallback_when = [](std::string_view path) { return path.starts_with("app/"); };
    htdocs custom {o};
    BOOST_TEST(get(custom, "/app/x.json").body == "<app>");
    BOOST_TEST(static_cast<int>(get(custom, "/other").status()) == 404);
}

// ---------------------------------------------------------------------------
// Content types

BOOST_AUTO_TEST_CASE(test_htdocs_content_types_charset_and_overrides)
{
    docroot root;
    for (auto const* name : {"a.html", "a.JS", "a.json", "a.svg", "a.png", "a.woff2", "a.unknownext", "README",
                             "a.md", "a.data", "a.txt"})
    {
        root.write(name, "x");
    }
    htdocs_options o = root.options();
    o.mime_types = {{".data", "application/x-custom"}, {"MD", "text/x-markdown; charset=latin1"},
                    {"txt", "text/x-plain"}};
    o.default_mime_type = "application/x-unknown";
    htdocs handler {o};
    auto type = [&](std::string const& path) { return get(handler, path)[http::field::content_type]; };
    BOOST_TEST(type("/a.html") == "text/html; charset=utf-8");
    BOOST_TEST(type("/a.JS") == "text/javascript; charset=utf-8");
    BOOST_TEST(type("/a.json") == "application/json; charset=utf-8");
    BOOST_TEST(type("/a.svg") == "image/svg+xml; charset=utf-8");
    BOOST_TEST(type("/a.png") == "image/png");
    BOOST_TEST(type("/a.woff2") == "font/woff2");
    BOOST_TEST(type("/a.unknownext") == "application/x-unknown");
    BOOST_TEST(type("/README") == "application/x-unknown");
    BOOST_TEST(type("/a.data") == "application/x-custom");
    BOOST_TEST(type("/a.md") == "text/x-markdown; charset=latin1");
    BOOST_TEST(type("/a.txt") == "text/x-plain; charset=utf-8");

    BOOST_TEST(get(handler, "/a.png")["X-Content-Type-Options"] == "nosniff");
    o.nosniff = false;
    BOOST_TEST(!get(htdocs {o}, "/a.png").head.count("X-Content-Type-Options"));
    BOOST_TEST(get(htdocs {root.options()}, "/a.unknownext")[http::field::content_type] == "application/octet-stream");
}

// ---------------------------------------------------------------------------
// Validators and conditional requests

/// Set the modification time of @a p to @a when (whole seconds).
void set_mtime(fs::path const& p, std::chrono::sys_seconds when)
{
    fs::last_write_time(p, std::chrono::file_clock::from_sys(when));
}

std::chrono::sys_seconds const jan_2020 = std::chrono::sys_days {std::chrono::year {2020} / 1 / 1} + std::chrono::hours {12};

BOOST_AUTO_TEST_CASE(test_htdocs_etag_and_last_modified)
{
    docroot root;
    auto const p = root.write("a.txt", std::string(300, 'a'));
    set_mtime(p, jan_2020);
    htdocs handler {root.options()};
    auto r = get(handler, "/a.txt");
    BOOST_TEST(r[http::field::last_modified] == "Wed, 01 Jan 2020 12:00:00 GMT");
    auto const etag = r[http::field::etag];
    BOOST_TEST(etag.starts_with("\"12c-"));                 // hex size first
    BOOST_TEST(etag.ends_with("\""));
    BOOST_TEST(etag.find("W/") == std::string::npos);       // strong
    BOOST_TEST(get(handler, "/a.txt")[http::field::etag] == etag);   // stable

    // A change of content and time changes the validator.
    root.write("a.txt", std::string(301, 'a'));
    set_mtime(p, jan_2020 + std::chrono::seconds {5});
    BOOST_TEST(get(handler, "/a.txt")[http::field::etag] != etag);

    // A modification time in the future is not announced.
    set_mtime(p, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) + std::chrono::hours {48});
    auto const lm = taar::parse_http_date(get(handler, "/a.txt")[http::field::last_modified]);
    BOOST_TEST_REQUIRE(lm.has_value());
    BOOST_TEST((*lm <= std::chrono::system_clock::now()));
}

BOOST_AUTO_TEST_CASE(test_htdocs_if_none_match_and_if_modified_since)
{
    docroot root;
    set_mtime(root.write("a.txt", "abc"), jan_2020);
    htdocs_options o = root.options();
    o.cache_rules = {taar::handler::cache_rule::glob("*.txt", "max-age=60")};
    htdocs handler {o};
    auto const etag = get(handler, "/a.txt")[http::field::etag];

    auto r = get(handler, "/a.txt", {{"If-None-Match", etag}});
    BOOST_TEST(static_cast<int>(r.status()) == 304);
    BOOST_TEST(r.body.empty());
    BOOST_TEST(r[http::field::etag] == etag);
    BOOST_TEST(r[http::field::last_modified] == "Wed, 01 Jan 2020 12:00:00 GMT");
    BOOST_TEST(r[http::field::cache_control] == "max-age=60");
    BOOST_TEST(!r.has(http::field::content_length));
    BOOST_TEST(!r.has(http::field::content_type));

    // Weak comparison, lists and "*".
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-None-Match", "W/" + etag}}).status()) == 304);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-None-Match", "\"x\", " + etag}}).status()) == 304);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-None-Match", "*"}}).status()) == 304);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-None-Match", "\"other\""}}).status()) == 200);
    // HEAD revalidates too.
    BOOST_TEST(static_cast<int>(call(handler, "/a.txt", {{"If-None-Match", etag}}, http::verb::head).status()) == 304);

    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Modified-Since", "Wed, 01 Jan 2020 12:00:00 GMT"}}).status()) == 304);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Modified-Since", "Thu, 02 Jan 2020 00:00:00 GMT"}}).status()) == 304);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Modified-Since", "Wed, 01 Jan 2020 11:59:59 GMT"}}).status()) == 200);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Modified-Since", "not a date"}}).status()) == 200);
    // If-None-Match wins over If-Modified-Since.
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-None-Match", "\"other\""},
        {"If-Modified-Since", "Thu, 02 Jan 2020 00:00:00 GMT"}}).status()) == 200);
}

BOOST_AUTO_TEST_CASE(test_htdocs_if_match_and_if_unmodified_since)
{
    docroot root;
    set_mtime(root.write("a.txt", "abc"), jan_2020);
    htdocs handler {root.options()};
    auto const etag = get(handler, "/a.txt")[http::field::etag];

    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Match", etag}}).status()) == 200);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Match", "*"}}).status()) == 200);
    auto r = get(handler, "/a.txt", {{"If-Match", "\"other\""}});
    BOOST_TEST(static_cast<int>(r.status()) == 412);
    BOOST_TEST(r.body == "Precondition Failed\n");
    // If-Match uses strong comparison.
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Match", "W/" + etag}}).status()) == 412);

    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Unmodified-Since", "Wed, 01 Jan 2020 12:00:00 GMT"}}).status()) == 200);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Unmodified-Since", "Wed, 01 Jan 2020 11:00:00 GMT"}}).status()) == 412);
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Unmodified-Since", "garbage"}}).status()) == 200);
    // If-Unmodified-Since is ignored when If-Match is present.
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Match", etag},
        {"If-Unmodified-Since", "Wed, 01 Jan 2020 11:00:00 GMT"}}).status()) == 200);
    // A failed If-Match precedes a matching If-None-Match.
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Match", "\"other\""}, {"If-None-Match", etag}}).status()) == 412);
    // A passed If-Match then lets If-None-Match answer 304.
    BOOST_TEST(static_cast<int>(get(handler, "/a.txt", {{"If-Match", etag}, {"If-None-Match", etag}}).status()) == 304);
}

BOOST_AUTO_TEST_CASE(test_htdocs_cache_control_rules)
{
    docroot root;
    for (auto const* name : {"index.html", "assets/app.3f9a1c2e.js", "assets/chunk-5ZQ3X2AB.js", "assets/app.js",
                             "img/logo.png", "fonts/inter.woff2"})
    {
        root.write(name, "x");
    }
    htdocs_options o = root.options();
    o.cache_rules = taar::handler::cache_rules::immutable_hashed();
    o.cache_rules.push_back(taar::handler::cache_rule::glob("img/**", "public, max-age=3600"));
    o.default_cache_control = "max-age=60";
    htdocs handler {o};
    auto cc = [&](std::string const& path) { return get(handler, path)[http::field::cache_control]; };
    BOOST_TEST(cc("/assets/app.3f9a1c2e.js") == "public, max-age=31536000, immutable");
    BOOST_TEST(cc("/assets/chunk-5ZQ3X2AB.js") == "public, max-age=31536000, immutable");
    BOOST_TEST(cc("/index.html") == "no-cache");
    BOOST_TEST(cc("/") == "no-cache");                       // the index file's own path decides
    BOOST_TEST(cc("/img/logo.png") == "public, max-age=3600");
    BOOST_TEST(cc("/assets/app.js") == "max-age=60");
    BOOST_TEST(!get(htdocs {root.options()}, "/assets/app.js").has(http::field::cache_control));
}

BOOST_AUTO_TEST_CASE(test_htdocs_glob_and_hash_helpers)
{
    using taar::handler::cache_rules::glob_match;
    using taar::handler::cache_rules::looks_content_hashed;
    BOOST_TEST(glob_match("*.css", "a/b/site.css"));
    BOOST_TEST(!glob_match("*.css", "a/b/site.js"));
    BOOST_TEST(glob_match("assets/*.js", "assets/app.js"));
    BOOST_TEST(!glob_match("assets/*.js", "assets/x/app.js"));
    BOOST_TEST(glob_match("assets/**", "assets/x/y/app.js"));
    BOOST_TEST(glob_match("assets/**/*.js", "assets/x/y/app.js"));
    BOOST_TEST(glob_match("assets/**/*.js", "assets/app.js"));
    BOOST_TEST(!glob_match("assets/**/*.js", "other/app.js"));
    BOOST_TEST(glob_match("img/?.png", "img/a.png"));
    BOOST_TEST(!glob_match("img/?.png", "img/ab.png"));
    BOOST_TEST(glob_match("**", ""));
    BOOST_TEST(!glob_match("a", ""));
    // No exponential blow-up on hostile input.
    BOOST_TEST(!glob_match("*a*a*a*a*a*a*a*a*b", std::string(5000, 'a')));

    BOOST_TEST(looks_content_hashed("app.3f9a1c2e.js"));
    BOOST_TEST(looks_content_hashed("assets/chunk-5ZQ3X2AB.js"));
    BOOST_TEST(looks_content_hashed("3f9a1c2e4b.css"));
    BOOST_TEST(looks_content_hashed("main.a1b2c3d4e5f6a7b8.bundle.js"));
    BOOST_TEST(!looks_content_hashed("app.js"));
    BOOST_TEST(!looks_content_hashed("jquery-3.7.1.min.js"));
    BOOST_TEST(!looks_content_hashed("bootstrap.min.css"));
    BOOST_TEST(!looks_content_hashed("report-20240101.pdf"));
    BOOST_TEST(!looks_content_hashed("a1b2c3d4/app.js"));     // only the file name counts
}

BOOST_AUTO_TEST_CASE(test_htdocs_end_to_end_revalidation)
{
    docroot root;
    root.write("page.html", "<p>cached</p>");
    net::io_context io;
    taar::session::http session;
    session.register_request_handler(method == http::verb::get && target == "/{*path}", htdocs {root.options()});
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto first = co_await e2e::send_all(port, {e2e::make_request(http::verb::get, "/page.html")});
        auto const etag = std::string {first[0][http::field::etag]};
        auto again = e2e::make_request(http::verb::get, "/page.html");
        again.set(http::field::if_none_match, etag);
        auto res = co_await e2e::send_all(port, {std::move(again), e2e::make_request(http::verb::get, "/page.html")});
        BOOST_TEST(static_cast<int>(res[0].result()) == 304);
        BOOST_TEST(res[0].body().empty());
        BOOST_TEST(res[0][http::field::etag] == etag);
        BOOST_TEST(res[1].body() == "<p>cached</p>");          // the connection survived the 304
    });
}

// ---------------------------------------------------------------------------
// Responses

BOOST_AUTO_TEST_CASE(test_htdocs_head_has_the_length_but_no_body)
{
    docroot root;
    root.write("big.bin", std::string(5000, 'x'));
    root.write("empty.txt", "");
    htdocs handler {root.options()};
    auto r = call(handler, "/big.bin", {}, http::verb::head);
    BOOST_TEST(static_cast<int>(r.status()) == 200);
    BOOST_TEST(r[http::field::content_length] == "5000");
    BOOST_TEST(r.body.empty());
    auto e = get(handler, "/empty.txt");
    BOOST_TEST(static_cast<int>(e.status()) == 200);
    BOOST_TEST(e[http::field::content_length] == "0");
}

BOOST_AUTO_TEST_CASE(test_htdocs_on_response_hook)
{
    docroot root;
    root.write("files/report.pdf", "%PDF");
    htdocs_options o = root.options();
    std::vector<std::string> seen;
    o.on_response = [&](std::string_view path, http::response_header<>& h)
    {
        seen.emplace_back(path);
        if (path.starts_with("files/")) h.set(http::field::content_disposition, "attachment");
    };
    htdocs handler {o};
    auto r = get(handler, "/files/report.pdf");
    BOOST_TEST(r[http::field::content_disposition] == "attachment");
    BOOST_TEST(static_cast<int>(get(handler, "/missing.pdf").status()) == 404);   // not a served file
    BOOST_TEST((seen == std::vector<std::string> {"files/report.pdf"}));
}

BOOST_AUTO_TEST_CASE(test_htdocs_end_to_end_mount_query_and_head)
{
    docroot root;
    root.write("app.css", "body{}");
    root.write("docs/index.html", "<h1>docs</h1>");
    net::io_context io;
    taar::session::http session;
    session.set_response_filter([](http::request_header<> const&, http::response_header<>& h)
    {
        h.set(http::field::server, "taar-test");
    });
    session.register_request_handler(method == http::verb::get && target == "/static/{*path}",
        htdocs {root.options()});
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port, {
            e2e::make_request(http::verb::get, "/static/app.css?v=3"),
            e2e::make_request(http::verb::get, "/static/docs"),
            e2e::make_request(http::verb::get, "/static/docs/"),
            e2e::make_request(http::verb::get, "/static/nope"),
            e2e::make_request(http::verb::put, "/static/app.css"),
            e2e::make_request(http::verb::get, "/static/%2e%2e/x")});
        BOOST_TEST(res[0].body() == "body{}");
        BOOST_TEST(res[0][http::field::server] == "taar-test");        // the filter applies to files
        BOOST_TEST(static_cast<int>(res[1].result()) == 301);
        BOOST_TEST(res[1][http::field::location] == "/static/docs/");
        BOOST_TEST(res[2].body() == "<h1>docs</h1>");
        BOOST_TEST(static_cast<int>(res[3].result()) == 404);
        BOOST_TEST(res[3][http::field::server] == "taar-test");        // and to errors
        BOOST_TEST(static_cast<int>(res[4].result()) == 405);          // session-level: GET route only
        BOOST_TEST(static_cast<int>(res[5].result()) == 400);
    });
}

} // namespace
