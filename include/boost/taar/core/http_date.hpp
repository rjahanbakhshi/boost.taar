//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_HTTP_DATE_HPP
#define BOOST_TAAR_CORE_HTTP_DATE_HPP

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace boost::taar {

/// @cond
namespace detail {

inline constexpr std::array<std::string_view, 7> http_date_days {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
inline constexpr std::array<std::string_view, 7> http_date_long_days {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
inline constexpr std::array<std::string_view, 12> http_date_months {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/// Reads exactly @a n digits at @a pos; advances @a pos.
inline bool http_date_digits(std::string_view s, std::size_t& pos, std::size_t n, int& out) noexcept
{
    if (s.size() < pos + n) return false;
    int v = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        char const c = s[pos + i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    pos += n;
    out = v;
    return true;
}

inline bool http_date_literal(std::string_view s, std::size_t& pos, std::string_view lit) noexcept
{
    if (s.substr(pos, lit.size()) != lit) return false;
    pos += lit.size();
    return true;
}

inline bool http_date_month(std::string_view s, std::size_t& pos, int& month) noexcept
{
    for (std::size_t i = 0; i < http_date_months.size(); ++i)
    {
        if (s.substr(pos, 3) == http_date_months[i])
        {
            pos += 3;
            month = static_cast<int>(i) + 1;
            return true;
        }
    }
    return false;
}

/// "HH:MM:SS"
inline bool http_date_time(std::string_view s, std::size_t& pos, int& h, int& m, int& sec) noexcept
{
    return http_date_digits(s, pos, 2, h) && http_date_literal(s, pos, ":") &&
        http_date_digits(s, pos, 2, m) && http_date_literal(s, pos, ":") &&
        http_date_digits(s, pos, 2, sec) && h < 24 && m < 60 && sec <= 60;
}

inline std::optional<std::chrono::sys_seconds> http_date_make(int y, int mo, int d, int h, int mi, int s) noexcept
{
    using namespace std::chrono;
    year_month_day const ymd {year {y}, month {static_cast<unsigned>(mo)}, day {static_cast<unsigned>(d)}};
    if (!ymd.ok()) return std::nullopt;
    return sys_days {ymd} + hours {h} + minutes {mi} + seconds {s};
}

} // namespace detail
/// @endcond

/** Format @a tp as an IMF-fixdate (RFC 9110 5.6.7), e.g.
    `Sun, 06 Nov 1994 08:49:37 GMT`. Sub-second precision is truncated.
*/
inline std::string format_http_date(std::chrono::system_clock::time_point tp)
{
    using namespace std::chrono;
    auto const secs = floor<seconds>(tp);
    auto const days = floor<std::chrono::days>(secs);
    year_month_day const ymd {days};
    hh_mm_ss<seconds> const hms {secs - days};
    weekday const wd {days};

    auto two = [](std::string& out, unsigned v)
    {
        out += static_cast<char>('0' + v / 10 % 10);
        out += static_cast<char>('0' + v % 10);
    };

    std::string out;
    out.reserve(29);
    out += detail::http_date_days[wd.c_encoding()];
    out += ", ";
    two(out, static_cast<unsigned>(ymd.day()));
    out += ' ';
    out += detail::http_date_months[static_cast<unsigned>(ymd.month()) - 1];
    out += ' ';
    int const y = static_cast<int>(ymd.year());
    two(out, static_cast<unsigned>(y / 100));
    two(out, static_cast<unsigned>(y % 100));
    out += ' ';
    two(out, static_cast<unsigned>(hms.hours().count()));
    out += ':';
    two(out, static_cast<unsigned>(hms.minutes().count()));
    out += ':';
    two(out, static_cast<unsigned>(hms.seconds().count()));
    out += " GMT";
    return out;
}

/** Parse an HTTP-date (RFC 9110 5.6.7).

    Accepts the preferred IMF-fixdate (`Sun, 06 Nov 1994 08:49:37 GMT`)
    and the two obsolete forms a recipient must accept: RFC 850
    (`Sunday, 06-Nov-94 08:49:37 GMT`) and asctime
    (`Sun Nov  6 08:49:37 1994`). A two-digit RFC 850 year that would lie
    more than 50 years in the future is taken as the most recent past year
    with the same last two digits. The day name is checked for form, not
    for consistency with the date.

    @returns The instant, or `std::nullopt` if @a text is not an HTTP-date.
*/
inline std::optional<std::chrono::sys_seconds> parse_http_date(std::string_view text) noexcept
{
    using namespace detail;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;

    // IMF-fixdate: "Sun, 06 Nov 1994 08:49:37 GMT"
    if (text.size() == 29 && text[3] == ',')
    {
        std::size_t pos = 0;
        bool day_ok = false;
        for (auto const name : http_date_days) day_ok = day_ok || text.starts_with(name);
        pos = 3;
        if (day_ok &&
            http_date_literal(text, pos, ", ") && http_date_digits(text, pos, 2, d) &&
            http_date_literal(text, pos, " ") && http_date_month(text, pos, mo) &&
            http_date_literal(text, pos, " ") && http_date_digits(text, pos, 4, y) &&
            http_date_literal(text, pos, " ") && http_date_time(text, pos, h, mi, s) &&
            http_date_literal(text, pos, " GMT") && pos == text.size())
        {
            return http_date_make(y, mo, d, h, mi, s);
        }
        return std::nullopt;
    }

    // RFC 850: "Sunday, 06-Nov-94 08:49:37 GMT"
    if (auto const comma = text.find(','); comma != std::string_view::npos && comma > 3)
    {
        bool day_ok = false;
        for (auto const name : http_date_long_days) day_ok = day_ok || text.substr(0, comma) == name;
        std::size_t pos = comma;
        int yy = 0;
        if (day_ok &&
            http_date_literal(text, pos, ", ") && http_date_digits(text, pos, 2, d) &&
            http_date_literal(text, pos, "-") && http_date_month(text, pos, mo) &&
            http_date_literal(text, pos, "-") && http_date_digits(text, pos, 2, yy) &&
            http_date_literal(text, pos, " ") && http_date_time(text, pos, h, mi, s) &&
            http_date_literal(text, pos, " GMT") && pos == text.size())
        {
            using namespace std::chrono;
            int const now_year = static_cast<int>(
                year_month_day {floor<days>(system_clock::now())}.year());
            y = now_year / 100 * 100 + yy;
            if (y > now_year + 50) y -= 100;
            return http_date_make(y, mo, d, h, mi, s);
        }
        return std::nullopt;
    }

    // asctime: "Sun Nov  6 08:49:37 1994"
    if (text.size() == 24 && text[3] == ' ')
    {
        bool day_ok = false;
        for (auto const name : http_date_days) day_ok = day_ok || text.starts_with(name);
        std::size_t pos = 3;
        if (!day_ok ||
            !http_date_literal(text, pos, " ") || !http_date_month(text, pos, mo) ||
            !http_date_literal(text, pos, " "))
        {
            return std::nullopt;
        }
        if (text[pos] == ' ')
        {
            ++pos;
            if (!http_date_digits(text, pos, 1, d)) return std::nullopt;
        }
        else if (!http_date_digits(text, pos, 2, d))
        {
            return std::nullopt;
        }
        if (http_date_literal(text, pos, " ") && http_date_time(text, pos, h, mi, s) &&
            http_date_literal(text, pos, " ") && http_date_digits(text, pos, 4, y) &&
            pos == text.size())
        {
            return http_date_make(y, mo, d, h, mi, s);
        }
    }
    return std::nullopt;
}

/// @cond
namespace detail {

/** The current time as an IMF-fixdate, formatted at most once per second
    per thread.

    The cache is thread-local, so connections served by several threads
    neither share nor lock it. The view stays valid until the next call
    on the same thread; copy it (as `fields::set` does) before then.
*/
inline std::string_view cached_http_date()
{
    struct cache
    {
        std::chrono::sys_seconds second {};
        std::string text;
    };
    thread_local cache c;
    auto const now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    if (c.text.empty() || now != c.second)
    {
        c.second = now;
        c.text = format_http_date(now);
    }
    return c.text;
}

} // namespace detail
/// @endcond

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_HTTP_DATE_HPP
