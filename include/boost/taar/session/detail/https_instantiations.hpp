//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// session::https, compiled once by <boost/taar/src_tls.hpp>.

#ifdef BOOST_TAAR_DETAIL_INSTANTIATE
#  ifndef BOOST_TAAR_SESSION_DETAIL_HTTPS_INSTANTIATIONS_DEFINED
#    define BOOST_TAAR_SESSION_DETAIL_HTTPS_INSTANTIATIONS_DEFINED
#    define BOOST_TAAR_SESSION_DETAIL_HTTPS_INSTANTIATIONS_PASS
#  endif
#else
#  ifndef BOOST_TAAR_SESSION_DETAIL_HTTPS_INSTANTIATIONS_DECLARED
#    define BOOST_TAAR_SESSION_DETAIL_HTTPS_INSTANTIATIONS_DECLARED
#    define BOOST_TAAR_SESSION_DETAIL_HTTPS_INSTANTIATIONS_PASS
#  endif
#endif

#ifdef BOOST_TAAR_SESSION_DETAIL_HTTPS_INSTANTIATIONS_PASS
#undef BOOST_TAAR_SESSION_DETAIL_HTTPS_INSTANTIATIONS_PASS

#include <boost/taar/session/https.hpp>
#include <boost/taar/core/segmented_body.hpp>

#ifdef BOOST_TAAR_DETAIL_INSTANTIATE
#  define BOOST_TAAR_DETAIL_EXTERN
#else
#  define BOOST_TAAR_DETAIL_EXTERN extern
#endif

#define BOOST_TAAR_DETAIL_STREAM ::boost::taar::session::tls_stream
#include <boost/taar/session/detail/instantiate.hpp>
#undef BOOST_TAAR_DETAIL_STREAM

#undef BOOST_TAAR_DETAIL_EXTERN
#endif
