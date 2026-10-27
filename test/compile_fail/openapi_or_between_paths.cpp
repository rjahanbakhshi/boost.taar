//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Expected failure: "openapi::generator cannot describe this route"
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

void probe()
{
    taar::session::http session;
    taar::openapi::generator gen {{.title = "t", .version = "1"}};
    session.register_request_handler(
        method == http::verb::get && (target == "/a" || target == "/b"),
        taar::handler::rest([] { return 1; }),
        gen.describe("either"));
}
