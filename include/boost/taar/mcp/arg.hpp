//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_MCP_ARG_HPP
#define BOOST_TAAR_MCP_ARG_HPP

#include <algorithm>
#include <cctype>
#include <boost/taar/core/error.hpp>
#include <boost/taar/jsonrpc/envelope.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
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

    taar::result<boost::json::value> operator()(
        tool_call const& c,
        matcher::context const&) const
    {
        auto const* v = c.arguments.if_contains(name_);
        if (v == nullptr)
        {
            return taar::failure {taar::error::argument_not_found};
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

    taar::result<std::string> operator()(
        resource_call const& c,
        matcher::context const&) const
    {
        auto it = c.args.find(name_);
        if (it == c.args.end())
        {
            return taar::failure {taar::error::argument_not_found};
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

namespace detail {

struct uri_var
{
    std::string name;
    std::size_t max_length = 0;     ///< `{name:3}` prefix modifier; 0 for none.
    bool explode = false;           ///< `{name*}`.
};

struct uri_expression
{
    char op = 0;                    ///< 0, '+', '#', '.', '/', ';', '?' or '&'.
    std::vector<uri_var> vars;
};

inline bool parse_uri_expression(std::string_view body, uri_expression& out)
{
    out = {};
    if (body.empty()) return false;
    if (std::string_view {"+#./;?&"}.find(body.front()) != std::string_view::npos)
    {
        out.op = body.front();
        body.remove_prefix(1);
    }
    while (!body.empty())
    {
        auto const comma = body.find(',');
        auto spec = body.substr(0, comma);
        body = comma == std::string_view::npos ? std::string_view {} : body.substr(comma + 1);
        uri_var var;
        if (!spec.empty() && spec.back() == '*')
        {
            var.explode = true;
            spec.remove_suffix(1);
        }
        else if (auto const colon = spec.find(':'); colon != std::string_view::npos)
        {
            for (char c : spec.substr(colon + 1))
            {
                if (c < '0' || c > '9') return false;
                var.max_length = var.max_length * 10 + static_cast<std::size_t>(c - '0');
            }
            spec = spec.substr(0, colon);
        }
        if (spec.empty()) return false;
        // varname: ALPHA / DIGIT / "_" / "." / pct-encoded
        for (std::size_t i = 0; i < spec.size(); ++i)
        {
            unsigned char const c = static_cast<unsigned char>(spec[i]);
            if (c == '%')
            {
                if (i + 2 >= spec.size() || !std::isxdigit(static_cast<unsigned char>(spec[i + 1])) ||
                    !std::isxdigit(static_cast<unsigned char>(spec[i + 2])))
                {
                    return false;
                }
                i += 2;
            }
            else if (!std::isalnum(c) && c != '_' && c != '.')
            {
                return false;
            }
        }
        var.name = std::string {spec};
        out.vars.push_back(std::move(var));
    }
    return !out.vars.empty();
}

/// Characters a value may contain under @a op (anything but the delimiters the operator reserves).
inline bool uri_value_char(char op, char c, bool explode) noexcept
{
    switch (op)
    {
    case '+': case '#': return true;
    case '/': return explode ? c != '?' && c != '#' : c != '/' && c != '?' && c != '#';
    case '.': return c != '.' && c != '/' && c != '?' && c != '#';
    case ';': return c != ';' && c != '/' && c != '?' && c != '#' && c != '&';
    case '?': case '&': return c != '&' && c != '#';
    default: return c != '/' && c != '?' && c != '#' && c != ',';
    }
}

} // namespace detail

/** Match @a uri against an RFC 6570 URI template (levels 1 to 3, plus the
    `*` and `:n` modifiers of level 4 in their matching-relevant form).

    Supported expressions: `{var}` (one or more characters other than
    `/`, `?`, `#` and `,`), `{+var}` (reserved expansion: any characters,
    so paths with slashes), `{#var}` (fragment), `{.var}` (dot label),
    `{/var}` (path segment), `{;var}` (path parameter), `{?a,b}` and
    `{&a,b}` (query parameters, in any order; others are ignored). Several
    variables may share an expression (`{a,b}`); the prefixed forms are
    optional, so `calc://x{?format}` matches `calc://x` with no `format`
    argument, and `{;a,b}` / `{?a,b}` accept their pairs in any order. `{var*}` lets a `/` or `.` variable span several segments.
    `{var:3}` caps the value's length. Literal runs must match exactly.

    @returns The captured variables (absent optional ones are left out),
             or `std::nullopt` on mismatch or on a malformed template.
*/
inline std::optional<std::unordered_map<std::string, std::string>> match_uri_template(
    std::string_view tmpl,
    std::string_view uri)
{
    std::unordered_map<std::string, std::string> args;
    std::size_t upos = 0;
    std::size_t tpos = 0;

    // The literal text following the current expression, up to the next one.
    auto next_literal = [&](std::size_t from) -> std::string_view
    {
        auto const brace = tmpl.find('{', from);
        return tmpl.substr(from, brace == std::string_view::npos ? std::string_view::npos : brace - from);
    };
    // Where the value starting at @a from may extend to at most.
    auto limit_for = [&](std::size_t from, std::string_view literal) -> std::size_t
    {
        if (literal.empty()) return uri.size();
        auto const found = uri.find(literal, from);
        return found == std::string_view::npos ? uri.size() : found;
    };
    auto take = [&](std::size_t from, std::size_t limit, char op, bool explode) -> std::size_t
    {
        std::size_t end = from;
        while (end < limit && detail::uri_value_char(op, uri[end], explode)) ++end;
        return end;
    };
    auto fits = [](detail::uri_var const& var, std::size_t length)
    {
        return var.max_length == 0 || length <= var.max_length;
    };

    while (tpos < tmpl.size())
    {
        if (tmpl[tpos] != '{')
        {
            auto const literal = next_literal(tpos);
            if (uri.substr(upos, literal.size()) != literal) return std::nullopt;
            upos += literal.size();
            tpos += literal.size();
            continue;
        }

        auto const close = tmpl.find('}', tpos);
        if (close == std::string_view::npos) return std::nullopt;
        detail::uri_expression expr;
        if (!detail::parse_uri_expression(tmpl.substr(tpos + 1, close - tpos - 1), expr)) return std::nullopt;
        tpos = close + 1;
        auto const literal = next_literal(tpos);
        auto limit = limit_for(upos, literal);
        if (literal.empty() && tpos < tmpl.size())
        {
            // Directly followed by another expression: a prefixed operator
            // there ends this value at its prefix character (`{name}{.ext}`).
            detail::uri_expression following;
            auto const following_close = tmpl.find('}', tpos);
            if (following_close != std::string_view::npos &&
                detail::parse_uri_expression(tmpl.substr(tpos + 1, following_close - tpos - 1), following) &&
                following.op != 0 && following.op != '+')
            {
                auto const at = following.op == '.' ? uri.rfind('.', limit == 0 ? 0 : limit - 1) : uri.find(following.op, upos);
                if (at != std::string_view::npos && at >= upos && at < limit) limit = at;
            }
        }

        switch (expr.op)
        {
        case 0:
        case '+':
        {
            for (std::size_t i = 0; i < expr.vars.size(); ++i)
            {
                auto const& var = expr.vars[i];
                bool const last = i + 1 == expr.vars.size();
                std::size_t end = take(upos, limit, expr.op, var.explode);
                if (expr.op == '+' && !last)
                {
                    // Reserved values may contain commas; the separator is the first one.
                    auto const comma = uri.find(',', upos);
                    end = comma == std::string_view::npos || comma > limit ? limit : comma;
                }
                if (end == upos || !fits(var, end - upos)) return std::nullopt;
                args[var.name] = std::string {uri.substr(upos, end - upos)};
                upos = end;
                if (!last)
                {
                    if (upos >= uri.size() || uri[upos] != ',') return std::nullopt;
                    ++upos;
                }
            }
            break;
        }
        case '#':
        {
            if (upos < uri.size() && uri[upos] == '#')
            {
                ++upos;
                for (std::size_t i = 0; i < expr.vars.size(); ++i)
                {
                    auto const& var = expr.vars[i];
                    bool const last = i + 1 == expr.vars.size();
                    std::size_t end = limit;
                    if (!last)
                    {
                        auto const comma = uri.find(',', upos);
                        end = comma == std::string_view::npos || comma > limit ? limit : comma;
                    }
                    if (!fits(var, end - upos)) return std::nullopt;
                    args[var.name] = std::string {uri.substr(upos, end - upos)};
                    upos = end;
                    if (!last && upos < uri.size() && uri[upos] == ',') ++upos;
                }
            }
            break;
        }
        case '.':
        case '/':
        {
            for (auto const& var : expr.vars)
            {
                if (upos >= limit || uri[upos] != expr.op) break;      // optional
                std::size_t const end = take(upos + 1, limit, expr.op, var.explode);
                if (end == upos + 1 || !fits(var, end - upos - 1)) return std::nullopt;
                args[var.name] = std::string {uri.substr(upos + 1, end - upos - 1)};
                upos = end;
            }
            break;
        }
        case ';':
        {
            // `;name=value` or `;name` pairs, in any order; each must name a
            // variable of this expression.
            while (upos < limit && uri[upos] == ';')
            {
                std::size_t const end = take(upos + 1, limit, ';', false);
                auto const pair = uri.substr(upos + 1, end - upos - 1);
                auto const eq = pair.find('=');
                auto const key = pair.substr(0, eq);
                auto const value = eq == std::string_view::npos ? std::string_view {} : pair.substr(eq + 1);
                auto const var = std::ranges::find_if(expr.vars, [&](auto const& v) { return v.name == key; });
                if (var == expr.vars.end() || !fits(*var, value.size())) return std::nullopt;
                args[var->name] = std::string {value};
                upos = end;
            }
            break;
        }
        case '?':
        case '&':
        {
            if (upos < limit && uri[upos] == expr.op)
            {
                std::size_t pos = upos + 1;
                while (pos < limit)
                {
                    std::size_t const amp = (std::min)(limit, uri.find('&', pos) == std::string_view::npos ? limit : uri.find('&', pos));
                    auto const pair = uri.substr(pos, amp - pos);
                    auto const eq = pair.find('=');
                    auto const key = pair.substr(0, eq);
                    auto const value = eq == std::string_view::npos ? std::string_view {} : pair.substr(eq + 1);
                    for (auto const& var : expr.vars)
                    {
                        if (var.name != key) continue;
                        if (!fits(var, value.size())) return std::nullopt;
                        auto [it, inserted] = args.emplace(var.name, std::string {value});
                        if (!inserted) it->second += "," + std::string {value};    // repeated key
                    }
                    pos = amp < limit ? amp + 1 : limit;
                }
                upos = limit;
            }
            break;
        }
        default:
            return std::nullopt;
        }
    }

    if (upos != uri.size()) return std::nullopt;
    return args;
}

} // namespace boost::taar::mcp

#endif // BOOST_TAAR_MCP_ARG_HPP
