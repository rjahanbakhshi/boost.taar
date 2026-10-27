//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_MATCHER_ROUTE_SHAPE_HPP
#define BOOST_TAAR_MATCHER_ROUTE_SHAPE_HPP

#include <boost/taar/matcher/metadata.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <concepts>
#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace boost::taar::matcher {

/** Compile-time summary of a matcher AST's shape.

    Used to decide whether a route can be described as "these methods on
    this path": by the OpenAPI generator, and by the session to answer
    `405 Method Not Allowed` and to serve `HEAD` from a `GET` route.
*/
struct route_shape
{
    bool valid = true;          ///< No negation, no `!=`, no opaque callables.
    bool methods_only = true;   ///< The subtree consists of `method ==` leaves only.
    int targets = 0;            ///< Number of `target ==` leaves.
    int methods = 0;            ///< Number of `method ==` leaves.
};

template <typename AST> struct route_shape_of
{
    static constexpr route_shape value {false, false, 0, 0};
};

template <> struct route_shape_of<method_eq_constraint> { static constexpr route_shape value {true, true, 0, 1}; };
template <> struct route_shape_of<target_constraint> { static constexpr route_shape value {true, false, 1, 0}; };
template <> struct route_shape_of<header_eq_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<header_exists_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<cookie_eq_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<cookie_exists_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<version_eq_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<version_ne_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<version_lt_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<version_le_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<version_gt_constraint> { static constexpr route_shape value {true, false, 0, 0}; };
template <> struct route_shape_of<version_ge_constraint> { static constexpr route_shape value {true, false, 0, 0}; };

template <typename L, typename R>
struct route_shape_of<and_<L, R>>
{
    static constexpr route_shape l = route_shape_of<L>::value;
    static constexpr route_shape r = route_shape_of<R>::value;
    static constexpr route_shape value {
        l.valid && r.valid,
        l.methods_only && r.methods_only,
        l.targets + r.targets,
        l.methods + r.methods};
};

template <typename L, typename R>
struct route_shape_of<or_<L, R>>
{
    static constexpr route_shape l = route_shape_of<L>::value;
    static constexpr route_shape r = route_shape_of<R>::value;
    static constexpr route_shape value {
        l.valid && r.valid && l.methods_only && r.methods_only,
        true,
        0,
        l.methods + r.methods};
};

template <typename X>
struct route_shape_of<not_<X>>
{
    static constexpr route_shape value {false, false, 0, 0};
};

/** True if an AST is a conjunction of exactly one `target == ...` and at
    least one `method == ...` (several may be joined with `||`), plus any
    number of header, cookie and version constraints.
*/
template <typename AST>
inline constexpr bool route_expressible_v =
    route_shape_of<AST>::value.valid &&
    route_shape_of<AST>::value.targets == 1 &&
    route_shape_of<AST>::value.methods >= 1;

/// A required header or cookie captured from a matcher.
struct route_input
{
    std::string name;
    std::string value;          ///< Empty for existence checks.
    bool exists_only = false;
};

/// The run-time values of an expressible route.
struct route_summary
{
    std::string template_str;           ///< The `target ==` path template.
    std::vector<std::string> methods;   ///< Verb names, in matcher order.
    std::vector<route_input> headers;
    std::vector<route_input> cookies;
};

namespace detail {

inline std::string verb_string(method_verb const& v)
{
    return std::visit([](auto const& x) -> std::string
    {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(x)>, std::string>) return x;
        else return std::string {boost::beast::http::to_string(x)};
    }, v);
}

inline std::string header_string(header_key const& k)
{
    return std::visit([](auto const& x) -> std::string
    {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(x)>, std::string>) return x;
        else return std::string {boost::beast::http::to_string(x)};
    }, k);
}

template <typename Leaf, typename Value>
void visit_route_leaf(Value const& v, route_summary& r)
{
    if constexpr (std::same_as<Leaf, method_eq_constraint>) r.methods.push_back(verb_string(v.verb));
    else if constexpr (std::same_as<Leaf, target_constraint>) r.template_str = v.template_str;
    else if constexpr (std::same_as<Leaf, header_eq_constraint>) r.headers.push_back({header_string(v.key), v.value, false});
    else if constexpr (std::same_as<Leaf, header_exists_constraint>) r.headers.push_back({header_string(v.key), {}, true});
    else if constexpr (std::same_as<Leaf, cookie_eq_constraint>) r.cookies.push_back({v.name, v.value, false});
    else if constexpr (std::same_as<Leaf, cookie_exists_constraint>) r.cookies.push_back({v.name, {}, true});
    else {}     // version constraints carry nothing a route summary records
}

template <typename TL> struct route_extractor;

template <typename... L>
struct route_extractor<type_list<L...>>
{
    template <typename Captures>
    static route_summary extract(Captures const& captures)
    {
        route_summary r;
        [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            (visit_route_leaf<L>(std::get<I>(captures), r), ...);
        }(std::index_sequence_for<L...> {});
        return r;
    }
};

} // namespace detail

/// Read the methods, path template, headers and cookies of an expressible operand.
template <typename Operand>
route_summary summarize_route(Operand const& op)
{
    using meta = typename Operand::metadata_type;
    static_assert(route_expressible_v<meta>, "summarize_route requires an expressible matcher.");
    return detail::route_extractor<flatten_t<meta>>::extract(op.captures());
}

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_ROUTE_SHAPE_HPP
