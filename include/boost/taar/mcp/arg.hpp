//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MCP_ARG_HPP
#define BOOST_TAAR_MCP_ARG_HPP

#include <boost/taar/core/error.hpp>
#include <boost/taar/jsonrpc/envelope.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/system/result.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace boost::taar::mcp {

/// What an MCP tool callable's argument providers receive.
struct tool_call
{
    jsonrpc::envelope const& env;
    boost::json::object const& arguments;
    boost::beast::http::request<boost::beast::http::string_body> const& request;
};

/// What an MCP resource callable's argument providers receive.
struct resource_call
{
    jsonrpc::envelope const& env;
    std::string const& uri;
    std::unordered_map<std::string, std::string> const& args;
    boost::beast::http::request<boost::beast::http::string_body> const& request;
};

/** Argument provider reading one member of `params.arguments` in a `tools/call`.

    Converted to the callable's parameter type through `rest_arg_cast`.
    The description feeds the tool's `inputSchema`.
*/
struct mcp_arg
{
    explicit mcp_arg(std::string name, std::string description = {})
        : name_ {std::move(name)}
        , description_ {std::move(description)}
    {}

    std::string const& name() const noexcept { return name_; }
    std::string const& description() const noexcept { return description_; }

    boost::system::result<boost::json::value> operator()(
        tool_call const& c,
        matcher::context const&) const
    {
        auto const* v = c.arguments.if_contains(name_);
        if (v == nullptr)
        {
            return taar::error::argument_not_found;
        }
        return *v;
    }

private:
    std::string name_;
    std::string description_;
};

/** Argument provider reading a `{name}` placeholder of a resource URI template. */
struct uri_arg
{
    explicit uri_arg(std::string name, std::string description = {})
        : name_ {std::move(name)}
        , description_ {std::move(description)}
    {}

    std::string const& name() const noexcept { return name_; }
    std::string const& description() const noexcept { return description_; }

    boost::system::result<std::string> operator()(
        resource_call const& c,
        matcher::context const&) const
    {
        auto it = c.args.find(name_);
        if (it == c.args.end())
        {
            return taar::error::argument_not_found;
        }
        return it->second;
    }

private:
    std::string name_;
    std::string description_;
};

/// True if @a uri contains a `{name}` placeholder.
inline bool is_uri_template(std::string_view uri) noexcept
{
    return uri.find('{') != std::string_view::npos;
}

/** Match @a uri against a URI template with `{name}` placeholders.

    Literal runs must match exactly; a placeholder captures one or more
    characters up to the next literal run (or the end of the URI).

    @returns The captured placeholders, or `std::nullopt` on mismatch.
*/
inline std::optional<std::unordered_map<std::string, std::string>> match_uri_template(
    std::string_view tmpl,
    std::string_view uri)
{
    std::unordered_map<std::string, std::string> args;
    std::size_t upos = 0;
    std::size_t tpos = 0;

    while (tpos < tmpl.size())
    {
        if (tmpl[tpos] != '{')
        {
            auto const next = tmpl.find('{', tpos);
            auto const literal = tmpl.substr(tpos, next == std::string_view::npos ? std::string_view::npos : next - tpos);
            if (uri.substr(upos, literal.size()) != literal)
            {
                return std::nullopt;
            }
            upos += literal.size();
            tpos += literal.size();
            continue;
        }

        auto const close = tmpl.find('}', tpos);
        if (close == std::string_view::npos)
        {
            return std::nullopt;
        }
        auto const name = tmpl.substr(tpos + 1, close - tpos - 1);
        tpos = close + 1;

        std::size_t end;
        if (tpos >= tmpl.size())
        {
            end = uri.size();
        }
        else
        {
            auto const next_brace = tmpl.find('{', tpos);
            auto const literal = tmpl.substr(tpos, next_brace == std::string_view::npos ? std::string_view::npos : next_brace - tpos);
            end = uri.find(literal, upos);
            if (end == std::string_view::npos)
            {
                return std::nullopt;
            }
        }
        if (end == upos)
        {
            return std::nullopt;
        }
        args.emplace(std::string {name}, std::string {uri.substr(upos, end - upos)});
        upos = end;
    }

    if (upos != uri.size())
    {
        return std::nullopt;
    }
    return args;
}

} // namespace boost::taar::mcp

#endif // BOOST_TAAR_MCP_ARG_HPP
