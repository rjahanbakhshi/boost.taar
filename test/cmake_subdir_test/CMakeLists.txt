#
# Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/rjahanbakhshi/boost.taar
#

# Uses Boost.Taar the way an individual Boost library is consumed without the
# superproject: taar and each of its dependencies added with add_subdirectory
# (https://github.com/boostorg/cmake#using-an-individual-boost-library-with-add_subdirectory).
# Run from a Boost superproject checkout, with taar in libs/taar.

cmake_minimum_required(VERSION 3.8...3.31)

project(cmake_subdir_test LANGUAGES CXX)

add_subdirectory(../.. boostorg/taar)

# taar's dependencies, direct and transitive, as listed by
# `boostdep --brief taar`.
set(deps
    algorithm
    align
    array
    asio
    assert
    beast
    bind
    charconv
    compat
    concept_check
    config
    container
    container_hash
    context
    conversion
    core
    date_time
    describe
    detail
    endian
    exception
    function
    function_types
    fusion
    integer
    intrusive
    io
    iterator
    json
    lexical_cast
    logic
    move
    mp11
    mpl
    numeric/conversion
    optional
    pool
    predef
    preprocessor
    range
    regex
    smart_ptr
    static_string
    system
    throw_exception
    tokenizer
    tuple
    type_index
    typeof
    type_traits
    unordered
    url
    utility
    variant2
    winapi
)

set(old_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF)
foreach(dep IN LISTS deps)
    add_subdirectory(../../../${dep} boostorg/${dep})
endforeach()
set(BUILD_TESTING ${old_BUILD_TESTING})

add_executable(quick ../quick.cpp)
target_link_libraries(quick Boost::taar)

enable_testing()
add_test(quick quick)

add_custom_target(check VERBATIM COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --no-tests=error -C $<CONFIG>)
