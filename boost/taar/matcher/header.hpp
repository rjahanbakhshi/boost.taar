//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_HEADER_HPP
#define BOOST_TAAR_MATCHER_HEADER_HPP

#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/beast/http/message.hpp>

namespace boost::taar::matcher {

template <class FieldsType = boost::beast::http::fields>
struct header_t
{
    using request_type = boost::beast::http::request_header<FieldsType>;

    template <typename KeyType>
    struct header_item
    {
        KeyType key;

        friend auto operator==(header_item lhs, std::string rhs)
        {
            return matcher::operand
            {
                [lhs = std::move(lhs), rhs = std::move(rhs)](
                    const request_type& request,
                    context& context)
                {
                    return request[lhs.key] == rhs;
                }
            };
        }

        friend auto operator==(std::string lhs, header_item rhs)
        {
            return operator==(std::move(rhs), std::move(lhs));
        }
    };

    auto operator()(boost::beast::http::field field) const
    {
        return header_item {std::move(field)};
    }

    auto operator()(std::string field) const
    {
        return header_item {std::move(field)};
    }
};

template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_header = header_t<FieldsType>{};

constexpr auto header = basic_header<boost::beast::http::fields>;

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_HEADER_HPP
