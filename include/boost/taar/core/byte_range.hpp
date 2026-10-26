//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_BYTE_RANGE_HPP
#define BOOST_TAAR_CORE_BYTE_RANGE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace boost::taar {

/// An inclusive byte interval `[first, last]` of a representation.
struct byte_range
{
    std::uint64_t first = 0;    ///< Offset of the first byte.
    std::uint64_t last = 0;     ///< Offset of the last byte (inclusive).

    /// Number of bytes in the range.
    constexpr std::uint64_t length() const noexcept { return last - first + 1; }

    friend constexpr bool operator==(byte_range const&, byte_range const&) = default;
};

/// Outcome of `parse_range`.
enum class range_status
{
    ignored,        ///< Malformed, another unit, or too many ranges: serve the whole representation (200).
    satisfiable,    ///< At least one range overlaps the representation (206).
    unsatisfiable,  ///< Valid syntax but no range overlaps the representation (416).
};

/// The result of `parse_range`.
struct range_set
{
    range_status status = range_status::ignored;    ///< What to answer.
    std::vector<byte_range> ranges;                 ///< Satisfiable ranges, clipped, in request order.
};

/** Parse a `Range` header value against a representation of @a size bytes (RFC 9110 14.1.2).

    Supports the `bytes` unit (case-insensitive) with `first-last`,
    `first-` and suffix `-length` specs, comma separated with optional
    whitespace. Ranges extending past the end are clipped; ranges starting
    at or after the end, and `-0`, are dropped as unsatisfiable. Large
    numbers saturate instead of overflowing.

    @param header     The field value, e.g. `bytes=0-499,-100`.
    @param size       The representation length in bytes.
    @param max_ranges More range specs than this make the header ignored,
                      a defence against range amplification.

    @returns `ignored` for a malformed header, another unit, a spec with
             `last < first`, or too many specs; `unsatisfiable` when no spec
             overlaps; otherwise `satisfiable` with the clipped ranges.
*/
inline range_set parse_range(std::string_view header, std::uint64_t size, std::size_t max_ranges = 16)
{
    range_set out;
    auto is_ws = [](char c) { return c == ' ' || c == '\t'; };
    auto trim = [&](std::string_view s)
    {
        while (!s.empty() && is_ws(s.front())) s.remove_prefix(1);
        while (!s.empty() && is_ws(s.back())) s.remove_suffix(1);
        return s;
    };
    auto number = [](std::string_view s, std::uint64_t& v)
    {
        if (s.empty()) return false;
        v = 0;
        constexpr auto max = (std::numeric_limits<std::uint64_t>::max)();
        for (char const c : s)
        {
            if (c < '0' || c > '9') return false;
            auto const d = static_cast<std::uint64_t>(c - '0');
            v = (v > (max - d) / 10) ? max : v * 10 + d;
        }
        return true;
    };

    header = trim(header);
    if (header.size() < 6) return out;
    for (std::size_t i = 0; i < 5; ++i)
    {
        char c = header[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != "bytes"[i]) return out;
    }
    std::string_view set = trim(header.substr(5));
    if (set.empty() || set.front() != '=') return out;
    set.remove_prefix(1);

    std::size_t specs = 0;
    std::vector<byte_range> ranges;
    while (true)
    {
        auto const comma = set.find(',');
        auto const spec = trim(set.substr(0, comma));
        if (!spec.empty())
        {
            if (++specs > max_ranges) return {};
            auto const dash = spec.find('-');
            if (dash == std::string_view::npos) return {};
            auto const a = spec.substr(0, dash);
            auto const b = spec.substr(dash + 1);
            std::uint64_t first = 0, last = 0;
            if (a.empty())
            {
                if (!number(b, last)) return {};
                if (last > 0 && size > 0)
                {
                    ranges.push_back({size - (std::min)(last, size), size - 1});
                }
            }
            else
            {
                if (!number(a, first)) return {};
                if (b.empty())
                {
                    last = (std::numeric_limits<std::uint64_t>::max)();
                }
                else if (!number(b, last) || last < first)
                {
                    return {};
                }
                if (first < size)
                {
                    ranges.push_back({first, (std::min)(last, size - 1)});
                }
            }
        }
        if (comma == std::string_view::npos) break;
        set.remove_prefix(comma + 1);
    }
    if (specs == 0) return {};
    out.status = ranges.empty() ? range_status::unsatisfiable : range_status::satisfiable;
    out.ranges = std::move(ranges);
    return out;
}

/** Sort @a ranges and merge the ones that overlap or touch.

    `0-9,5-14` becomes `0-14` and `0-4,5-9` becomes `0-9`, which RFC 9110
    allows a server to do before answering `multipart/byteranges`.
*/
inline std::vector<byte_range> coalesce_ranges(std::vector<byte_range> ranges)
{
    std::ranges::sort(ranges, {}, &byte_range::first);
    std::vector<byte_range> out;
    for (auto const& r : ranges)
    {
        if (!out.empty() && (out.back().last == (std::numeric_limits<std::uint64_t>::max)() ||
            r.first <= out.back().last + 1))
        {
            out.back().last = (std::max)(out.back().last, r.last);
        }
        else
        {
            out.push_back(r);
        }
    }
    return out;
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_BYTE_RANGE_HPP
