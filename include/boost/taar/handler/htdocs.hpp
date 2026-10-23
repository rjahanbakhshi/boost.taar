//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_HTDOCS_HPP
#define BOOST_TAAR_HANDLER_HTDOCS_HPP

#include <boost/taar/core/byte_range.hpp>
#include <boost/taar/core/entity_tag.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/core/http_date.hpp>
#include <boost/taar/core/mime.hpp>
#include <boost/taar/core/segmented_body.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/beast/core/file.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/system/errc.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url_view.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace boost::taar::handler {

/** A `Cache-Control` rule of @ref htdocs_options::cache_rules.

    Rules are tried in order on the served file's decoded path relative to
    the root (no leading slash, e.g. `assets/app.3f9a1c2e.js`); the first
    whose @ref match returns true supplies the header value.

    @code
    options.cache_rules = {
        taar::handler::cache_rule::glob("assets/**", "public, max-age=86400"),
        {[](std::string_view p) { return p.ends_with(".html"); }, "no-cache"},
    };
    @endcode
*/
struct cache_rule
{
    /// Predicate on the relative path.
    std::function<bool(std::string_view)> match;

    /// The `Cache-Control` value sent when @ref match accepts the path.
    std::string value;

    /** A rule matching a glob pattern (see @ref cache_rules::glob_match).

        @param pattern `*` matches within a segment, `**` across segments,
                       `?` one character. A pattern without `/` is matched
                       against the file name only (`*.css` matches
                       `a/b/site.css`).
        @param value   The `Cache-Control` value.
    */
    static cache_rule glob(std::string pattern, std::string value);
};

/// Helpers for building @ref cache_rule lists.
namespace cache_rules {

/** True if @a path matches the glob @a pattern.

    `*` matches any run of characters except `/`, `**` any run including
    `/`, and `?` one character other than `/`; everything else matches
    itself (case-sensitive). A pattern containing no `/` is matched
    against the last segment of @a path only.
*/
inline bool glob_match(std::string_view pattern, std::string_view path)
{
    if (pattern.find('/') == std::string_view::npos)
    {
        if (auto const slash = path.rfind('/'); slash != std::string_view::npos) path.remove_prefix(slash + 1);
    }
    // Dynamic programming over path positions: O(pattern * path), no backtracking.
    // reach[j]: the pattern consumed so far matches path[0, j).
    std::vector<bool> reach(path.size() + 1, false), next(path.size() + 1);
    reach[0] = true;
    std::size_t p = 0;
    while (p < pattern.size())
    {
        std::fill(next.begin(), next.end(), false);
        if (pattern[p] == '*')
        {
            bool const cross = p + 1 < pattern.size() && pattern[p + 1] == '*';
            if (cross && p + 2 < pattern.size() && pattern[p + 2] == '/')
            {
                // "**/": zero or more whole segments.
                bool seen = false;
                for (std::size_t j = 0; j <= path.size(); ++j)
                {
                    next[j] = reach[j] || (seen && j > 0 && path[j - 1] == '/');
                    if (reach[j]) seen = true;
                }
                p += 3;
            }
            else
            {
                for (std::size_t j = 0; j <= path.size(); ++j)
                {
                    next[j] = reach[j] || (j > 0 && next[j - 1] && (cross || path[j - 1] != '/'));
                }
                p += cross ? 2 : 1;
            }
        }
        else
        {
            char const c = pattern[p];
            for (std::size_t j = 0; j < path.size(); ++j)
            {
                next[j + 1] = reach[j] && (c == '?' ? path[j] != '/' : path[j] == c);
            }
            ++p;
        }
        reach.swap(next);
    }
    return reach[path.size()];
}

/** True if the file name in @a path carries a content hash, as bundlers emit:
    `app.3f9a1c2e.js`, `chunk-5ZQ3X2AB.js`, `3f9a1c2e4b.css`.

    The name without its last extension is split at `.` and `-`; a part
    of at least 8 characters from `[A-Za-z0-9_]` containing both a digit
    and a letter counts as a hash. A heuristic: provide your own
    predicate when your names follow another scheme.
*/
inline bool looks_content_hashed(std::string_view path) noexcept
{
    if (auto const slash = path.rfind('/'); slash != std::string_view::npos) path.remove_prefix(slash + 1);
    if (auto const dot = path.rfind('.'); dot != std::string_view::npos && dot != 0) path = path.substr(0, dot);
    while (!path.empty())
    {
        auto const sep = path.find_first_of(".-");
        auto const part = path.substr(0, sep);
        path = sep == std::string_view::npos ? std::string_view {} : path.substr(sep + 1);
        if (part.size() < 8) continue;
        bool digit = false, alpha = false, valid = true;
        for (char const c : part)
        {
            if (c >= '0' && c <= '9') digit = true;
            else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) alpha = true;
            else if (c != '_') valid = false;
        }
        if (valid && digit && alpha) return true;
    }
    return false;
}

/** The usual policy for bundled front ends: content-hashed file names
    (@ref looks_content_hashed) are cached for a year as `immutable`, and
    HTML documents are revalidated on every use (`no-cache`).
*/
inline std::vector<cache_rule> immutable_hashed()
{
    return {
        {[](std::string_view p) { return looks_content_hashed(p); }, "public, max-age=31536000, immutable"},
        {[](std::string_view p) { return glob_match("*.html", p) || glob_match("*.htm", p); }, "no-cache"},
    };
}

} // namespace cache_rules

inline cache_rule cache_rule::glob(std::string pattern, std::string value)
{
    return {
        [pattern = std::move(pattern)](std::string_view p) { return cache_rules::glob_match(pattern, p); },
        std::move(value)};
}

/** Configuration of an @ref htdocs handler.

    Every field has a production-ready default; set only what differs.

    @code
    taar::handler::htdocs_options options;
    options.root = "./public";
    options.fallback_document = "index.html";          // single-page application
    session.register_request_handler(
        method == http::verb::get && target == "/{*path}",
        taar::handler::htdocs {std::move(options)});
    @endcode
*/
struct htdocs_options
{
    /// The document root. Requests never resolve outside it.
    std::filesystem::path root = ".";

    /// Files tried in order for a directory request ending in `/`.
    std::vector<std::string> index_files {"index.html"};

    /** The `path_args` entry naming the file, relative to @ref root.

        Empty (the default) selects the route's greedy capture: the `{*}`
        entry, or the only path argument when there is exactly one; with
        no path argument the whole request path (without the query) is
        used. Set it when the route has several placeholders.
    */
    std::string path_arg {};

    /// Serve files and directories whose name starts with `.` (`.well-known` is always served).
    bool serve_hidden = false;

    /// Serve symbolic links that resolve outside @ref root (refused with 404 by default).
    bool follow_symlinks_outside_root = false;

    /** A document, relative to @ref root, served for requests that match
        no file: the single-page application fallback.

        It answers a GET or HEAD whose path does not exist when
        @ref fallback_when says so (by default: the last path segment has
        no extension, so `/app/settings` falls back but `/app/logo.png`
        stays 404). The fallback is sent with `Cache-Control: no-cache`.
    */
    std::optional<std::string> fallback_document {};

    /// Decides, from the decoded relative path, whether a missing file falls back; empty: no extension in the last segment.
    std::function<bool(std::string_view)> fallback_when {};

    /** Extra or overriding media types: extension (with or without the
        leading dot, case-insensitive) to type. `; charset=utf-8` is
        appended to text types unless the value already has a parameter.
    */
    std::unordered_map<std::string, std::string> mime_types {};

    /// Media type for extensions neither @ref mime_types nor the built-in table know.
    std::string default_mime_type = "application/octet-stream";

    /// Send `X-Content-Type-Options: nosniff` with every served file.
    bool nosniff = true;

    /** `Cache-Control` rules, first match wins (see @ref cache_rule and
        @ref cache_rules::immutable_hashed). Empty by default.
    */
    std::vector<cache_rule> cache_rules {};

    /// `Cache-Control` for files no rule matches; empty (the default) sends none.
    std::string default_cache_control {};

    /** Most ranges honoured in one `Range` header; more make the request
        a plain 200. Overlapping and adjacent ranges are merged. 0 disables
        range requests (no `Accept-Ranges`).
    */
    std::size_t max_ranges = 16;

    /** Called for every served file (200, 206, 304) with the file's
        relative path and the response header, just before the response
        is returned: add `Content-Disposition`, security headers, and so
        on. It runs on the connection's thread and must be thread-safe.
    */
    std::function<void(std::string_view, boost::beast::http::response_header<>&)> on_response {};
};

/** A request handler that serves static files from a document root.

    Register it on a GET route, normally with a greedy placeholder; the
    session answers HEAD through the same route:

    @code
    session.register_request_handler(
        method == http::verb::get && target == "/static/{*path}",
        taar::handler::htdocs {"./public"});      // GET /static/app.css -> ./public/app.css
    @endcode

    @par Resolution and safety
    The file is the route's greedy capture (see
    @ref htdocs_options::path_arg), percent-decoded by Boost.URL; the
    query never takes part. A path with an encoded `/`, `\` or NUL, or a
    `.` or `..` segment, is answered 400. The resolved file must stay
    under the canonical root (symbolic links included) and hidden names
    are refused; both answer 404 so the tree cannot be probed. A
    directory without a trailing slash is redirected (301) to the slash
    form; with the slash, the first existing index file is served, else
    404 (no directory listings). Methods other than GET and HEAD answer
    405 with `Allow: GET, HEAD`. Error responses are short fixed
    `text/plain` texts that never echo the request.

    @par Responses
    Every response, errors included, is a typed
    `http::response<segmented_body>`, so the session's response filter
    and HEAD handling apply. The handler is cheap to copy; copies share
    their state and may be used from several threads at once.

    @par Blocking I/O
    File system calls (stat, open, read) are synchronous and run on the
    connection's thread. Serve from local disks, or keep large trees
    behind a dedicated server.
*/
class htdocs
{
public:
    /// The request type the handler reads (the body is never needed).
    using request_type = boost::beast::http::request<boost::beast::http::empty_body>;

    /// The response type of every answer.
    using response_type = boost::beast::http::response<segmented_body>;

    /** Construct from @a options.

        @throws boost::taar::exception (`errc::invalid_argument`) if an
                index file or the fallback document is not a plain
                relative path.
    */
    explicit htdocs(htdocs_options options)
        : state_ {std::make_shared<state>(std::move(options))}
    {}

    /** Construct with a document root and one index file (the original interface).

        @param htdocs_root Path to the document root. Defaults to the
                           current working directory.
        @param default_doc Index file for directory requests ending in
                           `/`. Defaults to `"index.html"`; empty disables
                           index files.
    */
    htdocs(
        std::string htdocs_root = "./",
        std::string default_doc = "index.html")
        : htdocs {legacy_options(std::move(htdocs_root), std::move(default_doc))}
    {}

    /// Serve @a request; @a context supplies the route's path arguments.
    response_type operator()(
        request_type const& request,
        boost::taar::matcher::context const& context) const
    {
        namespace http = boost::beast::http;
        try
        {
            return serve(request, context);
        }
        catch (std::filesystem::filesystem_error const&)
        {
            return error_response(request, http::status::internal_server_error);
        }
        catch (boost::system::system_error const&)
        {
            return error_response(request, http::status::internal_server_error);
        }
    }

    /// The options the handler was built with (normalised).
    htdocs_options const& options() const noexcept
    {
        return state_->options;
    }

    /** The configured @ref htdocs_options::path_arg, for the session's
        registration-time check against the route template; empty when
        the default selection is used.
    */
    std::vector<std::string> path_arg_names() const
    {
        if (state_->options.path_arg.empty()) return {};
        return {state_->options.path_arg};
    }

private:
/// @cond
    enum class outcome { file, redirect, not_found, forbidden, error };

    /// One representation of a file on disk: the file itself or a precompressed sibling.
    struct variant
    {
        std::string encoding;               // Content-Encoding; empty for the file itself
        std::filesystem::path path;
        std::uint64_t size = 0;
        std::filesystem::file_time_type mtime {};
        std::string etag;
    };

    struct file_info
    {
        std::string relative;               // decoded path relative to the root
        std::string content_type;
        std::optional<std::string> cache_control;
        std::string last_modified;          // HTTP-date
        std::chrono::sys_seconds last_modified_time {};
        std::vector<variant> variants;      // [0] is the file itself
    };

    struct lookup
    {
        outcome kind = outcome::not_found;
        std::shared_ptr<file_info const> file;
    };

    struct state
    {
        explicit state(htdocs_options opts)
            : options {std::move(opts)}
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            auto absolute = fs::absolute(options.root, ec);
            if (ec) absolute = options.root;
            root = fs::weakly_canonical(absolute, ec);
            if (ec) root = absolute.lexically_normal();

            for (auto const& index : options.index_files)
            {
                if (!is_plain_relative(index) || index.find('/') != std::string::npos)
                {
                    throw invalid_option("index file", index);
                }
            }
            if (options.fallback_document)
            {
                auto& doc = *options.fallback_document;
                while (doc.starts_with('/')) doc.erase(0, 1);
                if (!is_plain_relative(doc)) throw invalid_option("fallback document", doc);
            }

            std::unordered_map<std::string, std::string> mime;
            for (auto const& [ext, type] : options.mime_types)
            {
                std::string key = ext.starts_with('.') ? ext.substr(1) : ext;
                std::ranges::transform(key, key.begin(), [](char c)
                {
                    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                });
                mime[key] = type.find(';') == std::string::npos ? with_utf8_charset(type) : type;
            }
            options.mime_types = std::move(mime);
            default_type = with_utf8_charset(options.default_mime_type);
        }

        static bool is_plain_relative(std::string_view p)
        {
            if (p.empty() || p.front() == '/' || p.find('\\') != std::string_view::npos ||
                p.find('\0') != std::string_view::npos)
            {
                return false;
            }
            while (!p.empty())
            {
                auto const slash = p.find('/');
                auto const seg = p.substr(0, slash);
                if (seg.empty() || seg == "." || seg == "..") return false;
#ifdef _WIN32
                if (seg.find(':') != std::string_view::npos) return false;
#endif
                p = slash == std::string_view::npos ? std::string_view {} : p.substr(slash + 1);
            }
            return true;
        }

        static taar::exception invalid_option(std::string_view what, std::string_view value)
        {
            return taar::exception {taar::failure {
                boost::system::errc::make_error_code(boost::system::errc::invalid_argument),
                "htdocs: the " + std::string {what} + " \"" + std::string {value} +
                    "\" must be a relative path without '.' or '..' segments"}};
        }

        htdocs_options options;
        std::filesystem::path root;         // canonical document root
        std::string default_type;
    };

    static htdocs_options legacy_options(std::string root, std::string default_doc)
    {
        htdocs_options options;
        options.root = std::move(root);
        options.index_files.clear();
        if (!default_doc.empty()) options.index_files.push_back(std::move(default_doc));
        return options;
    }

    /// UTF-8 text to a path, portably (std::string is the native narrow encoding on Windows).
    static std::filesystem::path utf8_path(std::string_view text)
    {
        return std::filesystem::path {std::u8string {text.begin(), text.end()}};
    }

    static std::string path_utf8(std::filesystem::path const& p)
    {
        auto const u8 = p.u8string();
        return std::string {u8.begin(), u8.end()};
    }

    static outcome classify(boost::system::error_code const& ec)
    {
        namespace errc = boost::system::errc;
        if (ec == errc::no_such_file_or_directory || ec == errc::not_a_directory ||
            ec == errc::filename_too_long || ec == errc::too_many_symbolic_link_levels)
        {
            return outcome::not_found;
        }
        if (ec == errc::permission_denied) return outcome::forbidden;
        return outcome::error;
    }

    static outcome classify(std::error_code const& ec)
    {
        if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory ||
            ec == std::errc::filename_too_long || ec == std::errc::too_many_symbolic_link_levels)
        {
            return outcome::not_found;
        }
        if (ec == std::errc::permission_denied) return outcome::forbidden;
        return outcome::error;
    }

    static response_type error_response(
        request_type const& request,
        boost::beast::http::status status)
    {
        namespace http = boost::beast::http;
        response_type res {status, request.version()};
        res.set(http::field::content_type, "text/plain; charset=utf-8");
        res.body().append_text(std::string {http::obsolete_reason(status)} + "\n");
        res.keep_alive(request.keep_alive());
        res.prepare_payload();
        return res;
    }

    /// The decoded path segments of the file to serve; empty optional on a 400.
    std::optional<std::vector<std::string>> requested_segments(
        boost::urls::url_view const& url,
        boost::taar::matcher::context const& context) const
    {
        auto const& args = context.path_args;
        std::string const* capture = nullptr;
        if (!state_->options.path_arg.empty())
        {
            auto const it = args.find(state_->options.path_arg);
            if (it != args.end()) capture = &it->second;
        }
        else if (auto const it = args.find("*"); it != args.end())
        {
            capture = &it->second;
        }
        else if (args.size() == 1)
        {
            capture = &args.begin()->second;
        }

        std::vector<std::string> segments;
        if (capture)
        {
            std::string_view rest = *capture;
            while (true)
            {
                auto const slash = rest.find('/');
                segments.emplace_back(rest.substr(0, slash));
                if (slash == std::string_view::npos) break;
                rest.remove_prefix(slash + 1);
            }
        }
        else
        {
            for (auto const& seg : url.segments()) segments.emplace_back(seg);
        }

        std::vector<std::string> out;
        for (auto& seg : segments)
        {
            if (seg.empty()) continue;
            if (seg == "." || seg == ".." ||
                seg.find('\\') != std::string::npos ||
                seg.find('\0') != std::string::npos)
            {
                return std::nullopt;
            }
#ifdef _WIN32
            if (seg.find(':') != std::string::npos) return std::nullopt;
#endif
            out.push_back(std::move(seg));
        }
        return out;
    }

    /// True if @a p (existing) lies under the canonical root after resolving links.
    bool contained(std::filesystem::path const& p) const
    {
        if (state_->options.follow_symlinks_outside_root) return true;
        std::error_code ec;
        auto const real = std::filesystem::canonical(p, ec);
        if (ec) return false;
        auto const& root = state_->root;
        auto const [r, q] = std::mismatch(root.begin(), root.end(), real.begin(), real.end());
        // A trailing empty element (root ending in a separator) counts as matched.
        return r == root.end() || (std::next(r) == root.end() && r->empty());
    }

    std::string content_type_for(std::string_view relative) const
    {
        auto const ext = file_extension(relative);
        if (!ext.empty())
        {
            auto const& user = state_->options.mime_types;
            if (auto const it = user.find(ext); it != user.end()) return it->second;
            if (auto const builtin = mime_type_for(relative); !builtin.empty())
            {
                return with_utf8_charset(builtin);
            }
        }
        return state_->default_type;
    }

    static std::chrono::system_clock::time_point to_system(std::filesystem::file_time_type t)
    {
#if defined(_MSC_VER)
        return std::chrono::clock_cast<std::chrono::system_clock>(t);
#else
        return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::chrono::file_clock::to_sys(t));
#endif
    }

    /// Strong validator from size, modification time and coding.
    static std::string make_etag(std::uint64_t size, std::filesystem::file_time_type mtime, std::string_view encoding)
    {
        auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch()).count();
        auto tag = std::format("\"{:x}-{:x}", size, static_cast<std::uint64_t>(ns));
        if (!encoding.empty())
        {
            tag += '-';
            tag += encoding;
        }
        tag += '"';
        return tag;
    }

    std::optional<std::string> cache_control_for(std::string_view relative) const
    {
        for (auto const& rule : state_->options.cache_rules)
        {
            if (rule.match && rule.match(relative)) return rule.value;
        }
        if (!state_->options.default_cache_control.empty()) return state_->options.default_cache_control;
        return std::nullopt;
    }

    lookup make_file(std::filesystem::path const& p, std::string relative) const
    {
        std::error_code ec;
        variant identity;
        identity.size = std::filesystem::file_size(p, ec);
        if (ec) return {classify(ec), nullptr};
        identity.mtime = std::filesystem::last_write_time(p, ec);
        if (ec) return {classify(ec), nullptr};
        identity.path = p;
        identity.etag = make_etag(identity.size, identity.mtime, {});

        auto info = std::make_shared<file_info>();
        auto modified = std::chrono::floor<std::chrono::seconds>(to_system(identity.mtime));
        // Never claim a modification later than now (RFC 9110 8.8.2.1).
        modified = std::min(modified, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        info->last_modified_time = modified;
        info->last_modified = format_http_date(modified);
        info->content_type = content_type_for(relative);
        info->cache_control = cache_control_for(relative);
        info->variants.push_back(std::move(identity));
        info->relative = std::move(relative);
        return {outcome::file, std::move(info)};
    }

    /// Map a decoded relative path (no trailing slash) to what it names on disk.
    lookup resolve(std::string const& relative, bool trailing_slash) const
    {
        namespace fs = std::filesystem;
        fs::path p = state_->root;
        if (!relative.empty()) p /= utf8_path(relative);

        std::error_code ec;
        auto const st = fs::status(p, ec);
        if (ec || !fs::exists(st)) return {ec ? classify(ec) : outcome::not_found, nullptr};
        if (!contained(p)) return {outcome::not_found, nullptr};

        if (fs::is_directory(st))
        {
            if (!trailing_slash) return {outcome::redirect, nullptr};
            for (auto const& index : state_->options.index_files)
            {
                auto const candidate = p / utf8_path(index);
                auto const ist = fs::status(candidate, ec);
                if (ec || !fs::is_regular_file(ist) || !contained(candidate)) continue;
                return make_file(candidate, relative.empty() ? index : relative + "/" + index);
            }
            return {outcome::not_found, nullptr};
        }
        if (!fs::is_regular_file(st) || trailing_slash) return {outcome::not_found, nullptr};
        return make_file(p, relative);
    }

    response_type serve(
        request_type const& request,
        boost::taar::matcher::context const& context) const
    {
        namespace http = boost::beast::http;
        auto const& options = state_->options;

        auto const method = request.method();
        if (method != http::verb::get && method != http::verb::head)
        {
            auto res = error_response(request, http::status::method_not_allowed);
            res.set(http::field::allow, "GET, HEAD");
            return res;
        }

        auto const parsed = boost::urls::parse_origin_form(request.target());
        if (!parsed) return error_response(request, http::status::bad_request);
        auto const url = *parsed;
        std::string_view const encoded_path = url.encoded_path();
        for (std::string_view const forbidden : {"%2f", "%5c", "%00"})
        {
            auto const hit = std::ranges::search(encoded_path, forbidden, [](char a, char b)
            {
                return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
            });
            if (!hit.empty()) return error_response(request, http::status::bad_request);
        }
        bool const trailing_slash = encoded_path.ends_with('/');

        auto const segments = requested_segments(url, context);
        if (!segments) return error_response(request, http::status::bad_request);

        std::string relative;
        for (auto const& seg : *segments)
        {
            if (!options.serve_hidden && seg.starts_with('.') && seg != ".well-known")
            {
                return error_response(request, http::status::not_found);
            }
            if (!relative.empty()) relative += '/';
            relative += seg;
        }

        auto found = resolve(relative, trailing_slash);
        bool fallback = false;
        if (found.kind == outcome::not_found && options.fallback_document)
        {
            auto const last = relative.substr(relative.rfind('/') + 1);
            bool const wanted = options.fallback_when
                ? options.fallback_when(relative)
                : last.find('.') == std::string::npos;
            if (wanted)
            {
                auto doc = resolve(*options.fallback_document, false);
                if (doc.kind == outcome::file)
                {
                    found = std::move(doc);
                    fallback = true;
                }
            }
        }

        switch (found.kind)
        {
        case outcome::file:
            break;
        case outcome::redirect:
        {
            auto res = error_response(request, http::status::moved_permanently);
            std::string location {encoded_path};
            location += '/';
            if (url.has_query())
            {
                location += '?';
                location += url.encoded_query();
            }
            res.set(http::field::location, location);
            return res;
        }
        case outcome::forbidden:
            return error_response(request, http::status::forbidden);
        case outcome::error:
            return error_response(request, http::status::internal_server_error);
        case outcome::not_found:
            return error_response(request, http::status::not_found);
        }

        return serve_file(request, *found.file, fallback);
    }

    enum class precondition { proceed, failed, not_modified };

    /// RFC 9110 13.2.2 steps 1-4 for a GET or HEAD.
    static precondition evaluate_preconditions(
        request_type const& request,
        std::string_view etag,
        std::chrono::sys_seconds last_modified)
    {
        namespace http = boost::beast::http;
        if (auto const it = request.find(http::field::if_match); it != request.end())
        {
            if (!etag_list_matches(it->value(), etag, false)) return precondition::failed;
        }
        else if (auto const ius = request.find(http::field::if_unmodified_since); ius != request.end())
        {
            auto const date = parse_http_date(ius->value());
            if (date && last_modified > *date) return precondition::failed;
        }
        if (auto const it = request.find(http::field::if_none_match); it != request.end())
        {
            if (etag_list_matches(it->value(), etag, true)) return precondition::not_modified;
        }
        else if (auto const ims = request.find(http::field::if_modified_since); ims != request.end())
        {
            auto const date = parse_http_date(ims->value());
            if (date && last_modified <= *date) return precondition::not_modified;
        }
        return precondition::proceed;
    }

    /// Validators and caching headers shared by 200, 206 and 304.
    void set_representation_headers(
        response_type& res,
        file_info const& info,
        variant const& chosen,
        bool fallback) const
    {
        namespace http = boost::beast::http;
        res.set(http::field::etag, chosen.etag);
        res.set(http::field::last_modified, info.last_modified);
        if (fallback) res.set(http::field::cache_control, "no-cache");
        else if (info.cache_control) res.set(http::field::cache_control, *info.cache_control);
        if (state_->options.nosniff) res.set("X-Content-Type-Options", "nosniff");
    }

    response_type serve_file(request_type const& request, file_info const& info, bool fallback) const
    {
        namespace http = boost::beast::http;
        auto const& options = state_->options;
        variant const& chosen = info.variants.front();

        response_type res {http::status::ok, request.version()};
        res.keep_alive(request.keep_alive());

        switch (evaluate_preconditions(request, chosen.etag, info.last_modified_time))
        {
        case precondition::failed:
            return error_response(request, http::status::precondition_failed);
        case precondition::not_modified:
            res.result(http::status::not_modified);
            set_representation_headers(res, info, chosen, fallback);
            if (options.on_response) options.on_response(info.relative, res.base());
            return res;
        case precondition::proceed:
            break;
        }

        set_representation_headers(res, info, chosen, fallback);
        bool const ranges_enabled = options.max_ranges > 0;
        if (ranges_enabled) res.set(http::field::accept_ranges, "bytes");

        if (request.method() == http::verb::head)
        {
            res.set(http::field::content_type, info.content_type);
            res.content_length(chosen.size);
            if (options.on_response) options.on_response(info.relative, res.base());
            return res;
        }

        // Range (GET only), subject to If-Range.
        std::vector<byte_range> ranges;
        if (auto const range = request.find(http::field::range);
            ranges_enabled && range != request.end() && if_range_holds(request, chosen, info))
        {
            auto parsed = parse_range(range->value(), chosen.size, options.max_ranges);
            if (parsed.status == range_status::unsatisfiable)
            {
                auto err = error_response(request, http::status::range_not_satisfiable);
                err.set(http::field::content_range, "bytes */" + std::to_string(chosen.size));
                return err;
            }
            if (parsed.status == range_status::satisfiable) ranges = coalesce_ranges(std::move(parsed.ranges));
        }

        auto& body = res.body();
        if (chosen.size > 0)
        {
            if (auto const failed = open_content(body, chosen)) return error_response(request, *failed);
        }
        auto append = [&](std::uint64_t offset, std::uint64_t length) { body.append_file_range(offset, length); };

        if (ranges.empty())
        {
            res.set(http::field::content_type, info.content_type);
            if (chosen.size > 0) append(0, chosen.size);
        }
        else if (ranges.size() == 1)
        {
            auto const& r = ranges.front();
            res.result(http::status::partial_content);
            res.set(http::field::content_type, info.content_type);
            res.set(http::field::content_range, content_range(r, chosen.size));
            append(r.first, r.length());
        }
        else
        {
            // multipart/byteranges (RFC 9110 14.6): text part headers between file windows.
            auto const boundary = make_boundary();
            res.result(http::status::partial_content);
            res.set(http::field::content_type, "multipart/byteranges; boundary=" + boundary);
            bool first = true;
            for (auto const& r : ranges)
            {
                std::string part = first ? "" : "\r\n";
                first = false;
                part += "--" + boundary + "\r\nContent-Type: " + info.content_type +
                    "\r\nContent-Range: " + content_range(r, chosen.size) + "\r\n\r\n";
                body.append_text(std::move(part));
                append(r.first, r.length());
            }
            body.append_text("\r\n--" + boundary + "--\r\n");
        }
        res.content_length(body.size());
        if (options.on_response) options.on_response(info.relative, res.base());
        return res;
    }

    /// RFC 9110 13.1.5: a strong entity tag or an exact date must match the chosen representation.
    static bool if_range_holds(request_type const& request, variant const& chosen, file_info const& info)
    {
        namespace http = boost::beast::http;
        auto const it = request.find(http::field::if_range);
        if (it == request.end()) return true;
        std::string_view value = it->value();
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
        if (value.starts_with('"') || value.starts_with("W/"))
        {
            std::string_view current = chosen.etag;
            auto const sent = parse_entity_tag(value);
            auto const ours = parse_entity_tag(current);
            return sent && ours && strong_match(*sent, *ours);
        }
        auto const date = parse_http_date(value);
        return date && *date == info.last_modified_time;
    }

    static std::string content_range(byte_range const& r, std::uint64_t size)
    {
        return "bytes " + std::to_string(r.first) + "-" + std::to_string(r.last) + "/" + std::to_string(size);
    }

    static std::string make_boundary()
    {
        thread_local std::mt19937_64 engine {std::random_device {}()};
        return std::format("taar-{:016x}{:016x}", engine(), engine());
    }

    /// Attach the chosen representation's content to @a body; an error status on failure.
    std::optional<boost::beast::http::status> open_content(segmented_body::value_type& body, variant const& chosen) const
    {
        namespace http = boost::beast::http;
        boost::beast::file file;
        boost::system::error_code ec;
        file.open(path_utf8(chosen.path).c_str(), boost::beast::file_mode::scan, ec);
        if (ec)
        {
            auto const kind = classify(ec);
            return kind == outcome::not_found ? http::status::not_found :
                kind == outcome::forbidden ? http::status::forbidden :
                http::status::internal_server_error;
        }
        // The file changed since it was examined: its validators are stale.
        if (file.size(ec) != chosen.size || ec) return http::status::internal_server_error;
        body.attach_file(std::move(file));
        return std::nullopt;
    }
/// @endcond

private:
/// @cond
    std::shared_ptr<state> state_;
/// @endcond
};

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_HTDOCS_HPP
