//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_BASE64_HPP
#define BOOST_TAAR_CORE_BASE64_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace boost::taar {

/// Standard base64 (RFC 4648) with padding, as MCP binary content requires.
inline std::string base64_encode(std::string_view bytes)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3)
    {
        std::uint32_t const n = (static_cast<unsigned char>(bytes[i]) << 16) |
            (static_cast<unsigned char>(bytes[i + 1]) << 8) | static_cast<unsigned char>(bytes[i + 2]);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
        out += table[n & 63];
    }
    if (i + 1 == bytes.size())
    {
        std::uint32_t const n = static_cast<unsigned char>(bytes[i]) << 16;
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += "==";
    }
    else if (i + 2 == bytes.size())
    {
        std::uint32_t const n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

/// Decode standard base64; `nullopt` on malformed input.
inline std::optional<std::string> base64_decode(std::string_view text)
{
    auto value = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    if (text.size() % 4 != 0) return std::nullopt;
    std::string out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4)
    {
        int const a = value(text[i]);
        int const b = value(text[i + 1]);
        bool const pad2 = text[i + 2] == '=';
        bool const pad3 = text[i + 3] == '=';
        int const c = pad2 ? 0 : value(text[i + 2]);
        int const d = pad3 ? 0 : value(text[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad3) || ((pad2 || pad3) && i + 4 != text.size()))
        {
            return std::nullopt;
        }
        std::uint32_t const n = (static_cast<std::uint32_t>(a) << 18) | (static_cast<std::uint32_t>(b) << 12) |
            (static_cast<std::uint32_t>(c) << 6) | static_cast<std::uint32_t>(d);
        out += static_cast<char>((n >> 16) & 0xff);
        if (!pad2) out += static_cast<char>((n >> 8) & 0xff);
        if (!pad3) out += static_cast<char>(n & 0xff);
    }
    return out;
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_BASE64_HPP
