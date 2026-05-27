//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_HANDLER_REST_ARG_CAST_TAG_HPP
#define BOOST_TAAR_HANDLER_REST_ARG_CAST_TAG_HPP

namespace boost::taar::handler {

/** Tag used to customise @ref rest_arg_cast for user-defined target types.

    Overload `tag_invoke(rest_arg_cast_tag<MyType>, FromType const&)` in the
    same namespace as `MyType` to teach the REST argument machinery how to
    produce a `MyType` from whatever value an argument provider returns.

    @code
    namespace my {
        struct user_id { std::uint64_t value; };

        user_id tag_invoke(
            boost::taar::handler::rest_arg_cast_tag<user_id>,
            std::string_view text)
        {
            return user_id{ std::stoull(std::string{text}) };
        }
    }
    @endcode

    User-defined overloads take precedence over the built-in conversions
    for arithmetic, boolean, JSON, and string-like targets.

    @tparam ToType The decayed target type.
*/
template <typename ToType>
struct rest_arg_cast_tag {};

} // namespace boost::taar::handler

#endif // BOOST_TAAR_HANDLER_REST_ARG_CAST_TAG_HPP
