//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Expected failure: "on_initialize for a stateful endpoint takes (Session&, initialize_params const&)"
#include <boost/taar/mcp/endpoint.hpp>

struct calc_session { int last = 0; };

void probe()
{
    boost::taar::mcp::endpoint<calc_session> e {{.name = "s", .version = "1"}};
    e.on_initialize([](int) {});
}
