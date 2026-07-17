//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_OPENAPI_GENERATOR_HPP
#define BOOST_TAAR_OPENAPI_GENERATOR_HPP

#include <boost/taar/core/is_async_generator.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/is_chunked_response.hpp>
#include <boost/taar/core/is_http_response.hpp>
#include <boost/taar/core/json_schema.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_param.hpp>
#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/matcher/template_parser.hpp>
#include <boost/taar/type_traits/string_like.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace boost::taar::openapi {

/// The document's `info` object.
struct info
{
    std::string title;
    std::string version;
    std::string description {};
};

/// One entry of the document's `servers` array.
struct server
{
    std::string url;
    std::string description {};
};

/// Per-operation documentation supplied at registration through @ref generator::describe.
struct operation
{
    std::string summary {};
    std::string description {};
    std::vector<std::string> tags {};
    std::string operation_id {};
    /// Status code -> description. Replaces the generated default response when non-empty.
    std::map<std::string, std::string> responses {};
    bool deprecated = false;
};

namespace detail {

// ---- compile-time matcher shape analysis ----------------------------------

struct shape
{
    bool valid = true;
    bool methods_only = true;   // subtree consists of method_eq leaves only
    int targets = 0;
    int methods = 0;
};

template <typename AST> struct shape_of
{
    static constexpr shape value {false, false, 0, 0};
};

template <> struct shape_of<matcher::method_eq_constraint> { static constexpr shape value {true, true, 0, 1}; };
template <> struct shape_of<matcher::target_constraint> { static constexpr shape value {true, false, 1, 0}; };
template <> struct shape_of<matcher::header_eq_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::header_exists_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::cookie_eq_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::cookie_exists_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::version_eq_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::version_ne_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::version_lt_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::version_le_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::version_gt_constraint> { static constexpr shape value {true, false, 0, 0}; };
template <> struct shape_of<matcher::version_ge_constraint> { static constexpr shape value {true, false, 0, 0}; };

template <typename L, typename R>
struct shape_of<matcher::and_<L, R>>
{
    static constexpr shape l = shape_of<L>::value;
    static constexpr shape r = shape_of<R>::value;
    static constexpr shape value {
        l.valid && r.valid,
        l.methods_only && r.methods_only,
        l.targets + r.targets,
        l.methods + r.methods};
};

template <typename L, typename R>
struct shape_of<matcher::or_<L, R>>
{
    static constexpr shape l = shape_of<L>::value;
    static constexpr shape r = shape_of<R>::value;
    static constexpr shape value {
        l.valid && r.valid && l.methods_only && r.methods_only,
        true,
        0,
        l.methods + r.methods};
};

template <typename X>
struct shape_of<matcher::not_<X>>
{
    static constexpr shape value {false, false, 0, 0};
};

/** True if a matcher AST can be rendered as OpenAPI operations.

    Accepted: a conjunction with exactly one `target == "..."`, at least
    one `method == ...` (several may be joined with `||`), plus any number
    of `header(...) == ...`, `exist(header(...))`, `cookie(...) == ...`,
    `exist(cookie(...))` and version constraints. Rejected: negation,
    `!=` leaves, `||` over anything but methods, and matchers without
    metadata (plain callables).
*/
template <typename AST>
inline constexpr bool expressible_v =
    shape_of<AST>::value.valid &&
    shape_of<AST>::value.targets == 1 &&
    shape_of<AST>::value.methods >= 1;

// ---- runtime extraction -----------------------------------------------------

struct required_input
{
    std::string name;
    std::string value;      // empty for existence checks
    bool exists_only = false;
};

struct route
{
    std::string template_str;
    std::vector<std::string> methods;
    std::vector<required_input> headers;
    std::vector<required_input> cookies;
};

inline std::string verb_string(matcher::method_verb const& v)
{
    return std::visit([](auto const& x) -> std::string
    {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(x)>, std::string>) return x;
        else return std::string {boost::beast::http::to_string(x)};
    }, v);
}

inline std::string header_string(matcher::header_key const& k)
{
    return std::visit([](auto const& x) -> std::string
    {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(x)>, std::string>) return x;
        else return std::string {boost::beast::http::to_string(x)};
    }, k);
}

template <typename Leaf, typename Value>
void visit_leaf(Value const& v, route& r)
{
    using namespace matcher;
    if constexpr (std::same_as<Leaf, method_eq_constraint>) r.methods.push_back(verb_string(v.verb));
    else if constexpr (std::same_as<Leaf, target_constraint>) r.template_str = v.template_str;
    else if constexpr (std::same_as<Leaf, header_eq_constraint>) r.headers.push_back({header_string(v.key), v.value, false});
    else if constexpr (std::same_as<Leaf, header_exists_constraint>) r.headers.push_back({header_string(v.key), {}, true});
    else if constexpr (std::same_as<Leaf, cookie_eq_constraint>) r.cookies.push_back({v.name, v.value, false});
    else if constexpr (std::same_as<Leaf, cookie_exists_constraint>) r.cookies.push_back({v.name, {}, true});
    else {} // version constraints carry nothing OpenAPI can express
}

template <typename TL> struct extractor;

template <typename... L>
struct extractor<matcher::type_list<L...>>
{
    template <typename Captures>
    static route extract(Captures const& captures)
    {
        route r;
        [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            (visit_leaf<L>(std::get<I>(captures), r), ...);
        }(std::index_sequence_for<L...> {});
        return r;
    }
};

// ---- handler response introspection -------------------------------------------

struct response_info
{
    bool known = false;
    std::string content_type;
    std::string json_type;      // empty: no schema
    bool no_content = false;
};

template <typename T>
response_info response_of_type()
{
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_void_v<U>) return {true, {}, {}, true};
    else if constexpr (is_awaitable<U>) return response_of_type<typename U::value_type>();
    else if constexpr (is_chunked_response<U> || is_async_generator<U>)
    {
        using V = std::remove_cvref_t<typename U::value_type>;
        if constexpr (std::same_as<V, boost::json::value>) return {true, "application/json", {}, false};
        else if constexpr (std::same_as<V, std::vector<std::byte>> || std::same_as<V, std::span<std::byte const>>)
            return {true, "application/octet-stream", {}, false};
        else return {true, "text/plain", {}, false};
    }
    else if constexpr (is_http_response<U> || std::same_as<U, boost::beast::http::message_generator>) return {};
    else if constexpr (std::same_as<U, bool>) return {true, "text/plain", "boolean", false};
    else if constexpr (std::is_arithmetic_v<U>) return {true, "text/plain", std::string {json_type_name<U>()}, false};
    else if constexpr (type_traits::string_like<U>) return {true, "text/plain", "string", false};
    else if constexpr (std::same_as<U, boost::json::value>) return {true, "application/json", {}, false};
    else if constexpr (std::same_as<U, boost::json::object> || std::same_as<U, boost::json::array>)
        return {true, "application/json", std::string {json_type_name<U>()}, false};
    else if constexpr (std::same_as<U, std::vector<std::byte>> || std::same_as<U, std::span<std::byte const>>)
        return {true, "application/octet-stream", {}, false};
    else return {};
}

template <typename Handler>
response_info response_of_handler()
{
    using H = std::remove_cvref_t<Handler>;
    if constexpr (requires { typename H::callable_result_type; })
        return response_of_type<typename H::callable_result_type>();
    else if constexpr (requires { typename H::inner_type; })
        return response_of_handler<typename H::inner_type>();
    else
        return {};
}

inline std::string lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace detail

/** Collects route descriptions at registration time and renders an
    OpenAPI 3.1 document.

    Pass @ref describe as a trailing argument of
    `basic_http::register_request_handler`. The generator reads the
    matcher's metadata (method, path template, required headers and
    cookies) and, for `handler::rest` handlers, the argument providers
    (name, location, JSON type, required, description) and the callable's
    return type. Matchers that OpenAPI cannot express (negation, `!=`,
    `||` over anything but methods, missing method or path) are rejected
    at compile time.

    @code
    taar::openapi::generator openapi {{.title = "Users API", .version = "1.0.0"}};

    session.register_request_handler(
        method == http::verb::get && target == "/users/{id}",
        taar::handler::rest(&get_user, taar::handler::path_arg("id", "User id")),
        openapi.describe({.summary = "Fetch a user", .tags = {"users"}}));

    session.register_request_handler(
        method == http::verb::get && target == "/openapi.json",
        openapi.handler());
    @endcode
*/
class generator
{
public:
    explicit generator(info document_info)
        : info_ {std::move(document_info)}
    {}

    generator(generator const&) = delete;
    generator& operator=(generator const&) = delete;

    generator& add_server(std::string url, std::string description = {})
    {
        servers_.push_back({std::move(url), std::move(description)});
        return *this;
    }

    /// A registration observer recording the route under @a op.
    class describer
    {
    public:
        describer(generator* owner, operation op)
            : owner_ {owner}
            , op_ {std::move(op)}
        {}

        template <typename Operand, typename Handler>
        void operator()(Operand const& operand, Handler const& handler) const
        {
            using meta = typename Operand::metadata_type;
            static_assert(detail::expressible_v<meta>,
                "openapi::generator cannot describe this route. The matcher must be a "
                "conjunction of exactly one `target == \"...\"` and at least one "
                "`method == ...` (methods may be joined with `||`), optionally with "
                "`header(...) == ...`, `exist(header(...))`, `cookie(...) == ...`, "
                "`exist(cookie(...))` and version constraints. Negation, `!=`, `||` "
                "over non-method constraints and plain callable matchers are not "
                "expressible in OpenAPI.");

            auto r = detail::extractor<matcher::flatten_t<meta>>::extract(operand.captures());

            std::vector<handler::rest_param_info> params;
            if constexpr (handler::has_rest_params<Handler>)
            {
                params = handler.params();
            }

            owner_->record(std::move(r), op_, std::move(params), detail::response_of_handler<Handler>());
        }

    private:
    /// @cond
        generator* owner_;
        operation op_;
    /// @endcond
    };

    /// Describe the next registered route. Pass as a trailing argument of `register_request_handler`.
    describer describe(operation op = {})
    {
        return describer {this, std::move(op)};
    }

    describer describe(std::string summary)
    {
        return describer {this, operation {.summary = std::move(summary)}};
    }

    std::size_t operation_count() const noexcept { return operations_.size(); }

    /// Render the OpenAPI 3.1 document.
    boost::json::object document() const
    {
        namespace json = boost::json;

        json::object doc;
        doc["openapi"] = "3.1.0";

        json::object info_obj;
        info_obj["title"] = info_.title;
        info_obj["version"] = info_.version;
        if (!info_.description.empty()) info_obj["description"] = info_.description;
        doc["info"] = std::move(info_obj);

        if (!servers_.empty())
        {
            json::array servers;
            for (auto const& s : servers_)
            {
                json::object o;
                o["url"] = s.url;
                if (!s.description.empty()) o["description"] = s.description;
                servers.push_back(std::move(o));
            }
            doc["servers"] = std::move(servers);
        }

        json::object paths;
        for (auto const& rec : operations_)
        {
            auto* item = paths.if_contains(rec.path);
            if (item == nullptr)
            {
                item = &paths.emplace(rec.path, json::object {}).first->value();
            }
            auto op = render_operation(rec);
            for (auto const& m : rec.methods)
            {
                item->as_object()[detail::lower(m)] = op;
            }
        }
        doc["paths"] = std::move(paths);
        return doc;
    }

    /// A handler serving @ref document as `application/json`.
    auto handler()
    {
        return handler::rest([this] { return boost::json::value {document()}; });
    }

private:
/// @cond
    struct path_param
    {
        std::string name;
        bool greedy;
    };

    struct recorded
    {
        std::string path;
        std::vector<path_param> path_params;
        std::vector<std::string> methods;
        detail::route route;
        operation op;
        std::vector<handler::rest_param_info> params;
        detail::response_info response;
    };

    void record(
        detail::route r,
        operation op,
        std::vector<handler::rest_param_info> params,
        detail::response_info response)
    {
        auto parsed = matcher::parse_template(r.template_str);
        if (parsed.has_error())
        {
            throw std::invalid_argument {"openapi::generator: invalid target template: " + r.template_str};
        }

        recorded rec;
        for (auto const& seg : parsed.value())
        {
            rec.path += '/';
            if (seg.type == matcher::template_segment_type::literal)
            {
                rec.path += seg.value;
            }
            else
            {
                rec.path += '{';
                rec.path += seg.value;
                rec.path += '}';
                rec.path_params.push_back({std::string {seg.value}, seg.type == matcher::template_segment_type::greedy_param});
            }
        }
        if (rec.path.empty()) rec.path = "/";

        for (auto const& m : r.methods)
        {
            for (auto const& existing : operations_)
            {
                if (existing.path == rec.path && std::ranges::find(existing.methods, m) != existing.methods.end())
                {
                    throw std::logic_error {"openapi::generator: operation already described: " + m + " " + rec.path};
                }
            }
        }

        rec.methods = r.methods;
        rec.route = std::move(r);
        rec.op = std::move(op);
        rec.params = std::move(params);
        rec.response = response;
        operations_.push_back(std::move(rec));
    }

    static boost::json::object render_operation(recorded const& rec)
    {
        namespace json = boost::json;
        using handler::param_location;

        json::object op;
        if (!rec.op.summary.empty()) op["summary"] = rec.op.summary;
        if (!rec.op.description.empty()) op["description"] = rec.op.description;
        if (!rec.op.operation_id.empty()) op["operationId"] = rec.op.operation_id;
        if (rec.op.deprecated) op["deprecated"] = true;
        if (!rec.op.tags.empty())
        {
            json::array tags;
            for (auto const& t : rec.op.tags) tags.emplace_back(t);
            op["tags"] = std::move(tags);
        }

        json::array parameters;
        auto type_schema = [](param_info const& info)
        {
            json::object schema;
            if (!info.json_type.empty()) schema["type"] = info.json_type;
            return schema;
        };
        auto add_param = [&](std::string const& name, std::string_view in, bool required,
                             std::string const& description, json::object schema)
        {
            json::object p;
            p["name"] = name;
            p["in"] = in;
            if (required) p["required"] = true;
            if (!description.empty()) p["description"] = description;
            p["schema"] = std::move(schema);
            parameters.push_back(std::move(p));
        };

        // Path placeholders not bound by a provider are still parameters.
        for (auto const& pp : rec.path_params)
        {
            bool bound = std::ranges::any_of(rec.params, [&](auto const& p)
            {
                return p.in == param_location::path && p.info.name == pp.name;
            });
            if (!bound)
            {
                json::object schema;
                schema["type"] = "string";
                add_param(pp.name, "path", true, pp.greedy ? "Matches the remainder of the path" : "", std::move(schema));
            }
        }

        json::object request_body;
        for (auto const& p : rec.params)
        {
            switch (p.in)
            {
            case param_location::path:
            {
                bool greedy = std::ranges::any_of(rec.path_params, [&](auto const& pp) { return pp.name == p.info.name && pp.greedy; });
                add_param(p.info.name, "path", true,
                    !p.info.description.empty() ? p.info.description : (greedy ? std::string {"Matches the remainder of the path"} : std::string {}),
                    type_schema(p.info));
                break;
            }
            case param_location::query:
                add_param(p.info.name, "query", p.info.required, p.info.description, type_schema(p.info));
                break;
            case param_location::header:
                add_param(p.info.name, "header", p.info.required, p.info.description, type_schema(p.info));
                break;
            case param_location::cookie:
                add_param(p.info.name, "cookie", p.info.required, p.info.description, type_schema(p.info));
                break;
            case param_location::body:
            {
                json::object content;
                for (auto const& ct : p.content_types)
                {
                    json::object media;
                    json::object schema;
                    if (!p.info.json_type.empty()) schema["type"] = p.info.json_type;
                    media["schema"] = std::move(schema);
                    content[ct] = std::move(media);
                }
                request_body["required"] = p.info.required;
                if (!p.info.description.empty()) request_body["description"] = p.info.description;
                request_body["content"] = std::move(content);
                break;
            }
            case param_location::none:
                break;
            }
        }

        for (auto const& h : rec.route.headers)
        {
            json::object schema;
            schema["type"] = "string";
            if (!h.exists_only) schema["const"] = h.value;
            add_param(h.name, "header", true, "", std::move(schema));
        }
        for (auto const& c : rec.route.cookies)
        {
            json::object schema;
            schema["type"] = "string";
            if (!c.exists_only) schema["const"] = c.value;
            add_param(c.name, "cookie", true, "", std::move(schema));
        }

        if (!parameters.empty()) op["parameters"] = std::move(parameters);
        if (!request_body.empty()) op["requestBody"] = std::move(request_body);

        json::object responses;
        if (!rec.op.responses.empty())
        {
            for (auto const& [code, description] : rec.op.responses)
            {
                json::object r;
                r["description"] = description;
                responses[code] = std::move(r);
            }
        }
        else
        {
            json::object ok;
            ok["description"] = rec.response.no_content ? "No content" : "Success";
            if (rec.response.known && !rec.response.no_content)
            {
                json::object media;
                json::object schema;
                if (!rec.response.json_type.empty()) schema["type"] = rec.response.json_type;
                media["schema"] = std::move(schema);
                json::object content;
                content[rec.response.content_type] = std::move(media);
                ok["content"] = std::move(content);
            }
            responses["200"] = std::move(ok);
        }
        op["responses"] = std::move(responses);
        return op;
    }

    info info_;
    std::vector<server> servers_;
    std::vector<recorded> operations_;
/// @endcond
};

} // namespace boost::taar::openapi

#endif // BOOST_TAAR_OPENAPI_GENERATOR_HPP
