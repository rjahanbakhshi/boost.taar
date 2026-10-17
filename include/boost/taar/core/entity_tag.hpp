//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_ENTITY_TAG_HPP
#define BOOST_TAAR_CORE_ENTITY_TAG_HPP

#include <optional>
#include <string_view>

namespace boost::taar {

/** A parsed entity tag (RFC 9110 8.8.3): `"xyz"` or `W/"xyz"`.

    The view refers into the parsed text.
*/
struct entity_tag
{
    bool weak = false;              ///< True for a `W/` tag.
    std::string_view opaque;        ///< The opaque tag without quotes.
};

/** Parse one entity tag from the start of @a text.

    Leading whitespace is skipped. On success @a text is advanced past the
    tag. The opaque part may contain any etagc character (`!`, `#`-`~` and
    obs-text) but no `"`.

    @returns The tag, or `std::nullopt` if @a text does not start with one.
*/
inline std::optional<entity_tag> parse_entity_tag(std::string_view& text) noexcept
{
    std::string_view s = text;
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    entity_tag tag;
    if (s.starts_with("W/"))
    {
        tag.weak = true;
        s.remove_prefix(2);
    }
    if (s.empty() || s.front() != '"') return std::nullopt;
    s.remove_prefix(1);
    std::size_t i = 0;
    for (; i < s.size() && s[i] != '"'; ++i)
    {
        auto const c = static_cast<unsigned char>(s[i]);
        if (c != 0x21 && (c < 0x23 || c == 0x7f)) return std::nullopt;
    }
    if (i == s.size()) return std::nullopt;
    tag.opaque = s.substr(0, i);
    text = s.substr(i + 1);
    return tag;
}

/** Strong comparison: both tags are strong and their opaque parts equal. */
inline bool strong_match(entity_tag const& a, entity_tag const& b) noexcept
{
    return !a.weak && !b.weak && a.opaque == b.opaque;
}

/** Weak comparison: the opaque parts are equal, whatever the weakness. */
inline bool weak_match(entity_tag const& a, entity_tag const& b) noexcept
{
    return a.opaque == b.opaque;
}

/** Evaluate an `If-Match` / `If-None-Match` field value against @a current.

    @param field   The field value: `*` or a comma-separated list of entity tags.
    @param current The current representation's entity tag as sent in
                   `ETag` (with quotes, optionally `W/`).
    @param weak    Use weak comparison (`If-None-Match`) instead of strong
                   (`If-Match`).

    @returns True if the field is `*` or any listed tag matches. A
             malformed list (or a malformed @a current) never matches.
*/
inline bool etag_list_matches(std::string_view field, std::string_view current, bool weak) noexcept
{
    auto const cur = parse_entity_tag(current);
    if (!cur) return false;
    auto trim = [](std::string_view s)
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
        return s;
    };
    field = trim(field);
    if (field == "*") return true;
    bool matched = false;
    while (true)
    {
        field = trim(field);
        if (field.empty()) break;
        if (field.front() == ',')
        {
            field.remove_prefix(1);
            continue;
        }
        auto const tag = parse_entity_tag(field);
        if (!tag) return false;
        matched = matched || (weak ? weak_match(*tag, *cur) : strong_match(*tag, *cur));
        field = trim(field);
        if (!field.empty() && field.front() != ',') return false;
    }
    return matched;
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_ENTITY_TAG_HPP
