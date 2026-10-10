//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_MATCHER_CONTEXT_HPP
#define BOOST_TAAR_MATCHER_CONTEXT_HPP

#include <boost/taar/core/request_attributes.hpp>
#include <unordered_map>
#include <string>

namespace boost::taar::matcher {

/** Scratch space passed to every matcher invocation on a request.

    The session creates one @ref context per request and threads it through
    each matcher in the registered route. Matchers may write to its members
    when they extract information from the request that subsequent handler
    code needs. The most common use is the
    @ref boost::taar::matcher::target_t "target" matcher populating
    @ref path_args with the values it extracted from a path template.

    Handlers can read the context through the
    `taar::handler::path_arg` argument provider or by accepting a
    `taar::matcher::context const&` directly.
*/
struct context
{
    /** Path-argument values extracted by the target matcher.

        Keys are the placeholder names from the path template
        (`/users/{id}` populates `path_args["id"]`). The greedy `{*name}`
        placeholder stores the captured suffix under the same name; the
        bare `{*}` form stores it under the key `"*"`.
    */
    std::unordered_map<std::string, std::string> path_args;

    /** Values attached to the request by interceptors and guards.

        Filled by session interceptors (`basic_http::add_interceptor`)
        and `handler::guard` checks, read by handlers directly or through
        `handler::attribute_arg<T>()`. Routing only resets
        @ref path_args between candidate routes; attributes survive it.
    */
    request_attributes attributes;
};

} // namespace boost::taar::matcher

#endif // BOOST_TAAR_MATCHER_CONTEXT_HPP
