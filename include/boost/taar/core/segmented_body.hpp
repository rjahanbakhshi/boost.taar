//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_CORE_SEGMENTED_BODY_HPP
#define BOOST_TAAR_CORE_SEGMENTED_BODY_HPP

#include <boost/asio/buffer.hpp>
#include <boost/beast/core/file.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/optional/optional.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace boost::taar {

/** A Beast body made of segments: owned text, windows of shared
    immutable buffers, and byte windows of one open file.

    It serves every response of @ref boost::taar::handler::htdocs "htdocs"
    with one type: a whole file, a single byte range, a
    `multipart/byteranges` sequence (text part headers between file
    windows) or a cached file kept in memory, without copying file data
    into the message. File windows are read on demand while the body is
    written, so the file is never loaded as a whole.

    The body is read-only during serialization (the writer takes the value
    by const reference), so a `response<segmented_body>` can be written
    as an rvalue or converted to a `message_generator`. An empty value
    writes nothing; a response describing a body it does not carry (HEAD,
    304) sets `Content-Length` itself.

    @code
    segmented_body::value_type body;
    boost::beast::file file;
    file.open("movie.mp4", boost::beast::file_mode::scan, ec);
    body.attach_file(std::move(file));
    body.append_file_range(0, 1024);
    @endcode
*/
struct segmented_body
{
    /// The body's value: the ordered segments and the file they may refer to.
    class value_type
    {
    public:
        /// Take ownership of an open file that later file ranges read from.
        void attach_file(boost::beast::file file)
        {
            file_ = std::make_shared<boost::beast::file>(std::move(file));
        }

        /// True if a file has been attached.
        bool has_file() const noexcept { return file_ != nullptr; }

        /// Append owned text.
        void append_text(std::string text)
        {
            size_ += text.size();
            segments_.emplace_back(std::move(text));
        }

        /** Append @a length bytes at @a offset of a shared immutable buffer.

            The buffer is kept alive by the body; nothing is copied.
        */
        void append_buffer(std::shared_ptr<std::string const> data, std::uint64_t offset, std::uint64_t length)
        {
            size_ += length;
            segments_.emplace_back(buffer_window {std::move(data), offset, length});
        }

        /// Append @a length bytes at @a offset of the attached file.
        void append_file_range(std::uint64_t offset, std::uint64_t length)
        {
            size_ += length;
            segments_.emplace_back(file_window {offset, length});
        }

        /// Total number of bytes the segments produce.
        std::uint64_t size() const noexcept { return size_; }

        /// True if there are no bytes to write.
        bool empty() const noexcept { return size_ == 0; }

    private:
        friend struct segmented_body;

        struct buffer_window
        {
            std::shared_ptr<std::string const> data;
            std::uint64_t offset;
            std::uint64_t length;
        };
        struct file_window
        {
            std::uint64_t offset;
            std::uint64_t length;
        };

        std::shared_ptr<boost::beast::file> file_;
        std::vector<std::variant<std::string, buffer_window, file_window>> segments_;
        std::uint64_t size_ = 0;
    };

    /// Payload size, for `prepare_payload()`.
    static std::uint64_t size(value_type const& body) noexcept
    {
        return body.size();
    }

    /// Serializes the segments in order; file windows are read in chunks.
    class writer
    {
    public:
        /// The buffer sequence type produced by `get()`.
        using const_buffers_type = boost::asio::const_buffer;

        /// Construct from the message header and the body value.
        template <bool isRequest, class Fields>
        writer(boost::beast::http::header<isRequest, Fields> const&, value_type const& body)
            : body_ {body}
        {}

        /// Nothing to prepare: files are opened when the value is built.
        void init(boost::system::error_code& ec)
        {
            ec = {};
        }

        /// The next buffer, with whether more follow; empty when done.
        boost::optional<std::pair<const_buffers_type, bool>> get(boost::system::error_code& ec)
        {
            ec = {};
            while (index_ < body_.segments_.size())
            {
                auto const& segment = body_.segments_[index_];
                if (auto const* text = std::get_if<std::string>(&segment))
                {
                    ++index_;
                    if (text->empty()) continue;
                    return std::pair {boost::asio::const_buffer {text->data(), text->size()}, more()};
                }
                if (auto const* window = std::get_if<value_type::buffer_window>(&segment))
                {
                    ++index_;
                    if (window->length == 0) continue;
                    return std::pair {
                        boost::asio::const_buffer {
                            window->data->data() + window->offset,
                            static_cast<std::size_t>(window->length)},
                        more()};
                }
                auto const& window = std::get<value_type::file_window>(segment);
                if (!started_)
                {
                    if (window.length == 0)
                    {
                        ++index_;
                        continue;
                    }
                    if (!body_.file_)
                    {
                        ec = boost::system::errc::make_error_code(boost::system::errc::bad_file_descriptor);
                        return boost::none;
                    }
                    body_.file_->seek(window.offset, ec);
                    if (ec) return boost::none;
                    remaining_ = window.length;
                    started_ = true;
                }
                auto const want = static_cast<std::size_t>(
                    std::min<std::uint64_t>(remaining_, buffer_.size()));
                auto const n = body_.file_->read(buffer_.data(), want, ec);
                if (ec) return boost::none;
                if (n == 0)
                {
                    // The file shrank under us: the promised length cannot be met.
                    ec = boost::system::errc::make_error_code(boost::system::errc::io_error);
                    return boost::none;
                }
                remaining_ -= n;
                if (remaining_ == 0)
                {
                    started_ = false;
                    ++index_;
                }
                return std::pair {boost::asio::const_buffer {buffer_.data(), n}, more()};
            }
            return boost::none;
        }

    private:
        bool more() const noexcept
        {
            if (started_) return true;
            for (auto i = index_; i < body_.segments_.size(); ++i)
            {
                bool const nonempty = std::visit([](auto const& s)
                {
                    if constexpr (std::is_same_v<std::decay_t<decltype(s)>, std::string>) return !s.empty();
                    else return s.length != 0;
                }, body_.segments_[i]);
                if (nonempty) return true;
            }
            return false;
        }

        value_type const& body_;
        std::size_t index_ = 0;
        bool started_ = false;
        std::uint64_t remaining_ = 0;
        std::array<char, 32 * 1024> buffer_;
    };
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_SEGMENTED_BODY_HPP
