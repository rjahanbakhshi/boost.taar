//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_MATCHER_METADATA_HPP
#define BOOST_TAAR_MATCHER_METADATA_HPP

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <cstddef>
#include <string>
#include <tuple>
#include <variant>

namespace boost::taar::matcher {

/** @defgroup matcher_metadata Matcher metadata

    The metadata machinery gives `boost::taar::matcher::operand` a
    *protocol-independent description* of the constraints it enforces, so
    that auxiliary generators (OpenAPI documents, MCP tool listings, ...) can
    introspect a registered route without re-implementing the matcher DSL.

    The description is split between the type level and the value level:

    @li Each matcher *kind* is a distinct tag type (`method_eq_constraint`,
        `target_constraint`, `header_eq_constraint`, ...) that carries no
        captured value. Tag types compose with the boolean combinators
        `and_<L, R>`, `or_<L, R>`, and `not_<X>` to form an AST that
        describes a matcher's shape.

    @li The *value* a leaf captured at construction time (the matched verb,
        the path template string, the required header value, ...) lives in
        the operand's runtime captures tuple. The tuple is keyed by the
        flattened leaf order in the AST and has type
        `data_tuple_t<Metadata>`.

    A generator walks the AST (compile-time) to make policy decisions about
    which shapes it accepts, and reads from the captures tuple (run-time)
    to obtain the actual values to embed in its output. The runtime
    cross-checks that depend on captured values (for example, that every
    `path_arg("name")` corresponds to a `{name}` segment in the captured
    target template) happen at handler-registration time.
 */

// ---- Leaf-tag types --------------------------------------------------------
// Each tag represents the *kind* of constraint a matcher imposes. Tags carry
// no values themselves; per-leaf values live in the operand's captures tuple,
// keyed by leaf order via @ref data_tuple_t.

/// Sentinel for matchers that expose no structured metadata. Generators that
/// require introspectable matchers reject ASTs containing `any_metadata`.
struct any_metadata {};

struct method_eq_constraint {};        ///< `method == V`
struct method_ne_constraint {};        ///< `method != V`

struct target_constraint {};           ///< `target == "/path/template"`

struct header_eq_constraint {};        ///< `header(k) == V`
struct header_ne_constraint {};        ///< `header(k) != V`
struct header_exists_constraint {};    ///< `exist(header(k))`

struct cookie_eq_constraint {};        ///< `cookie(n) == V`
struct cookie_ne_constraint {};        ///< `cookie(n) != V`
struct cookie_exists_constraint {};    ///< `exist(cookie(n))`

struct version_eq_constraint {};       ///< `version == N`
struct version_ne_constraint {};       ///< `version != N`
struct version_lt_constraint {};       ///< `version <  N`
struct version_le_constraint {};       ///< `version <= N`
struct version_gt_constraint {};       ///< `version >  N`
struct version_ge_constraint {};       ///< `version >= N`

// ---- Boolean combinators ---------------------------------------------------

template <typename L, typename R> struct and_ {};   ///< AST node: `L && R`.
template <typename L, typename R> struct or_  {};   ///< AST node: `L || R`.
template <typename X>             struct not_ {};   ///< AST node: `!X`.

// ---- Captured-value carriers ----------------------------------------------

/// Either a well-known beast field or an arbitrary header name.
using header_key = std::variant<boost::beast::http::field, std::string>;

/// Either a well-known beast verb or an arbitrary verb string.
using method_verb = std::variant<boost::beast::http::verb, std::string>;

/// Captured value for `method_eq_constraint` / `method_ne_constraint`.
struct method_constraint_value
{
    method_verb verb;
};

/// Captured value for `target_constraint`. The template string is the
/// literal supplied to `target == "..."`, unparsed.
struct target_constraint_value
{
    std::string template_str;
};

/// Captured value for `header_eq_constraint` / `header_ne_constraint` /
/// `header_exists_constraint`. The @c value field is empty when the leaf is
/// an existence check.
struct header_constraint_value
{
    header_key key;
    std::string value;
};

/// Captured value for `cookie_eq_constraint` / `cookie_ne_constraint` /
/// `cookie_exists_constraint`. The @c value field is empty when the leaf is
/// an existence check.
struct cookie_constraint_value
{
    std::string name;
    std::string value;
};

/// Captured value for all `version_*_constraint` leaves.
struct version_constraint_value
{
    unsigned value;
};

// ---- Leaf-to-data-type map ------------------------------------------------

/// Trait: the runtime captured-value type associated with leaf tag @a Tag.
template <typename Tag> struct data_of;

template <> struct data_of<method_eq_constraint>     { using type = method_constraint_value;  };
template <> struct data_of<method_ne_constraint>     { using type = method_constraint_value;  };
template <> struct data_of<target_constraint>        { using type = target_constraint_value;  };
template <> struct data_of<header_eq_constraint>     { using type = header_constraint_value;  };
template <> struct data_of<header_ne_constraint>     { using type = header_constraint_value;  };
template <> struct data_of<header_exists_constraint> { using type = header_constraint_value;  };
template <> struct data_of<cookie_eq_constraint>     { using type = cookie_constraint_value;  };
template <> struct data_of<cookie_ne_constraint>     { using type = cookie_constraint_value;  };
template <> struct data_of<cookie_exists_constraint> { using type = cookie_constraint_value;  };
template <> struct data_of<version_eq_constraint>    { using type = version_constraint_value; };
template <> struct data_of<version_ne_constraint>    { using type = version_constraint_value; };
template <> struct data_of<version_lt_constraint>    { using type = version_constraint_value; };
template <> struct data_of<version_le_constraint>    { using type = version_constraint_value; };
template <> struct data_of<version_gt_constraint>    { using type = version_constraint_value; };
template <> struct data_of<version_ge_constraint>    { using type = version_constraint_value; };

template <typename Tag> using data_of_t = typename data_of<Tag>::type;

// ---- Type-list utilities --------------------------------------------------

/// A pack-of-types holder used as the result of @ref flatten.
template <typename... T> struct type_list {};

template <typename A, typename B> struct concat_;
template <typename... A, typename... B>
struct concat_<type_list<A...>, type_list<B...>>
{
    using type = type_list<A..., B...>;
};

/** Flatten an AST into a left-to-right `type_list` of its leaves.

    `not_<X>` contributes the same leaves as @a X. `any_metadata` and the
    empty list are treated as having no leaves; leaves themselves form a
    one-element list.
*/
template <typename AST> struct flatten { using type = type_list<AST>; };
template <> struct flatten<any_metadata> { using type = type_list<>; };

template <typename L, typename R>
struct flatten<and_<L, R>>
{
    using type = typename concat_<typename flatten<L>::type, typename flatten<R>::type>::type;
};

template <typename L, typename R>
struct flatten<or_<L, R>>
{
    using type = typename concat_<typename flatten<L>::type, typename flatten<R>::type>::type;
};

template <typename X>
struct flatten<not_<X>>
{
    using type = typename flatten<X>::type;
};

/// Convenience alias for `flatten<AST>::type`.
template <typename AST> using flatten_t = typename flatten<AST>::type;

/// Trait: the number of leaves in a flattened type list.
template <typename TL> struct list_size;
template <typename... T>
struct list_size<type_list<T...>>
{
    static constexpr std::size_t value = sizeof...(T);
};

/// Convenience variable template for @ref list_size.
template <typename TL>
inline constexpr std::size_t list_size_v = list_size<TL>::value;

// ---- Captures-tuple synthesis ---------------------------------------------

template <typename TL> struct data_tuple_impl_;
template <typename... L>
struct data_tuple_impl_<type_list<L...>>
{
    using type = std::tuple<data_of_t<L>...>;
};

/** The runtime captures tuple for an AST.

    The tuple is keyed by the flattened leaf order, so the value captured
    for a particular leaf lives at the same index in the tuple as the leaf
    in @ref flatten_t.
*/
template <typename AST>
using data_tuple_t = typename data_tuple_impl_<flatten_t<AST>>::type;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_METADATA_HPP
