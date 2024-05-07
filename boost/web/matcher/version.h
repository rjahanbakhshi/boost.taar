#pragma once

#include "matcher_base.h"
#include <boost/beast/http/message.hpp>

namespace boost::web::matcher {

template<class FieldsType = boost::beast::http::fields>
struct version_t
{
  using request_type = boost::beast::http::request_header<FieldsType>;

  bool equals(
    const request_type& request,
    context& context,
    unsigned ver) const
  {
    return request.version() == ver;
  }

  bool less(
    const request_type& request,
    context& context,
    unsigned ver) const
  {
    return request.version() < ver;
  }

  bool greater(
    const request_type& request,
    context& context,
    unsigned ver) const
  {
    return request.version() > ver;
  }
};

template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_version = version_t<FieldsType>{};
constexpr auto version = basic_version<boost::beast::http::fields>;

} // namespace boost::web::matcher
