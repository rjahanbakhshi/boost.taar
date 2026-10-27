//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include <boost/taar/core/mime.hpp>
#include <boost/test/unit_test.hpp>
#include <string_view>

namespace {

namespace taar = boost::taar;

BOOST_AUTO_TEST_CASE(test_mime_extension_is_the_last_one_of_the_last_segment)
{
    BOOST_TEST(taar::file_extension("index.HTML") == "html");
    BOOST_TEST(taar::file_extension("archive.tar.gz") == "gz");
    BOOST_TEST(taar::file_extension("dir.d/README") == "");
    BOOST_TEST(taar::file_extension(".profile") == "");
    BOOST_TEST(taar::file_extension("a/b/.profile") == "");
    BOOST_TEST(taar::file_extension("noext") == "");
    BOOST_TEST(taar::file_extension("trailing.") == "");
}

BOOST_AUTO_TEST_CASE(test_mime_builtin_table_covers_the_web_formats)
{
    BOOST_TEST(taar::mime_type_for("index.html") == "text/html");
    BOOST_TEST(taar::mime_type_for("INDEX.HTM") == "text/html");
    BOOST_TEST(taar::mime_type_for("app.js") == "text/javascript");
    BOOST_TEST(taar::mime_type_for("mod.mjs") == "text/javascript");
    BOOST_TEST(taar::mime_type_for("app.css") == "text/css");
    BOOST_TEST(taar::mime_type_for("data.json") == "application/json");
    BOOST_TEST(taar::mime_type_for("app.js.map") == "application/json");
    BOOST_TEST(taar::mime_type_for("logo.svg") == "image/svg+xml");
    BOOST_TEST(taar::mime_type_for("photo.JPG") == "image/jpeg");
    BOOST_TEST(taar::mime_type_for("photo.avif") == "image/avif");
    BOOST_TEST(taar::mime_type_for("font.woff2") == "font/woff2");
    BOOST_TEST(taar::mime_type_for("module.wasm") == "application/wasm");
    BOOST_TEST(taar::mime_type_for("doc.pdf") == "application/pdf");
    BOOST_TEST(taar::mime_type_for("a.tar.gz") == "application/gzip");
    BOOST_TEST(taar::mime_type_for("clip.mp4") == "video/mp4");
    BOOST_TEST(taar::mime_type_for("clip.webm") == "video/webm");
    BOOST_TEST(taar::mime_type_for("song.mp3") == "audio/mpeg");
    BOOST_TEST(taar::mime_type_for("site.webmanifest") == "application/manifest+json");
    BOOST_TEST(taar::mime_type_for("sheet.xlsx") == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    BOOST_TEST(taar::mime_type_for("conf.yml") == "application/yaml");
    BOOST_TEST(taar::mime_type_for("conf.toml") == "application/toml");
    BOOST_TEST(taar::mime_type_for("lib.jar") == "application/java-archive");
    BOOST_TEST(taar::mime_type_for("cal.ics") == "text/calendar");
    BOOST_TEST(taar::mime_type_for("unknown.qqq") == "");
    BOOST_TEST(taar::mime_type_for("Makefile") == "");
    BOOST_TEST(taar::mime_type_for("page.php") == "");   // never served as HTML
}

BOOST_AUTO_TEST_CASE(test_mime_charset_is_added_to_text_types_only)
{
    BOOST_TEST(taar::is_text_mime_type("text/plain"));
    BOOST_TEST(taar::is_text_mime_type("TEXT/HTML"));
    BOOST_TEST(taar::is_text_mime_type("application/json"));
    BOOST_TEST(taar::is_text_mime_type("image/svg+xml"));
    BOOST_TEST(taar::is_text_mime_type("application/manifest+json; foo=bar"));
    BOOST_TEST(!taar::is_text_mime_type("image/png"));
    BOOST_TEST(!taar::is_text_mime_type("application/octet-stream"));
    BOOST_TEST(!taar::is_text_mime_type("text/"));
    BOOST_TEST(taar::with_utf8_charset("text/css") == "text/css; charset=utf-8");
    BOOST_TEST(taar::with_utf8_charset("application/javascript") == "application/javascript; charset=utf-8");
    BOOST_TEST(taar::with_utf8_charset("text/plain; charset=latin1") == "text/plain; charset=latin1");
    BOOST_TEST(taar::with_utf8_charset("image/png") == "image/png");
}

} // namespace
