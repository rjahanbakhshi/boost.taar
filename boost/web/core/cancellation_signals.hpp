//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HTTP_CORE_CANCELLATION_SIGNALS_HPP
#define BOOST_WEB_HTTP_CORE_CANCELLATION_SIGNALS_HPP

#include <boost/asio/cancellation_signal.hpp>
#include <list>
#include <algorithm>
#include <mutex>

namespace boost::web {

class cancellation_signals
{
public:
    void emit(boost::asio::cancellation_type ct = boost::asio::cancellation_type::all)
    {
        const std::lock_guard<std::mutex> lock {mutex_};
        for (auto& signal: signals_)
        {
            signal.emit(ct);
        }
    }

    boost::asio::cancellation_slot slot()
    {
        const std::lock_guard<std::mutex> lock {mutex_};

        auto itr = std::find_if(
            signals_.begin(),
            signals_.end(),
            [](boost::asio::cancellation_signal& signal)
            {
                return !signal.slot().has_handler();
            });

        if (itr != signals_.end())
        {
            return itr->slot();
        }

        return signals_.emplace_back().slot();
    }

private:
    std::list<boost::asio::cancellation_signal> signals_;
    std::mutex mutex_;
};

} // namespace boost::web

#endif // BOOST_WEB_HTTP_CORE_CANCELLATION_SIGNALS_HPP
