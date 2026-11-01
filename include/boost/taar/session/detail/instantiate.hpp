//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

// Session instantiations for BOOST_TAAR_DETAIL_STREAM. No include guard:
// BOOST_TAAR_DETAIL_EXTERN is `extern` (declarations) or empty (definitions).

namespace boost::taar::session {

BOOST_TAAR_DETAIL_EXTERN template class basic_http<no_context, BOOST_TAAR_DETAIL_STREAM>;

BOOST_TAAR_DETAIL_EXTERN template awaitable<boost::system::error_code>
basic_http<no_context, BOOST_TAAR_DETAIL_STREAM>::read_body(
    BOOST_TAAR_DETAIL_STREAM&,
    boost::beast::flat_buffer&,
    boost::beast::http::request_parser<boost::beast::http::string_body>&);

BOOST_TAAR_DETAIL_EXTERN template awaitable<boost::system::error_code>
basic_http<no_context, BOOST_TAAR_DETAIL_STREAM>::read_body(
    BOOST_TAAR_DETAIL_STREAM&,
    boost::beast::flat_buffer&,
    boost::beast::http::request_parser<boost::beast::http::empty_body>&);

namespace detail {

// Responses from response_from (string, empty) and htdocs (segmented).
BOOST_TAAR_DETAIL_EXTERN template awaitable<void> write_response(
    BOOST_TAAR_DETAIL_STREAM&,
    boost::beast::http::response<boost::beast::http::string_body>&&,
    bool,
    ::boost::taar::detail::timeout_duration);

BOOST_TAAR_DETAIL_EXTERN template awaitable<void> write_response(
    BOOST_TAAR_DETAIL_STREAM&,
    boost::beast::http::response<boost::beast::http::empty_body>&&,
    bool,
    ::boost::taar::detail::timeout_duration);

BOOST_TAAR_DETAIL_EXTERN template awaitable<void> write_response(
    BOOST_TAAR_DETAIL_STREAM&,
    boost::beast::http::response<segmented_body>&&,
    bool,
    ::boost::taar::detail::timeout_duration);

BOOST_TAAR_DETAIL_EXTERN template awaitable<bool> write_chunked_response(
    chunked_response<std::string>&,
    BOOST_TAAR_DETAIL_STREAM&,
    unsigned,
    bool,
    ::boost::asio::cancellation_slot,
    std::exception_ptr*,
    response_settings const*,
    ::boost::beast::http::request_header<> const*,
    bool);

} // namespace detail
} // namespace boost::taar::session
