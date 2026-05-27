//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CHUNKED_RESPONSE_META_HPP
#define BOOST_TAAR_CORE_CHUNKED_RESPONSE_META_HPP

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <string>
#include <utility>

namespace boost::taar {

/// Metadata token: yield to set the HTTP status of a @ref chunked_response.
struct chunked_set_status
{
    boost::beast::http::status value;
};

/// Metadata token: yield to add a header to a @ref chunked_response.
struct chunked_set_header
{
    boost::beast::http::field field = boost::beast::http::field::unknown;
    std::string name;
    std::string value;
};

/// Build a @ref chunked_set_status token. Convenience factory.
inline chunked_set_status set_status(boost::beast::http::status s)
{
    return {s};
}

/// Build a @ref chunked_set_header token for a well-known Beast field.
inline chunked_set_header set_header(boost::beast::http::field f, std::string v)
{
    return {f, {}, std::move(v)};
}

/// Build a @ref chunked_set_header token for an arbitrary header name.
inline chunked_set_header set_header(std::string name, std::string v)
{
    return {boost::beast::http::field::unknown, std::move(name), std::move(v)};
}

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CHUNKED_RESPONSE_META_HPP
