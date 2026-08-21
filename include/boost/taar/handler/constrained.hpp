//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_CONSTRAINED_HPP
#define BOOST_TAAR_HANDLER_CONSTRAINED_HPP

#include <boost/taar/core/error.hpp>
#include <boost/taar/core/json_schema.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/type_traits/string_like.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/system/system_error.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace boost::taar::handler {

/** Declarative validation rules for one argument.

    Checked at request time against the converted value (after the cast to
    the callable's parameter type) and emitted as JSON Schema keywords in
    OpenAPI parameters and MCP `inputSchema`. Rules that do not apply to the
    value's type are ignored: numeric bounds for numbers, length and pattern
    and `one_of` for strings, item counts for sequences. `format` and
    `example` are documentation only.
*/
struct constraints
{
    std::optional<double> minimum {};
    std::optional<double> maximum {};
    std::optional<double> exclusive_minimum {};
    std::optional<double> exclusive_maximum {};
    std::optional<std::size_t> min_length {};
    std::optional<std::size_t> max_length {};
    std::optional<std::size_t> min_items {};
    std::optional<std::size_t> max_items {};
    std::string pattern {};                 ///< ECMAScript regular expression the whole string must match.
    std::vector<std::string> one_of {};     ///< Allowed string values (`enum`).
    std::string format {};                  ///< JSON Schema `format`, e.g. "email" or "date-time".
    boost::json::value example {};          ///< Rendered as `examples: [example]` when not null.

    /// Add the corresponding JSON Schema keywords to @a schema.
    void apply_to(boost::json::object& schema) const
    {
        auto number = [](double d) -> boost::json::value
        {
            if (d == std::floor(d) && std::fabs(d) < 9007199254740992.0) return static_cast<std::int64_t>(d);
            return d;
        };
        if (minimum) schema["minimum"] = number(*minimum);
        if (maximum) schema["maximum"] = number(*maximum);
        if (exclusive_minimum) schema["exclusiveMinimum"] = number(*exclusive_minimum);
        if (exclusive_maximum) schema["exclusiveMaximum"] = number(*exclusive_maximum);
        if (min_length) schema["minLength"] = static_cast<std::uint64_t>(*min_length);
        if (max_length) schema["maxLength"] = static_cast<std::uint64_t>(*max_length);
        if (min_items) schema["minItems"] = static_cast<std::uint64_t>(*min_items);
        if (max_items) schema["maxItems"] = static_cast<std::uint64_t>(*max_items);
        if (!pattern.empty()) schema["pattern"] = pattern;
        if (!one_of.empty())
        {
            boost::json::array values;
            for (auto const& v : one_of) values.emplace_back(v);
            schema["enum"] = std::move(values);
        }
        if (!format.empty()) schema["format"] = format;
        if (!example.is_null()) schema["examples"] = boost::json::array {example};
    }
};

/** Argument provider wrapper that validates the converted value against
    @ref constraints. Wraps any REST, JSON-RPC or MCP provider, including
    one already wrapped in `with_default`:

        rest(&list, constrained(with_default(query_arg("page"), 1), {.minimum = 1}))

    A violation is reported as `error::constraint_violated` with a
    message naming the argument and the rule (`400` on REST, `-32602` on
    JSON-RPC, a tool error on MCP). An absent optional value is not checked.
*/
template <typename ArgProviderType>
struct constrained
{
    using arg_provider_type = ArgProviderType;
    using arg_provider_request_type = detail::arg_provider_request_t<arg_provider_type>;
    using arg_provider_result_type = detail::arg_provider_result_t<arg_provider_type>;

    constrained(arg_provider_type arg_provider, constraints rules)
        : arg_provider_ {std::move(arg_provider)}
        , constraints_ {std::move(rules)}
    {
        static_assert(
            detail::is_rest_arg_provider<arg_provider_type>,
            "constrained must be used with an arg_provider as input!");
        if (!constraints_.pattern.empty())
        {
            regex_.emplace(constraints_.pattern, std::regex::ECMAScript);
        }
    }

    std::string name() const
    {
        return detail::arg_provider_name(arg_provider_);
    }

    std::string description() const
    {
        return detail::arg_provider_description(arg_provider_);
    }

    decltype(auto) operator()(
        arg_provider_request_type const& request,
        matcher::context const& context) const
    {
        return arg_provider_(request, context);
    }

    constraints const& rules() const noexcept
    {
        return constraints_;
    }

    /// Validate @a value; throws `system_error(error::constraint_violated)`.
    template <typename T>
    void check(T const& value) const
    {
        using U = std::remove_cvref_t<T>;
        if constexpr (is_optional_v<U>)
        {
            if (value.has_value()) check(*value);
        }
        else if constexpr (std::is_same_v<U, bool>)
        {
            // Nothing to constrain.
        }
        else if constexpr (std::is_arithmetic_v<U>)
        {
            double const v = static_cast<double>(value);
            if (constraints_.minimum && v < *constraints_.minimum)
                fail(std::format("must be >= {}", *constraints_.minimum));
            if (constraints_.maximum && v > *constraints_.maximum)
                fail(std::format("must be <= {}", *constraints_.maximum));
            if (constraints_.exclusive_minimum && v <= *constraints_.exclusive_minimum)
                fail(std::format("must be > {}", *constraints_.exclusive_minimum));
            if (constraints_.exclusive_maximum && v >= *constraints_.exclusive_maximum)
                fail(std::format("must be < {}", *constraints_.exclusive_maximum));
        }
        else if constexpr (type_traits::string_like<U>)
        {
            std::string const s {value};
            if (constraints_.min_length && s.size() < *constraints_.min_length)
                fail(std::format("must be at least {} characters", *constraints_.min_length));
            if (constraints_.max_length && s.size() > *constraints_.max_length)
                fail(std::format("must be at most {} characters", *constraints_.max_length));
            if (regex_ && !std::regex_match(s, *regex_))
                fail("does not match pattern " + constraints_.pattern);
            if (!constraints_.one_of.empty() &&
                std::ranges::find(constraints_.one_of, s) == constraints_.one_of.end())
            {
                std::string allowed;
                for (auto const& o : constraints_.one_of) allowed += (allowed.empty() ? "" : ", ") + o;
                fail("must be one of " + allowed);
            }
        }
        else if constexpr (requires { { value.size() } -> std::convertible_to<std::size_t>; })
        {
            std::size_t const n = value.size();
            if (constraints_.min_items && n < *constraints_.min_items)
                fail(std::format("must have at least {} items", *constraints_.min_items));
            if (constraints_.max_items && n > *constraints_.max_items)
                fail(std::format("must have at most {} items", *constraints_.max_items));
        }
    }

    arg_provider_type arg_provider_;
    constraints constraints_;
    std::optional<std::regex> regex_;

private:
    [[noreturn]] void fail(std::string what) const
    {
        throw boost::system::system_error {error::constraint_violated, name() + " " + what};
    }
};

/// @cond
// CTAD
template <typename T>
constrained(T, constraints) -> constrained<std::remove_cvref_t<T>>;
/// @endcond

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_CONSTRAINED_HPP
