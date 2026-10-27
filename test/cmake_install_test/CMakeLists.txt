#
# Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/rjahanbakhshi/boost.taar
#

# Uses an installed Boost.Taar through find_package, as installed by the
# Boost superproject (cmake --install with BOOST_INCLUDE_LIBRARIES=taar).

cmake_minimum_required(VERSION 3.8...3.31)

project(cmake_install_test LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED COMPONENTS taar)

add_executable(quick ../quick.cpp)
target_link_libraries(quick Boost::taar)

enable_testing()
add_test(quick quick)

add_custom_target(check VERBATIM COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --no-tests=error -C $<CONFIG>)
