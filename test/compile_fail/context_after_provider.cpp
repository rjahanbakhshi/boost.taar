//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Expected failure: a context parameter after a provider-fed parameter is not
// injected; the provider's string cannot be cast to conn_ctx&
// ("The number of REST argument providers must equal" or rest_arg_cast failure).
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

struct conn_ctx { int n = 0; };

void probe()
{
    taar::session::basic_http<conn_ctx> session;
    session.register_request_handler(
        method == http::verb::get && target == "/{a}",
        taar::handler::rest([](int a, conn_ctx& c) { return a + c.n; }, taar::handler::path_arg("a")));
}
