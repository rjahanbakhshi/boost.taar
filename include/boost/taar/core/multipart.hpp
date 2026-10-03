//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_MULTIPART_HPP
#define BOOST_TAAR_CORE_MULTIPART_HPP

#include <boost/beast/core/string.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/body_reader.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace boost::taar {

/// The headers of one `multipart/form-data` part.
struct multipart_part_header
{
    std::string name;                       ///< `name` from Content-Disposition.
    std::optional<std::string> filename;    ///< `filename` from Content-Disposition, if present.
    std::string content_type;               ///< Part Content-Type; empty if absent.
    std::vector<std::pair<std::string, std::string>> headers;   ///< All part headers, in order.

    /// True if the part carried a filename, i.e. it is a file field.
    bool is_file() const noexcept { return filename.has_value(); }
};

/// One fully buffered part: headers plus data.
struct multipart_part : multipart_part_header
{
    std::string data;
};

/// A fully parsed `multipart/form-data` body.
struct multipart_form
{
    std::vector<multipart_part> parts;

    /// The first part named @a name, or null.
    multipart_part const* find(std::string_view name) const noexcept
    {
        for (auto const& p : parts)
        {
            if (p.name == name) return &p;
        }
        return nullptr;
    }

    /// The data of the first part named @a name, if any.
    std::optional<std::string_view> value(std::string_view name) const noexcept
    {
        auto const* p = find(name);
        if (p == nullptr) return std::nullopt;
        return std::string_view {p->data};
    }

    /// Every part that carries a filename.
    std::vector<multipart_part const*> files() const
    {
        std::vector<multipart_part const*> out;
        for (auto const& p : parts)
        {
            if (p.is_file()) out.push_back(&p);
        }
        return out;
    }
};

/** Extract the boundary parameter of a `multipart/form-data` Content-Type.

    @returns The boundary, or `std::nullopt` if @a content_type is not
             multipart/form-data or lacks a boundary.
*/
inline std::optional<std::string> multipart_boundary(std::string_view content_type)
{
    auto lower = [](std::string_view s)
    {
        std::string out {s};
        for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    auto const semi = content_type.find(';');
    auto type = content_type.substr(0, semi);
    while (!type.empty() && type.back() == ' ') type.remove_suffix(1);
    if (lower(type) != "multipart/form-data") return std::nullopt;

    auto rest = semi == std::string_view::npos ? std::string_view {} : content_type.substr(semi + 1);
    while (!rest.empty())
    {
        auto next = rest.find(';');
        auto param = rest.substr(0, next);
        rest = next == std::string_view::npos ? std::string_view {} : rest.substr(next + 1);
        auto eq = param.find('=');
        if (eq == std::string_view::npos) continue;
        auto key = param.substr(0, eq);
        while (!key.empty() && key.front() == ' ') key.remove_prefix(1);
        while (!key.empty() && key.back() == ' ') key.remove_suffix(1);
        if (lower(key) != "boundary") continue;
        auto value = param.substr(eq + 1);
        while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
        while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        {
            value = value.substr(1, value.size() - 2);
        }
        if (value.empty()) return std::nullopt;
        return std::string {value};
    }
    return std::nullopt;
}

namespace detail {

/** Incremental `multipart/form-data` scanner.

    Feed bytes with @ref feed (and @ref finish at end of input), then pull
    events with @ref next. Data views point into the scanner's buffer and
    are valid until the next call.
*/
class multipart_scanner
{
public:
    enum class event { need_more, part_begin, data, part_end, finished };

    struct step
    {
        event kind;
        multipart_part_header header {};    ///< For part_begin.
        std::string_view data {};           ///< For data.
    };

    explicit multipart_scanner(std::string boundary, std::size_t max_header_size = 16 * 1024)
        : first_delim_ {"--" + boundary}
        , delim_ {"\r\n--" + boundary}
        , max_header_size_ {max_header_size}
    {}

    void feed(std::string_view bytes)
    {
        compact();
        buf_.append(bytes);
    }

    void finish() noexcept { eof_ = true; }

    bool finished() const noexcept { return state_ == state::finished; }

    step next()
    {
        switch (state_)
        {
        case state::preamble:
        {
            auto const i = buf_.find(first_delim_, pos_);
            if (i == std::string::npos)
            {
                if (eof_) malformed("multipart body without an opening boundary");
                // Keep the tail that could be the start of the delimiter.
                pos_ = buf_.size() > first_delim_.size() ? buf_.size() - first_delim_.size() : 0;
                return {event::need_more};
            }
            pos_ = i + first_delim_.size();
            state_ = state::after_delim;
            return next();
        }

        case state::after_delim:
        {
            if (buf_.size() - pos_ < 2)
            {
                if (eof_) malformed("multipart body truncated after a boundary");
                return {event::need_more};
            }
            auto const two = std::string_view {buf_}.substr(pos_, 2);
            if (two == "--")
            {
                state_ = state::finished;
                return {event::finished};
            }
            if (two != "\r\n") malformed("unexpected bytes after a multipart boundary");
            pos_ += 2;
            state_ = state::headers;
            return next();
        }

        case state::headers:
        {
            auto const end = buf_.find("\r\n\r\n", pos_);
            if (end == std::string::npos)
            {
                if (buf_.size() - pos_ > max_header_size_) malformed("multipart part headers too large");
                if (eof_) malformed("multipart part headers truncated");
                return {event::need_more};
            }
            step out {event::part_begin};
            out.header = parse_headers(std::string_view {buf_}.substr(pos_, end - pos_));
            pos_ = end + 4;
            state_ = state::data;
            return out;
        }

        case state::data:
        {
            auto const i = buf_.find(delim_, pos_);
            if (i != std::string::npos)
            {
                step out {event::data};
                out.data = std::string_view {buf_}.substr(pos_, i - pos_);
                pos_ = i + delim_.size();
                state_ = state::part_ending;
                if (out.data.empty()) return next();
                return out;
            }
            if (eof_) malformed("multipart part without a closing boundary");
            // Deliver what cannot be part of a delimiter yet.
            auto const hold = delim_.size() - 1;
            auto const available = buf_.size() - pos_;
            if (available <= hold) return {event::need_more};
            step out {event::data};
            out.data = std::string_view {buf_}.substr(pos_, available - hold);
            pos_ += available - hold;
            return out;
        }

        case state::part_ending:
            state_ = state::after_delim;
            return {event::part_end};

        case state::finished:
            return {event::finished};
        }
        return {event::finished};
    }

private:
    enum class state { preamble, after_delim, headers, data, part_ending, finished };

    [[noreturn]] static void malformed(char const* what)
    {
        throw taar::exception {failure {error::invalid_request_format, what}};
    }

    void compact()
    {
        if (pos_ > 0)
        {
            buf_.erase(0, pos_);
            pos_ = 0;
        }
    }

    static std::string_view trim(std::string_view s)
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
        return s;
    }

    static bool iequals(std::string_view a, std::string_view b)
    {
        return boost::beast::iequals(a, b);
    }

    /// `form-data; name="a"; filename="b.txt"` -> parameters (quotes and `\"` escapes handled).
    static void parse_disposition(std::string_view value, multipart_part_header& h)
    {
        auto semi = value.find(';');
        auto rest = semi == std::string_view::npos ? std::string_view {} : value.substr(semi + 1);
        while (!rest.empty())
        {
            rest = trim(rest);
            auto eq = rest.find('=');
            if (eq == std::string_view::npos) break;
            auto key = trim(rest.substr(0, eq));
            rest.remove_prefix(eq + 1);
            std::string param;
            if (!rest.empty() && rest.front() == '"')
            {
                rest.remove_prefix(1);
                while (!rest.empty() && rest.front() != '"')
                {
                    if (rest.front() == '\\' && rest.size() > 1) rest.remove_prefix(1);
                    param += rest.front();
                    rest.remove_prefix(1);
                }
                if (!rest.empty()) rest.remove_prefix(1);
                auto next = rest.find(';');
                rest = next == std::string_view::npos ? std::string_view {} : rest.substr(next + 1);
            }
            else
            {
                auto next = rest.find(';');
                param = std::string {trim(rest.substr(0, next))};
                rest = next == std::string_view::npos ? std::string_view {} : rest.substr(next + 1);
            }
            if (iequals(key, "name")) h.name = param;
            else if (iequals(key, "filename")) h.filename = param;
        }
    }

    multipart_part_header parse_headers(std::string_view block)
    {
        multipart_part_header h;
        while (!block.empty())
        {
            auto eol = block.find("\r\n");
            auto line = block.substr(0, eol);
            block = eol == std::string_view::npos ? std::string_view {} : block.substr(eol + 2);
            auto colon = line.find(':');
            if (colon == std::string_view::npos) malformed("malformed multipart part header");
            auto key = trim(line.substr(0, colon));
            auto value = trim(line.substr(colon + 1));
            h.headers.emplace_back(std::string {key}, std::string {value});
            if (iequals(key, "Content-Disposition")) parse_disposition(value, h);
            else if (iequals(key, "Content-Type")) h.content_type = std::string {value};
        }
        if (h.name.empty()) malformed("multipart part without a name");
        return h;
    }

    std::string first_delim_;
    std::string delim_;
    std::size_t max_header_size_;
    std::string buf_;
    std::size_t pos_ = 0;
    bool eof_ = false;
    state state_ = state::preamble;
};

} // namespace detail

/** Parse a complete `multipart/form-data` body.

    @throws boost::taar::exception carrying @ref error::invalid_request_format
            and a message naming what is malformed.
*/
inline multipart_form parse_multipart(std::string_view body, std::string_view boundary)
{
    detail::multipart_scanner scanner {std::string {boundary}};
    scanner.feed(body);
    scanner.finish();

    multipart_form form;
    for (;;)
    {
        auto s = scanner.next();
        switch (s.kind)
        {
        case detail::multipart_scanner::event::part_begin:
            form.parts.emplace_back();
            static_cast<multipart_part_header&>(form.parts.back()) = std::move(s.header);
            break;
        case detail::multipart_scanner::event::data:
            form.parts.back().data.append(s.data);
            break;
        case detail::multipart_scanner::event::part_end:
            break;
        case detail::multipart_scanner::event::finished:
            return form;
        case detail::multipart_scanner::event::need_more:
            throw taar::exception {failure {
                error::invalid_request_format,
                "multipart body ends without a closing boundary"}};
        }
    }
}

/** Incremental reader over a streamed `multipart/form-data` body.

    Obtain one through `handler::multipart_stream_arg`. Call @ref next_part
    to move to the next part (skipping whatever is left of the current
    one), then @ref read_some / @ref read_all for its data. Only the
    current part is buffered, in pieces, so file fields of any size can be
    consumed with bounded memory.

    @code
    while (auto part = co_await form.next_part())
    {
        if (part->is_file())
            while (!form.part_done()) store(co_await form.read_some());
        else
            fields[part->name] = co_await form.read_all();
    }
    @endcode
*/
class multipart_reader
{
public:
    multipart_reader(body_reader& body, std::string boundary)
        : body_ {&body}
        , scanner_ {std::move(boundary)}
    {}

    /// Advance to the next part. Returns its headers, or `nullopt` after the last part.
    awaitable<std::optional<multipart_part_header>> next_part()
    {
        for (;;)
        {
            if (!pending_.empty())
            {
                pending_.clear();     // discard unread data of the current part
            }
            auto s = co_await step();
            switch (s.kind)
            {
            case detail::multipart_scanner::event::part_begin:
                part_done_ = false;
                co_return std::move(s.header);
            case detail::multipart_scanner::event::finished:
                finished_ = true;
                part_done_ = true;
                co_return std::nullopt;
            default:
                break;              // data/part_end of a part being skipped
            }
        }
    }

    /// Read some of the current part into @a buffer; 0 once the part is done.
    awaitable<std::size_t> read_some(boost::asio::mutable_buffer buffer)
    {
        while (pending_.empty() && !part_done_)
        {
            auto s = co_await step();
            switch (s.kind)
            {
            case detail::multipart_scanner::event::data:
                pending_.assign(s.data);
                break;
            case detail::multipart_scanner::event::part_end:
                part_done_ = true;
                break;
            case detail::multipart_scanner::event::finished:
                part_done_ = true;
                finished_ = true;
                break;
            default:
                break;
            }
        }
        auto const n = std::min(buffer.size(), pending_.size());
        std::memcpy(buffer.data(), pending_.data(), n);
        pending_.erase(0, n);
        co_return n;
    }

    /// Read some of the current part as a string of at most @a max bytes; empty once done.
    awaitable<std::string> read_some(std::size_t max = 64 * 1024)
    {
        if (scratch_.size() < max) scratch_.resize(max);
        auto n = co_await read_some(boost::asio::buffer(scratch_.data(), max));
        co_return std::string(scratch_.data(), n);
    }

    /** Read the rest of the current part, bounded by @a limit.

        @throws boost::taar::exception carrying
                `boost::beast::http::error::body_limit` once the part
                exceeds @a limit bytes, @ref error::invalid_request_format
                if the body is malformed, or the transport error.
    */
    awaitable<std::string> read_all(std::uint64_t limit = 16 * 1024 * 1024)
    {
        constexpr std::size_t chunk = 64 * 1024;
        std::string out;
        while (!part_done())
        {
            auto const old = out.size();
            out.resize(old + chunk);
            auto n = co_await read_some(boost::asio::buffer(out.data() + old, chunk));
            out.resize(old + n);
            if (out.size() > limit)
            {
                throw taar::exception {failure {
                    boost::beast::http::error::body_limit,
                    "multipart part exceeds the read limit of " + std::to_string(limit) + " bytes"}};
            }
        }
        co_return out;
    }

    /// True when the current part's data has been fully delivered.
    bool part_done() const noexcept { return part_done_ && pending_.empty(); }

    /// True after the closing boundary.
    bool done() const noexcept { return finished_; }

private:
    awaitable<detail::multipart_scanner::step> step()
    {
        for (;;)
        {
            auto s = scanner_.next();
            if (s.kind != detail::multipart_scanner::event::need_more) co_return s;
            if (body_->done())
            {
                scanner_.finish();
                continue;
            }
            auto piece = co_await body_->read_some(64 * 1024);
            if (piece.empty()) scanner_.finish();
            else scanner_.feed(piece);
        }
    }

    body_reader* body_;
    detail::multipart_scanner scanner_;
    std::string pending_;
    std::string scratch_;
    bool part_done_ = true;
    bool finished_ = false;
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_MULTIPART_HPP
