//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_SRC_TLS_HPP
#define BOOST_TAAR_SRC_TLS_HPP

// Include in exactly one source file of a program that defines
// BOOST_TAAR_SEPARATE_COMPILATION and uses TLS.

#ifndef BOOST_TAAR_SEPARATE_COMPILATION
#  error "Define BOOST_TAAR_SEPARATE_COMPILATION for the whole program before using <boost/taar/src_tls.hpp>."
#endif

#include <boost/taar/server/tls.hpp>
#include <boost/taar/session/https.hpp>

#define BOOST_TAAR_DETAIL_INSTANTIATE
#include <boost/taar/session/detail/https_instantiations.hpp>
#include <boost/taar/server/detail/tls_instantiations.hpp>
#undef BOOST_TAAR_DETAIL_INSTANTIATE

#endif // BOOST_TAAR_SRC_TLS_HPP
