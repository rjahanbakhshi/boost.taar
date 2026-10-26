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

#include <boost/taar/core/accept_encoding.hpp>
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
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace boost::taar::handler {

/** A `Cache-Control` rule of `htdocs_options::cache_rules`.

    Rules are tried in order on the served file's decoded path relative to
    the root (no leading slash, e.g. `assets/app.3f9a1c2e.js`); the first
    whose `match` returns true supplies the header value.

    @code
    options.cache_rules = {
        taar::handler::cache_rule::glob("*.woff2", "public, max-age=86400"),
        {[](std::string_view p) { return p.ends_with(".html"); }, "no-cache"},
    };
    @endcode
*/
struct cache_rule
{
    /// Predicate on the relative path.
    std::function<bool(std::string_view)> match;

    /// The `Cache-Control` value sent when `match` accepts the path.
    std::string value;

    /** A rule matching a glob pattern (see `cache_rules::glob_match`).

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
    (`looks_content_hashed`) are cached for a year as `immutable`, and
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

    /** The `path_args` entry naming the file, relative to `root`.

        Empty (the default) selects the route's greedy capture: the `{*}`
        entry, or the only path argument when there is exactly one; with
        no path argument the whole request path (without the query) is
        used. Set it when the route has several placeholders.
    */
    std::string path_arg {};

    /// Serve files and directories whose name starts with `.` (`.well-known` is always served).
    bool serve_hidden = false;

    /// Serve symbolic links that resolve outside `root` (refused with 404 by default).
    bool follow_symlinks_outside_root = false;

    /** A document, relative to `root`, served for requests that match
        no file: the single-page application fallback.

        It answers a GET or HEAD whose path does not exist when
        `fallback_when` says so (by default: the last path segment has
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

    /// Media type for extensions neither `mime_types` nor the built-in table know.
    std::string default_mime_type = "application/octet-stream";

    /// Send `X-Content-Type-Options: nosniff` with every served file.
    bool nosniff = true;

    /** `Cache-Control` rules, first match wins (see @ref cache_rule and
        `cache_rules::immutable_hashed`). Empty by default.
    */
    std::vector<cache_rule> cache_rules {};

    /// `Cache-Control` for files no rule matches; empty (the default) sends none.
    std::string default_cache_control {};

    /** Most ranges honoured in one `Range` header; more make the request
        a plain 200. Overlapping and adjacent ranges are merged. 0 disables
        range requests (no `Accept-Ranges`).
    */
    std::size_t max_ranges = 16;

    /** Precompressed siblings to look for, in order of preference, as
        content codings: `br` is `file.br`, `gzip` is `file.gz`, `zstd` is
        `file.zst`, and any other coding `xyz` is `file.xyz`. A sibling is
        served with `Content-Encoding` and the original's media type when the
        request's `Accept-Encoding` accepts the coding (q-values honoured)
        and it is not older than the original. Files with a sibling are
        sent with `Vary: Accept-Encoding`. No compression on the fly.
    */
    std::vector<std::string> precompressed {"br", "gzip"};

    /** Capacity of the metadata cache (path resolution, stat results,
        variants, validators, media type), keyed by the decoded relative
        path; least recently used entries are dropped. 0 disables it.
    */
    std::size_t metadata_cache_entries = 4096;

    /** Age after which a cached entry is checked against the file system
        again on its next use. Within this window a changed file may still
        be described by its previous validators.
    */
    std::chrono::milliseconds revalidate_after {1000};

    /// Files up to this size are kept in memory once read; 0 disables the content cache.
    std::size_t memory_cache_max_file_size = 64 * 1024;

    /// Total bytes the content cache may hold (least recently used files are dropped first).
    std::size_t memory_cache_max_total = 32 * 1024 * 1024;

    /** Called for every served file (200, 206, 304) with the file's
        relative path and the response header, just before the response
        is returned: add `Content-Disposition`, security headers, and so
        on. It runs on the connection's thread and must be thread-safe.
    */
    std::function<void(std::string_view, boost::beast::http::response_header<>&)> on_response {};
};

/// Counters of an @ref htdocs handler's caches, from `htdocs::cache_stats`.
struct htdocs_cache_stats
{
    std::size_t metadata_entries = 0;   ///< Entries in the metadata cache.
    std::size_t memory_entries = 0;     ///< Files held by the content cache.
    std::size_t memory_bytes = 0;       ///< Bytes held by the content cache.
    std::size_t memory_hits = 0;        ///< Responses served from the content cache.
    std::size_t memory_loads = 0;       ///< Files read into the content cache.
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
    `htdocs_options::path_arg`), percent-decoded by Boost.URL; the
    query never takes part. A path with an encoded `/`, `\` or NUL, or a
    `.` or `..` segment, is answered 400. The resolved file must stay
    under the canonical root (symbolic links included) and hidden names
    are refused; both answer 404 so the tree cannot be probed. A
    directory without a trailing slash is redirected (301) to the slash
    form; with the slash, the first existing index file is served, else
    404 (no directory listings). Methods other than GET and HEAD answer
    405 with `Allow: GET, HEAD`. Error responses are short fixed
    `text/plain` texts that never echo the request.

    @par Validators and conditional requests
    Files carry `Last-Modified` and a strong `ETag` (size, modification
    time and content coding). `If-Match`, `If-Unmodified-Since`,
    `If-None-Match` and `If-Modified-Since` are evaluated in the order of
    RFC 9110 13.2.2 (412, 304). `Cache-Control` comes from
    `htdocs_options::cache_rules`.

    @par Ranges and variants
    `Range` requests are answered with 206 (one range, or
    `multipart/byteranges`), 416 when unsatisfiable, honouring `If-Range`.
    Precompressed siblings (`file.br`, `file.gz`) are chosen by
    `Accept-Encoding` and announced with `Vary: Accept-Encoding`; range
    requests are served from the uncompressed file.

    @par Responses
    Every response, errors included, is a typed
    `http::response<segmented_body>` (convertible to a
    `message_generator`), so the session's response filter and HEAD
    handling apply. HEAD and 304 never open the file. The handler is cheap
    to copy; copies share their state and caches and may be used from
    several threads at once.

    @par Caches
    Path resolution, stat results and validators are cached and checked
    again after `htdocs_options::revalidate_after`; small files are kept
    in memory. See the cache fields of @ref htdocs_options.

    @par Blocking I/O
    File system calls (stat, open, read) are synchronous and run on the
    connection's thread; asynchronous file I/O is not used. Serve from
    local disks, and size the thread pool for the slowest disk.
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
                relative path, or a precompressed entry is not a content
                coding token (or is `identity`).
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

    /// A snapshot of the cache counters (shared by all copies of this handler).
    htdocs_cache_stats cache_stats() const
    {
        std::lock_guard lock {state_->mutex};
        auto out = state_->stats;
        out.metadata_entries = state_->metadata.size();
        out.memory_entries = state_->content.size();
        out.memory_bytes = state_->content_bytes;
        return out;
    }

    /** The configured `htdocs_options::path_arg`, for the session's
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

    /// A small least-recently-used map.
    template <typename Value>
    class lru
    {
    public:
        std::size_t size() const noexcept { return index_.size(); }

        Value* find(std::string const& key)
        {
            auto const it = index_.find(key);
            if (it == index_.end()) return nullptr;
            items_.splice(items_.begin(), items_, it->second);
            return &it->second->second;
        }

        void put(std::string key, Value value)
        {
            if (auto* existing = find(key))
            {
                *existing = std::move(value);
                return;
            }
            items_.emplace_front(key, std::move(value));
            index_.emplace(std::move(key), items_.begin());
        }

        void erase(std::string const& key)
        {
            auto const it = index_.find(key);
            if (it == index_.end()) return;
            items_.erase(it->second);
            index_.erase(it);
        }

        /// Remove and return the least recently used value.
        Value pop_oldest()
        {
            auto value = std::move(items_.back().second);
            index_.erase(items_.back().first);
            items_.pop_back();
            return value;
        }

    private:
        std::list<std::pair<std::string, Value>> items_;
        std::unordered_map<std::string, typename std::list<std::pair<std::string, Value>>::iterator> index_;
    };

    struct metadata_entry
    {
        lookup result;
        std::chrono::steady_clock::time_point checked;
    };

    struct content_entry
    {
        std::shared_ptr<std::string const> data;
        std::filesystem::file_time_type mtime;
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

            for (auto const& coding : options.precompressed)
            {
                bool const token = !coding.empty() && std::ranges::all_of(coding, [](char c)
                {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '-' || c == '_' || c == '.';
                });
                if (!token || coding == "identity")
                {
                    throw taar::exception {taar::failure {
                        boost::system::errc::make_error_code(boost::system::errc::invalid_argument),
                        "htdocs: \"" + coding + "\" is not a content coding for precompressed files"}};
                }
            }
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

        // Shared by every copy of the handler and every connection strand.
        std::mutex mutex;
        lru<metadata_entry> metadata;
        lru<content_entry> content;         // keyed by the file's path
        std::size_t content_bytes = 0;
        htdocs_cache_stats stats;
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
        // Raw clock ticks: nanoseconds on POSIX; MSVC counts 100 ns ticks
        // since 1601, which overflows int64 when converted to nanoseconds.
        auto const ticks = static_cast<std::uint64_t>(mtime.time_since_epoch().count());
        auto tag = std::format("\"{:x}-{:x}", size, ticks);
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
        for (auto const& coding : state_->options.precompressed)
        {
            variant v;
            v.path = p;
            v.path += utf8_path(coding == "br" ? ".br" : coding == "gzip" ? ".gz" : coding == "zstd" ? ".zst" : "." + coding);
            auto const st = std::filesystem::status(v.path, ec);
            if (ec || !std::filesystem::is_regular_file(st) || !contained(v.path)) continue;
            v.size = std::filesystem::file_size(v.path, ec);
            if (ec) continue;
            v.mtime = std::filesystem::last_write_time(v.path, ec);
            if (ec || v.mtime < identity.mtime) continue;     // stale: older than the original
            v.encoding = coding;
            v.etag = make_etag(v.size, v.mtime, coding);
            info->variants.push_back(std::move(v));
        }
        info->variants.insert(info->variants.begin(), std::move(identity));
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

    /// `resolve` through the metadata cache; @a fresh bypasses (and refreshes) it.
    lookup cached_resolve(std::string const& relative, bool trailing_slash, bool fresh) const
    {
        auto& st = *state_;
        if (st.options.metadata_cache_entries == 0) return resolve(relative, trailing_slash);
        std::string key = trailing_slash ? relative + "/" : relative;
        auto const now = std::chrono::steady_clock::now();
        if (!fresh)
        {
            std::lock_guard lock {st.mutex};
            if (auto const* entry = st.metadata.find(key); entry && now - entry->checked < st.options.revalidate_after)
            {
                return entry->result;
            }
        }
        auto result = resolve(relative, trailing_slash);
        std::lock_guard lock {st.mutex};
        if (result.kind == outcome::error)
        {
            st.metadata.erase(key);         // transient: do not remember
        }
        else
        {
            st.metadata.put(std::move(key), {result, now});
            while (st.metadata.size() > st.options.metadata_cache_entries) st.metadata.pop_oldest();
        }
        return result;
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

        // A file that changed between the cached stat and the open is
        // looked up once more, bypassing the cache.
        for (bool fresh : {false, true})
        {
            auto found = cached_resolve(relative, trailing_slash, fresh);
            bool fallback = false;
            if (found.kind == outcome::not_found && options.fallback_document)
            {
                auto const last = relative.substr(relative.rfind('/') + 1);
                bool const wanted = options.fallback_when
                    ? options.fallback_when(relative)
                    : last.find('.') == std::string::npos;
                if (wanted)
                {
                    auto doc = cached_resolve(*options.fallback_document, false, fresh);
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

            if (auto res = serve_file(request, *found.file, fallback)) return std::move(*res);
        }
        return error_response(request, http::status::internal_server_error);
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
        if (info.variants.size() > 1) res.set(http::field::vary, "Accept-Encoding");
        if (state_->options.nosniff) res.set("X-Content-Type-Options", "nosniff");
    }

    /** The representation to send: a precompressed sibling the client
        accepts with the highest q (ties: configuration order), unless the
        client prefers identity explicitly; identity for range requests.
    */
    static variant const& select_variant(request_type const& request, file_info const& info)
    {
        namespace http = boost::beast::http;
        auto const& identity = info.variants.front();
        if (info.variants.size() == 1) return identity;
        if (request.method() == http::verb::get && request.find(http::field::range) != request.end()) return identity;
        auto const field = request.find(http::field::accept_encoding);
        if (field == request.end()) return identity;
        variant const* best = nullptr;
        double best_q = 0;
        for (auto it = std::next(info.variants.begin()); it != info.variants.end(); ++it)
        {
            auto const q = accept_encoding_quality(field->value(), it->encoding).value_or(0);
            if (q > best_q)
            {
                best = &*it;
                best_q = q;
            }
        }
        if (!best) return identity;
        auto const identity_q = accept_encoding_quality(field->value(), "identity");
        if (identity_q && *identity_q > best_q) return identity;
        return *best;
    }

    /// The response for a resolved file; empty if the file changed under the cached metadata.
    std::optional<response_type> serve_file(request_type const& request, file_info const& info, bool fallback) const
    {
        namespace http = boost::beast::http;
        auto const& options = state_->options;
        variant const& chosen = select_variant(request, info);

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
        if (!chosen.encoding.empty()) res.set(http::field::content_encoding, chosen.encoding);
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
        std::shared_ptr<std::string const> memory;
        if (chosen.size > 0)
        {
            switch (open_content(body, chosen, memory))
            {
            case content_status::ok: break;
            case content_status::changed: return std::nullopt;
            case content_status::not_found: return error_response(request, http::status::not_found);
            case content_status::forbidden: return error_response(request, http::status::forbidden);
            case content_status::error: return error_response(request, http::status::internal_server_error);
            }
        }
        auto append = [&](std::uint64_t offset, std::uint64_t length)
        {
            if (memory) body.append_buffer(memory, offset, length);
            else body.append_file_range(offset, length);
        };

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

    enum class content_status { ok, changed, not_found, forbidden, error };

    /** Make the chosen representation's bytes available: a shared buffer
        from (or newly put into) the content cache in @a memory, or the open
        file attached to @a body.
    */
    content_status open_content(
        segmented_body::value_type& body,
        variant const& chosen,
        std::shared_ptr<std::string const>& memory) const
    {
        auto& st = *state_;
        bool const cacheable = chosen.size <= st.options.memory_cache_max_file_size &&
            chosen.size <= st.options.memory_cache_max_total;
        auto const key = path_utf8(chosen.path);
        if (cacheable)
        {
            std::lock_guard lock {st.mutex};
            if (auto const* entry = st.content.find(key))
            {
                if (entry->data->size() == chosen.size && entry->mtime == chosen.mtime)
                {
                    ++st.stats.memory_hits;
                    memory = entry->data;
                    return content_status::ok;
                }
                st.content_bytes -= entry->data->size();
                st.content.erase(key);
            }
        }

        boost::beast::file file;
        boost::system::error_code ec;
        file.open(key.c_str(), boost::beast::file_mode::scan, ec);
        if (ec)
        {
            switch (classify(ec))
            {
            case outcome::not_found: return content_status::changed;    // vanished: look again
            case outcome::forbidden: return content_status::forbidden;
            default: return content_status::error;
            }
        }
        // The file changed since it was examined: its validators are stale.
        if (file.size(ec) != chosen.size || ec) return ec ? content_status::error : content_status::changed;

        if (!cacheable)
        {
            body.attach_file(std::move(file));
            return content_status::ok;
        }

        auto data = std::make_shared<std::string>(static_cast<std::size_t>(chosen.size), '\0');
        std::size_t done = 0;
        while (done < data->size())
        {
            auto const n = file.read(data->data() + done, data->size() - done, ec);
            if (ec) return content_status::error;
            if (n == 0) return content_status::changed;
            done += n;
        }
        memory = std::move(data);
        std::lock_guard lock {st.mutex};
        ++st.stats.memory_loads;
        if (auto const* old = st.content.find(key))
        {
            st.content_bytes -= old->data->size();
        }
        st.content.put(key, {memory, chosen.mtime});
        st.content_bytes += memory->size();
        while (st.content_bytes > st.options.memory_cache_max_total)
        {
            st.content_bytes -= st.content.pop_oldest().data->size();
        }
        return content_status::ok;
    }
/// @endcond

private:
/// @cond
    std::shared_ptr<state> state_;
/// @endcond
};

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_HTDOCS_HPP
