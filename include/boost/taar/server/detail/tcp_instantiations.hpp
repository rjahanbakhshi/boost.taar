//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// server::tcp over session::http, compiled once by <boost/taar/src.hpp>.

#ifdef BOOST_TAAR_DETAIL_INSTANTIATE
#  ifndef BOOST_TAAR_SERVER_DETAIL_TCP_INSTANTIATIONS_DEFINED
#    define BOOST_TAAR_SERVER_DETAIL_TCP_INSTANTIATIONS_DEFINED
#    define BOOST_TAAR_SERVER_DETAIL_TCP_INSTANTIATIONS_PASS
#  endif
#else
#  ifndef BOOST_TAAR_SERVER_DETAIL_TCP_INSTANTIATIONS_DECLARED
#    define BOOST_TAAR_SERVER_DETAIL_TCP_INSTANTIATIONS_DECLARED
#    define BOOST_TAAR_SERVER_DETAIL_TCP_INSTANTIATIONS_PASS
#  endif
#endif

#ifdef BOOST_TAAR_SERVER_DETAIL_TCP_INSTANTIATIONS_PASS
#undef BOOST_TAAR_SERVER_DETAIL_TCP_INSTANTIATIONS_PASS

#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>

#ifdef BOOST_TAAR_DETAIL_INSTANTIATE
#  define BOOST_TAAR_DETAIL_EXTERN
#else
#  define BOOST_TAAR_DETAIL_EXTERN extern
#endif

namespace boost::taar::server {

BOOST_TAAR_DETAIL_EXTERN template awaitable<void> tcp<session::http&>(
    boost::asio::any_io_executor,
    std::string,
    std::string,
    session::http&,
    cancellation_signals&,
    std::function<void(boost::asio::ip::tcp::endpoint const&)>);

BOOST_TAAR_DETAIL_EXTERN template awaitable<void> tcp<session::http&>(
    std::string,
    std::string,
    session::http&,
    cancellation_signals&,
    std::function<void(boost::asio::ip::tcp::endpoint const&)>);

} // namespace boost::taar::server

#undef BOOST_TAAR_DETAIL_EXTERN
#endif
