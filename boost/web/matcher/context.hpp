//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_MATCHER_CONTEXT_HPP
#define BOOST_WEB_MATCHER_CONTEXT_HPP

#include <unordered_map>
#include <string>

namespace boost::web::matcher {

struct context
{
    std::unordered_map<std::string, std::string> path_args;
};

} // namespace boost::web::matcher

#endif // BOOST_WEB_MATCHER_CONTEXT_HPP
