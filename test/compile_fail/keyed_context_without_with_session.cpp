//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Expected failure: "requests a context type that is not in scope for this handler"
// (a keyed session type is only in scope inside with_session).
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

struct conn_ctx { int n = 0; };
struct sess_ctx { int items = 0; };

void probe()
{
    taar::session::basic_http<conn_ctx> session;
    session.register_request_handler(
        method == http::verb::get && target == "/",
        taar::handler::rest([](sess_ctx& s) { return s.items; }));
}
