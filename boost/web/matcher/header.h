#pragma once

#include "matcher_base.h"
#include <boost/beast/http/message.hpp>

namespace boost::web::matcher {

template <typename FieldsType = boost::beast::http::fields>
struct header_t
{
  template <typename KeyType>
  struct header_item
  {
    using request_type = boost::beast::http::request_header<FieldsType>;

    KeyType key;

    bool equals(
      const request_type& request,
      context& context,
      const std::string& value) const
    {
      return request[key] == value;
    }
  };

  auto operator()(boost::beast::http::field field) const
  {
    return header_item {field};
  }

  auto operator()(std::string key) const
  {
    return header_item {std::move(key)};
  }
};

template<class FieldsType = boost::beast::http::fields>
constexpr auto basic_header = header_t<FieldsType>{};
constexpr auto header = basic_header<boost::beast::http::fields>;

} // namespace boost::web::matcher
