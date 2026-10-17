//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_ACCEPT_ENCODING_HPP
#define BOOST_TAAR_CORE_ACCEPT_ENCODING_HPP

#include <boost/beast/core/string.hpp>
#include <optional>
#include <string_view>

namespace boost::taar {

/// @cond
namespace detail {

/// Parse a qvalue ("0", "0.5", "1", "1.000"); nullopt when malformed.
inline std::optional<double> parse_qvalue(std::string_view s) noexcept
{
    if (s.empty() || s.size() > 5) return std::nullopt;
    if (s[0] != '0' && s[0] != '1') return std::nullopt;
    if (s.size() == 1) return s[0] - '0';
    if (s[1] != '.') return std::nullopt;
    // Exact thousandths, so "0.3" compares equal to 0.3.
    int thousandths = 0;
    int scale = 100;
    for (std::size_t i = 2; i < s.size(); ++i, scale /= 10)
    {
        if (s[i] < '0' || s[i] > '9') return std::nullopt;
        if (s[0] == '1' && s[i] != '0') return std::nullopt;
        thousandths += (s[i] - '0') * scale;
    }
    return s[0] == '1' ? 1.0 : thousandths / 1000.0;
}

} // namespace detail
/// @endcond

/** The quality an `Accept-Encoding` field value gives @a coding (RFC 9110 12.5.3).

    Codings compare case-insensitively; `x-gzip` is an alias of `gzip`.
    An explicit entry for @a coding wins over `*`. Elements with a
    malformed `q` are skipped. The caller decides what an absent field
    means.

    @param field  The `Accept-Encoding` field value, e.g. `br;q=1.0, gzip;q=0.5, *;q=0`.
    @param coding A content coding such as `br`, `gzip` or `identity`.

    @returns The q-value in `[0, 1]` from the entry naming @a coding, else
             from `*`, else `std::nullopt` (not mentioned).
*/
inline std::optional<double> accept_encoding_quality(std::string_view field, std::string_view coding) noexcept
{
    using boost::beast::iequals;
    auto trim = [](std::string_view s)
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
        return s;
    };
    auto same = [&](std::string_view a, std::string_view b)
    {
        auto norm = [](std::string_view x) { return iequals(x, "x-gzip") ? std::string_view {"gzip"} : x; };
        return iequals(norm(a), norm(b));
    };

    std::optional<double> exact;
    std::optional<double> star;
    while (!field.empty())
    {
        auto const comma = field.find(',');
        auto element = trim(field.substr(0, comma));
        field = comma == std::string_view::npos ? std::string_view {} : field.substr(comma + 1);
        if (element.empty()) continue;

        auto const semi = element.find(';');
        auto const name = trim(element.substr(0, semi));
        std::optional<double> q = 1.0;
        if (semi != std::string_view::npos)
        {
            // Parameters: only "q" is defined.
            auto params = element.substr(semi + 1);
            while (!params.empty())
            {
                auto const next = params.find(';');
                auto const param = trim(params.substr(0, next));
                params = next == std::string_view::npos ? std::string_view {} : params.substr(next + 1);
                auto const eq = param.find('=');
                if (eq == std::string_view::npos) continue;
                if (iequals(trim(param.substr(0, eq)), "q"))
                {
                    q = detail::parse_qvalue(trim(param.substr(eq + 1)));
                }
            }
        }
        if (!q) continue;
        if (name == "*") star = q;
        else if (same(name, coding)) exact = q;
    }
    return exact ? exact : star;
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_ACCEPT_ENCODING_HPP
