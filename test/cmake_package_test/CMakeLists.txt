#
# Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/rjahanbakhshi/boost.taar
#

# Uses the CMake package a standalone build installs (cmake --install):
# find_package(boost-taar) and both target names it provides.

cmake_minimum_required(VERSION 3.23...3.31)

project(cmake_package_test LANGUAGES CXX)

find_package(boost-taar 1.0 CONFIG REQUIRED)

add_executable(quick ../quick.cpp)
target_link_libraries(quick boost-taar::boost-taar)

add_executable(quick_alias ../quick.cpp)
target_link_libraries(quick_alias Boost::taar)

# No C++20 modules here: skip the dependency scan, which needs
# clang-scan-deps with Clang and Ninja.
set_target_properties(quick quick_alias PROPERTIES CXX_SCAN_FOR_MODULES OFF)

enable_testing()
add_test(quick quick)
add_test(quick_alias quick_alias)

add_custom_target(check VERBATIM COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --no-tests=error -C $<CONFIG>)
