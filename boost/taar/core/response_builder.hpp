//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_RESPONSE_BUILDER_HPP
#define BOOST_TAAR_CORE_RESPONSE_BUILDER_HPP

#include <boost/beast/http/message_generator.hpp>
#include <boost/taar/core/pack_element.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/status.hpp>
#include <memory>

namespace boost::taar {
namespace detail {

class response_builder_impl_base
{
public:
    response_builder_impl_base() = default;
    virtual ~response_builder_impl_base() = default;
    response_builder_impl_base(const response_builder_impl_base&) = delete;
    response_builder_impl_base(response_builder_impl_base&&) = default;
    response_builder_impl_base& operator=(const response_builder_impl_base&) = delete;
    response_builder_impl_base& operator=(response_builder_impl_base&&) = default;

    virtual void set_version(unsigned version) = 0;
    virtual void set_status(boost::beast::http::status status) = 0;

    virtual void insert_header(
        std::string_view name,
        std::string_view const& value) = 0;

    virtual void insert_header(
        boost::beast::http::field field,
        std::string_view const& value) = 0;

    virtual void set_header(
        std::string_view name,
        std::string_view const& value) = 0;

    virtual void set_header(
        boost::beast::http::field field,
            std::string_view const& value) = 0;

    virtual boost::beast::http::message_generator get_response()&& = 0;
};

template <typename... T>
class response_builder_impl : public response_builder_impl_base
{
public:
    response_builder_impl(const T&... args)
        : response_{response_from(args...)}
    {}

    void set_version(unsigned version) override
    {
        response_.version(version);
    }

    void set_status(boost::beast::http::status status) override
    {
        response_.result(status);
    }

    void insert_header(
        std::string_view name,
        std::string_view const& value) override
    {
        response_.insert(name, value);
    }

    void insert_header(
        boost::beast::http::field field,
        std::string_view const& value) override
    {
        response_.insert(field, value);
    }

    void set_header(
        std::string_view name,
        std::string_view const& value) override
    {
        response_.set(name, value);
    }

    void set_header(
        boost::beast::http::field field,
            std::string_view const& value) override
    {
        response_.set(field, value);
    }

    boost::beast::http::message_generator get_response()&& override
    {
        return boost::beast::http::message_generator{std::move(response_)};
    }

private:
    response_from_t<T...> response_;
};

} // namespace detail

class response_builder
{
public:
    template <typename... ArgsType>
    response_builder(ArgsType&&... args)
        : impl_ {
            std::make_unique<detail::response_builder_impl<ArgsType...>>(
                std::forward<ArgsType>(args)...)}
    {}

    response_builder set_version(unsigned version)
    {
        impl_->set_version(version);
        return std::move(*this);
    }

    response_builder set_status(boost::beast::http::status status)
    {
        impl_->set_status(status);
        return std::move(*this);
    }

    response_builder insert_header(
        std::string_view name,
        std::string_view const& value)
    {
        impl_->insert_header(name, value);
        return std::move(*this);
    }

    response_builder insert_header(
        boost::beast::http::field field,
            std::string_view const& value)
    {
        impl_->insert_header(field, value);
        return std::move(*this);
    }

    response_builder set_header(
        std::string_view name,
        std::string_view const& value)
    {
        impl_->set_header(name, value);
        return std::move(*this);
    }

    response_builder set_header(
        boost::beast::http::field field,
            std::string_view const& value)
    {
        impl_->set_header(field, value);
        return std::move(*this);
    }

    boost::beast::http::message_generator get_response() &&
    {
        return std::move(*impl_).get_response();
    }

    // Support for response_from
    friend auto tag_invoke(
        detail::response_from_built_in_tag,
        response_builder&& value)
    {
        return std::move(value).get_response();
    }

private:
    std::unique_ptr<detail::response_builder_impl_base> impl_;
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_RESPONSE_BUILDER_HPP
