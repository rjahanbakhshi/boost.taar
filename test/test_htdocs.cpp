//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include "joining_thread.hpp"
#include <boost/taar/core/http_date.hpp>
#include <boost/taar/handler/htdocs.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
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
#if defined(_MSC_VER)
    // MSVC has no file_clock::from_sys; it has clock_cast (which libc++ lacks).
    fs::last_write_time(p, std::chrono::clock_cast<std::chrono::file_clock>(when));
#else
    fs::last_write_time(p, std::chrono::file_clock::from_sys(when));
#endif
}

std::chrono::sys_seconds const jan_2020 = std::chrono::sys_days {std::chrono::year {2020} / 1 / 1} + std::chrono::hours {12};

BOOST_AUTO_TEST_CASE(test_htdocs_etag_and_last_modified)
{
    docroot root;
    auto const p = root.write("a.txt", std::string(300, 'a'));
    set_mtime(p, jan_2020);
    htdocs_options o = root.options();
    o.revalidate_after = std::chrono::milliseconds {0};    // see every change at once
    htdocs handler {o};
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
        auto first = co_await e2e::send_all(port, e2e::make_request(http::verb::get, "/page.html"));
        auto const etag = std::string {first[0][http::field::etag]};
        auto again = e2e::make_request(http::verb::get, "/page.html");
        again.set(http::field::if_none_match, etag);
        auto res = co_await e2e::send_all(port, std::move(again), e2e::make_request(http::verb::get, "/page.html"));
        BOOST_TEST(static_cast<int>(res[0].result()) == 304);
        BOOST_TEST(res[0].body().empty());
        BOOST_TEST(res[0][http::field::etag] == etag);
        BOOST_TEST(res[1].body() == "<p>cached</p>");          // the connection survived the 304
    });
}

// ---------------------------------------------------------------------------
// Ranges

std::string pattern_content(std::size_t n)
{
    std::string out(n, '\0');
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<char>('A' + (i * 7 + i / 26) % 26);
    return out;
}

struct byterange_part
{
    std::string content_type;
    std::string content_range;
    std::string data;
};

/// Parse a multipart/byteranges body (RFC 9110 14.6, RFC 2046 5.1.1).
std::vector<byterange_part> parse_byteranges(std::string const& content_type, std::string const& body)
{
    std::vector<byterange_part> parts;
    auto const b = content_type.find("boundary=");
    BOOST_TEST_REQUIRE(b != std::string::npos);
    std::string const delimiter = "--" + content_type.substr(b + 9);
    std::size_t pos = body.find(delimiter);
    BOOST_TEST_REQUIRE(pos == 0u);
    while (true)
    {
        pos += delimiter.size();
        if (body.compare(pos, 2, "--") == 0)
        {
            BOOST_TEST(body.substr(pos) == "--\r\n");
            break;
        }
        BOOST_TEST_REQUIRE(body.compare(pos, 2, "\r\n") == 0);
        pos += 2;
        byterange_part part;
        while (body.compare(pos, 2, "\r\n") != 0)
        {
            auto const eol = body.find("\r\n", pos);
            auto const line = body.substr(pos, eol - pos);
            auto const colon = line.find(": ");
            if (line.substr(0, colon) == "Content-Type") part.content_type = line.substr(colon + 2);
            if (line.substr(0, colon) == "Content-Range") part.content_range = line.substr(colon + 2);
            pos = eol + 2;
        }
        pos += 2;
        auto const next = body.find("\r\n" + delimiter, pos);
        BOOST_TEST_REQUIRE(next != std::string::npos);
        part.data = body.substr(pos, next - pos);
        parts.push_back(std::move(part));
        pos = next + 2;
    }
    return parts;
}

BOOST_AUTO_TEST_CASE(test_htdocs_single_ranges)
{
    docroot root;
    auto const content = pattern_content(10000);
    root.write("data.bin", content);
    htdocs handler {root.options()};

    auto full = get(handler, "/data.bin");
    BOOST_TEST(static_cast<int>(full.status()) == 200);
    BOOST_TEST(full[http::field::accept_ranges] == "bytes");

    auto r = get(handler, "/data.bin", {{"Range", "bytes=0-499"}});
    BOOST_TEST(static_cast<int>(r.status()) == 206);
    BOOST_TEST(r[http::field::content_range] == "bytes 0-499/10000");
    BOOST_TEST(r[http::field::content_length] == "500");
    BOOST_TEST(r[http::field::content_type] == "application/octet-stream");
    BOOST_TEST(r.body == content.substr(0, 500));

    r = get(handler, "/data.bin", {{"Range", "bytes=9500-"}});
    BOOST_TEST(r[http::field::content_range] == "bytes 9500-9999/10000");
    BOOST_TEST(r.body == content.substr(9500));

    r = get(handler, "/data.bin", {{"Range", "bytes=-100"}});
    BOOST_TEST(r[http::field::content_range] == "bytes 9900-9999/10000");
    BOOST_TEST(r.body == content.substr(9900));

    // Overlapping ranges coalesce into one.
    r = get(handler, "/data.bin", {{"Range", "bytes=0-99,50-199"}});
    BOOST_TEST(static_cast<int>(r.status()) == 206);
    BOOST_TEST(r[http::field::content_range] == "bytes 0-199/10000");
    BOOST_TEST(r.body == content.substr(0, 200));
}

BOOST_AUTO_TEST_CASE(test_htdocs_multipart_byteranges_round_trip)
{
    docroot root;
    auto const content = pattern_content(10000);
    root.write("data.txt", content);
    htdocs handler {root.options()};
    auto r = get(handler, "/data.txt", {{"Range", "bytes=9000-9009, 0-9, -5"}});
    BOOST_TEST(static_cast<int>(r.status()) == 206);
    auto const type = r[http::field::content_type];
    BOOST_TEST(type.starts_with("multipart/byteranges; boundary="));
    BOOST_TEST(r[http::field::content_length] == std::to_string(r.body.size()));
    BOOST_TEST(!r.has(http::field::content_range));
    auto const parts = parse_byteranges(type, r.body);
    BOOST_TEST_REQUIRE(parts.size() == 3u);
    BOOST_TEST(parts[0].content_range == "bytes 0-9/10000");          // sorted while coalescing
    BOOST_TEST(parts[0].data == content.substr(0, 10));
    BOOST_TEST(parts[1].content_range == "bytes 9000-9009/10000");
    BOOST_TEST(parts[1].data == content.substr(9000, 10));
    BOOST_TEST(parts[2].content_range == "bytes 9995-9999/10000");
    BOOST_TEST(parts[2].data == content.substr(9995));
    BOOST_TEST(parts[0].content_type == "text/plain; charset=utf-8");

    // Every response picks a fresh boundary.
    auto again = get(handler, "/data.txt", {{"Range", "bytes=0-0,5-5"}});
    BOOST_TEST(again[http::field::content_type] != type);
}

BOOST_AUTO_TEST_CASE(test_htdocs_unsatisfiable_malformed_and_limited_ranges)
{
    docroot root;
    auto const content = pattern_content(1000);
    root.write("d.bin", content);
    htdocs_options o = root.options();
    o.max_ranges = 3;
    htdocs handler {o};

    auto r = get(handler, "/d.bin", {{"Range", "bytes=1000-2000"}});
    BOOST_TEST(static_cast<int>(r.status()) == 416);
    BOOST_TEST(r[http::field::content_range] == "bytes */1000");

    for (auto const* ignored : {"bytes=5-1", "bytes=abc", "pages=1-2", "bytes=0-0,2-2,4-4,6-6"})
    {
        BOOST_TEST_INFO(ignored);
        auto full = get(handler, "/d.bin", {{"Range", ignored}});
        BOOST_TEST(static_cast<int>(full.status()) == 200);
        BOOST_TEST(full.body == content);
    }
    // HEAD ignores Range.
    auto head = call(handler, "/d.bin", {{"Range", "bytes=0-9"}}, http::verb::head);
    BOOST_TEST(static_cast<int>(head.status()) == 200);
    BOOST_TEST(head[http::field::content_length] == "1000");

    // Disabled.
    o.max_ranges = 0;
    htdocs off {o};
    auto plain = get(off, "/d.bin", {{"Range", "bytes=0-9"}});
    BOOST_TEST(static_cast<int>(plain.status()) == 200);
    BOOST_TEST(!plain.has(http::field::accept_ranges));
}

BOOST_AUTO_TEST_CASE(test_htdocs_if_range)
{
    docroot root;
    auto const content = pattern_content(1000);
    set_mtime(root.write("d.bin", content), jan_2020);
    htdocs handler {root.options()};
    auto const etag = get(handler, "/d.bin")[http::field::etag];
    auto status = [&](std::string const& if_range)
    {
        return static_cast<int>(get(handler, "/d.bin", {{"Range", "bytes=0-9"}, {"If-Range", if_range}}).status());
    };
    BOOST_TEST(status(etag) == 206);
    BOOST_TEST(status("\"stale\"") == 200);
    BOOST_TEST(status("W/" + etag) == 200);                          // weak tags never match
    BOOST_TEST(status("Wed, 01 Jan 2020 12:00:00 GMT") == 206);       // exact date
    BOOST_TEST(status("Thu, 02 Jan 2020 12:00:00 GMT") == 200);       // later is not exact
    BOOST_TEST(status("garbage") == 200);
    // A precondition failure still wins over the range.
    BOOST_TEST(static_cast<int>(get(handler, "/d.bin", {{"Range", "bytes=0-9"}, {"If-None-Match", etag}}).status()) == 304);
}

BOOST_AUTO_TEST_CASE(test_htdocs_end_to_end_range_download_of_a_large_file)
{
    docroot root;
    auto const content = pattern_content(1024 * 1024);
    root.write("big.bin", content);
    net::io_context io;
    taar::session::http session;
    session.register_request_handler(method == http::verb::get && target == "/files/{*path}", htdocs {root.options()});
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        // Download in four pieces and reassemble, then once whole.
        std::vector<e2e::request> reqs;
        std::uint64_t const quarter = content.size() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto req = e2e::make_request(http::verb::get, "/files/big.bin");
            req.set(http::field::range, "bytes=" + std::to_string(i * quarter) + "-" +
                (i == 3 ? std::string {} : std::to_string((i + 1) * quarter - 1)));
            reqs.push_back(std::move(req));
        }
        reqs.push_back(e2e::make_request(http::verb::get, "/files/big.bin"));
        auto res = co_await e2e::send_all(port, std::move(reqs));
        std::string assembled;
        for (int i = 0; i < 4; ++i)
        {
            BOOST_TEST(static_cast<int>(res[i].result()) == 206);
            assembled += res[i].body();
        }
        BOOST_TEST((assembled == content));
        BOOST_TEST(res[3][http::field::content_range] == "bytes 786432-1048575/1048576");
        BOOST_TEST(res[4].body().size() == content.size());
        BOOST_TEST((res[4].body() == content));
    });
}

// ---------------------------------------------------------------------------
// Precompressed variants

struct compressed_tree
{
    docroot root;
    compressed_tree()
    {
        set_mtime(root.write("app.js", "identity content"), jan_2020);
        set_mtime(root.write("app.js.br", "BR"), jan_2020 + std::chrono::seconds {1});
        set_mtime(root.write("app.js.gz", "GZIP"), jan_2020 + std::chrono::seconds {1});
        set_mtime(root.write("old.css", "identity css"), jan_2020);
        set_mtime(root.write("old.css.gz", "STALE"), jan_2020 - std::chrono::seconds {1});
        set_mtime(root.write("plain.txt", "plain"), jan_2020);
    }
};

BOOST_AUTO_TEST_CASE(test_htdocs_precompressed_selection)
{
    compressed_tree tree;
    htdocs handler {tree.root.options()};
    auto pick = [&](std::string const& accept)
    {
        auto r = get(handler, "/app.js", {{"Accept-Encoding", accept}});
        return r.body;
    };
    BOOST_TEST(pick("gzip, deflate, br, zstd") == "BR");                 // ties: configuration order
    BOOST_TEST(pick("gzip") == "GZIP");
    BOOST_TEST(pick("x-gzip") == "GZIP");
    BOOST_TEST(pick("br;q=0.5, gzip;q=0.8") == "GZIP");
    BOOST_TEST(pick("br;q=0, gzip;q=0") == "identity content");
    BOOST_TEST(pick("deflate") == "identity content");
    BOOST_TEST(pick("*") == "BR");
    BOOST_TEST(pick("*;q=0.5, br;q=0") == "GZIP");
    BOOST_TEST(pick("identity;q=0, gzip;q=0.1") == "GZIP");              // identity refused
    BOOST_TEST(pick("identity, br;q=0.5") == "identity content");         // identity preferred explicitly
    BOOST_TEST(pick("") == "identity content");
    BOOST_TEST(get(handler, "/app.js").body == "identity content");     // no Accept-Encoding

    auto r = get(handler, "/app.js", {{"Accept-Encoding", "br"}});
    BOOST_TEST(r[http::field::content_encoding] == "br");
    BOOST_TEST(r[http::field::content_type] == "text/javascript; charset=utf-8");
    BOOST_TEST(r[http::field::content_length] == "2");
    BOOST_TEST(r[http::field::vary] == "Accept-Encoding");
    auto plain = get(handler, "/app.js");
    BOOST_TEST(!plain.has(http::field::content_encoding));
    BOOST_TEST(plain[http::field::vary] == "Accept-Encoding");

    // HEAD reports the chosen variant's length.
    auto head = call(handler, "/app.js", {{"Accept-Encoding", "gzip"}}, http::verb::head);
    BOOST_TEST(head[http::field::content_encoding] == "gzip");
    BOOST_TEST(head[http::field::content_length] == "4");
}

BOOST_AUTO_TEST_CASE(test_htdocs_precompressed_validators_staleness_and_ranges)
{
    compressed_tree tree;
    htdocs handler {tree.root.options()};
    auto const id_tag = get(handler, "/app.js")[http::field::etag];
    auto const br_tag = get(handler, "/app.js", {{"Accept-Encoding", "br"}})[http::field::etag];
    auto const gz_tag = get(handler, "/app.js", {{"Accept-Encoding", "gzip"}})[http::field::etag];
    BOOST_TEST(br_tag != id_tag);
    BOOST_TEST(gz_tag != br_tag);
    BOOST_TEST(br_tag.ends_with("-br\""));

    // Revalidating the br variant: 304 keeps Vary.
    auto nm = get(handler, "/app.js", {{"Accept-Encoding", "br"}, {"If-None-Match", br_tag}});
    BOOST_TEST(static_cast<int>(nm.status()) == 304);
    BOOST_TEST(nm[http::field::vary] == "Accept-Encoding");
    BOOST_TEST(nm[http::field::etag] == br_tag);
    // The identity tag does not validate the br variant.
    BOOST_TEST(static_cast<int>(get(handler, "/app.js", {{"Accept-Encoding", "br"}, {"If-None-Match", id_tag}}).status()) == 200);

    // A sibling older than the original is ignored, and no Vary is sent.
    auto old = get(handler, "/old.css", {{"Accept-Encoding", "gzip"}});
    BOOST_TEST(old.body == "identity css");
    BOOST_TEST(!old.has(http::field::vary));
    BOOST_TEST(!get(handler, "/plain.txt", {{"Accept-Encoding", "gzip"}}).has(http::field::vary));

    // A range applies to identity.
    auto range = get(handler, "/app.js", {{"Accept-Encoding", "br"}, {"Range", "bytes=0-7"}});
    BOOST_TEST(static_cast<int>(range.status()) == 206);
    BOOST_TEST(range.body == "identity");
    BOOST_TEST(!range.has(http::field::content_encoding));
    BOOST_TEST(range[http::field::vary] == "Accept-Encoding");

    // Disabled, or another coding list.
    htdocs_options o = tree.root.options();
    o.precompressed = {};
    BOOST_TEST(get(htdocs {o}, "/app.js", {{"Accept-Encoding", "br"}}).body == "identity content");
    o.precompressed = {"gzip"};
    BOOST_TEST(get(htdocs {o}, "/app.js", {{"Accept-Encoding", "br, gzip"}}).body == "GZIP");
    o.precompressed = {"identity"};
    BOOST_CHECK_THROW(htdocs {o}, taar::exception);
}

BOOST_AUTO_TEST_CASE(test_htdocs_end_to_end_compressed_variant)
{
    compressed_tree tree;
    net::io_context io;
    taar::session::http session;
    session.register_request_handler(method == http::verb::get && target == "/{*path}", htdocs {tree.root.options()});
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto req = e2e::make_request(http::verb::get, "/app.js");
        req.set(http::field::accept_encoding, "gzip, br");
        auto res = co_await e2e::send_all(port, std::move(req), e2e::make_request(http::verb::get, "/app.js"));
        BOOST_TEST(res[0][http::field::content_encoding] == "br");
        BOOST_TEST(res[0].body() == "BR");
        BOOST_TEST(res[0][http::field::vary] == "Accept-Encoding");
        BOOST_TEST(res[1].body() == "identity content");
    });
}

// ---------------------------------------------------------------------------
// Caches

BOOST_AUTO_TEST_CASE(test_htdocs_metadata_cache_revalidation)
{
    docroot root;
    auto const p = root.write("a.txt", "version-1");
    set_mtime(p, jan_2020);
    htdocs_options o = root.options();
    o.revalidate_after = std::chrono::hours {1};
    o.memory_cache_max_file_size = 0;
    htdocs cached {o};
    auto const before = get(cached, "/a.txt")[http::field::etag];
    BOOST_TEST(cached.cache_stats().metadata_entries == 1u);

    // Same size, newer time: within the window the cached validators stand
    // (the bytes are read from disk, so they are the new ones).
    root.write("a.txt", "version-2");
    set_mtime(p, jan_2020 + std::chrono::seconds {10});
    auto r = get(cached, "/a.txt");
    BOOST_TEST(r[http::field::etag] == before);
    BOOST_TEST(r.body == "version-2");

    // A size change is noticed when the file is opened: looked up afresh.
    root.write("a.txt", "version-three");
    r = get(cached, "/a.txt");
    BOOST_TEST(r.body == "version-three");
    BOOST_TEST(r[http::field::etag] != before);
    BOOST_TEST(r[http::field::content_length] == "13");

    // A short window: the next use after it re-stats. Wide enough that the
    // write below fits in it even on a slow (virus-scanned) CI file system.
    o.revalidate_after = std::chrono::milliseconds {500};
    htdocs quick {o};
    auto const first = get(quick, "/a.txt")[http::field::etag];
    root.write("a.txt", "version-four!");
    set_mtime(p, jan_2020 + std::chrono::seconds {20});
    BOOST_TEST(get(quick, "/a.txt")[http::field::etag] == first);
    std::this_thread::sleep_for(std::chrono::milliseconds {600});
    BOOST_TEST(get(quick, "/a.txt")[http::field::etag] != first);

    // New and deleted files are noticed after the window too.
    BOOST_TEST(static_cast<int>(get(quick, "/new.txt").status()) == 404);
    root.write("new.txt", "new");
    std::this_thread::sleep_for(std::chrono::milliseconds {600});
    BOOST_TEST(get(quick, "/new.txt").body == "new");
    fs::remove(root.dir / "new.txt");
    BOOST_TEST(static_cast<int>(get(quick, "/new.txt").status()) == 404);    // vanished at open: looked up again

    // Bounded.
    o.metadata_cache_entries = 2;
    htdocs bounded {o};
    for (auto const* name : {"/a.txt", "/b", "/c", "/d"}) get(bounded, name);
    BOOST_TEST(bounded.cache_stats().metadata_entries == 2u);
    o.metadata_cache_entries = 0;
    htdocs uncached {o};
    get(uncached, "/a.txt");
    BOOST_TEST(uncached.cache_stats().metadata_entries == 0u);
}

BOOST_AUTO_TEST_CASE(test_htdocs_memory_cache_hits_and_eviction)
{
    docroot root;
    auto const pa = root.write("a.txt", std::string(60, 'a'));
    root.write("b.txt", std::string(60, 'b'));
    root.write("large.bin", std::string(500, 'L'));
    htdocs_options o = root.options();
    o.memory_cache_max_file_size = 100;
    o.memory_cache_max_total = 100;
    o.revalidate_after = std::chrono::hours {1};
    htdocs handler {o};
    htdocs copy = handler;      // copies share the caches

    BOOST_TEST(get(handler, "/a.txt").body == std::string(60, 'a'));
    auto stats = handler.cache_stats();
    BOOST_TEST(stats.memory_loads == 1u);
    BOOST_TEST(stats.memory_entries == 1u);
    BOOST_TEST(stats.memory_bytes == 60u);

    // A hit is served from memory: rewrite the file with the same size and
    // time and the old bytes are still sent.
    auto const mtime = fs::last_write_time(pa);
    root.write("a.txt", std::string(60, 'z'));
    fs::last_write_time(pa, mtime);
    BOOST_TEST(get(copy, "/a.txt").body == std::string(60, 'a'));
    BOOST_TEST(get(copy, "/a.txt", {{"Range", "bytes=0-1,4-5"}}).body.find("aa") != std::string::npos);
    BOOST_TEST(handler.cache_stats().memory_hits == 2u);

    // HEAD and 304 do not read the file.
    auto const etag = get(handler, "/b.txt")[http::field::etag];   // loads b, evicts a
    stats = handler.cache_stats();
    BOOST_TEST(stats.memory_loads == 2u);
    BOOST_TEST(stats.memory_entries == 1u);
    BOOST_TEST(stats.memory_bytes == 60u);
    call(handler, "/a.txt", {}, http::verb::head);
    BOOST_TEST(static_cast<int>(get(handler, "/b.txt", {{"If-None-Match", etag}}).status()) == 304);
    BOOST_TEST(handler.cache_stats().memory_loads == 2u);
    BOOST_TEST(handler.cache_stats().memory_hits == 2u);

    // a was evicted: it is read again (now with the new bytes).
    BOOST_TEST(get(handler, "/a.txt").body == std::string(60, 'z'));
    BOOST_TEST(handler.cache_stats().memory_loads == 3u);

    // Too large for the cache: streamed from disk, not kept.
    BOOST_TEST(get(handler, "/large.bin").body.size() == 500u);
    BOOST_TEST(handler.cache_stats().memory_loads == 3u);
}

BOOST_AUTO_TEST_CASE(test_htdocs_shared_handler_on_many_threads)
{
    docroot root;
    std::vector<std::string> contents;
    for (int i = 0; i < 8; ++i)
    {
        contents.push_back(pattern_content(1000 + i * 30000));
        root.write("f" + std::to_string(i) + ".bin", contents.back());
    }
    htdocs_options o = root.options();
    o.metadata_cache_entries = 4;
    o.memory_cache_max_total = 64 * 1024;
    o.revalidate_after = std::chrono::milliseconds {1};
    htdocs handler {o};
    std::atomic<int> failures {0};
    {
        std::vector<test_util::joining_thread> threads;
        for (int t = 0; t < 6; ++t)
        {
            threads.emplace_back([&, t]
            {
                for (int n = 0; n < 200; ++n)
                {
                    int const i = (t + n) % 8;
                    auto r = get(handler, "/f" + std::to_string(i) + ".bin");
                    if (r.body != contents[i]) ++failures;
                }
            });
        }
    }
    BOOST_TEST(failures.load() == 0);
    BOOST_TEST(handler.cache_stats().metadata_entries <= 4u);
    BOOST_TEST(handler.cache_stats().memory_bytes <= 64u * 1024u);
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
        auto res = co_await e2e::send_all(port,
            e2e::make_request(http::verb::get, "/static/app.css?v=3"),
            e2e::make_request(http::verb::get, "/static/docs"),
            e2e::make_request(http::verb::get, "/static/docs/"),
            e2e::make_request(http::verb::get, "/static/nope"),
            e2e::make_request(http::verb::put, "/static/app.css"),
            e2e::make_request(http::verb::get, "/static/%2e%2e/x"));
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
