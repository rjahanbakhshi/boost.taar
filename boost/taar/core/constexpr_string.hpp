//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CONSTEXPR_STRING_HPP
#define BOOST_TAAR_CORE_CONSTEXPR_STRING_HPP

#include <string>
#include <array>
#include <cstddef>

namespace boost::taar {

template <std::size_t N>
struct constexpr_string
{
    std::array<char, N> data;

    constexpr constexpr_string(const char (&str)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            data[i] = str[i];
        }
    }

    constexpr const char* c_str() const
    {
        return data.data();
    }

    constexpr std::size_t size() const
    {
        return N - 1;
    }

    constexpr std::string string() const
    {
        return std::string(data.data());
    }

    constexpr operator std::string() const
    {
        return string();
    }

    template <std::size_t RN>
    friend constexpr bool operator==(const constexpr_string& lhs, const constexpr_string<RN>& rhs)
    {
        if constexpr (N != RN)
        {
            return false;
        }
        else
        {
            return lhs.data == rhs.data;
        }
    }
};

namespace literals {

template <constexpr_string CS>
constexpr auto operator""_cs() {
    return CS;
}

} // namespace literals

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CONSTEXPR_STRING_HPP
