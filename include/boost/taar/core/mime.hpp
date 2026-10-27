//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_CORE_MIME_HPP
#define BOOST_TAAR_CORE_MIME_HPP

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace boost::taar {

/// @cond
namespace detail {

// Sorted by extension (lowercase, without the dot) for binary search.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 76> mime_table {{
    {"7z", "application/x-7z-compressed"},
    {"aac", "audio/aac"},
    {"appcache", "text/cache-manifest"},
    {"avi", "video/x-msvideo"},
    {"avif", "image/avif"},
    {"bmp", "image/bmp"},
    {"css", "text/css"},
    {"csv", "text/csv"},
    {"doc", "application/msword"},
    {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"eot", "application/vnd.ms-fontobject"},
    {"epub", "application/epub+zip"},
    {"flac", "audio/flac"},
    {"gif", "image/gif"},
    {"gz", "application/gzip"},
    {"htm", "text/html"},
    {"html", "text/html"},
    {"ico", "image/vnd.microsoft.icon"},
    {"ics", "text/calendar"},
    {"jar", "application/java-archive"},
    {"jpeg", "image/jpeg"},
    {"jpg", "image/jpeg"},
    {"js", "text/javascript"},
    {"json", "application/json"},
    {"jsonld", "application/ld+json"},
    {"m4a", "audio/mp4"},
    {"m4v", "video/mp4"},
    {"manifest", "text/cache-manifest"},
    {"map", "application/json"},
    {"md", "text/markdown"},
    {"mjs", "text/javascript"},
    {"mkv", "video/x-matroska"},
    {"mov", "video/quicktime"},
    {"mp3", "audio/mpeg"},
    {"mp4", "video/mp4"},
    {"mpeg", "video/mpeg"},
    {"mpg", "video/mpeg"},
    {"ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {"odt", "application/vnd.oasis.opendocument.text"},
    {"oga", "audio/ogg"},
    {"ogg", "audio/ogg"},
    {"ogv", "video/ogg"},
    {"opus", "audio/ogg"},
    {"otf", "font/otf"},
    {"pdf", "application/pdf"},
    {"png", "image/png"},
    {"ppt", "application/vnd.ms-powerpoint"},
    {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {"rar", "application/vnd.rar"},
    {"rtf", "application/rtf"},
    {"svg", "image/svg+xml"},
    {"tar", "application/x-tar"},
    {"tif", "image/tiff"},
    {"tiff", "image/tiff"},
    {"toml", "application/toml"},
    {"ts", "video/mp2t"},
    {"ttf", "font/ttf"},
    {"txt", "text/plain"},
    {"vcf", "text/vcard"},
    {"vtt", "text/vtt"},
    {"wasm", "application/wasm"},
    {"wav", "audio/wav"},
    {"weba", "audio/webm"},
    {"webm", "video/webm"},
    {"webmanifest", "application/manifest+json"},
    {"webp", "image/webp"},
    {"woff", "font/woff"},
    {"woff2", "font/woff2"},
    {"xhtml", "application/xhtml+xml"},
    {"xls", "application/vnd.ms-excel"},
    {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {"xml", "application/xml"},
    {"yaml", "application/yaml"},
    {"yml", "application/yaml"},
    {"zip", "application/zip"},
    {"zst", "application/zstd"},
}};

inline constexpr char mime_lower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline constexpr bool mime_table_sorted() noexcept
{
    for (std::size_t i = 1; i < mime_table.size(); ++i)
    {
        if (!(mime_table[i - 1].first < mime_table[i].first)) return false;
    }
    return true;
}

} // namespace detail
/// @endcond

/** The last extension of @a filename, lowercased and without the dot.

    Only the final path segment is considered: `a.b/c` has no extension,
    `archive.tar.gz` has `gz`, and a leading dot (`.profile`) does not
    start an extension. Returns an empty string when there is none.
*/
inline std::string file_extension(std::string_view filename)
{
    if (auto const slash = filename.find_last_of("/\\"); slash != std::string_view::npos)
    {
        filename.remove_prefix(slash + 1);
    }
    auto const dot = filename.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return {};
    std::string ext {filename.substr(dot + 1)};
    std::ranges::transform(ext, ext.begin(), detail::mime_lower);
    return ext;
}

/** Media type for @a filename from the built-in table, without parameters.

    Matching is case-insensitive on the last extension (see
    `file_extension`). The table covers web documents, scripts, styles,
    images, fonts, audio, video, archives and office formats; JavaScript is
    `text/javascript` (RFC 9239).

    @returns The media type, or an empty view for an unknown extension.
*/
inline std::string_view mime_type_for(std::string_view filename)
{
    static_assert(detail::mime_table_sorted());
    auto const ext = file_extension(filename);
    if (ext.empty()) return {};
    auto const it = std::ranges::lower_bound(detail::mime_table, std::string_view {ext},
        {}, &std::pair<std::string_view, std::string_view>::first);
    if (it != detail::mime_table.end() && it->first == ext) return it->second;
    return {};
}

/** True for media types whose content is text and benefits from a charset.

    `text/` types, `application/json`, `application/javascript`,
    `application/xml`, `application/manifest+json`, `application/ld+json`,
    `application/xhtml+xml`, `application/yaml`, `application/toml` and
    `image/svg+xml`. Parameters in @a type are ignored; comparison is
    case-insensitive.
*/
inline bool is_text_mime_type(std::string_view type) noexcept
{
    if (auto const semi = type.find(';'); semi != std::string_view::npos) type = type.substr(0, semi);
    while (!type.empty() && (type.back() == ' ' || type.back() == '\t')) type.remove_suffix(1);
    auto ieq = [](std::string_view a, std::string_view b)
    {
        return a.size() == b.size() &&
            std::ranges::equal(a, b, {}, detail::mime_lower, detail::mime_lower);
    };
    if (type.size() > 5 && ieq(type.substr(0, 5), "text/")) return true;
    for (std::string_view const t : {
        "application/json", "application/javascript", "application/xml",
        "application/manifest+json", "application/ld+json", "application/xhtml+xml",
        "application/yaml", "application/toml", "image/svg+xml"})
    {
        if (ieq(type, t)) return true;
    }
    return false;
}

/** @a type with `; charset=utf-8` appended when it is a text type
    (`is_text_mime_type`) that carries no parameter yet; otherwise
    @a type unchanged.
*/
inline std::string with_utf8_charset(std::string_view type)
{
    std::string out {type};
    if (type.find(';') == std::string_view::npos && is_text_mime_type(type))
    {
        out += "; charset=utf-8";
    }
    return out;
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_MIME_HPP
