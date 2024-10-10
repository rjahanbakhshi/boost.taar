cmake_minimum_required(VERSION 3.28)

if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(ENABLE_CLANG_TIDY OFF CACHE BOOL "Enable clang-tidy for supported targets.")
set(ENABLE_UNITY_BUILD OFF CACHE BOOL "Enable unity build.")

project(
    boost-taar
    VERSION 1.0
    DESCRIPTION "A collection of tools to facilitate web server and client development."
)

find_package(Boost 1.84 COMPONENTS system REQUIRED)

add_library(${PROJECT_NAME} INTERFACE)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)

target_sources(
    ${PROJECT_NAME}
    PUBLIC FILE_SET HEADERS BASE_DIRS . FILES
        boost/taar/core/always_false.hpp
        boost/taar/core/awaitable.hpp
        boost/taar/core/callable_traits.hpp
        boost/taar/core/cancellation_signals.hpp
        boost/taar/core/constexpr_string.hpp
        boost/taar/core/cookies.hpp
        boost/taar/core/error.hpp
        boost/taar/core/form_kvp.hpp
        boost/taar/core/ignore_and_rethrow.hpp
        boost/taar/core/is_awaitable.hpp
        boost/taar/core/is_http_response.hpp
        boost/taar/core/member_function_of.hpp
        boost/taar/core/move_only_function.hpp
        boost/taar/core/pack_element.hpp
        boost/taar/core/rebind_executor.hpp
        boost/taar/core/response_builder.hpp
        boost/taar/core/response_from.hpp
        boost/taar/core/response_from_tag.hpp
        boost/taar/core/specialization_of.hpp
        boost/taar/core/super_type.hpp
        boost/taar/handler/htdocs.hpp
        boost/taar/handler/rest.hpp
        boost/taar/handler/rest_arg.hpp
        boost/taar/handler/rest_arg_cast.hpp
        boost/taar/handler/rest_arg_cast_tag.hpp
        boost/taar/matcher/detail/callable_with.hpp
        boost/taar/matcher/detail/join_with.hpp
        boost/taar/matcher/detail/ranges_to.hpp
        boost/taar/matcher/context.hpp
        boost/taar/matcher/cookie.hpp
        boost/taar/matcher/header.hpp
        boost/taar/matcher/method.hpp
        boost/taar/matcher/operand.hpp
        boost/taar/matcher/target.hpp
        boost/taar/matcher/template_parser.hpp
        boost/taar/matcher/version.hpp
        boost/taar/server/tcp.hpp
        boost/taar/session/http.hpp
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

