//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_RESULT_RESPONSE_HPP
#define BOOST_TAAR_CORE_RESULT_RESPONSE_HPP

#include <boost/taar/core/pack_element.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/status.hpp>
#include <type_traits>

namespace boost::taar {
namespace detail {

template <typename ResultType, typename Enable = void>
class result_response_base;

template <typename ResultType>
class result_response_base<ResultType, std::enable_if_t<!std::is_void_v<ResultType>>>
{
public:
    result_response_base(ResultType&& v)
        : response {response_from(std::forward<ResultType>(v))}
    {}

    response_from_t<ResultType> response;
};

template <>
class result_response_base<void>
{
public:
    response_from_t<> response;
};

} // namespace detail

template <typename ResultType, typename FieldsType = boost::beast::http::fields>
class result_response : public detail::result_response_base<ResultType>
{
    using base_type = detail::result_response_base<ResultType>;

public:
    template <typename... ArgsType>
    result_response(ArgsType&&... args)
        : base_type {std::forward<ArgsType>(args)...}
    {}

    result_response& set_version(unsigned version)
    {
        base_type::response.version(version);
        return *this;
    }

    result_response& set_result(boost::beast::http::status status)
    {
        base_type::response.result(status);
        return *this;
    }

    result_response& insert_header(
        std::string_view name,
        std::string_view const& value)
    {
        base_type::response.insert(name, value);
        return *this;
    }

    result_response& insert_header(
        boost::beast::http::field field,
            std::string_view const& value)
    {
        base_type::response.insert(field, value);
        return *this;
    }

    result_response& set_header(
        std::string_view name,
        std::string_view const& value)
    {
        base_type::response.set(name, value);
        return *this;
    }

    result_response& set_header(
        boost::beast::http::field field,
            std::string_view const& value)
    {
        base_type::response.set(field, value);
        return *this;
    }

    // Support for response_from
    friend auto tag_invoke(
        detail::response_from_built_in_tag,
        const result_response& value)
    {
        return value.response;
    }
};

template <typename... ArgsType> requires (sizeof...(ArgsType) == 0)
result_response(ArgsType&&...)
-> result_response<void, boost::beast::http::fields>;

template <typename... ArgsType> requires (sizeof...(ArgsType) == 1)
result_response(ArgsType&&...)
-> result_response<pack_element_t<0, ArgsType...>, boost::beast::http::fields>;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_RESULT_RESPONSE_HPP
