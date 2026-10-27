//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#include <boost/taar/core/accept_encoding.hpp>
#include <boost/test/unit_test.hpp>
#include <optional>

namespace {

namespace taar = boost::taar;

BOOST_AUTO_TEST_CASE(test_accept_encoding_quality)
{
    BOOST_TEST((taar::accept_encoding_quality("gzip, deflate, br", "br") == 1.0));
    BOOST_TEST((taar::accept_encoding_quality("gzip, deflate", "br") == std::nullopt));
    BOOST_TEST((taar::accept_encoding_quality("br;q=0.5, gzip;q=0.8", "br") == 0.5));
    BOOST_TEST((taar::accept_encoding_quality("BR ; Q=0.25", "br") == 0.25));
    BOOST_TEST((taar::accept_encoding_quality("x-gzip", "gzip") == 1.0));
    BOOST_TEST((taar::accept_encoding_quality("*;q=0.3, br", "gzip") == 0.3));
    BOOST_TEST((taar::accept_encoding_quality("*;q=0.3, br;q=0", "br") == 0.0));
    BOOST_TEST((taar::accept_encoding_quality("identity;q=0", "identity") == 0.0));
    BOOST_TEST((taar::accept_encoding_quality("*;q=0", "identity") == 0.0));
    BOOST_TEST((taar::accept_encoding_quality("br;q=2", "br") == std::nullopt));    // malformed q skipped
    BOOST_TEST((taar::accept_encoding_quality("br;q=0.1234", "br") == std::nullopt));
    BOOST_TEST((taar::accept_encoding_quality("", "br") == std::nullopt));
}

} // namespace
