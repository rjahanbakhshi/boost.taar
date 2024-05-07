#pragma once

#include "matcher_base.h"
#include <boost/beast/http/message.hpp>

namespace boost::web::matcher {

template<class FieldsType = boost::beast::http::fields>
struct method_t
{
  using request_type = boost::beast::http::request_header<FieldsType>;

  bool equals(
    const request_type& request,
    context& context,
    boost::beast::http::verb verb) const
  {
    return request.method() == verb;
  }
};

template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_method = method_t<FieldsType>{};
constexpr auto method = basic_method<boost::beast::http::fields>;

} // namespace boost::web::matcher
