//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Expected failure: no response_from conversion for the callable's return type
// (constraint "has_response_from" not satisfied).
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

struct opaque { int v = 0; };

void probe()
{
    taar::session::http session;
    session.register_request_handler(
        method == http::verb::get && target == "/",
        taar::handler::rest([] { return opaque {}; }));
}
