//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_PATH_ARG_CHECK_HPP
#define BOOST_TAAR_HANDLER_PATH_ARG_CHECK_HPP

#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/rest_param.hpp>
#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/matcher/template_parser.hpp>
#include <boost/taar/type_traits/specialization_of.hpp>
#include <boost/system/system_error.hpp>
#include <boost/system/errc.hpp>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace boost::taar::handler {

/** Concept: true if @a T is a `path_arg`, possibly wrapped in
    `with_default` and/or `constrained`.

    The check is by type identity rather than behaviour, because the
    distinguishing fact is *which kind of input* the provider reads (a path
    placeholder) -- something not visible in the call signature.
*/
template <typename T>
concept is_path_arg =
    std::is_same_v<typename detail::strip_default<std::remove_cvref_t<T>>::type, path_arg>;

/** Concept: true if @a Handler exposes a const `path_arg_names()` accessor.

    Handlers built with @ref boost::taar::handler::rest "rest" satisfy this
    concept; raw lambdas and other callables do not, so the cross-check
    silently skips them.
*/
template <typename Handler>
concept has_path_arg_names = requires(Handler const& h)
{
    { h.path_arg_names() } -> std::ranges::input_range;
};

namespace detail {

template <typename TL> struct find_target_index_;

template <>
struct find_target_index_<matcher::type_list<>>
{
    static constexpr std::size_t value = static_cast<std::size_t>(-1);
};

template <typename... R>
struct find_target_index_<matcher::type_list<matcher::target_constraint, R...>>
{
    static constexpr std::size_t value = 0;
};

template <typename Head, typename... R>
struct find_target_index_<matcher::type_list<Head, R...>>
{
private:
    static constexpr std::size_t tail_ = find_target_index_<matcher::type_list<R...>>::value;
public:
    static constexpr std::size_t value =
        tail_ == static_cast<std::size_t>(-1)
        ? static_cast<std::size_t>(-1)
        : 1 + tail_;
};

} // namespace detail

/** Find the first `target_constraint` leaf's index in an AST's flattened leaves.

    Returns `static_cast<std::size_t>(-1)` when no target leaf exists.
    Most callers don't need this directly -- use @ref find_target_template
    on an operand instead.
*/
template <typename AST>
inline constexpr std::size_t find_target_index_v =
    detail::find_target_index_<matcher::flatten_t<AST>>::value;

/** Read the raw template string captured by the first `target_constraint`.

    @returns The unparsed template string on success, or `std::nullopt`
             when the matcher's metadata contains no `target_constraint`
             leaf (for example, a matcher that constrains only method or
             headers). Multi-target matchers (`target == "/a" || target ==
             "/b"`) return the first occurrence; the cross-check only
             validates against that one.
*/
template <typename Operand>
auto find_target_template(Operand const& op)
    -> std::optional<std::string>
{
    using meta = typename Operand::metadata_type;
    constexpr std::size_t idx = find_target_index_v<meta>;
    if constexpr (idx == static_cast<std::size_t>(-1))
    {
        return std::nullopt;
    }
    else
    {
        return std::get<idx>(op.captures()).template_str;
    }
}

/** Cross-check the handler's `path_arg` providers against the captured target template.

    Runs at handler-registration time. Every name declared by a
    @ref path_arg (or @ref with_default wrapping one) on @a handler must
    appear as a `{name}` or `{*name}` placeholder in the template string
    captured by @a op's `target_constraint`. A mismatch -- typo, dropped
    placeholder, or wrong path -- throws `boost::system::system_error`
    with a message naming the offending argument, so the failure surfaces
    at server startup instead of at the first request.

    The check is a no-op when:

    @li @a op's metadata contains no `target_constraint` (e.g. the matcher
        only constrains method or headers, or is registered against an
        MCP envelope), or
    @li @a handler does not satisfy `has_path_arg_names` (e.g. it's a
        raw lambda rather than a @ref boost::taar::handler::rest "rest"
        handler).

    Path placeholders without a matching `path_arg` provider are
    intentionally allowed -- a handler may legitimately ignore part of the
    path.
*/
template <typename Operand, typename Handler>
void validate_path_args_against_target(Operand const& op, Handler const& handler)
{
    using meta = typename Operand::metadata_type;
    constexpr std::size_t idx = find_target_index_v<meta>;

    if constexpr (idx == static_cast<std::size_t>(-1))
    {
        return;
    }
    else if constexpr (!has_path_arg_names<Handler>)
    {
        return;
    }
    else
    {
        // Several `target == ...` leaves may be joined with `||`: a
        // placeholder from any of them is a legitimate path_arg.
        std::unordered_set<std::string> template_names;
        std::string template_str;
        std::apply([&](auto const&... capture)
        {
            ([&]<typename C>(C const& c)
            {
                if constexpr (requires { c.template_str; })
                {
                    if (!template_str.empty()) template_str += " | ";
                    template_str += c.template_str;
                    auto parsed = matcher::parse_template(c.template_str);
                    if (parsed.has_error())
                    {
                        throw boost::system::system_error{
                            parsed.error().code(),
                            std::format(
                                "Invalid target template \"{}\" at handler registration",
                                c.template_str)};
                    }
                    for (auto const& seg : parsed.value())
                    {
                        if (seg.type == matcher::template_segment_type::param ||
                            seg.type == matcher::template_segment_type::greedy_param)
                        {
                            template_names.emplace(seg.value);
                        }
                    }
                }
            }(capture), ...);
        }, op.captures());
        for (auto const& name : handler.path_arg_names())
        {
            if (!template_names.contains(name))
            {
                throw boost::system::system_error{
                    boost::system::errc::make_error_code(
                        boost::system::errc::invalid_argument),
                    std::format(
                        "path_arg(\"{}\") is not present as a placeholder in "
                        "target template \"{}\"",
                        name, template_str)};
            }
        }
    }
}

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_PATH_ARG_CHECK_HPP
