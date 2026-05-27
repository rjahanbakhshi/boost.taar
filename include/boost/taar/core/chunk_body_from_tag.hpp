//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_CHUNK_BODY_FROM_TAG_HPP
#define BOOST_TAAR_CORE_CHUNK_BODY_FROM_TAG_HPP

namespace boost::taar {

/** Tag used to customise @ref chunk_body_from for user-defined chunk types.

    Overload `tag_invoke(chunk_body_from_tag<MyType>, MyType const&)` to
    teach the chunked-response machinery how to serialise a value of your
    type into the bytes of one HTTP chunk. The overload must return a type
    convertible to `std::string`.

    @tparam T The decayed chunk value type.
*/
template <typename T>
struct chunk_body_from_tag {};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_CHUNK_BODY_FROM_TAG_HPP
