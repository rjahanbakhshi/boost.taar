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
#include <boost/url/url_view.hpp>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost::taar::matcher {

/** The canonical wrapper around a request matcher.

    `operand` adapts an arbitrary callable into the unified matcher
    interface that @ref boost::taar::session::basic_http "session::http" uses. The callable may
    take any of the following parameter packs (after `request_type const&`
    and `context&`):

    @li no extra parameters,
    @li `boost::urls::url_view const& parsed_target`,
    @li `taar::cookies const& parsed_cookies`,
    @li both, in either order.

    `operand` advertises whether the wrapped callable needs the parsed
    target and/or parsed cookies through the `with_parsed_target` and
    `with_parsed_cookies` constants, so the session can avoid the
    parsing cost when no registered matcher needs it.

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
    captures tuples. See `<boost/taar/matcher/metadata.hpp>` for the type vocabulary
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
    detail::callable_with<CallableType, bool, RequestType const&, context&, boost::urls::url_view const&, cookies const&> ||
    detail::callable_with<CallableType, bool, RequestType const&, context&, cookies const&, boost::urls::url_view const&>)
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
            CallableType,
            bool,
            RequestType const&,
            context&,
            boost::urls::url_view const&,
            cookies const&> ? 4 :
        detail::callable_with<
            CallableType,
            bool,
            RequestType const&,
            context&,
            cookies const&,
            boost::urls::url_view const&> ? 3 :
        detail::callable_with<
            CallableType,
            bool,
            RequestType const&,
            context&,
            boost::urls::url_view const&> ? 2 :
        detail::callable_with<
            CallableType,
            bool,
            RequestType const&,
            context&,
            cookies const&> ? 1 : 0;
    /// True if the wrapped callable needs the URL-parsed target.
    static constexpr auto with_parsed_target =
        callabe_kind == 4 || callabe_kind == 3 || callabe_kind == 2;
    /// True if the wrapped callable needs the parsed cookie map.
    static constexpr auto with_parsed_cookies =
        callabe_kind == 4 || callabe_kind == 3 || callabe_kind == 1;

public:
    /** Construct from a callable that satisfies the matcher concept.

        This constructor is available only when @a Metadata is
        @ref any_metadata, because the operand has no leaf-specific value
        to capture. Leaf matchers use the
        @ref operand(callable_type, Metadata, captures_type) "metadata-aware constructor"
        instead.
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

        The unnamed second parameter is a value of @a Metadata. It carries
        no information at run time; it is passed by value to drive CTAD on
        the deduction guide.

        @param callable The wrapped predicate.
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

        The session always passes the parsed target and the parsed cookies;
        operands that do not need them ignore the corresponding arguments.

        @param request        The incoming request header.
        @param context        Per-request scratch space (see @ref context).
        @param parsed_target  The request target after URL parsing.
        @param parsed_cookies The `Cookie` header parsed into a key/value map.
        @returns `true` if the request matches.
    */

    auto operator()(
        request_type const& request,
        context& context,
        boost::urls::url_view const& parsed_target,
        cookies const& parsed_cookies) const
    {
        if constexpr (callabe_kind == 4)
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

        if constexpr (with_parsed_target && with_parsed_cookies)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target,
                    cookies const& parsed_cookies)
                {
                    return !opr.operator()(request, context, parsed_target, parsed_cookies);
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
                    return !opr.operator()(request, context, parsed_target, {});
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
                    return !opr.operator()(request, context, {}, parsed_cookies);
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
                    return !opr.operator()(request, context, {}, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
    }

    /// Short-circuit AND; the right operand is a raw callable.
    template <typename RHSType>
    requires (!type_traits::specialization_of<matcher::operand, std::remove_cvref_t<RHSType>>)
    friend auto operator&&(
        operand lhs_operand,
        RHSType&& rhs)
    {
        matcher::operand rhs_operand {std::forward<RHSType>(rhs)};
        using rhs_operand_type = decltype(rhs_operand);
        using rhs_request_type = typename rhs_operand_type::request_type;
        using rhs_meta = typename rhs_operand_type::metadata_type;
        using super_request_type = type_traits::super_type_t<request_type, rhs_request_type>;
        using new_meta = and_<Metadata, rhs_meta>;

        auto new_caps = std::tuple_cat(lhs_operand.captures_, rhs_operand.captures());

        constexpr auto with_target = with_parsed_target || rhs_operand_type::with_parsed_target;
        constexpr auto with_cookie = with_parsed_cookies || rhs_operand_type::with_parsed_cookies;

        if constexpr (with_target && with_cookie)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target,
                    cookies const& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, parsed_target, parsed_cookies) &&
                        rhs_operand(request, context, parsed_target, parsed_cookies);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target, {}) &&
                        rhs_operand(request, context, parsed_target, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_cookie)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    cookies const& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, {}, parsed_cookies) &&
                        rhs_operand(request, context, {}, parsed_cookies);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context, {}, {}) &&
                        rhs_operand(request, context, {}, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
    }

    /// Short-circuit AND; the left operand is a raw callable.
    template <typename LHSType>
    requires (!type_traits::specialization_of<matcher::operand, std::remove_cvref_t<LHSType>>)
    friend auto operator&&(
        LHSType&& lhs,
        operand rhs_operand)
    {
        matcher::operand lhs_operand {std::forward<LHSType>(lhs)};
        using lhs_operand_type = decltype(lhs_operand);
        using lhs_request_type = typename lhs_operand_type::request_type;
        using lhs_meta = typename lhs_operand_type::metadata_type;
        using super_request_type = type_traits::super_type_t<request_type, lhs_request_type>;
        using new_meta = and_<lhs_meta, Metadata>;

        auto new_caps = std::tuple_cat(lhs_operand.captures(), rhs_operand.captures_);

        constexpr auto with_target = with_parsed_target || lhs_operand_type::with_parsed_target;
        constexpr auto with_cookie = with_parsed_cookies || lhs_operand_type::with_parsed_cookies;

        if constexpr (with_target && with_cookie)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target,
                    cookies const& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, parsed_target, parsed_cookies) &&
                        rhs_operand(request, context, parsed_target, parsed_cookies);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target, {}) &&
                        rhs_operand(request, context, parsed_target, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_cookie)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    cookies const& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, {}, parsed_cookies) &&
                        rhs_operand(request, context, {}, parsed_cookies);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context, {}, {}) &&
                        rhs_operand(request, context, {}, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
    }

    /// Short-circuit OR; the right operand is a raw callable.
    template <typename RHSType>
    requires (!type_traits::specialization_of<matcher::operand, std::remove_cvref_t<RHSType>>)
    friend auto operator||(
        operand lhs_operand,
        RHSType&& rhs)
    {
        matcher::operand rhs_operand {std::forward<RHSType>(rhs)};
        using rhs_operand_type = decltype(rhs_operand);
        using rhs_request_type = typename rhs_operand_type::request_type;
        using rhs_meta = typename rhs_operand_type::metadata_type;
        using super_request_type = type_traits::super_type_t<request_type, rhs_request_type>;
        using new_meta = or_<Metadata, rhs_meta>;

        auto new_caps = std::tuple_cat(lhs_operand.captures_, rhs_operand.captures());

        constexpr auto with_target = with_parsed_target || rhs_operand_type::with_parsed_target;
        constexpr auto with_cookie = with_parsed_cookies || rhs_operand_type::with_parsed_cookies;

        if constexpr (with_target && with_cookie)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target,
                    cookies const& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, parsed_target, parsed_cookies) ||
                        rhs_operand(request, context, parsed_target, parsed_cookies);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target, {}) ||
                        rhs_operand(request, context, parsed_target, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_cookie)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    cookies const& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, {}, parsed_cookies) ||
                        rhs_operand(request, context, {}, parsed_cookies);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context, {}, {}) ||
                        rhs_operand(request, context, {}, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
    }

    /// Short-circuit OR; the left operand is a raw callable.
    template <typename LHSType>
    requires (!type_traits::specialization_of<matcher::operand, std::remove_cvref_t<LHSType>>)
    friend auto operator||(
        LHSType&& lhs,
        operand rhs_operand)
    {
        matcher::operand lhs_operand {std::forward<LHSType>(lhs)};
        using lhs_operand_type = decltype(lhs_operand);
        using lhs_request_type = typename lhs_operand_type::request_type;
        using lhs_meta = typename lhs_operand_type::metadata_type;
        using super_request_type = type_traits::super_type_t<request_type, lhs_request_type>;
        using new_meta = or_<lhs_meta, Metadata>;

        auto new_caps = std::tuple_cat(lhs_operand.captures(), rhs_operand.captures_);

        constexpr auto with_target = with_parsed_target || lhs_operand_type::with_parsed_target;
        constexpr auto with_cookie = with_parsed_cookies || lhs_operand_type::with_parsed_cookies;

        if constexpr (with_target && with_cookie)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target,
                    cookies const& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, parsed_target, parsed_cookies) ||
                        rhs_operand(request, context, parsed_target, parsed_cookies);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    boost::urls::url_view const& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target, {}) ||
                        rhs_operand(request, context, parsed_target, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else if constexpr (with_cookie)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context,
                    cookies const& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, {}, parsed_cookies) ||
                        rhs_operand(request, context, {}, parsed_cookies);
                },
                new_meta {},
                std::move(new_caps)
            };
        }
        else
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    super_request_type const& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context, {}, {}) ||
                        rhs_operand(request, context, {}, {});
                },
                new_meta {},
                std::move(new_caps)
            };
        }
    }

private:
    callable_type callable_;
    captures_type captures_;
};

// Deduction guides, hidden from Doxygen: BoostBook lists a guide both in
// the class synopsis and as an entry of its own, with the same id.
/// @cond
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
/// @endcond

// --- Operand-by-operand composition ----------------------------------------
//
// The class-template friend overloads above intentionally reject the case
// where the *other* side is itself an `operand` specialisation, so we route
// `op && op` and `op || op` through the function templates below. They
// combine both sides' metadata under @ref and_ / @ref or_ and concatenate
// the captures tuples without re-wrapping either operand.

template <
    typename LReq, typename LCall, typename LMeta,
    typename RReq, typename RCall, typename RMeta>
auto operator&&(operand<LReq, LCall, LMeta> lhs, operand<RReq, RCall, RMeta> rhs)
{
    using lhs_type = operand<LReq, LCall, LMeta>;
    using rhs_type = operand<RReq, RCall, RMeta>;
    using super_request_type = type_traits::super_type_t<LReq, RReq>;
    using new_meta = and_<LMeta, RMeta>;

    auto new_caps = std::tuple_cat(lhs.captures(), rhs.captures());

    constexpr auto with_target = lhs_type::with_parsed_target || rhs_type::with_parsed_target;
    constexpr auto with_cookie = lhs_type::with_parsed_cookies || rhs_type::with_parsed_cookies;

    if constexpr (with_target && with_cookie)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                super_request_type const& request,
                context& context,
                boost::urls::url_view const& parsed_target,
                cookies const& parsed_cookies)
            {
                return
                    lhs(request, context, parsed_target, parsed_cookies) &&
                    rhs(request, context, parsed_target, parsed_cookies);
            },
            new_meta {},
            std::move(new_caps)
        };
    }
    else if constexpr (with_target)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                super_request_type const& request,
                context& context,
                boost::urls::url_view const& parsed_target)
            {
                return
                    lhs(request, context, parsed_target, {}) &&
                    rhs(request, context, parsed_target, {});
            },
            new_meta {},
            std::move(new_caps)
        };
    }
    else if constexpr (with_cookie)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                super_request_type const& request,
                context& context,
                cookies const& parsed_cookies)
            {
                return
                    lhs(request, context, {}, parsed_cookies) &&
                    rhs(request, context, {}, parsed_cookies);
            },
            new_meta {},
            std::move(new_caps)
        };
    }
    else
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                super_request_type const& request,
                context& context)
            {
                return
                    lhs(request, context, {}, {}) &&
                    rhs(request, context, {}, {});
            },
            new_meta {},
            std::move(new_caps)
        };
    }
}

template <
    typename LReq, typename LCall, typename LMeta,
    typename RReq, typename RCall, typename RMeta>
auto operator||(operand<LReq, LCall, LMeta> lhs, operand<RReq, RCall, RMeta> rhs)
{
    using lhs_type = operand<LReq, LCall, LMeta>;
    using rhs_type = operand<RReq, RCall, RMeta>;
    using super_request_type = type_traits::super_type_t<LReq, RReq>;
    using new_meta = or_<LMeta, RMeta>;

    auto new_caps = std::tuple_cat(lhs.captures(), rhs.captures());

    constexpr auto with_target = lhs_type::with_parsed_target || rhs_type::with_parsed_target;
    constexpr auto with_cookie = lhs_type::with_parsed_cookies || rhs_type::with_parsed_cookies;

    if constexpr (with_target && with_cookie)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                super_request_type const& request,
                context& context,
                boost::urls::url_view const& parsed_target,
                cookies const& parsed_cookies)
            {
                return
                    lhs(request, context, parsed_target, parsed_cookies) ||
                    rhs(request, context, parsed_target, parsed_cookies);
            },
            new_meta {},
            std::move(new_caps)
        };
    }
    else if constexpr (with_target)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                super_request_type const& request,
                context& context,
                boost::urls::url_view const& parsed_target)
            {
                return
                    lhs(request, context, parsed_target, {}) ||
                    rhs(request, context, parsed_target, {});
            },
            new_meta {},
            std::move(new_caps)
        };
    }
    else if constexpr (with_cookie)
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                super_request_type const& request,
                context& context,
                cookies const& parsed_cookies)
            {
                return
                    lhs(request, context, {}, parsed_cookies) ||
                    rhs(request, context, {}, parsed_cookies);
            },
            new_meta {},
            std::move(new_caps)
        };
    }
    else
    {
        return matcher::operand
        {
            [lhs = std::move(lhs), rhs = std::move(rhs)](
                super_request_type const& request,
                context& context)
            {
                return
                    lhs(request, context, {}, {}) ||
                    rhs(request, context, {}, {});
            },
            new_meta {},
            std::move(new_caps)
        };
    }
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
