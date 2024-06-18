cmake_minimum_required(VERSION 3.29)

if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(ENABLE_CLANG_TIDY OFF CACHE BOOL "Enable clang-tidy for supported targets.")

project(
    boost-web
    VERSION 1.0
    DESCRIPTION "A collection of tools to facilitate web server development."
)

find_package(Boost 1.84 COMPONENTS system REQUIRED)

add_library(${PROJECT_NAME} INTERFACE)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)

target_sources(
    ${PROJECT_NAME}
    PUBLIC FILE_SET HEADERS BASE_DIRS . FILES
        boost/web/matcher/detail/always_false.hpp
        boost/web/matcher/detail/callable_traits.hpp
        boost/web/matcher/detail/callable_with.hpp
        boost/web/matcher/detail/ranges_to.hpp
        boost/web/matcher/detail/specialization_of.hpp
        boost/web/matcher/detail/super_type.hpp
        boost/web/matcher/context.hpp
        boost/web/matcher/error.hpp
        boost/web/matcher/header.hpp
        boost/web/matcher/method.hpp
        boost/web/matcher/operand.hpp
        boost/web/matcher/segments_view.hpp
        boost/web/matcher/target.hpp
        boost/web/matcher/target_parser.hpp
        boost/web/matcher/version.hpp
)

target_link_libraries(
    ${PROJECT_NAME}
    INTERFACE
        Boost::boost
)

install(TARGETS ${PROJECT_NAME} FILE_SET HEADERS)

if(ENABLE_CLANG_TIDY)
    include(${CMAKE_SOURCE_DIR}/cmake/clang-tidy.cmake)
    enable_clang_tidy_for(${PROJECT_NAME})
endif()

# enable testing
include(CTest)
add_subdirectory(test)
