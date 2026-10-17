//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/segmented_body.hpp>
#include <boost/beast/core/buffers_range.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>

namespace {

namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace taar = boost::taar;

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
        if (!chunk->second) break;
    }
    BOOST_TEST(!ec);
    return out;
}

struct temp_file
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("taar-segmented-" + std::to_string(std::random_device {}()));
    explicit temp_file(std::string const& content) { std::ofstream {path, std::ios::binary} << content; }
    ~temp_file() { std::filesystem::remove(path); }
};

BOOST_AUTO_TEST_CASE(test_segmented_body_mixes_text_buffers_and_file_windows)
{
    std::string content(100000, '\0');
    for (std::size_t i = 0; i < content.size(); ++i) content[i] = static_cast<char>('a' + i % 26);
    temp_file file {content};

    beast::file f;
    boost::system::error_code ec;
    f.open(file.path.string().c_str(), beast::file_mode::scan, ec);
    BOOST_TEST_REQUIRE(!ec);

    taar::segmented_body::value_type body;
    BOOST_TEST(body.empty());
    body.attach_file(std::move(f));
    body.append_text("<");
    body.append_file_range(10, 5);
    body.append_text("");
    body.append_buffer(std::make_shared<std::string const>("0123456789"), 2, 3);
    body.append_file_range(50000, 40000);   // larger than the writer's chunk
    body.append_file_range(0, 0);
    body.append_text(">");
    BOOST_TEST(body.size() == 1u + 5u + 3u + 40000u + 1u);
    BOOST_TEST(taar::segmented_body::size(body) == body.size());

    auto const out = drain(body);
    BOOST_TEST(out == "<" + content.substr(10, 5) + "234" + content.substr(50000, 40000) + ">");
}

BOOST_AUTO_TEST_CASE(test_segmented_body_is_a_const_writable_beast_body)
{
    static_assert(http::is_body<taar::segmented_body>::value);
    static_assert(http::is_body_writer<taar::segmented_body>::value);
    static_assert(!http::is_mutable_body_writer<taar::segmented_body>::value);

    http::response<taar::segmented_body> res {http::status::ok, 11};
    res.body().append_text("hello");
    res.prepare_payload();
    BOOST_TEST(res[http::field::content_length] == "5");
    http::message_generator gen {std::move(res)};   // convertible to an opaque generator
    std::string wire;
    boost::system::error_code ec;
    while (!gen.is_done())
    {
        auto buffers = gen.prepare(ec);
        BOOST_TEST_REQUIRE(!ec);
        std::size_t n = 0;
        for (auto const& b : beast::buffers_range(buffers))
        {
            wire.append(static_cast<char const*>(b.data()), b.size());
            n += b.size();
        }
        gen.consume(n);
    }
    BOOST_TEST(wire.ends_with("\r\n\r\nhello"));
}

BOOST_AUTO_TEST_CASE(test_segmented_body_reports_a_file_that_shrank)
{
    temp_file file {"short"};
    beast::file f;
    boost::system::error_code ec;
    f.open(file.path.string().c_str(), beast::file_mode::scan, ec);
    BOOST_TEST_REQUIRE(!ec);
    taar::segmented_body::value_type body;
    body.attach_file(std::move(f));
    body.append_file_range(0, 100);
    http::response_header<> header;
    taar::segmented_body::writer writer {header, body};
    writer.init(ec);
    auto first = writer.get(ec);
    BOOST_TEST_REQUIRE(first.has_value());
    BOOST_TEST(first->first.size() == 5u);
    BOOST_TEST(first->second);
    auto second = writer.get(ec);
    BOOST_TEST(!second.has_value());
    BOOST_TEST(ec.failed());
}

} // namespace
