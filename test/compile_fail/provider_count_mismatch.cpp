//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Expected failure: "The number of REST argument providers must equal the number of callable parameters"
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>

void probe()
{
    auto h = boost::taar::handler::rest([](int, int) { return 0; }, boost::taar::handler::path_arg("a"));
    (void)h;
}
