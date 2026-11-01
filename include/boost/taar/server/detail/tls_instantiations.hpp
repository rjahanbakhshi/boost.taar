//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// server::tls over session::https, compiled once by <boost/taar/src_tls.hpp>.

#ifdef BOOST_TAAR_DETAIL_INSTANTIATE
#  ifndef BOOST_TAAR_SERVER_DETAIL_TLS_INSTANTIATIONS_DEFINED
#    define BOOST_TAAR_SERVER_DETAIL_TLS_INSTANTIATIONS_DEFINED
#    define BOOST_TAAR_SERVER_DETAIL_TLS_INSTANTIATIONS_PASS
#  endif
#else
#  ifndef BOOST_TAAR_SERVER_DETAIL_TLS_INSTANTIATIONS_DECLARED
#    define BOOST_TAAR_SERVER_DETAIL_TLS_INSTANTIATIONS_DECLARED
#    define BOOST_TAAR_SERVER_DETAIL_TLS_INSTANTIATIONS_PASS
#  endif
#endif

#ifdef BOOST_TAAR_SERVER_DETAIL_TLS_INSTANTIATIONS_PASS
#undef BOOST_TAAR_SERVER_DETAIL_TLS_INSTANTIATIONS_PASS

#include <boost/taar/server/tls.hpp>
#include <boost/taar/session/https.hpp>

#ifdef BOOST_TAAR_DETAIL_INSTANTIATE
#  define BOOST_TAAR_DETAIL_EXTERN
#else
#  define BOOST_TAAR_DETAIL_EXTERN extern
#endif

namespace boost::taar::server {

BOOST_TAAR_DETAIL_EXTERN template awaitable<void> tls<session::https&>(
    boost::asio::any_io_executor,
    std::string,
    std::string,
    boost::asio::ssl::context&,
    session::https&,
    cancellation_signals&,
    std::function<void(boost::asio::ip::tcp::endpoint const&)>,
    std::chrono::steady_clock::duration);

BOOST_TAAR_DETAIL_EXTERN template awaitable<void> tls<session::https&>(
    std::string,
    std::string,
    boost::asio::ssl::context&,
    session::https&,
    cancellation_signals&,
    std::function<void(boost::asio::ip::tcp::endpoint const&)>,
    std::chrono::steady_clock::duration);

} // namespace boost::taar::server

#undef BOOST_TAAR_DETAIL_EXTERN
#endif
