//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#ifndef BOOST_TAAR_CORE_FORM_KVP_HPP
#define BOOST_TAAR_CORE_FORM_KVP_HPP
#include <unordered_map>
#include <string>

namespace boost::taar {

/** Key/value map produced by @ref boost::taar::handler::url_encoded_form_data_arg.

    Keys are percent-decoded parameter names. Values are percent-decoded
    parameter values. The map is unordered; if the same key appears more
    than once in the form data only one value is retained (which one is
    unspecified).
*/
using form_kvp = std::unordered_map<std::string, std::string>;

} // namespace boost::taar

#endif // BOOST_TAAR_CORE_FORM_KVP_HPP
