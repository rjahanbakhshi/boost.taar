//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
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
    /// Index of the provider among the call-reading providers, for positional `params` arrays.
    std::optional<std::size_t> position {};
};

/** Argument provider reading one member of the envelope's `params`: by name
    when `params` is an object, by position when it is an array.

    Positions count only the `rpc_arg` providers of a method, in order, so
    HTTP providers (`header_arg`, ...) mixed in do not shift them. A short
    array leaves trailing parameters missing (an error unless they are
    optional or defaulted); extra elements are ignored.

    The value is converted to the callable's parameter type through
    `rest_arg_cast` (so `int`, `double`, `bool`, `std::string`,
    `boost::json::value`, and user types with `boost::json::value_to`
    support all work). Wrap in `with_default` or use a `std::optional<T>`
    parameter to make it optional.

    An absent parameter is reported as `taar::error::argument_not_found`
    (without a message, so optional and defaulted parameters stay cheap);
    scalar `params` as `jsonrpc::error::invalid_params`. The binder wraps
    either in an `invalid_argument_format` failure naming the parameter
    and its position.

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

    taar::result<boost::json::value> operator()(
        call const& c,
        matcher::context const&) const
    {
        if (!c.env.params.has_value())
        {
            return taar::failure {taar::error::argument_not_found};
        }
        if (auto const* arr = c.env.params->if_array())
        {
            if (!c.position || *c.position >= arr->size())
            {
                return taar::failure {taar::error::argument_not_found};
            }
            return (*arr)[*c.position];
        }
        auto const* obj = c.env.params->if_object();
        if (obj == nullptr)
        {
            // Scalar params are not supported.
            return taar::failure {jsonrpc::error::invalid_params, "params must be an object or an array"};
        }
        auto const* v = obj->if_contains(name_);
        if (v == nullptr)
        {
            return taar::failure {taar::error::argument_not_found};
        }
        return *v;
    }

private:
    std::string name_;
    std::string description_;
};

} // namespace boost::taar::jsonrpc

#endif // BOOST_TAAR_JSONRPC_ARG_HPP
