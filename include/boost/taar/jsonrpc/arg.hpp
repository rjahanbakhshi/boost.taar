//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_JSONRPC_ARG_HPP
#define BOOST_TAAR_JSONRPC_ARG_HPP

#include <boost/taar/core/error.hpp>
#include <boost/taar/jsonrpc/envelope.hpp>
#include <boost/taar/jsonrpc/error.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json/value.hpp>
#include <boost/system/result.hpp>
#include <string>
#include <utility>

namespace boost::taar::jsonrpc {

/** What a JSON-RPC method callable's argument providers receive.

    One `call` is built per envelope (so one per element of a batch). It
    carries the parsed envelope and the HTTP request that delivered it.
*/
struct call
{
    envelope const& env;
    boost::beast::http::request<boost::beast::http::string_body> const& request;
};

/** Argument provider reading a named member of the envelope's `params` object.

    The value is converted to the callable's parameter type through
    `rest_arg_cast` (so `int`, `double`, `bool`, `std::string`,
    `boost::json::value`, and user types with `boost::json::value_to`
    support all work). Wrap in `with_default` or use a `std::optional<T>`
    parameter to make it optional.

    The optional description is not sent on the wire; it feeds generators
    (MCP `inputSchema`, OpenAPI) that describe the method.
*/
struct rpc_arg
{
    explicit rpc_arg(std::string name, std::string description = {})
        : name_ {std::move(name)}
        , description_ {std::move(description)}
    {}

    std::string const& name() const noexcept { return name_; }
    std::string const& description() const noexcept { return description_; }

    boost::system::result<boost::json::value> operator()(
        call const& c,
        matcher::context const&) const
    {
        if (!c.env.params.has_value())
        {
            return taar::error::argument_not_found;
        }
        auto const* obj = c.env.params->if_object();
        if (obj == nullptr)
        {
            // Positional (array) or scalar params are not supported by
            // named providers.
            return jsonrpc::error::invalid_params;
        }
        auto const* v = obj->if_contains(name_);
        if (v == nullptr)
        {
            return taar::error::argument_not_found;
        }
        return *v;
    }

private:
    std::string name_;
    std::string description_;
};

} // namespace boost::taar::jsonrpc

#endif // BOOST_TAAR_JSONRPC_ARG_HPP
