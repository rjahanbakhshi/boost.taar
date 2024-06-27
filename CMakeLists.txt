cmake_minimum_required(VERSION 3.28)

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
        boost/web/core/always_false.hpp
        boost/web/core/awaitable.hpp
        boost/web/core/callable_traits.hpp
        boost/web/core/cancellation_signals.hpp
        boost/web/core/ignore_and_rethrow.hpp
        boost/web/core/invoke_response.hpp
        boost/web/core/member_function_of.hpp
        boost/web/core/rebind_executor.hpp
        boost/web/core/specialization_of.hpp
        boost/web/core/super_type.hpp
        boost/web/handler/htdocs.hpp
        boost/web/handler/rest.hpp
        boost/web/handler/rest_arg.hpp
        boost/web/handler/rest_error.hpp
        boost/web/matcher/detail/callable_with.hpp
        boost/web/matcher/detail/join_with.hpp
        boost/web/matcher/detail/ranges_to.hpp
        boost/web/matcher/context.hpp
        boost/web/matcher/error.hpp
        boost/web/matcher/header.hpp
        boost/web/matcher/method.hpp
        boost/web/matcher/operand.hpp
        boost/web/matcher/target.hpp
        boost/web/matcher/template_parser.hpp
        boost/web/matcher/version.hpp
        boost/web/server/tcp.hpp
        boost/web/session/http.hpp
)

target_link_libraries(
    ${PROJECT_NAME}
    INTERFACE
        Boost::boost
        Boost::url
        Boost::json
)

install(TARGETS ${PROJECT_NAME} FILE_SET HEADERS)

if(ENABLE_CLANG_TIDY)
    include(${CMAKE_SOURCE_DIR}/cmake/clang-tidy.cmake)
    enable_clang_tidy_for(${PROJECT_NAME})
endif()

# enable testing
include(CTest)
add_subdirectory(test)

# examples
add_subdirectory(examples)

