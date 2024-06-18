//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HTTP_MATCHER_SEGMENTS_VIEW_HPP
#define BOOST_WEB_HTTP_MATCHER_SEGMENTS_VIEW_HPP

#include <vector>
#include <string_view>

namespace boost::web::matcher {

class segments_view : public std::vector<std::string_view>
{
public:
    using std::vector<std::string_view>::vector;
    using std::vector<std::string_view>::operator=;
};

} // namespace boost::web::matcher

#endif // BOOST_WEB_HTTP_MATCHER_SEGMENTS_VIEW_HPP
