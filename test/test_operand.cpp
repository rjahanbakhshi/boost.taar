//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/matcher/operand.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/test/unit_test.hpp>
#include <optional>

namespace {

struct request_base {};
struct request_derrived : request_base {};

bool free_function(
    const request_base& /*request*/,
    boost::taar::matcher::context& /*context*/)
{
    return true;
}

std::optional<int> contextual_bool(
    const request_derrived& /*request*/,
    boost::taar::matcher::context& /*context*/)
{
    return {};
}

bool free_function_target(
    const request_base& /*request*/,
    boost::taar::matcher::context& /*context*/,
    const boost::urls::url_view& parsed_target)
{
    return !parsed_target.segments().empty();
}

BOOST_AUTO_TEST_CASE(test_matcher_operand_free_function)
{
    using namespace boost::taar::matcher;
    context ctx;

    operand free_function_operand {free_function};
    BOOST_TEST(free_function_operand({}, ctx));
    BOOST_TEST(!(!free_function_operand)({}, ctx));

    operand contextual_bool_operand {contextual_bool};
    BOOST_TEST(!contextual_bool_operand({}, ctx));
    BOOST_TEST(!(contextual_bool_operand)({}, ctx));
    BOOST_TEST((!contextual_bool_operand && free_function_operand)({}, ctx));
    BOOST_TEST(!(contextual_bool_operand && free_function_operand)({}, ctx));
    BOOST_TEST(!(contextual_bool_operand && !free_function_operand)({}, ctx));
    BOOST_TEST((contextual_bool_operand || free_function_operand)({}, ctx));

    static_assert(!decltype(free_function_operand)::with_parsed_target);
    static_assert(!decltype(contextual_bool_operand)::with_parsed_target);
    static_assert(!decltype(!free_function_operand)::with_parsed_target);
    static_assert(!decltype(!contextual_bool_operand)::with_parsed_target);

    static_assert(!decltype(free_function_operand && contextual_bool_operand)::with_parsed_target);
    static_assert(!decltype(contextual_bool_operand && free_function_operand)::with_parsed_target);

    static_assert(!decltype(free_function_operand || contextual_bool_operand)::with_parsed_target);
    static_assert(!decltype(contextual_bool_operand || free_function_operand)::with_parsed_target);
}

BOOST_AUTO_TEST_CASE(test_matcher_operand_free_function_with_target)
{
    using namespace boost::taar::matcher;
    context ctx;

    operand free_function_operand {free_function};
    operand free_function_target_operand {free_function_target};

    BOOST_TEST(!free_function_target_operand({}, ctx));
    BOOST_TEST(free_function_target_operand({}, ctx, {"/test"}));
    BOOST_TEST(!(free_function_target_operand)({}, ctx));
    BOOST_TEST((!free_function_target_operand && free_function_operand)({}, ctx));
    BOOST_TEST(!(free_function_target_operand && free_function_operand)({}, ctx));
    BOOST_TEST(!(free_function_target_operand && !free_function_operand)({}, ctx));
    BOOST_TEST((free_function_target_operand || free_function_operand)({}, ctx));

    static_assert(decltype(free_function_target_operand)::with_parsed_target);
    static_assert(decltype(!free_function_target_operand)::with_parsed_target);

    static_assert(decltype(free_function_operand && free_function_target_operand)::with_parsed_target);
    static_assert(decltype(free_function_target_operand && free_function_operand)::with_parsed_target);

    static_assert(decltype(free_function_operand || free_function_target_operand)::with_parsed_target);
    static_assert(decltype(free_function_target_operand || free_function_operand)::with_parsed_target);
}

BOOST_AUTO_TEST_CASE(test_matcher_operand_lambda)
{
    using namespace boost::taar::matcher;
    context ctx;

    auto lambda = [](
        const request_base& /*request*/,
        context& /*context*/)
    {
        return true;
    };

    auto cap_lambda = [lambda](
        const request_derrived& request,
        context& context)
    {
        return !lambda(request, context);
    };

    operand lambda_operand {lambda};
    operand cap_lambda_operand {cap_lambda};

    BOOST_TEST(lambda_operand({}, ctx));
    BOOST_TEST(!(!lambda_operand)({}, ctx));
    BOOST_TEST(!cap_lambda_operand({}, ctx));
    BOOST_TEST((!cap_lambda_operand)({}, ctx));
    BOOST_TEST((lambda_operand && lambda_operand)({}, ctx));
    BOOST_TEST(!(cap_lambda_operand && lambda_operand)({}, ctx));
    BOOST_TEST((!cap_lambda_operand && lambda_operand)({}, ctx));
    BOOST_TEST((lambda_operand && !cap_lambda_operand)({}, ctx));
    BOOST_TEST(!(cap_lambda_operand && !lambda_operand)({}, ctx));
    BOOST_TEST((cap_lambda_operand || lambda_operand)({}, ctx));
    BOOST_TEST((lambda_operand || cap_lambda_operand)({}, ctx));
}

BOOST_AUTO_TEST_CASE(test_matcher_operand_implicit)
{
    using namespace boost::taar::matcher;
    context ctx;

    auto lambda = [](
        const request_base& /*request*/,
        context& /*context*/)
    {
        return true;
    };

    operand lambda_operand {lambda};
    operand contextual_bool_operand {contextual_bool};

    BOOST_TEST((!contextual_bool_operand && lambda)({}, ctx));
    BOOST_TEST((lambda_operand && lambda)({}, ctx));
    BOOST_TEST(!(contextual_bool_operand && free_function)({}, ctx));
    BOOST_TEST((lambda_operand && free_function)({}, ctx));

    BOOST_TEST((lambda && !contextual_bool_operand)({}, ctx));
    BOOST_TEST((lambda && lambda_operand)({}, ctx));
    BOOST_TEST(!(free_function && contextual_bool_operand)({}, ctx));
    BOOST_TEST((free_function && lambda_operand)({}, ctx));

    BOOST_TEST((!contextual_bool_operand || lambda)({}, ctx));
    BOOST_TEST((lambda_operand || lambda)({}, ctx));
    BOOST_TEST((contextual_bool_operand || free_function)({}, ctx));
    BOOST_TEST((!lambda_operand || free_function)({}, ctx));

    BOOST_TEST((lambda || !contextual_bool_operand)({}, ctx));
    BOOST_TEST((lambda || lambda_operand)({}, ctx));
    BOOST_TEST((free_function || contextual_bool_operand)({}, ctx));
    BOOST_TEST((free_function || !lambda_operand)({}, ctx));
}

} // namespace
