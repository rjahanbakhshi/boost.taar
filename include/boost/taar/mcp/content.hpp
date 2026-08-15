//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MCP_CONTENT_HPP
#define BOOST_TAAR_MCP_CONTENT_HPP

#include <boost/taar/core/base64.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Typed results for MCP tool and resource callables. Returning one of these
// types instead of a plain value lets a tool emit images, audio or embedded
// resources, several content items, or an explicit isError flag; a resource
// callable can return a blob instead of text. Each converts to the protocol's
// JSON shape through boost::json::value_from.

namespace boost::taar::mcp {

/// A text content item.
struct text
{
    std::string value;
};

/// An image content item; `data` is base64.
struct image
{
    std::string data;
    std::string mime_type;

    /// Build from raw bytes.
    static image from_bytes(std::string_view bytes, std::string mime_type)
    {
        return {base64_encode(bytes), std::move(mime_type)};
    }
};

/// An audio content item; `data` is base64.
struct audio
{
    std::string data;
    std::string mime_type;

    static audio from_bytes(std::string_view bytes, std::string mime_type)
    {
        return {base64_encode(bytes), std::move(mime_type)};
    }
};

/// Binary resource contents; `data` is base64. Also usable as a resource callable's result.
struct blob
{
    std::string data;
    std::string mime_type;

    static blob from_bytes(std::string_view bytes, std::string mime_type)
    {
        return {base64_encode(bytes), std::move(mime_type)};
    }
};

/// A resource embedded in a tool result: text or blob contents under a URI.
struct embedded_resource
{
    std::string uri;
    std::string mime_type;
    std::string text {};                    ///< Used when `data` is empty.
    std::optional<std::string> data {};     ///< Base64 bytes; takes precedence over `text`.
};

/// Any one content item.
using content_item = std::variant<text, image, audio, embedded_resource>;

/// A complete `tools/call` result: content items, optional structured content, error flag.
struct tool_result
{
    std::vector<content_item> content;
    std::optional<boost::json::value> structured {};
    bool is_error = false;

    tool_result& add(content_item item)
    {
        content.push_back(std::move(item));
        return *this;
    }
};

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& v, text const& t)
{
    v = boost::json::object {{"type", "text"}, {"text", t.value}};
}

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& v, image const& i)
{
    v = boost::json::object {{"type", "image"}, {"data", i.data}, {"mimeType", i.mime_type}};
}

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& v, audio const& a)
{
    v = boost::json::object {{"type", "audio"}, {"data", a.data}, {"mimeType", a.mime_type}};
}

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& v, blob const& b)
{
    v = boost::json::object {{"blob", b.data}, {"mimeType", b.mime_type}};
}

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& v, embedded_resource const& r)
{
    boost::json::object resource {{"uri", r.uri}, {"mimeType", r.mime_type}};
    if (r.data) resource["blob"] = *r.data;
    else resource["text"] = r.text;
    v = boost::json::object {{"type", "resource"}, {"resource", std::move(resource)}};
}

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& v, content_item const& c)
{
    std::visit([&](auto const& item) { v = boost::json::value_from(item); }, c);
}

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& v, tool_result const& r)
{
    boost::json::array content;
    for (auto const& c : r.content) content.push_back(boost::json::value_from(c));
    boost::json::object out;
    out["content"] = std::move(content);
    if (r.structured) out["structuredContent"] = *r.structured;
    out["isError"] = r.is_error;
    v = std::move(out);
}

} // namespace boost::taar::mcp

#endif // BOOST_TAAR_MCP_CONTENT_HPP
