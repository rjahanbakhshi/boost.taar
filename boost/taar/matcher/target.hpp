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
#include <iterator>
#include <ranges>
#include <vector>
#include <string>

namespace boost::taar::matcher {

template<class FieldsType = boost::beast::http::fields>
struct target_t
{
    using request_type = boost::beast::http::request_header<FieldsType>;

    friend auto operator==(target_t, std::string_view target_template)
    {
        auto ptt = parse_template(target_template);
        std::vector<std::string> ptt_segments {ptt.value().begin(), ptt.value().end()};
        return matcher::operand
        {
            [ptt_segments = std::move(ptt_segments)](
                const request_type&,
                context& context,
                const boost::urls::url_view& parsed_target)
            {
                auto target_iter = parsed_target.segments().begin();
                auto template_iter = ptt_segments.begin();
                for (;;)
                {
                    if (template_iter == ptt_segments.end())
                    {
                        break;
                    }
                    auto const& template_value = *template_iter;

                    if (template_value == "{*}")
                    {
                        using namespace std::ranges;
                        using namespace std::views;

                        context.path_args.emplace(
                            "*",
                            subrange(target_iter, parsed_target.segments().end()) |
                                detail::join_with('/') |
                                detail::to<std::string>());
                        return true;
                    }

                    if (target_iter == parsed_target.segments().end())
                    {
                        break;
                    }
                    auto const& target_value = *target_iter;

                    if (template_value.size() >= 2 &&
                        template_value.front() == '{' &&
                        template_value.back() == '}')
                    {
                        context.path_args.emplace(
                            std::string {
                                std::next(template_value.begin()),
                                std::prev(template_value.end())},
                            target_value);
                    }
                    else if (template_value != target_value)
                    {
                        return false;
                    }

                    ++target_iter;
                    ++template_iter;
                }

                return
                    target_iter == parsed_target.segments().end() &&
                    template_iter == ptt_segments.end();
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

template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_target = target_t<FieldsType>{};

constexpr auto target = basic_target<boost::beast::http::fields>;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_TARGET_HPP
