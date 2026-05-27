//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_TARGET_HPP
#define BOOST_TAAR_MATCHER_TARGET_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/taar/matcher/template_parser.hpp>
#include <boost/taar/matcher/detail/ranges_to.hpp>
#include <boost/taar/matcher/detail/join_with.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/url/url_view.hpp>
#include <ranges>
#include <vector>
#include <string>

namespace boost::taar::matcher {

/** A placeholder that produces matchers over the request target (path).

    Compose with `==` and `!=` against a path template:

    @code
    using boost::taar::matcher::target;

    auto exact   = target == "/api/version";
    auto by_id   = target == "/users/{id}";
    auto bucket  = target == "/files/{*path}";
    auto any     = target == "/{*}";
    @endcode

    Path templates use the following grammar:

    @li Literal segments are matched verbatim.
    @li `{name}` matches one segment and stores it in
        @ref context.path_args under @c name.
    @li `{*name}` (greedy) matches zero or more segments joined with `/` and
        stores the result under @c name.
    @li `{*}` is shorthand for a greedy capture stored under the key `"*"`.

    On a successful match the captured arguments are written to
    @ref boost::taar::matcher::context::path_args, where the
    `taar::handler::path_arg` argument provider can later read them.

    @tparam FieldsType The Beast HTTP fields container of the request to
                       inspect. Defaults to `boost::beast::http::fields`.
*/
template<class FieldsType = boost::beast::http::fields>
struct target_t
{
    /// The request type these matchers accept.
    using request_type = boost::beast::http::request_header<FieldsType>;

    /// Build a matcher for path-template @a target_template.
    friend auto operator==(target_t, std::string_view target_template)
    {
        auto const ptt = parse_template(target_template);
        parsed_template template_segments {ptt.value().begin(), ptt.value().end()};

        return matcher::operand
        {
            [template_segments = std::move(template_segments)](
                request_type const&,
                context& context,
                boost::urls::url_view const& parsed_target)
            {
                auto const& target_segments = parsed_target.segments();

                auto target_iter = target_segments.begin();
                auto template_iter = template_segments.begin();

                auto greedy_target_iter = target_segments.end();
                std::string greedy_param_name;

                auto finish_greedy_param_if_any = [&]()
                {
                    if (greedy_param_name.empty())
                    {
                        return;
                    }

                    using namespace std::ranges;
                    using namespace std::views;

                    context.path_args.emplace(
                        greedy_param_name,
                        subrange(greedy_target_iter, target_iter) |
                            detail::join_with('/') |
                            detail::to<std::string>());

                    greedy_param_name.clear();
                };

                while (template_iter != template_segments.end())
                {
                    auto const& template_type = template_iter->type;
                    auto const& template_value = template_iter->value;

                    if (template_type == template_segment_type::greedy_param)
                    {
                        finish_greedy_param_if_any();

                        greedy_target_iter = target_iter;
                        greedy_param_name = template_value;

                        ++template_iter;
                        if (template_iter == template_segments.end())
                        {
                            // greedy param at the end, consume all remaining target segments
                            target_iter = target_segments.end();
                            break;
                        }
                        else if (target_iter != target_segments.end())
                        {
                            ++target_iter;
                        }

                        continue;
                    }

                    if (target_iter == target_segments.end())
                    {
                        break;
                    }
                    auto const& target_value = *target_iter;

                    if (template_type == template_segment_type::param)
                    {
                        finish_greedy_param_if_any();
                        context.path_args.emplace(template_value, target_value);

                        ++target_iter;
                        ++template_iter;

                        continue;
                    }

                    // template_type == template_segment_type::literal
                    if (template_value == target_value)
                    {
                        finish_greedy_param_if_any();

                        ++target_iter;
                        ++template_iter;

                        continue;
                    }

                    if (!greedy_param_name.empty())
                    {
                        ++target_iter;
                        continue;
                    }

                    return false;
                }

                finish_greedy_param_if_any();

                return
                    target_iter == target_segments.end() &&
                    template_iter == template_segments.end();
            }
        };
    }

    friend auto operator==(std::string_view target_template, target_t)
    {
        return operator==(target_t{}, target_template);
    }

    friend auto operator!=(target_t, std::string_view target_template)
    {
        return !operator==(target_t{}, target_template);
    }

    friend auto operator!=(std::string_view target_template, target_t)
    {
        return operator!=(target_t{}, target_template);
    }
};

/// A target-matcher placeholder parameterised by the Fields type.
template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_target = target_t<FieldsType>{};

/// The default target-matcher placeholder.
constexpr auto target = basic_target<boost::beast::http::fields>;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_TARGET_HPP
