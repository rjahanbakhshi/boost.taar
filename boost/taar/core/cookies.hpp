//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_COOKIES_HPP
#define BOOST_TAAR_CORE_COOKIES_HPP

#include <boost/taar/core/error.hpp>
#include <boost/url/decode_view.hpp>
#include <boost/url/grammar/all_chars.hpp>
#include <boost/url/grammar/lut_chars.hpp>
#include <boost/system/result.hpp>
#include <unordered_map>
#include <string_view>
#include <string>

namespace boost::taar {
namespace detail {

constexpr auto ctl_chars =
    boost::urls::grammar::lut_chars {[](char ch){ return ch <= '\31'; }} +
    boost::urls::grammar::lut_chars {'\177'}; // DEL
constexpr auto separators_chars =
    boost::urls::grammar::lut_chars {"()<>@,;:\\\"/[]?={} \t"};
constexpr auto token_chars =
    boost::urls::grammar::all_chars - ctl_chars - separators_chars;
constexpr auto cookie_quoted_octet_chars =
    boost::urls::grammar::all_chars - ctl_chars;
constexpr auto cookie_unquoted_octet_chars =
    cookie_quoted_octet_chars -
    boost::urls::grammar::lut_chars {" \",;\\"};

} // namespace detail

struct cookie
{
    std::string name;
    std::string value;
};

class cookies
{
public:
    using items_type = std::unordered_map<std::string, std::string>;
    using key_type = items_type::key_type;
    using mapped_type = items_type::mapped_type;
    using value_type = items_type::value_type;
    using size_type = items_type::size_type;
    using difference_type = items_type::difference_type;
    using hasher = items_type::hasher;
    using key_equal = items_type::key_equal;
    using allocator_type = items_type::allocator_type;
    using reference = items_type::reference;
    using const_reference = items_type::const_reference;
    using pointer = items_type::pointer;
    using const_pointer = items_type::const_pointer;
    using iterator = items_type::iterator;
    using const_iterator = items_type::const_iterator;
    using local_iterator = items_type::local_iterator;
    using const_local_iterator = items_type::const_local_iterator;
    using node_type = items_type::node_type;
    using insert_return_type = items_type::insert_return_type;

public:
    cookies() = default;

    cookies(std::string_view cookie_string)
    {
        auto e = parse_cookies(cookie_string, *this);
        if (e != error::success)
        {
            throw boost::system::system_error{e};
        }
    }

    cookies(std::initializer_list<value_type> init)
        : items_(init)
    {}

    [[nodiscard]] size_type size() const noexcept
    {
        return items_.size();
    }

    [[nodiscard]] auto begin() noexcept
    {
        return items_.begin();
    }

    [[nodiscard]] auto begin() const noexcept
    {
        return items_.begin();
    }

    [[nodiscard]] auto cbegin() const noexcept
    {
        return items_.cbegin();
    }

    [[nodiscard]] auto end() noexcept
    {
        return items_.end();
    }

    [[nodiscard]] auto end() const noexcept
    {
        return items_.end();
    }

    [[nodiscard]] auto cend() const noexcept
    {
        return items_.cend();
    }

    [[nodiscard]] const mapped_type& at(key_type const& name) const
    {
        return items_.at(name);
    }

    [[nodiscard]] mapped_type& at(key_type const& name)
    {
        return items_.at(name);
    }

    [[nodiscard]] const mapped_type& operator[](key_type const& name) const
    {
        return items_.at(name);
    }

    [[nodiscard]] mapped_type& operator[](key_type const& name)
    {
        return items_.at(name);
    }

    void clear() noexcept
    {
        items_.clear();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return items_.empty();
    }

    [[nodiscard]] size_type count(key_type const& key) const
    {
        return items_.count(key);
    }

    [[nodiscard]] iterator find(key_type const& key)
    {
        return items_.find(key);
    }

    [[nodiscard]] const_iterator find(key_type const& key) const
    {
        return items_.find(key);
    }

    [[nodiscard]] bool contains(key_type const& key) const
    {
        return items_.contains(key);
    }

    std::pair<iterator, iterator> equal_range(key_type const& key)
    {
        return items_.equal_range(key);
    }

    std::pair<const_iterator, const_iterator> equal_range(key_type const& key) const
    {
        return items_.equal_range(key);
    }

    friend error parse_cookies(std::string_view cookie_string, cookies& result)
    {
        enum class states
        {
            name_start,
            name,
            value_start,
            value_quoted,
            value_quoted_end,
            value_unquoted,
            separator,
        };

        std::string name;
        std::string value;
        states state = states::name_start;

        for (char ch : cookie_string)
        {
            switch (state)
            {
            case states::separator:
                if (ch == ' ')
                {
                    state = states::name_start;
                    break;
                }
                [[fallthrough]];

            case states::name_start:
                if (!detail::token_chars(ch))
                {
                    return error::invalid_cookie_format;
                }
                name = ch;
                state = states::name;
                break;

            case states::name:
                if (ch == '=')
                {
                    value.clear();
                    state = states::value_start;
                }
                else if (detail::token_chars(ch))
                {
                    name.push_back(ch);
                }
                else
                {
                    return error::invalid_cookie_format;
                }
                break;

            case states::value_start:
                if (ch == ';')
                {
                    boost::urls::decode_view decoded_name {name};
                    result.items_.emplace(
                        std::string(decoded_name.begin(), decoded_name.end()),
                        std::string());
                    state = states::separator;
                }
                else if (ch == '"')
                {
                    state = states::value_quoted;
                }
                else if (detail::cookie_unquoted_octet_chars(ch))
                {
                    state = states::value_unquoted;
                    value.push_back(ch);
                }
                else
                {
                    return error::invalid_cookie_format;
                }
                break;

            case states::value_unquoted:
                if (ch == ';')
                {
                    boost::urls::decode_view decoded_name {name};
                    boost::urls::decode_view decoded_value {value};
                    result.items_.emplace(
                        std::string(decoded_name.begin(), decoded_name.end()),
                        std::string(decoded_value.begin(), decoded_value.end()));
                    state = states::separator;
                }
                else if (detail::cookie_unquoted_octet_chars(ch))
                {
                    value.push_back(ch);
                }
                else
                {
                    return error::invalid_cookie_format;
                }
                break;

            case states::value_quoted:
                if (ch == '"')
                {
                    boost::urls::decode_view decoded_name {name};
                    boost::urls::decode_view decoded_value {value};
                    result.items_.emplace(
                        std::string(decoded_name.begin(), decoded_name.end()),
                        std::string(decoded_value.begin(), decoded_value.end()));
                    state = states::value_quoted_end;
                }
                else if (detail::cookie_quoted_octet_chars(ch))
                {
                    value.push_back(ch);
                }
                else
                {
                    return error::invalid_cookie_format;
                }
                break;

            case states::value_quoted_end:
                if (ch != ';')
                {
                    return error::invalid_cookie_format;
                }
                state = states::separator;
                break;
            }
        }

        if (state == states::name || state == states::value_quoted)
        {
            return error::invalid_cookie_format;
        }

        if (state == states::value_start ||
            state == states::value_unquoted ||
            state == states::value_quoted_end)
        {
            boost::urls::decode_view decoded_name {name};
            boost::urls::decode_view decoded_value {value};
            result.items_.emplace(
                std::string(decoded_name.begin(), decoded_name.end()),
                std::string(decoded_value.begin(), decoded_value.end()));
        }

        return error::success;
    }

    friend boost::system::result<cookies, error> parse_cookies(
        std::string_view cookie_string)
    {
        cookies result;
        auto e = parse_cookies(cookie_string, result);
        if (e != error::success)
            return e;

        return result;
    }

    [[nodiscard]] friend bool operator==(cookies const& lhs, cookies const& rhs)
    {
        return lhs.items_ == rhs.items_;
    }

    [[nodiscard]] friend bool operator!=(cookies const& lhs, cookies const& rhs)
    {
        return lhs.items_ != rhs.items_;
    }

private:
    items_type items_;
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_COOKIES_HPP
