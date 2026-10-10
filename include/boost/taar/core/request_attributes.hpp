//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_REQUEST_ATTRIBUTES_HPP
#define BOOST_TAAR_CORE_REQUEST_ATTRIBUTES_HPP

#include <any>
#include <type_traits>
#include <utility>
#include <vector>

namespace boost::taar {

/** A small type-indexed bag of values attached to one request.

    Carried by @ref boost::taar::matcher::context::attributes, so whatever
    an interceptor or a `handler::guard` check stores is visible to the
    handler that later runs on the same request, and to argument providers
    such as `handler::attribute_arg<T>()`. At most one value is kept per
    type: storing a second value of the same type replaces the first.
    Define a dedicated type (`struct principal { std::string user; };`)
    rather than storing a bare `std::string`, so that unrelated code does
    not collide on the key.

    Values must be copy-constructible (they are held in a `std::any`). An
    empty bag allocates nothing.

    @code
    struct principal { std::string user; };

    ctx.attributes.set(principal {"alice"});
    if (auto const* p = ctx.attributes.find<principal>())
        use(p->user);
    @endcode
*/
class request_attributes
{
public:
    /// Store @a value under its type `T`, replacing any value of that type.
    template <typename T>
    void set(T value)
    {
        static_assert(std::is_copy_constructible_v<T>,
            "request_attributes stores copy-constructible values only.");
        auto const id = id_of<T>();
        for (auto& e : entries_)
        {
            if (e.id == id)
            {
                e.value.template emplace<T>(std::move(value));
                return;
            }
        }
        entries_.push_back(entry {id, std::any {std::in_place_type<T>, std::move(value)}});
    }

    /// The stored value of type `T`, or `nullptr` if there is none.
    template <typename T>
    T* find() noexcept
    {
        auto const id = id_of<T>();
        for (auto& e : entries_)
        {
            if (e.id == id) return std::any_cast<T>(&e.value);
        }
        return nullptr;
    }

    /// The stored value of type `T`, or `nullptr` if there is none.
    template <typename T>
    T const* find() const noexcept
    {
        auto const id = id_of<T>();
        for (auto const& e : entries_)
        {
            if (e.id == id) return std::any_cast<T>(&e.value);
        }
        return nullptr;
    }

    /// True if a value of type `T` is stored.
    template <typename T>
    bool contains() const noexcept
    {
        return find<T>() != nullptr;
    }

    /// True if no value is stored.
    bool empty() const noexcept
    {
        return entries_.empty();
    }

private:
/// @cond
    using type_id = void const*;

    template <typename T>
    struct tag
    {
        static constexpr char id = 0;
    };

    template <typename T>
    static type_id id_of() noexcept
    {
        return &tag<T>::id;
    }

    struct entry
    {
        type_id id;
        std::any value;
    };

    std::vector<entry> entries_;
/// @endcond
};

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_REQUEST_ATTRIBUTES_HPP
