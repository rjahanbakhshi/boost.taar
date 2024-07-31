//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_future.hpp>

template <typename Body, typename Fields>
decltype(auto) to_response(boost::beast::http::response<Body, Fields>& response)
{
    return response;
}

template <typename Body, typename Fields>
decltype(auto) to_response(boost::beast::http::response<Body, Fields>&& response)
{
    return response;
}

// Function to convert message_generator to http::response
template<class BodyType>
boost::beast::http::response<BodyType> to_response(
    boost::beast::http::message_generator&& generator)
{
    boost::beast::error_code ec;
    boost::beast::http::response_parser<BodyType> parser;

    while (!parser.is_done())
    {
        auto buffers = generator.prepare(ec);
        if(ec)
        {
            throw boost::system::system_error{ec};
        }

        auto n = parser.put(buffers, ec);
        if(ec && ec != boost::beast::http::error::need_buffer)
        {
            throw boost::system::system_error{ec};
        }

        generator.consume(n);
    }

    return parser.release();
}

template <typename T, typename Executor>
T to_sync_response(boost::asio::awaitable<T, Executor>&& val)
{
    boost::asio::io_context io_context;
    auto fut = boost::asio::co_spawn(io_context, std::move(val), boost::asio::use_future);
    io_context.run();
    return fut.get();
}

template <typename Body, typename Fields, typename Executor>
auto to_response(
    boost::asio::awaitable<boost::beast::http::response<Body, Fields>, Executor>&& response)
{
    return to_sync_response(std::move(response));
}

template <typename BodyType, typename Executor>
auto to_response(
    boost::asio::awaitable<boost::beast::http::message_generator, Executor>&& generator)
{
    return to_sync_response(std::move(generator));
}
