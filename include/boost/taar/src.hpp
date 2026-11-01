//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_SRC_HPP
#define BOOST_TAAR_SRC_HPP

// Include in exactly one source file of a program that defines
// BOOST_TAAR_SEPARATE_COMPILATION. TLS also needs <boost/taar/src_tls.hpp>.

#ifndef BOOST_TAAR_SEPARATE_COMPILATION
#  error "Define BOOST_TAAR_SEPARATE_COMPILATION for the whole program before using <boost/taar/src.hpp>."
#endif

#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>

#define BOOST_TAAR_DETAIL_INSTANTIATE
#include <boost/taar/session/detail/http_instantiations.hpp>
#include <boost/taar/server/detail/tcp_instantiations.hpp>
#undef BOOST_TAAR_DETAIL_INSTANTIATE

#endif // BOOST_TAAR_SRC_HPP
