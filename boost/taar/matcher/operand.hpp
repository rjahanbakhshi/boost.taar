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
#include <boost/taar/core/cookies.hpp>
#include <boost/taar/core/callable_traits.hpp>
#include <boost/taar/core/specialization_of.hpp>
#include <boost/taar/matcher/detail/callable_with.hpp>
#include <boost/taar/core/super_type.hpp>
#include <boost/url/url_view.hpp>
#include <type_traits>
#include <utility>

namespace boost::taar::matcher {

template <typename RequestType, typename CallableType>
requires (
    detail::callable_with<CallableType, bool, const RequestType&, context&> ||
    detail::callable_with<CallableType, bool, const RequestType&, context&, const boost::urls::url_view&> ||
    detail::callable_with<CallableType, bool, const RequestType&, context&, const cookies&> ||
    detail::callable_with<CallableType, bool, const RequestType&, context&, const boost::urls::url_view&, const cookies&> ||
    detail::callable_with<CallableType, bool, const RequestType&, context&, const cookies&, const boost::urls::url_view&>)
class operand
{
public:
    using request_type = RequestType;
    using callable_type = CallableType;

    static constexpr int callabe_kind =
        detail::callable_with<
            CallableType,
            bool,
            const RequestType&,
            context&,
            const boost::urls::url_view&,
            const cookies&> ? 4 :
        detail::callable_with<
            CallableType,
            bool,
            const RequestType&,
            context&,
            const cookies&,
            const boost::urls::url_view&> ? 3 :
        detail::callable_with<
            CallableType,
            bool,
            const RequestType&,
            context&,
            const boost::urls::url_view&> ? 2 :
        detail::callable_with<
            CallableType,
            bool,
            const RequestType&,
            context&,
            const cookies&> ? 1 : 0;
    static constexpr auto with_parsed_target =
        callabe_kind == 4 || callabe_kind == 3 || callabe_kind == 2;
    static constexpr auto with_parsed_cookies =
        callabe_kind == 4 || callabe_kind == 3 || callabe_kind == 1;

public:
    operand(callable_type callable)
    : callable_ {std::move(callable)}
    {}

    auto operator()(
        const request_type& request,
        context& context,
        const boost::urls::url_view& parsed_target,
        const cookies& parsed_cookies) const
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

    friend auto operator!(operand opr)
    {
        if constexpr (with_parsed_target && with_parsed_cookies)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    const request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target,
                    const cookies& parsed_cookies)
                {
                    return !opr.operator()(request, context, parsed_target, parsed_cookies);
                }
            };
        }
        else if constexpr (with_parsed_target)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    const request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target)
                {
                    return !opr.operator()(request, context, parsed_target, {});
                }
            };
        }
        else if constexpr (with_parsed_cookies)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    const request_type& request,
                    context& context,
                    const cookies& parsed_cookies)
                {
                    return !opr.operator()(request, context, {}, parsed_cookies);
                }
            };
        }
        else
        {
            return matcher::operand {
                [opr = std::move(opr)](const request_type& request, context& context)
                {
                    return !opr.operator()(request, context, {}, {});
                }
            };
        }
    }

    template <typename RHSType>
    requires (!specialization_of<matcher::operand, std::remove_cvref_t<RHSType>>)
    friend auto operator&&(
        operand lhs_operand,
        RHSType&& rhs)
    {
        matcher::operand rhs_operand {std::forward<RHSType&&>(rhs)};
        using rhs_operand_type = decltype(rhs_operand);
        using rhs_request_type = typename rhs_operand_type::request_type;
        using super_request_type = super_type_t<request_type, rhs_request_type>;

        if constexpr (
            (with_parsed_target && with_parsed_cookies) ||
            (rhs_operand_type::with_parsed_target && rhs_operand_type::with_parsed_cookies))
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target,
                    const cookies& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, parsed_target, parsed_cookies) &&
                        rhs_operand(request, context, parsed_target, parsed_cookies);
                }
            };
        }
        else if constexpr (with_parsed_target || rhs_operand_type::with_parsed_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target, {}) &&
                        rhs_operand(request, context, parsed_target, {});
                }
            };
        }
        else if constexpr (with_parsed_cookies || rhs_operand_type::with_parsed_cookies)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const cookies& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, {}, parsed_cookies) &&
                        rhs_operand(request, context, {}, parsed_cookies);
                }
            };
        }
        else
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context, {}, {}) &&
                        rhs_operand(request, context, {}, {});
                }
            };
        }
    }

    template <typename LHSType>
    requires (!specialization_of<matcher::operand, std::remove_cvref<LHSType>>)
    friend auto operator&&(
        LHSType&& lhs,
        operand rhs_operand)
    {
        matcher::operand lhs_operand {std::forward<LHSType&&>(lhs)};
        using lhs_operand_type = decltype(lhs_operand);
        using lhs_request_type = typename lhs_operand_type::request_type;
        using super_request_type = super_type_t<request_type, lhs_request_type>;

        if constexpr (
            (with_parsed_target && with_parsed_cookies) ||
            (lhs_operand_type::with_parsed_target && lhs_operand_type::with_parsed_cookies))
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target,
                    const cookies& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, parsed_target, parsed_cookies) &&
                        rhs_operand(request, context, parsed_target, parsed_cookies);
                }
            };
        }
        else if constexpr (with_parsed_target || lhs_operand_type::with_parsed_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target, {}) &&
                        rhs_operand(request, context, parsed_target, {});
                }
            };
        }
        else if constexpr (with_parsed_cookies || lhs_operand_type::with_parsed_cookies)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const cookies& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, {}, parsed_cookies) &&
                        rhs_operand(request, context, {}, parsed_cookies);
                }
            };
        }
        else
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context, {}, {}) &&
                        rhs_operand(request, context, {}, {});
                }
            };
        }
    }

    template <typename RHSType>
    requires (!specialization_of<matcher::operand, std::remove_cvref_t<RHSType>>)
    friend auto operator||(
        operand lhs_operand,
        RHSType&& rhs)
    {
        matcher::operand rhs_operand {std::forward<RHSType&&>(rhs)};
        using rhs_operand_type = decltype(rhs_operand);
        using rhs_request_type = typename rhs_operand_type::request_type;
        using super_request_type = super_type_t<request_type, rhs_request_type>;

        if constexpr (
            (with_parsed_target && with_parsed_cookies) ||
            (rhs_operand_type::with_parsed_target && rhs_operand_type::with_parsed_cookies))
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target,
                    const cookies& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, parsed_target, parsed_cookies) ||
                        rhs_operand(request, context, parsed_target, parsed_cookies);
                }
            };
        }
        else if constexpr (with_parsed_target || rhs_operand_type::with_parsed_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target, {}) ||
                        rhs_operand(request, context, parsed_target, {});
                }
            };
        }
        else if constexpr (with_parsed_cookies || rhs_operand_type::with_parsed_cookies)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const cookies& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, {}, parsed_cookies) ||
                        rhs_operand(request, context, {}, parsed_cookies);
                }
            };
        }
        else
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context, {}, {}) ||
                        rhs_operand(request, context, {}, {});
                }
            };
        }
    }

    template <typename LHSType>
    requires (!specialization_of<matcher::operand, std::remove_cvref<LHSType>>)
    friend auto operator||(
        LHSType&& lhs,
        operand rhs_operand)
    {
        matcher::operand lhs_operand {std::forward<LHSType&&>(lhs)};
        using lhs_operand_type = decltype(lhs_operand);
        using lhs_request_type = typename lhs_operand_type::request_type;
        using super_request_type = super_type_t<request_type, lhs_request_type>;

        if constexpr (
            (with_parsed_target && with_parsed_cookies) ||
            (lhs_operand_type::with_parsed_target && lhs_operand_type::with_parsed_cookies))
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target,
                    const cookies& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, parsed_target, parsed_cookies) ||
                        rhs_operand(request, context, parsed_target, parsed_cookies);
                }
            };
        }
        else if constexpr (with_parsed_target || lhs_operand_type::with_parsed_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const boost::urls::url_view& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target, {}) ||
                        rhs_operand(request, context, parsed_target, {});
                }
            };
        }
        else if constexpr (with_parsed_cookies || lhs_operand_type::with_parsed_cookies)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const cookies& parsed_cookies)
                {
                    return
                        lhs_operand(request, context, {}, parsed_cookies) ||
                        rhs_operand(request, context, {}, parsed_cookies);
                }
            };
        }
        else
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context, {}, {}) ||
                        rhs_operand(request, context, {}, {});
                }
            };
        }
    }

private:
    callable_type callable_;
};

template <typename ObjectType>
operand(ObjectType) ->
    operand<
        std::remove_cvref_t<
            typename callable_traits<std::remove_cvref_t<ObjectType>
        >::template arg_type<0>
    >, ObjectType>;

template <typename MatcherType>
concept is_matcher = requires(MatcherType&& matcher)
{
    operand{std::forward<MatcherType>(matcher)};
};

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_MATCHER_HPP
