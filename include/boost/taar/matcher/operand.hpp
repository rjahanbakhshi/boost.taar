//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_MATCHER_HPP
#define BOOST_TAAR_MATCHER_MATCHER_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/core/cookies.hpp>
#include <boost/taar/matcher/detail/callable_with.hpp>
#include <boost/taar/type_traits/specialization_of.hpp>
#include <boost/taar/type_traits/super_type.hpp>
#include <boost/taar/type_traits/callable.hpp>
#include <boost/json/value.hpp>
#include <boost/url/url_view.hpp>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost::taar::matcher {

/** The canonical wrapper around a request matcher.

    `operand` adapts an arbitrary callable into the unified matcher
    interface that @ref boost::taar::session::http uses. After
    `request_type const&` and `context&` the callable may take any
    suffix of `(parsed_target, parsed_cookies, parsed_body)` in canonical
    order:

    @li no extras,
    @li `boost::urls::url_view const& parsed_target`,
    @li `taar::cookies const& parsed_cookies`,
    @li `boost::json::value const& parsed_body`,
    @li any pair of those, in canonical order,
    @li all three, in canonical order.

    Two legacy signatures from earlier versions remain supported for
    backward compatibility: `(parsed_target, parsed_cookies)` and its
    reversed counterpart `(parsed_cookies, parsed_target)`. New code is
    encouraged to use the canonical order.

    `operand` advertises which inputs the wrapped callable needs through
    `with_parsed_target`, `with_parsed_cookies`, and `with_parsed_body`.
    The session ORs each flag across all registered matchers and avoids
    the parse cost when no matcher needs the corresponding input.
    Composition operators preserve this opt-out: the composed operand
    only advertises an input when at least one of the composed sides
    actually needs it.

    Instances compose with `&&`, `||`, and unary `!`. Composition lifts the
    request type to the @ref boost::taar::type_traits::super_type_t
    "super type" of the operands and forwards the parsed inputs to whichever
    side needs them.

    In addition to its runtime behaviour, every operand carries a
    *metadata description* of its constraint: the type-level AST (the
    @a Metadata template parameter) and a runtime captures tuple
    (@ref captures_type, accessible via @ref captures). Leaf matchers
    such as @ref method_t and @ref target_t attach a tag and the
    captured value at construction time; composition operators combine
    the tags with @ref and_, @ref or_, @ref not_ and concatenate the
    captures tuples. See @ref matcher_metadata for the type vocabulary
    and the rationale.

    Users normally do not construct `operand` directly; matcher placeholders
    such as @ref boost::taar::matcher::method_t "method",
    @ref boost::taar::matcher::target_t "target", and others produce
    operands through their relational operators.

    @tparam RequestType  The Beast request header type the matcher accepts.
    @tparam CallableType The wrapped callable type.
    @tparam Metadata     The metadata AST node for this operand. Defaults
                         to @ref any_metadata, which means "opaque to
                         introspecting generators".
*/
template <typename RequestType, typename CallableType, typename Metadata = any_metadata>
requires (
    detail::callable_with<CallableType, bool, RequestType const&, context&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, boost::urls::url_view const&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, cookies const&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, boost::json::value const&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, boost::urls::url_view const&, cookies const&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, cookies const&, boost::urls::url_view const&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, boost::urls::url_view const&, boost::json::value const&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, cookies const&, boost::json::value const&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, boost::urls::url_view const&, cookies const&, boost::json::value const&>)
class operand
{
public:
    /// The Beast request-header type accepted by this matcher.
    using request_type = RequestType;
    /// The wrapped callable type.
    using callable_type = CallableType;
    /// The metadata AST that describes this operand's constraint.
    using metadata_type = Metadata;
    /// The runtime captures tuple keyed by the metadata's flattened leaf order.
    using captures_type = data_tuple_t<Metadata>;

    static constexpr int callabe_kind =
        detail::callable_with<
            CallableType, bool,
            RequestType const&, context&,
            boost::urls::url_view const&, cookies const&, boost::json::value const&> ? 8 :
        detail::callable_with<
            CallableType, bool,
            RequestType const&, context&,
            cookies const&, boost::json::value const&> ? 7 :
        detail::callable_with<
            CallableType, bool,
            RequestType const&, context&,
            boost::urls::url_view const&, boost::json::value const&> ? 6 :
        detail::callable_with<
            CallableType, bool,
            RequestType const&, context&,
            boost::json::value const&> ? 5 :
        detail::callable_with<
            CallableType, bool,
            RequestType const&, context&,
            boost::urls::url_view const&, cookies const&> ? 4 :
        detail::callable_with<
            CallableType, bool,
            RequestType const&, context&,
            cookies const&, boost::urls::url_view const&> ? 3 :
        detail::callable_with<
            CallableType, bool,
            RequestType const&, context&,
            boost::urls::url_view const&> ? 2 :
        detail::callable_with<
            CallableType, bool,
            RequestType const&, context&,
            cookies const&> ? 1 : 0;

    /// True if the wrapped callable needs the URL-parsed target.
    static constexpr auto with_parsed_target =
        callabe_kind == 8 || callabe_kind == 6 || callabe_kind == 4 ||
        callabe_kind == 3 || callabe_kind == 2;
    /// True if the wrapped callable needs the parsed cookie map.
    static constexpr auto with_parsed_cookies =
        callabe_kind == 8 || callabe_kind == 7 || callabe_kind == 4 ||
        callabe_kind == 3 || callabe_kind == 1;
    /// True if the wrapped callable needs the parsed JSON body.
    static constexpr auto with_parsed_body =
        callabe_kind == 8 || callabe_kind == 7 || callabe_kind == 6 ||
        callabe_kind == 5;

public:
    /** Construct from a callable that satisfies the matcher concept.

        This constructor is available only when @a Metadata is
        @ref any_metadata, because the operand has no leaf-specific value
        to capture. Leaf matchers use the
        @ref operand(callable_type, Metadata, captures_type) "metadata-aware
        constructor" instead.
    */
    operand(callable_type callable)
        requires std::same_as<Metadata, any_metadata>
    : callable_ {std::move(callable)}
    , captures_ {}
    {}

    /** Construct with an explicit metadata description and captures.

        Used by leaf matcher placeholders (e.g. @ref method_t,
        @ref target_t) and by the composition operators to assemble a
        new operand whose metadata describes the combined constraint.

        @param callable The wrapped predicate.
        @param          A value of @a Metadata. Carries no information at
                        run time; passed by value to drive CTAD on the
                        deduction guide.
        @param captures The captured values for the leaves of @a Metadata.
    */
    operand(callable_type callable, Metadata, captures_type captures)
    : callable_ {std::move(callable)}
    , captures_ {std::move(captures)}
    {}

    /// The captured values for this operand's metadata leaves.
    constexpr captures_type const& captures() const noexcept
    {
        return captures_;
    }

    /** Invoke the wrapped matcher.

        The session always passes the parsed target, the parsed cookies,
        and the parsed JSON body; operands that do not need a particular
        input ignore the corresponding argument.

        @param request        The incoming request header.
        @param context        Per-request scratch space (see @ref context).
        @param parsed_target  The request target after URL parsing.
        @param parsed_cookies The `Cookie` header parsed into a key/value map.
        @param parsed_body    The request body parsed as a `boost::json::value`.
                              Null when the session has no body cache or the
                              body could not be parsed as JSON.
        @returns `true` if the request matches.
    */
    auto operator()(
        request_type const& request,
        context& context,
        boost::urls::url_view const& parsed_target,
        cookies const& parsed_cookies,
        boost::json::value const& parsed_body) const
    {
        if constexpr (callabe_kind == 8)
        {
            return callable_(request, context, parsed_target, parsed_cookies, parsed_body);
        }
        else if constexpr (callabe_kind == 7)
        {
            return callable_(request, context, parsed_cookies, parsed_body);
        }
        else if constexpr (callabe_kind == 6)
        {
            return callable_(request, context, parsed_target, parsed_body);
        }
        else if constexpr (callabe_kind == 5)
        {
            return callable_(request, context, parsed_body);
        }
        else if constexpr (callabe_kind == 4)
        {
            return callable_(request, context, parsed_target, parsed_cookies);
        }
        else if constexpr (callabe_kind == 3)
        {
            return callable_(request, context, parsed_cookies, parsed_target);
        }
        else if constexpr (callabe_kind == 2)
        {
            return callable_(request, context, parsed_target);
        }
        else if constexpr (callabe_kind == 1)
        {
            return callable_(request, context, parsed_cookies);
        }
        else
        {
            return callable_(request, context);
        }
    }

    /// Negate the wrapped matcher.
    friend auto operator!(operand opr)
    {
        using new_meta = not_<Metadata>;
        auto new_caps = opr.captures_;

        if constexpr (with_parsed_target && with_parsed_cookies && with_parsed_body)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target,
                    cookies const& parsed_cookies,
                    boost::json::value const& parsed_body)
                {
                    return !opr(request, context, parsed_target, parsed_cookies, parsed_body);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_parsed_target && with_parsed_cookies)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target,
                    cookies const& parsed_cookies)
                {
                    return !opr(request, context, parsed_target, parsed_cookies, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_parsed_target && with_parsed_body)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target,
                    boost::json::value const& parsed_body)
                {
                    return !opr(request, context, parsed_target, {}, parsed_body);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_parsed_cookies && with_parsed_body)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    request_type const& request,
                    context& context,
                    cookies const& parsed_cookies,
                    boost::json::value const& parsed_body)
                {
                    return !opr(request, context, {}, parsed_cookies, parsed_body);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_parsed_target)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target)
                {
                    return !opr(request, context, parsed_target, {}, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_parsed_cookies)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    request_type const& request,
                    context& context,
                    cookies const& parsed_cookies)
                {
                    return !opr(request, context, {}, parsed_cookies, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_parsed_body)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    request_type const& request,
                    context& context,
                    boost::json::value const& parsed_body)
                {
                    return !opr(request, context, {}, {}, parsed_body);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else
        {
            return matcher::operand {
                [opr = std::move(opr)](request_type const& request, context& context)
                {
                    return !opr(request, context, {}, {}, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
    }

    // Binary composition `op && raw`, `raw && op`, `op || raw`, `raw || op`,
    // and `op && op`, `op || op` are defined as free function templates
    // below — they don't need private access.

private:
    callable_type callable_;
    captures_type captures_;
};

template <typename ObjectType>
operand(ObjectType) ->
    operand<
        std::remove_cvref_t<
            typename type_traits::callable<std::remove_cvref_t<ObjectType>
        >::template arg<0>
    >, ObjectType>;

template <typename ObjectType, typename MetaType>
operand(ObjectType, MetaType, data_tuple_t<MetaType>) ->
    operand<
        std::remove_cvref_t<
            typename type_traits::callable<std::remove_cvref_t<ObjectType>
        >::template arg<0>
    >, ObjectType, MetaType>;

namespace detail {

struct and_tag {};
struct or_tag  {};

/** Build a composed operand from two operands and a binary tag.

    Computes the union of `with_parsed_target / _cookies / _body` across
    the two sides and emits a lambda whose signature drops the unused
    parsed inputs — so the composed operand correctly advertises only
    what it actually needs. 8 branches cover every subset of the three
    parsed inputs.
 */
template <
    typename Op,
    typename SuperReq,
    typename Lhs,
    typename Rhs,
    typename NewMeta,
    typename NewCaps>
auto compose_operands(Lhs lhs, Rhs rhs, NewMeta meta_tag, NewCaps caps)
{
    constexpr auto wt = Lhs::with_parsed_target  || Rhs::with_parsed_target;
    constexpr auto wc = Lhs::with_parsed_cookies || Rhs::with_parsed_cookies;
    constexpr auto wb = Lhs::with_parsed_body    || Rhs::with_parsed_body;

    if constexpr (wt && wc && wb)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                SuperReq const& request,
                context& context,
                boost::urls::url_view const& parsed_target,
                cookies const& parsed_cookies,
                boost::json::value const& parsed_body)
            {
                if constexpr (std::is_same_v<Op, and_tag>)
                    return lhs(request, context, parsed_target, parsed_cookies, parsed_body) &&
                           rhs(request, context, parsed_target, parsed_cookies, parsed_body);
                else
                    return lhs(request, context, parsed_target, parsed_cookies, parsed_body) ||
                           rhs(request, context, parsed_target, parsed_cookies, parsed_body);
            },
            meta_tag,
            std::move(caps)
        };
    }
    else if constexpr (wt && wc)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                SuperReq const& request,
                context& context,
                boost::urls::url_view const& parsed_target,
                cookies const& parsed_cookies)
            {
                if constexpr (std::is_same_v<Op, and_tag>)
                    return lhs(request, context, parsed_target, parsed_cookies, {}) &&
                           rhs(request, context, parsed_target, parsed_cookies, {});
                else
                    return lhs(request, context, parsed_target, parsed_cookies, {}) ||
                           rhs(request, context, parsed_target, parsed_cookies, {});
            },
            meta_tag,
            std::move(caps)
        };
    }
    else if constexpr (wt && wb)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                SuperReq const& request,
                context& context,
                boost::urls::url_view const& parsed_target,
                boost::json::value const& parsed_body)
            {
                if constexpr (std::is_same_v<Op, and_tag>)
                    return lhs(request, context, parsed_target, {}, parsed_body) &&
                           rhs(request, context, parsed_target, {}, parsed_body);
                else
                    return lhs(request, context, parsed_target, {}, parsed_body) ||
                           rhs(request, context, parsed_target, {}, parsed_body);
            },
            meta_tag,
            std::move(caps)
        };
    }
    else if constexpr (wc && wb)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                SuperReq const& request,
                context& context,
                cookies const& parsed_cookies,
                boost::json::value const& parsed_body)
            {
                if constexpr (std::is_same_v<Op, and_tag>)
                    return lhs(request, context, {}, parsed_cookies, parsed_body) &&
                           rhs(request, context, {}, parsed_cookies, parsed_body);
                else
                    return lhs(request, context, {}, parsed_cookies, parsed_body) ||
                           rhs(request, context, {}, parsed_cookies, parsed_body);
            },
            meta_tag,
            std::move(caps)
        };
    }
    else if constexpr (wt)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                SuperReq const& request,
                context& context,
                boost::urls::url_view const& parsed_target)
            {
                if constexpr (std::is_same_v<Op, and_tag>)
                    return lhs(request, context, parsed_target, {}, {}) &&
                           rhs(request, context, parsed_target, {}, {});
                else
                    return lhs(request, context, parsed_target, {}, {}) ||
                           rhs(request, context, parsed_target, {}, {});
            },
            meta_tag,
            std::move(caps)
        };
    }
    else if constexpr (wc)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                SuperReq const& request,
                context& context,
                cookies const& parsed_cookies)
            {
                if constexpr (std::is_same_v<Op, and_tag>)
                    return lhs(request, context, {}, parsed_cookies, {}) &&
                           rhs(request, context, {}, parsed_cookies, {});
                else
                    return lhs(request, context, {}, parsed_cookies, {}) ||
                           rhs(request, context, {}, parsed_cookies, {});
            },
            meta_tag,
            std::move(caps)
        };
    }
    else if constexpr (wb)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                SuperReq const& request,
                context& context,
                boost::json::value const& parsed_body)
            {
                if constexpr (std::is_same_v<Op, and_tag>)
                    return lhs(request, context, {}, {}, parsed_body) &&
                           rhs(request, context, {}, {}, parsed_body);
                else
                    return lhs(request, context, {}, {}, parsed_body) ||
                           rhs(request, context, {}, {}, parsed_body);
            },
            meta_tag,
            std::move(caps)
        };
    }
    else
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                SuperReq const& request,
                context& context)
            {
                if constexpr (std::is_same_v<Op, and_tag>)
                    return lhs(request, context, {}, {}, {}) &&
                           rhs(request, context, {}, {}, {});
                else
                    return lhs(request, context, {}, {}, {}) ||
                           rhs(request, context, {}, {}, {});
            },
            meta_tag,
            std::move(caps)
        };
    }
}

} // namespace detail

/// Short-circuit AND; the right operand is a raw callable.
template <typename Req, typename Call, typename Meta, typename RHSType>
requires (!type_traits::specialization_of<matcher::operand, std::remove_cvref_t<RHSType>>)
auto operator&&(operand<Req, Call, Meta> lhs_operand, RHSType&& rhs)
{
    matcher::operand rhs_operand {std::forward<RHSType>(rhs)};
    using rhs_operand_type   = decltype(rhs_operand);
    using rhs_request_type   = typename rhs_operand_type::request_type;
    using rhs_meta           = typename rhs_operand_type::metadata_type;
    using super_request_type = type_traits::super_type_t<Req, rhs_request_type>;
    using new_meta           = and_<Meta, rhs_meta>;

    auto new_caps = std::tuple_cat(lhs_operand.captures(), rhs_operand.captures());

    return detail::compose_operands<detail::and_tag, super_request_type>(
        std::move(lhs_operand),
        std::move(rhs_operand),
        new_meta {},
        std::move(new_caps));
}

/// Short-circuit AND; the left operand is a raw callable.
template <typename LHSType, typename Req, typename Call, typename Meta>
requires (!type_traits::specialization_of<matcher::operand, std::remove_cvref_t<LHSType>>)
auto operator&&(LHSType&& lhs, operand<Req, Call, Meta> rhs_operand)
{
    matcher::operand lhs_operand {std::forward<LHSType>(lhs)};
    using lhs_operand_type   = decltype(lhs_operand);
    using lhs_request_type   = typename lhs_operand_type::request_type;
    using lhs_meta           = typename lhs_operand_type::metadata_type;
    using super_request_type = type_traits::super_type_t<lhs_request_type, Req>;
    using new_meta           = and_<lhs_meta, Meta>;

    auto new_caps = std::tuple_cat(lhs_operand.captures(), rhs_operand.captures());

    return detail::compose_operands<detail::and_tag, super_request_type>(
        std::move(lhs_operand),
        std::move(rhs_operand),
        new_meta {},
        std::move(new_caps));
}

/// Short-circuit OR; the right operand is a raw callable.
template <typename Req, typename Call, typename Meta, typename RHSType>
requires (!type_traits::specialization_of<matcher::operand, std::remove_cvref_t<RHSType>>)
auto operator||(operand<Req, Call, Meta> lhs_operand, RHSType&& rhs)
{
    matcher::operand rhs_operand {std::forward<RHSType>(rhs)};
    using rhs_operand_type   = decltype(rhs_operand);
    using rhs_request_type   = typename rhs_operand_type::request_type;
    using rhs_meta           = typename rhs_operand_type::metadata_type;
    using super_request_type = type_traits::super_type_t<Req, rhs_request_type>;
    using new_meta           = or_<Meta, rhs_meta>;

    auto new_caps = std::tuple_cat(lhs_operand.captures(), rhs_operand.captures());

    return detail::compose_operands<detail::or_tag, super_request_type>(
        std::move(lhs_operand),
        std::move(rhs_operand),
        new_meta {},
        std::move(new_caps));
}

/// Short-circuit OR; the left operand is a raw callable.
template <typename LHSType, typename Req, typename Call, typename Meta>
requires (!type_traits::specialization_of<matcher::operand, std::remove_cvref_t<LHSType>>)
auto operator||(LHSType&& lhs, operand<Req, Call, Meta> rhs_operand)
{
    matcher::operand lhs_operand {std::forward<LHSType>(lhs)};
    using lhs_operand_type   = decltype(lhs_operand);
    using lhs_request_type   = typename lhs_operand_type::request_type;
    using lhs_meta           = typename lhs_operand_type::metadata_type;
    using super_request_type = type_traits::super_type_t<lhs_request_type, Req>;
    using new_meta           = or_<lhs_meta, Meta>;

    auto new_caps = std::tuple_cat(lhs_operand.captures(), rhs_operand.captures());

    return detail::compose_operands<detail::or_tag, super_request_type>(
        std::move(lhs_operand),
        std::move(rhs_operand),
        new_meta {},
        std::move(new_caps));
}

// --- Operand-by-operand composition ----------------------------------------
//
// The four overloads above intentionally reject the case where the *other*
// side is itself an `operand` specialisation, so we route `op && op` and
// `op || op` through these dedicated function templates. Both forms combine
// metadata under `and_` / `or_` and concatenate the captures tuples without
// re-wrapping either operand.

template <
    typename LReq, typename LCall, typename LMeta,
    typename RReq, typename RCall, typename RMeta>
auto operator&&(operand<LReq, LCall, LMeta> lhs, operand<RReq, RCall, RMeta> rhs)
{
    using super_request_type = type_traits::super_type_t<LReq, RReq>;
    using new_meta = and_<LMeta, RMeta>;

    auto new_caps = std::tuple_cat(lhs.captures(), rhs.captures());

    return detail::compose_operands<detail::and_tag, super_request_type>(
        std::move(lhs), std::move(rhs), new_meta {}, std::move(new_caps));
}

template <
    typename LReq, typename LCall, typename LMeta,
    typename RReq, typename RCall, typename RMeta>
auto operator||(operand<LReq, LCall, LMeta> lhs, operand<RReq, RCall, RMeta> rhs)
{
    using super_request_type = type_traits::super_type_t<LReq, RReq>;
    using new_meta = or_<LMeta, RMeta>;

    auto new_caps = std::tuple_cat(lhs.captures(), rhs.captures());

    return detail::compose_operands<detail::or_tag, super_request_type>(
        std::move(lhs), std::move(rhs), new_meta {}, std::move(new_caps));
}

/** True if @a MatcherType can be wrapped in an `operand`.

    This is the formal definition of the *Matcher* named requirement
    described in the library's Concepts chapter.
*/
template <typename MatcherType>
concept is_matcher = requires(MatcherType&& matcher)
{
    operand{std::forward<MatcherType>(matcher)};
};

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_MATCHER_HPP
