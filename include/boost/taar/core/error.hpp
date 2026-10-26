//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_ERROR_HPP
#define BOOST_TAAR_HANDLER_ERROR_HPP

#include <boost/system/error_category.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>
#include <boost/system/system_error.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/json/value.hpp>
#include <boost/assert/source_location.hpp>
#include <concepts>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace boost::taar {

/** Error codes reported by Boost.Taar.

    All values share the @ref error_category category, and they can be used
    transparently with `boost::system::error_code` thanks to the
    `is_error_code_enum` specialisation at the bottom of this header.

    Many of these are reported as *soft* errors by
    @ref boost::taar::session::http through its soft-error handler, which
    by default turns them into 400/404/415 HTTP responses.
*/
enum class error
{
    success = 0,                ///< No error.
    no_absolute_template,       ///< A path template did not begin with '/'.
    invalid_template,           ///< Path-template syntax error.
    argument_not_found,         ///< A required argument was absent from the request.
    argument_ambiguous,         ///< A header or query parameter occurred more than once.
    invalid_argument_format,    ///< An argument could not be parsed to the requested type.
    invalid_request_format,     ///< The request body could not be decoded.
    invalid_url_format,         ///< The request URL could not be parsed.
    invalid_content_type,       ///< The request `Content-Type` does not match the handler.
    invalid_cookie_format,      ///< The `Cookie` header could not be parsed.
    invalid_boolean_format,     ///< A boolean argument could not be parsed.
    invalid_number_format,      ///< A numeric argument could not be parsed.
    late_chunk_metadata,        ///< Chunked metadata was yielded after body data.
    constraint_violated,        ///< An argument failed its declared constraints.
    unknown_exception,          ///< A foreign exception that carries no error code.
};

/// Singleton @c boost::system::error_category instance for the @c error enum.
#if (__cpp_constexpr >= 202211L)
constexpr
#endif
inline boost::system::error_category const& error_category() noexcept
{
    struct error_category_type : boost::system::error_category
    {
        [[nodiscard]] char const* name() const noexcept override
        {
            return "Taar error";
        }

        [[nodiscard]] std::string message(int ev) const override
        {
            switch (static_cast<error>(ev))
            {
            case error::success:
                return "Success.";
            case error::no_absolute_template:
                return "Specified template is not an absolute path.";
            case error::invalid_template:
                return "Invalid template path.";
            case error::argument_not_found:
                return "Argument not found";
            case error::argument_ambiguous:
                return "Argument ambiguous";
            case error::invalid_argument_format:
                return "Invalid argument format.";
            case error::invalid_request_format:
                return "Invalid request format";
            case error::invalid_url_format:
                return "Invalid url format";
            case error::invalid_content_type:
                return "Invalid content type";
            case error::invalid_cookie_format:
                return "Invalid cookie format";
            case error::invalid_boolean_format:
                return "Invalid boolean format";
            case error::invalid_number_format:
                return "Invalid number format";
            case error::late_chunk_metadata:
                return "Chunk metadata yielded after data.";
            case error::constraint_violated:
                return "Argument constraint violated";
            case error::unknown_exception:
                return "Unknown exception";
            }

            return "(Unknown error)";
        }
    };

#if (__cpp_constexpr >= 202211L)
    constexpr
#endif
    static error_category_type instance;
    return instance;
}

/// ADL-found factory required by `is_error_code_enum<error>`.
#if (__cpp_constexpr >= 202211L)
constexpr
#endif
inline boost::system::error_code make_error_code(error ev) noexcept
{
    return boost::system::error_code {
        static_cast<std::underlying_type<error>::type>(ev),
        error_category()};
}

} // namespace boost::taar

namespace boost::system {

template<>
struct is_error_code_enum<::boost::taar::error>
{
    static bool const value = true;
};

} // namespace boost::system

namespace boost::taar {

class exception;

/** A failure: what went wrong, where, and why.

    The one error value of Boost.Taar. A failure carries

    @li an `error_code` from any category (@ref error, Asio, Beast,
        Boost.JSON, `jsonrpc::error`, generic `errc`, ...);
    @li a message specific to this occurrence ("query parameter 'page'
        occurs more than once"), which does not repeat the code's generic
        text and reads on its own, since @ref text prints it instead of
        the code's text;
    @li optional structured data (a JSON value);
    @li an optional nested cause, so a failure can explain itself in
        layers ("argument page (position 1) is invalid" caused by
        "value 'abc' is not a number");
    @li the source location where it was raised;
    @li optionally the foreign exception it was made from (see
        @ref from_exception), so a user can still @ref rethrow and catch
        their own type.

    A default-constructed failure means "no error": its code is success.
    A failure with no message, data or cause allocates nothing, which
    keeps hot paths such as @ref error::argument_not_found cheap. Copies
    share the cause chain.

    @see result, exception
*/
class failure
{
public:
    /// No error: `code()` is success.
    failure() = default;

    /// A failure carrying @a code, an optional @a message for this occurrence, and its location.
    explicit failure(
            boost::system::error_code code,
            std::string message = {},
            boost::source_location const& where = BOOST_CURRENT_LOCATION)
        : code_ {code}
        , message_ {std::move(message)}
        , where_ {where}
    {}

    /// Convenience for codes of the @ref error enum.
    explicit failure(
            error code,
            std::string message = {},
            boost::source_location const& where = BOOST_CURRENT_LOCATION)
        : failure {make_error_code(code), std::move(message), where}
    {}

    /// The error code; any category.
    [[nodiscard]] boost::system::error_code const& code() const noexcept
    {
        return code_;
    }

    /// The message for this occurrence only; may be empty.
    [[nodiscard]] std::string const& message() const noexcept
    {
        return message_;
    }

    /// Structured data attached to this occurrence; null when none.
    [[nodiscard]] boost::json::value const& data() const noexcept
    {
        return data_;
    }

    /// The nested cause, or `nullptr`.
    [[nodiscard]] failure const* cause() const noexcept
    {
        return cause_.get();
    }

    /// Where the failure was raised.
    [[nodiscard]] boost::source_location const& where() const noexcept
    {
        return where_;
    }

    /// The innermost foreign exception kept in the chain, if any.
    [[nodiscard]] std::exception_ptr exception() const noexcept
    {
        std::exception_ptr out;
        for (failure const* f = this; f != nullptr; f = f->cause())
        {
            if (f->exception_) out = f->exception_;
        }
        return out;
    }

    /// Replace the message.
    failure& with_message(std::string message)
    {
        message_ = std::move(message);
        return *this;
    }

    /// Replace the structured data.
    failure& with_data(boost::json::value data)
    {
        data_ = std::move(data);
        return *this;
    }

    /// Nest @a cause below this failure.
    failure& with_cause(failure cause)
    {
        cause_ = std::make_shared<failure const>(std::move(cause));
        return *this;
    }

    /// Keep @a exception, the foreign exception this failure stands for.
    failure& with_exception(std::exception_ptr exception)
    {
        exception_ = std::move(exception);
        return *this;
    }

    /// True if this failure's own code equals @a code.
    [[nodiscard]] bool is(boost::system::error_code const& code) const noexcept
    {
        return code_ == code;
    }

    /// True if this failure or any of its causes carries @a code.
    [[nodiscard]] bool has(boost::system::error_code const& code) const noexcept
    {
        for (failure const* f = this; f != nullptr; f = f->cause())
        {
            if (f->code_ == code) return true;
        }
        return false;
    }

    /// The innermost cause (this failure if it has none).
    [[nodiscard]] failure const& root() const noexcept
    {
        failure const* f = this;
        while (f->cause() != nullptr) f = f->cause();
        return *f;
    }

    /** Render the failure and its causes as text.

        Each level reads `message: code text [category:value]` (or
        `code text [category:value]` when it has no message); causes
        follow in `(caused by: ...)`. For a failure without cause this is
        the same text as `boost::system::system_error::what()`.
    */
    [[nodiscard]] std::string chain() const
    {
        std::string out;
        std::size_t depth = 0;
        for (failure const* f = this; f != nullptr; f = f->cause(), ++depth)
        {
            if (depth > 0) out += " (caused by: ";
            if (!f->message_.empty())
            {
                out += f->message_;
                out += ": ";
            }
            out += f->code_.message();
            out += " [";
            out += f->code_.to_string();
            out += "]";
        }
        if (depth > 1) out.append(depth - 1, ')');
        return out;
    }

    /** Render the failure and its causes as human-readable text.

        Like @ref chain but without category tags: each level reads its
        @ref message, or the code's own text when the message is empty;
        causes follow in `(caused by: ...)`. Suitable for response bodies
        and tool error content; @ref chain is for logs.
    */
    [[nodiscard]] std::string text() const
    {
        std::string out;
        std::size_t depth = 0;
        for (failure const* f = this; f != nullptr; f = f->cause(), ++depth)
        {
            if (depth > 0) out += " (caused by: ";
            out += f->message_.empty() ? f->code_.message() : f->message_;
        }
        if (depth > 1) out.append(depth - 1, ')');
        return out;
    }

    /** A copy of the kept foreign exception (@ref exception) if it is an @a E.

        Lets a hook recognise its own domain exception without a
        try/catch ladder:

        @code
        if (auto const e = f.as<not_found>()) return reply(404, e->what());
        @endcode

        The result is a copy because the standard leaves it unspecified
        whether `std::rethrow_exception` copies the exception object (MSVC
        does), so the address of a caught object cannot be returned
        portably. The copy is of the @a E subobject: ask for the type that
        was thrown, or a base that carries what you need (the standard
        exceptions keep their `what()` text; `std::exception` itself does
        not, so test with `f.as<std::exception>().has_value()` rather than
        reading it).

        @tparam E A copy-constructible exception type.
        @return The copy, or `std::nullopt` if there is no kept exception
                or it is not an @a E.
    */
    template <typename E>
        requires std::copy_constructible<E>
    [[nodiscard]] std::optional<E> as() const
    {
        auto const eptr = exception();
        if (!eptr) return std::nullopt;
        try
        {
            std::rethrow_exception(eptr);
        }
        catch (E const& e)
        {
            return e;
        }
        catch (...)
        {
        }
        return std::nullopt;
    }

    /** Throw the failure.

        Rethrows the kept foreign exception (@ref exception) if there is
        one, so a user's own exception type can be caught again; throws
        @ref boost::taar::exception carrying this failure otherwise.
    */
    [[noreturn]] void rethrow() const;

    /** Wrap an exception as a failure.

        @li @ref boost::taar::exception: its failure, unwrapped.
        @li `boost::system::system_error` and `std::system_error`: the
            exception's code, its text without the code's own text as the
            message, the exception kept.
        @li Any other `std::exception`: @ref error::unknown_exception with
            `what()` as the message, the exception kept.
        @li Anything else: @ref error::unknown_exception, message
            "unknown exception", the exception kept.
        @li A null pointer: no error.
    */
    [[nodiscard]] static failure from_exception(std::exception_ptr eptr);

private:
    /// @a what without a trailing @a suffix (and its ": " separator), if present.
    static std::string strip_code_text(std::string_view what, std::string_view suffix)
    {
        if (what == suffix) return {};
        if (what.size() > suffix.size() + 2 &&
            what.ends_with(suffix) &&
            what.substr(what.size() - suffix.size() - 2, 2) == ": ")
        {
            return std::string {what.substr(0, what.size() - suffix.size() - 2)};
        }
        return std::string {what};
    }

    boost::system::error_code code_ {};
    std::string message_ {};
    boost::json::value data_ {};
    std::shared_ptr<failure const> cause_ {};
    boost::source_location where_ {};
    std::exception_ptr exception_ {};
};

/** The non-throwing result shape of Boost.Taar: a `T` or a @ref failure.

    Accessing `value()` on a failed result throws
    @ref boost::taar::exception carrying the failure (through the ADL hook
    @ref throw_exception_from_error). Construct a failed result explicitly:
    `return failure {error::argument_not_found};`.
*/
template <typename T>
using result = boost::system::result<T, failure>;

/** The exception type Boost.Taar throws.

    Carries a @ref failure; `what()` renders its cause chain
    (@ref failure::chain). Copies are cheap and do not throw.
*/
class exception : public std::exception
{
public:
    /// Throwable wrapper for @a f.
    explicit exception(failure f)
        : state_ {std::make_shared<state const>(std::move(f))}
    {}

    /// The failure this exception carries.
    [[nodiscard]] failure const& error() const noexcept
    {
        return state_->failure_;
    }

    /// The rendered cause chain.
    [[nodiscard]] char const* what() const noexcept override
    {
        return state_->what_.c_str();
    }

private:
    struct state
    {
        explicit state(failure f)
            : failure_ {std::move(f)}
            , what_ {failure_.chain()}
        {}

        failure failure_;
        std::string what_;
    };

    std::shared_ptr<state const> state_;
};

/** ADL hook used by `result<T>::value()`: throws @ref boost::taar::exception.

    The failure is thrown as is; its own @ref failure::where is kept and
    @a loc (the `value()` call site) is not recorded.
*/
[[noreturn]] inline void throw_exception_from_error(
    failure const& f,
    boost::source_location const& loc)
{
    (void)loc;
    throw exception {f};
}

inline void failure::rethrow() const
{
    if (auto eptr = exception())
    {
        std::rethrow_exception(eptr);
    }
    throw taar::exception {*this};
}

inline failure failure::from_exception(std::exception_ptr eptr)
{
    if (!eptr) return failure {};
    try
    {
        std::rethrow_exception(eptr);
    }
    catch (taar::exception const& e)
    {
        return e.error();
    }
    catch (boost::system::system_error const& e)
    {
        return failure {e.code(), strip_code_text(e.what(), e.code().what())}.with_exception(eptr);
    }
    catch (std::system_error const& e)
    {
        return failure {
            boost::system::error_code {e.code()},
            strip_code_text(e.what(), e.code().message())}.with_exception(eptr);
    }
    catch (std::exception const& e)
    {
        return failure {error::unknown_exception, e.what()}.with_exception(eptr);
    }
    catch (...)
    {
        return failure {error::unknown_exception, "unknown exception"}.with_exception(eptr);
    }
}

/** The HTTP status a failure maps to by default.

    @li Request-related codes of @ref error (argument not found or
        ambiguous, invalid argument, request, URL, content type, cookie,
        boolean or number format, constraint violated): `400 Bad Request`.
    @li `boost::beast::http::error::body_limit`: `413 Payload Too Large`.
    @li `boost::beast::http::error::header_limit`:
        `431 Request Header Fields Too Large`.
    @li `boost::beast::error::timeout` (a request header or body that did
        not arrive in time): `408 Request Timeout`.
    @li Anything else: `500 Internal Server Error`.

    Only the outermost code is consulted.
*/
[[nodiscard]] inline boost::beast::http::status http_status_for(failure const& f) noexcept
{
    namespace http = boost::beast::http;
    auto const& code = f.code();
    if (code.category() == error_category())
    {
        switch (static_cast<error>(code.value()))
        {
        case error::argument_not_found:
        case error::argument_ambiguous:
        case error::invalid_argument_format:
        case error::invalid_request_format:
        case error::invalid_url_format:
        case error::invalid_content_type:
        case error::invalid_cookie_format:
        case error::invalid_boolean_format:
        case error::invalid_number_format:
        case error::constraint_violated:
            return http::status::bad_request;
        default:
            return http::status::internal_server_error;
        }
    }
    if (code == http::error::body_limit) return http::status::payload_too_large;
    if (code == http::error::header_limit) return http::status::request_header_fields_too_large;
    if (code == boost::beast::error::timeout) return http::status::request_timeout;
    return http::status::internal_server_error;
}

} // namespace boost::taar

#endif // BOOST_TAAR_HANDLER_ERROR_HPP
