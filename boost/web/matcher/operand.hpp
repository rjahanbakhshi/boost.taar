//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HTTP_MATCHER_MATCHER_HPP
#define BOOST_WEB_HTTP_MATCHER_MATCHER_HPP

#include <boost/web/matcher/context.hpp>
#include <boost/web/matcher/segments_view.hpp>
#include <boost/web/matcher/detail/callable_traits.hpp>
#include <boost/web/matcher/detail/callable_with.hpp>
#include <boost/web/matcher/detail/specialization_of.hpp>
#include <boost/web/matcher/detail/super_type.hpp>
#include <type_traits>
#include <utility>

namespace boost::web::matcher {

template <typename RequestType, typename CallableType>
requires (
    detail::callable_with<CallableType, bool, const RequestType&, context&> ||
    detail::callable_with<CallableType, bool, const RequestType&, context&, const segments_view&>)
class operand
{
public:
    using request_type = RequestType;
    using callable_type = CallableType;
    static constexpr auto with_parsed_target = detail::callable_with<
        CallableType, bool, const RequestType&, context&, const segments_view&>;

public:
    operand(callable_type callable)
    : callable_ {std::move(callable)}
    {}

    auto operator()(
        const request_type& request,
        context& context,
        const segments_view& parsed_target = {}) const
    {
        if constexpr (with_parsed_target)
        {
            return callable_(request, context, parsed_target);
        }
        else //if constexpr (detail::always_false<callable_type>)
        {
            return callable_(request, context);
        }
    }

    friend auto operator!(operand opr)
    {
        if constexpr (with_parsed_target)
        {
            return matcher::operand {
                [opr = std::move(opr)](
                    const request_type& request,
                    context& context,
                    const segments_view& parsed_target)
                {
                    return !opr.operator()(request, context, parsed_target);
                }
            };
        }
        else //if constexpr (detail::always_false<callable_type>)
        {
            return matcher::operand {
                [opr = std::move(opr)](const request_type& request, context& context)
                {
                    return !opr.operator()(request, context);
                }
            };
        }
    }

    template <typename RHSType>
    requires (!detail::specialization_of<matcher::operand, std::remove_cvref_t<RHSType>>)
    friend auto operator&&(
        operand lhs_operand,
        RHSType&& rhs)
    {
        matcher::operand rhs_operand {std::forward<RHSType&&>(rhs)};
        using rhs_request_type = typename decltype(rhs_operand)::request_type;
        using super_request_type = detail::super_type_t<request_type, rhs_request_type>;

        if constexpr (with_parsed_target || decltype(rhs_operand)::with_parsed_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const segments_view& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target) &&
                        rhs_operand(request, context, parsed_target);
                }
            };
        }
        else //if constexpr (detail::always_false<callable_type>)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context) &&
                        rhs_operand(request, context);
                }
            };
        }
    }

    template <typename LHSType>
    requires (!detail::specialization_of<matcher::operand, std::remove_cvref<LHSType>>)
    friend auto operator&&(
        LHSType&& lhs,
        operand rhs_operand)
    {
        matcher::operand lhs_operand {std::forward<LHSType&&>(lhs)};
        using lhs_request_type = typename decltype(lhs_operand)::request_type;
        using super_request_type = detail::super_type_t<request_type, lhs_request_type>;

        if constexpr (with_parsed_target || decltype(lhs_operand)::with_parsed_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const segments_view& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target) &&
                        rhs_operand(request, context, parsed_target);
                }
            };
        }
        else //if constexpr (detail::always_false<callable_type>)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                const super_request_type& request,
                context& context)
                {
                    return
                        lhs_operand(request, context) &&
                        rhs_operand(request, context);
                }
            };
        }
    }

    template <typename RHSType>
    requires (!detail::specialization_of<matcher::operand, std::remove_cvref_t<RHSType>>)
    friend auto operator||(
        operand lhs_operand,
        RHSType&& rhs)
    {
        matcher::operand rhs_operand {std::forward<RHSType&&>(rhs)};
        using rhs_request_type = typename decltype(rhs_operand)::request_type;
        using super_request_type = detail::super_type_t<request_type, rhs_request_type>;

        if constexpr (with_parsed_target || decltype(rhs_operand)::with_parsed_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const segments_view& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target) ||
                        rhs_operand(request, context, parsed_target);
                }
            };
        }
        else //if constexpr (detail::always_false<callable_type>)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context) ||
                        rhs_operand(request, context);
                }
            };
        }
    }

    template <typename LHSType>
    requires (!detail::specialization_of<matcher::operand, std::remove_cvref<LHSType>>)
    friend auto operator||(
        LHSType&& lhs,
        operand rhs_operand)
    {
        matcher::operand lhs_operand {std::forward<LHSType&&>(lhs)};
        using lhs_request_type = typename decltype(lhs_operand)::request_type;
        using super_request_type = detail::super_type_t<request_type, lhs_request_type>;

        if constexpr (with_parsed_target || decltype(lhs_operand)::with_parsed_target)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context,
                    const segments_view& parsed_target)
                {
                    return
                        lhs_operand(request, context, parsed_target) ||
                        rhs_operand(request, context, parsed_target);
                }
            };
        }
        else //if constexpr (detail::always_false<callable_type>)
        {
            return matcher::operand
            {
                [lhs_operand = std::move(lhs_operand), rhs_operand = std::move(rhs_operand)](
                    const super_request_type& request,
                    context& context)
                {
                    return
                        lhs_operand(request, context) ||
                        rhs_operand(request, context);
                }
            };
        }
    }

private:
    callable_type callable_;
};

template <typename RequestType, typename ResultType, typename... ArgsType>
operand(ResultType(*)(const RequestType&, ArgsType...))
    -> operand<RequestType, ResultType(*)(const RequestType&, ArgsType...)>;

template <typename ObjectType>
operand(ObjectType)
    -> operand<detail::arg1_t<decltype(&ObjectType::operator())>, ObjectType>;

} // namespace boost::web::matcher

#endif // BOOST_WEB_HTTP_MATCHER_MATCHER_HPP
