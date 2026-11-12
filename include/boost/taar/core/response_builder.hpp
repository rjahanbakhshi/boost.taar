//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//

#ifndef BOOST_TAAR_CORE_RESPONSE_BUILDER_HPP
#define BOOST_TAAR_CORE_RESPONSE_BUILDER_HPP

#include <boost/beast/http/message_generator.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <memory>

namespace boost::taar {
namespace detail {

class response_builder_impl_base
{
public:
    response_builder_impl_base() = default;
    virtual ~response_builder_impl_base() = default;
    response_builder_impl_base(response_builder_impl_base const&) = delete;
    response_builder_impl_base(response_builder_impl_base&&) = default;
    response_builder_impl_base& operator=(response_builder_impl_base const&) = delete;
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

    virtual boost::beast::http::response_header<>& base() = 0;
    virtual bool keep_alive() const = 0;
    virtual void keep_alive(bool value) = 0;

    /// The message to write; with @a head_only, its header alone.
    virtual boost::beast::http::message_generator get_response(bool head_only)&& = 0;
};

template <typename... T>
class response_builder_impl : public response_builder_impl_base
{
public:
    template <typename... U>
    explicit response_builder_impl(U&&... args)
        : response_{response_from(std::forward<U>(args)...)}
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

    boost::beast::http::response_header<>& base() override
    {
        return response_.base();
    }

    bool keep_alive() const override
    {
        return response_.keep_alive();
    }

    void keep_alive(bool value) override
    {
        response_.keep_alive(value);
    }

    boost::beast::http::message_generator get_response(bool head_only)&& override
    {
        namespace http = boost::beast::http;
        if (head_only)
        {
            // No body follows, so no chunk framing either (RFC 9110 9.3.2).
            http::response<http::empty_body> header {std::move(response_.base())};
            if (header.chunked()) header.chunked(false);
            return http::message_generator {std::move(header)};
        }
        return http::message_generator {std::move(response_)};
    }

private:
    response_from_t<T...> response_;
};

/** A @ref boost::taar::response_builder answer as a session receives it:
    the header stays reachable so it is finished like a typed response.
*/
class built_response
{
public:
    explicit built_response(std::unique_ptr<response_builder_impl_base> impl) noexcept
        : impl_ {std::move(impl)}
    {}

    boost::beast::http::response_header<>& base() { return impl_->base(); }
    boost::beast::http::status result() const { return impl_->base().result(); }
    unsigned version() const { return impl_->base().version(); }
    void version(unsigned value) { impl_->base().version(value); }
    bool keep_alive() const { return impl_->keep_alive(); }
    void keep_alive(bool value) { impl_->keep_alive(value); }

    /// The message to write; with @a head_only, its header alone.
    boost::beast::http::message_generator to_generator(bool head_only = false) &&
    {
        return std::move(*impl_).get_response(head_only);
    }

    operator boost::beast::http::message_generator() &&
    {
        return std::move(*this).to_generator();
    }

private:
    std::unique_ptr<response_builder_impl_base> impl_;
};

} // namespace detail

/** Fluent builder around any value that @ref response_from can convert.

    Wrap an existing body value (a `std::string`, a `boost::json::value`,
    a `boost::beast::http::response`, etc.), then chain
    @ref set_status, @ref set_version, @ref set_header, and
    @ref insert_header on the result before consuming the final response
    with @ref get_response, or returning the builder from a REST handler,
    in which case the conversion is automatic:

    @code
    return taar::response_builder(boost::json::value{ {"ok", true} })
        .set_status(http::status::created)
        .set_header(http::field::cache_control, "no-store");
    @endcode

    Member functions return a reference to allow chaining; the rvalue
    overloads return a moved-from copy so the temporary can be returned
    directly.
*/
class response_builder
{
public:
    /// Construct around a value that @ref response_from can convert.
    template <typename... ArgsType>
    response_builder(ArgsType&&... args)
        : impl_ {
            std::make_unique<detail::response_builder_impl<ArgsType...>>(
                std::forward<ArgsType>(args)...)}
    {}

    // Following overloads will be replaced when P0847R7 becomes widely available
    /// Set the HTTP version (`10` or `11`).
    auto&& set_version(unsigned version) &
    {
        impl_->set_version(version);
        return *this;
    }

    /// Set the HTTP version (`10` or `11`).
    auto&& set_version(unsigned version) &&
    {
        impl_->set_version(version);
        return std::move(*this);
    }

    /// Set the response status.
    auto&& set_status(boost::beast::http::status status) &
    {
        impl_->set_status(status);
        return *this;
    }

    /// Set the response status.
    auto&& set_status(boost::beast::http::status status) &&
    {
        impl_->set_status(status);
        return std::move(*this);
    }

    /// Add a header field, keeping fields of the same name.
    auto&& insert_header(
        std::string_view name,
        std::string_view const& value) &
    {
        impl_->insert_header(name, value);
        return *this;
    }

    /// Add a header field, keeping fields of the same name.
    auto&& insert_header(
        std::string_view name,
        std::string_view const& value) &&
    {
        impl_->insert_header(name, value);
        return std::move(*this);
    }

    /// Add a header field, keeping fields of the same name.
    auto&& insert_header(
        boost::beast::http::field field,
            std::string_view const& value) &
    {
        impl_->insert_header(field, value);
        return *this;
    }

    /// Add a header field, keeping fields of the same name.
    auto&& insert_header(
        boost::beast::http::field field,
            std::string_view const& value) &&
    {
        impl_->insert_header(field, value);
        return std::move(*this);
    }

    /// Set a header field, replacing fields of the same name.
    auto&& set_header(
        std::string_view name,
        std::string_view const& value) &
    {
        impl_->set_header(name, value);
        return *this;
    }

    /// Set a header field, replacing fields of the same name.
    auto&& set_header(
        std::string_view name,
        std::string_view const& value) &&
    {
        impl_->set_header(name, value);
        return std::move(*this);
    }

    /// Set a header field, replacing fields of the same name.
    auto&& set_header(
        boost::beast::http::field field,
            std::string_view const& value) &
    {
        impl_->set_header(field, value);
        return *this;
    }

    /// Set a header field, replacing fields of the same name.
    auto&& set_header(
        boost::beast::http::field field,
            std::string_view const& value) &&
    {
        impl_->set_header(field, value);
        return std::move(*this);
    }

    /// Consume the builder and return a Beast message generator.
    boost::beast::http::message_generator get_response() &&
    {
        return std::move(*impl_).get_response(false);
    }

    // Support for response_from
    friend auto tag_invoke(
        detail::response_from_built_in_tag<response_builder>,
        response_builder&& value)
    {
        return std::move(value).get_response();
    }

    // A session finishes the header before writing it.
    friend detail::built_response tag_invoke(
        detail::session_response_tag,
        response_builder&& value)
    {
        return detail::built_response {std::move(value.impl_)};
    }

private:
    std::unique_ptr<detail::response_builder_impl_base> impl_;
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_RESPONSE_BUILDER_HPP
