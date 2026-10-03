//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include "e2e.hpp"
#include <boost/taar/core/body_reader.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/core/multipart.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/json/parse.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;
using taar::handler::rest;
using taar::handler::multipart_form_arg;
using taar::handler::multipart_stream_arg;
using taar::handler::query_arg;

std::string const boundary = "----taarBoundary42";

std::string field(std::string name, std::string value)
{
    return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
}

std::string file(std::string name, std::string filename, std::string type, std::string const& data)
{
    return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"; filename=\"" + filename +
        "\"\r\nContent-Type: " + type + "\r\n\r\n" + data + "\r\n";
}

std::string closing() { return "--" + boundary + "--\r\n"; }

std::string pattern(std::size_t n)
{
    std::string s(n, '\0');
    for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<char>('a' + i % 26);
    return s;
}

// ---- boundary extraction -----------------------------------------------------

BOOST_AUTO_TEST_CASE(test_multipart_boundary_extraction)
{
    BOOST_TEST(taar::multipart_boundary("multipart/form-data; boundary=abc").value() == "abc");
    BOOST_TEST(taar::multipart_boundary("Multipart/Form-Data; charset=utf-8; boundary=\"q u o\"").value() == "q u o");
    BOOST_TEST(taar::multipart_boundary("multipart/form-data;boundary=x").value() == "x");
    BOOST_TEST(!taar::multipart_boundary("multipart/form-data").has_value());
    BOOST_TEST(!taar::multipart_boundary("application/json; boundary=abc").has_value());
    BOOST_TEST(!taar::multipart_boundary("multipart/form-data; boundary=").has_value());
}

// ---- buffered parser -----------------------------------------------------

BOOST_AUTO_TEST_CASE(test_multipart_parse_fields_and_files)
{
    auto body = "preamble is ignored\r\n" + field("title", "hello world") +
        file("doc", "notes.txt", "text/plain", "line one\r\nline two") +
        field("empty", "") + closing() + "epilogue";
    auto form = taar::parse_multipart(body, boundary);
    BOOST_REQUIRE(form.parts.size() == 3u);
    BOOST_TEST(form.parts[0].name == "title");
    BOOST_TEST(!form.parts[0].is_file());
    BOOST_TEST(form.parts[0].data == "hello world");
    BOOST_TEST(form.parts[1].name == "doc");
    BOOST_TEST(form.parts[1].filename.value() == "notes.txt");
    BOOST_TEST(form.parts[1].content_type == "text/plain");
    BOOST_TEST(form.parts[1].data == "line one\r\nline two");       // CRLF inside data preserved
    BOOST_TEST(form.parts[1].headers.size() == 2u);
    BOOST_TEST(form.parts[2].data == "");
    BOOST_TEST(form.value("title").value() == "hello world");
    BOOST_TEST(!form.value("missing").has_value());
    BOOST_TEST(form.files().size() == 1u);
    BOOST_TEST(form.find("doc") == form.files()[0]);
}

BOOST_AUTO_TEST_CASE(test_multipart_parse_quoting_and_lookalike_data)
{
    // Escaped quote in the filename, a boundary-like string inside the data, no preamble.
    std::string body = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"f\"; filename=\"we \\\"ird\\\".bin\"\r\n\r\n" +
        "--" + boundary + "x is not a delimiter\r\n--not-the-boundary\r\n" + closing();
    auto form = taar::parse_multipart(body, boundary);
    BOOST_REQUIRE(form.parts.size() == 1u);
    BOOST_TEST(form.parts[0].filename.value() == "we \"ird\".bin");
    BOOST_TEST(form.parts[0].data == "--" + boundary + "x is not a delimiter\r\n--not-the-boundary");
}

BOOST_AUTO_TEST_CASE(test_multipart_parse_rejects_malformed_bodies)
{
    BOOST_CHECK_THROW(taar::parse_multipart("no boundary at all", boundary), taar::exception);
    BOOST_CHECK_THROW(taar::parse_multipart(field("a", "1"), boundary), taar::exception);          // no closing boundary
    BOOST_CHECK_THROW(taar::parse_multipart("--" + boundary + "\r\nContent-Type: text/plain\r\n\r\nx\r\n" + closing(), boundary),
        taar::exception);                                                                             // no name
    BOOST_CHECK_THROW(taar::parse_multipart("--" + boundary + "\r\nbroken header line\r\n\r\nx\r\n" + closing(), boundary),
        taar::exception);
    BOOST_CHECK_THROW(taar::parse_multipart("--" + boundary + "??", boundary), taar::exception);
    auto empty = taar::parse_multipart(closing(), boundary);
    BOOST_TEST(empty.parts.empty());

    // The failure carries invalid_request_format and names what is malformed.
    try
    {
        (void)taar::parse_multipart("--" + boundary + "\r\nContent-Type: text/plain\r\n\r\nx\r\n" + closing(), boundary);
        BOOST_FAIL("no throw");
    }
    catch (taar::exception const& e)
    {
        BOOST_TEST(e.error().is(taar::error::invalid_request_format));
        BOOST_TEST(e.error().text() == "multipart part without a name");
    }
}

// ---- read limits ------------------------------------------------------------

/// A body_reader delivering @a data from memory.
taar::body_reader memory_reader(std::shared_ptr<std::string const> data)
{
    auto pos = std::make_shared<std::size_t>(0);
    return taar::body_reader {
        [data, pos](net::mutable_buffer out) -> taar::awaitable<std::size_t>
        {
            auto const n = std::min(out.size(), data->size() - *pos);
            std::memcpy(out.data(), data->data() + *pos, n);
            *pos += n;
            co_return n;
        },
        [data, pos] { return *pos == data->size(); },
        data->size()};
}

template <typename Coroutine>
void run_coroutine(Coroutine coroutine)
{
    net::io_context io;
    std::exception_ptr error;
    bool done = false;
    net::co_spawn(io, coroutine(), [&](std::exception_ptr e) { error = e; done = true; });
    io.run();
    BOOST_REQUIRE(done);
    if (error) std::rethrow_exception(error);
}

BOOST_AUTO_TEST_CASE(test_body_reader_read_all_limit_is_a_body_limit_failure)
{
    auto const data = std::make_shared<std::string const>(pattern(300));
    run_coroutine([&]() -> taar::awaitable<void>
    {
        auto reader = memory_reader(data);
        bool caught = false;
        try
        {
            (void)co_await reader.read_all(100);
        }
        catch (taar::exception const& e)
        {
            BOOST_TEST(e.error().is(http::error::body_limit));
            BOOST_TEST(e.error().text() == "request body exceeds the read limit of 100 bytes");
            BOOST_TEST(taar::http_status_for(e.error()) == http::status::payload_too_large);
            caught = true;
        }
        BOOST_TEST(caught);
    });
}

BOOST_AUTO_TEST_CASE(test_multipart_reader_read_all_limit_is_a_body_limit_failure)
{
    auto const data = std::make_shared<std::string const>(field("big", pattern(300)) + closing());
    run_coroutine([&]() -> taar::awaitable<void>
    {
        auto reader = memory_reader(data);
        taar::multipart_reader form {reader, boundary};
        auto part = co_await form.next_part();
        BOOST_REQUIRE(part.has_value());
        bool caught = false;
        try
        {
            (void)co_await form.read_all(100);
        }
        catch (taar::exception const& e)
        {
            BOOST_TEST(e.error().is(http::error::body_limit));
            BOOST_TEST(e.error().text() == "multipart part exceeds the read limit of 100 bytes");
            caught = true;
        }
        BOOST_TEST(caught);
    });
}

BOOST_AUTO_TEST_CASE(test_multipart_scanner_is_chunk_boundary_agnostic)
{
    // Feed one byte at a time: delimiters straddling feeds must still be found.
    auto body = field("a", "12345") + file("f", "x.bin", "application/octet-stream", pattern(300)) + closing();
    taar::detail::multipart_scanner scanner {boundary};
    std::vector<std::string> names;
    std::vector<std::string> datas;
    std::size_t fed = 0;
    bool finished = false;
    while (!finished)
    {
        auto s = scanner.next();
        using ev = taar::detail::multipart_scanner::event;
        switch (s.kind)
        {
        case ev::need_more:
            if (fed < body.size()) scanner.feed(std::string_view {body}.substr(fed++, 1));
            else scanner.finish();
            break;
        case ev::part_begin: names.push_back(s.header.name); datas.emplace_back(); break;
        case ev::data: datas.back().append(s.data); break;
        case ev::part_end: break;
        case ev::finished: finished = true; break;
        }
    }
    BOOST_REQUIRE(names.size() == 2u);
    BOOST_TEST(datas[0] == "12345");
    BOOST_TEST(datas[1] == pattern(300));
}

// ---- providers over the wire ----------------------------------------------

std::atomic<std::size_t> peak_piece {0};

taar::session::http make_session()
{
    taar::session::http session;
    session.set_request_body_limit(64 * 1024);

    session.register_request_handler(
        method == http::verb::post && target == "/form",
        rest([](taar::multipart_form form, std::optional<std::string> tag)
        {
            json::object out;
            out["title"] = std::string {form.value("title").value_or("")};
            out["files"] = form.files().size();
            out["tag"] = tag.value_or("none");
            if (auto const* f = form.find("doc"))
            {
                out["filename"] = *f->filename;
                out["size"] = f->data.size();
                out["type"] = f->content_type;
            }
            return json::value {std::move(out)};
        },
        multipart_form_arg {},
        query_arg("tag")));

    session.register_request_handler(
        method == http::verb::post && target == "/files",
        rest([](taar::multipart_reader form) -> taar::awaitable<json::value>
        {
            json::object out;
            json::array files;
            while (auto part = co_await form.next_part())
            {
                if (part->is_file())
                {
                    std::uint64_t bytes = 0;
                    std::uint64_t sum = 0;
                    while (!form.part_done())
                    {
                        auto piece = co_await form.read_some(4096);
                        if (piece.size() > peak_piece) peak_piece = piece.size();
                        bytes += piece.size();
                        for (unsigned char c : piece) sum += c;
                    }
                    files.push_back(json::object {{"name", part->name}, {"filename", *part->filename}, {"bytes", bytes}, {"sum", sum}});
                }
                else if (part->name == "skipped")
                {
                    continue;       // next_part() discards the unread data
                }
                else
                {
                    out[part->name] = co_await form.read_all(1024);
                }
            }
            out["files"] = std::move(files);
            co_return json::value {std::move(out)};
        },
        multipart_stream_arg {}));

    session.register_request_handler(method == http::verb::get && target == "/ping", rest([] { return "pong"; }));
    return session;
}

e2e::request multipart_request(std::string target, std::string body)
{
    return e2e::make_request(http::verb::post, std::move(target), std::move(body), "multipart/form-data; boundary=" + boundary);
}

BOOST_AUTO_TEST_CASE(test_e2e_multipart_form_arg_buffered)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto body = field("title", "Quarterly report") + file("doc", "q3.pdf", "application/pdf", pattern(5000)) + closing();
        auto res = co_await e2e::send_all(port, {
            multipart_request("/form?tag=x", body),
            e2e::make_request(http::verb::post, "/form", "{}", "application/json"),
            multipart_request("/form", "--" + boundary + " garbage"),
            e2e::make_request(http::verb::get, "/ping")});
        BOOST_TEST(res[0].result() == http::status::ok);
        auto v = json::parse(res[0].body());
        BOOST_TEST(v.at("title").as_string() == "Quarterly report");
        BOOST_TEST(v.at("files").to_number<std::uint64_t>() == 1u);
        BOOST_TEST(v.at("filename").as_string() == "q3.pdf");
        BOOST_TEST(v.at("size").to_number<std::uint64_t>() == 5000u);
        BOOST_TEST(v.at("type").as_string() == "application/pdf");
        BOOST_TEST(v.at("tag").as_string() == "x");
        BOOST_TEST(res[1].result() == http::status::bad_request);     // wrong content type
        BOOST_TEST(res[2].result() == http::status::bad_request);     // malformed body
        BOOST_TEST(res[2].body().find(
            "malformed multipart body (caused by: unexpected bytes after a multipart boundary)") != std::string::npos);
        BOOST_TEST(res[3].body() == "pong");                           // connection still fine
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_multipart_stream_arg_large_file_beyond_body_limit)
{
    net::io_context io;
    auto session = make_session();
    auto const payload = pattern(2 * 1024 * 1024);
    std::uint64_t sum = 0;
    for (unsigned char c : payload) sum += c;

    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto body = field("title", "big") + field("skipped", pattern(10000)) +
            file("upload", "big.bin", "application/octet-stream", payload) + field("after", "last") + closing();
        auto res = co_await e2e::send_all(port, {multipart_request("/files", body), e2e::make_request(http::verb::get, "/ping")});
        BOOST_TEST(res[0].result() == http::status::ok);
        auto v = json::parse(res[0].body());
        BOOST_TEST(v.at("title").as_string() == "big");
        BOOST_TEST(v.at("after").as_string() == "last");
        BOOST_TEST(!v.as_object().contains("skipped"));
        auto const& f = v.at("files").as_array()[0];
        BOOST_TEST(f.at("filename").as_string() == "big.bin");
        BOOST_TEST(f.at("bytes").to_number<std::uint64_t>() == payload.size());
        BOOST_TEST(f.at("sum").to_number<std::uint64_t>() == sum);
        BOOST_TEST(peak_piece <= 4096u);
        BOOST_TEST(res[1].body() == "pong");                           // fully consumed: keep-alive intact
    });
}

BOOST_AUTO_TEST_CASE(test_e2e_multipart_stream_arg_rejects_non_multipart)
{
    net::io_context io;
    auto session = make_session();
    e2e::run(io, session, [&](unsigned short port) -> taar::awaitable<void>
    {
        auto res = co_await e2e::send_all(port, {e2e::make_request(http::verb::post, "/files", "plain", "text/plain")});
        BOOST_TEST(res[0].result() == http::status::bad_request);
        BOOST_TEST(!res[0].keep_alive());                              // body left unread
    });
}

BOOST_AUTO_TEST_CASE(test_openapi_documents_multipart_bodies)
{
    taar::session::http session;
    taar::openapi::generator gen {{.title = "t", .version = "1"}};
    session.register_request_handler(method == http::verb::post && target == "/form",
        rest([](taar::multipart_form) { return 0; }, multipart_form_arg {taar::handler::description {"Fields and attachments"}}),
        gen.describe("Form"));
    session.register_request_handler(method == http::verb::post && target == "/files",
        rest([](taar::multipart_reader) { return 0; }, multipart_stream_arg {}),
        gen.describe("Files"));
    auto doc = gen.document();
    auto const& form = doc.at("paths").at("/form").at("post").at("requestBody");
    BOOST_TEST(form.at("description").as_string() == "Fields and attachments");
    BOOST_TEST(form.at("content").at("multipart/form-data").at("schema").at("type").as_string() == "object");
    BOOST_TEST(doc.at("paths").at("/files").at("post").at("requestBody").at("content").as_object().contains("multipart/form-data"));
}

} // namespace
