//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_BODY_READER_HPP
#define BOOST_TAAR_CORE_BODY_READER_HPP

#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace boost::taar {

/** Incremental access to a request body while it is still arriving.

    Handed to handlers through @ref streaming_request. The body is never
    buffered by the session: each @ref read_some pulls the next piece
    straight from the connection into the caller's buffer, so uploads of
    any size can be consumed with bounded memory. The session's body limit
    does not apply; the handler decides how much to accept.

    If the handler returns (or throws) before @ref done is true, the
    session answers and then closes the connection instead of draining
    what remains.
*/
class body_reader
{
public:
    using read_function = std::function<awaitable<std::size_t>(boost::asio::mutable_buffer)>;

    body_reader(
        read_function read,
        std::function<bool()> is_done,
        std::optional<std::uint64_t> content_length)
        : read_ {std::move(read)}
        , done_ {std::move(is_done)}
        , content_length_ {content_length}
    {}

    /// Read into @a buffer. Returns the number of bytes stored; 0 only when @ref done.
    awaitable<std::size_t> read_some(boost::asio::mutable_buffer buffer)
    {
        auto n = co_await read_(buffer);
        bytes_read_ += n;
        co_return n;
    }

    /// Read the next piece as a string of at most @a max bytes; empty when @ref done.
    awaitable<std::string> read_some(std::size_t max = 64 * 1024)
    {
        if (scratch_.size() < max) scratch_.resize(max);
        auto n = co_await read_some(boost::asio::buffer(scratch_.data(), max));
        co_return std::string(scratch_.data(), n);
    }

    /** Read everything that remains, bounded by @a limit.

        @throws boost::taar::exception carrying
                `boost::beast::http::error::body_limit` once more than
                @a limit bytes have arrived, or the transport error if
                reading fails.
    */
    awaitable<std::string> read_all(std::uint64_t limit = 16 * 1024 * 1024)
    {
        constexpr std::size_t chunk = 64 * 1024;
        std::string out;
        while (!done())
        {
            auto const old = out.size();
            out.resize(old + chunk);
            auto n = co_await read_some(boost::asio::buffer(out.data() + old, chunk));
            out.resize(old + n);
            if (out.size() > limit)
            {
                throw taar::exception {failure {
                    boost::beast::http::error::body_limit,
                    "request body exceeds the read limit of " + std::to_string(limit) + " bytes"}};
            }
        }
        co_return out;
    }

    /// True once the whole body has been consumed.
    bool done() const { return done_(); }

    /// Bytes delivered so far.
    std::uint64_t bytes_read() const noexcept { return bytes_read_; }

    /// The request's `Content-Length`, if it had one (chunked uploads have none).
    std::optional<std::uint64_t> content_length() const noexcept { return content_length_; }

private:
    read_function read_;
    std::function<bool()> done_;
    std::optional<std::uint64_t> content_length_;
    std::uint64_t bytes_read_ = 0;
    std::string scratch_;
};

/** The request a streaming handler receives: the parsed header plus a
    @ref body_reader for the body.

    Declare it as the request type of a raw handler, or use
    `handler::stream_body_arg` on a `rest` callable to receive the
    @ref body_reader directly. Header, path, query and cookie providers
    work as usual because this type is a `request_header<>`.
*/
struct streaming_request : boost::beast::http::request_header<>
{
    streaming_request(boost::beast::http::request_header<> const& header, body_reader& reader)
        : boost::beast::http::request_header<> {header}
        , body {reader}
    {}

    body_reader& body;
};

namespace detail {

/// Build a @ref body_reader over a `buffer_body` parser continuing on @a stream.
template <typename Stream>
body_reader make_body_reader(
    Stream& stream,
    boost::beast::flat_buffer& buffer,
    boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser)
{
    namespace http = boost::beast::http;
    std::optional<std::uint64_t> length;
    if (auto cl = parser.content_length()) length = *cl;

    return body_reader {
        [&stream, &buffer, &parser](boost::asio::mutable_buffer out) -> awaitable<std::size_t>
        {
            if (parser.is_done() || out.size() == 0) co_return 0;
            parser.get().body().data = out.data();
            parser.get().body().size = out.size();
            parser.get().body().more = true;
            auto [ec, n] = co_await http::async_read_some(stream, buffer, parser, use_awaitable_tuple);
            if (ec && ec != http::error::need_buffer)
            {
                // The outer level says what failed, the cause why; both
                // carry the transport code so mappers see it unchanged.
                throw taar::exception {
                    failure {ec, "reading the request body failed"}.with_cause(failure {ec})};
            }
            co_return out.size() - parser.get().body().size;
        },
        [&parser] { return parser.is_done(); },
        length};
}

} // namespace detail
} // namespace boost::taar

#endif // BOOST_TAAR_CORE_BODY_READER_HPP
