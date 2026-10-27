cmake_minimum_required(VERSION 3.8...3.28)

# Usage requirements shared by the Boost superproject and standalone builds.
function(taar_interface_settings target)
    # Boost.Asio requires _WIN32_WINNT to be defined on Windows.
    if(WIN32)
        target_compile_definitions(
            ${target}
            INTERFACE
                _WIN32_WINNT=0x0A00
                WIN32_LEAN_AND_MEAN
                NOMINMAX
        )
    endif()

    # MSVC needs the conforming preprocessor for boost.describe and other
    # variadic-macro-heavy boost code, plus accurate __cplusplus reporting.
    # /bigobj lifts the 65535-section COFF limit that heavy template
    # instantiation in consumer TUs (boost.beast, asio coroutines, etc.)
    # routinely exceeds, especially under Debug.
    if(MSVC)
        target_compile_options(
            ${target}
            INTERFACE
                /bigobj
                /Zc:preprocessor
                /Zc:__cplusplus
        )
    endif()
endfunction()

#
# Inside the Boost superproject (add_subdirectory from boost/CMakeLists.txt):
# behave like a Boost library. The superproject scans the Boost::<lib> names
# below (one per line) to resolve the dependencies of
# BOOST_INCLUDE_LIBRARIES=taar, and installs the target with boost_install.
#
if(BOOST_SUPERPROJECT_VERSION AND NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(boost_taar VERSION "${BOOST_SUPERPROJECT_VERSION}" LANGUAGES CXX)

    add_library(boost_taar INTERFACE)
    add_library(Boost::taar ALIAS boost_taar)

    target_include_directories(boost_taar INTERFACE include)

    # Every Boost library the headers include directly.
    target_link_libraries(boost_taar
        INTERFACE
            Boost::algorithm
            Boost::asio
            Boost::assert
            Boost::beast
            Boost::core
            Boost::describe
            Boost::json
            Boost::mp11
            Boost::optional
            Boost::system
            Boost::url
    )

    if(NOT CMAKE_VERSION VERSION_LESS 3.12)
        target_compile_features(boost_taar INTERFACE cxx_std_20)
    endif()

    taar_interface_settings(boost_taar)

    if(COMMAND boost_install AND NOT BOOST_SKIP_INSTALL_RULES)
        boost_install(
            TARGETS boost_taar
            VERSION "${BOOST_SUPERPROJECT_VERSION}"
            HEADER_DIRECTORY include)
    endif()

    if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt")
        set(ENABLE_UNITY_BUILD OFF)
        add_subdirectory(test)
    endif()

    return()
endif()

#
# Standalone build: Boost from find_package (Conan or an installed Boost),
# with tests, examples and an install rule.
#
cmake_minimum_required(VERSION 3.28)

if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(ENABLE_CLANG_TIDY OFF CACHE BOOL "Enable clang-tidy for supported targets.")
set(ENABLE_UNITY_BUILD OFF CACHE BOOL "Enable unity build.")

project(
    boost-taar
    VERSION 1.0.1
    DESCRIPTION "A collection of tools to facilitate web server and client development."
)

find_package(Boost 1.84 CONFIG COMPONENTS url json system REQUIRED)

add_library(${PROJECT_NAME} INTERFACE)
add_library(Boost::taar ALIAS ${PROJECT_NAME})
# C++20 is the minimum; consumers (and CI) may build with a newer standard
# through CMAKE_CXX_STANDARD or Conan's compiler.cppstd.
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

target_sources(
    ${PROJECT_NAME}
    PUBLIC FILE_SET HEADERS BASE_DIRS include FILES
        include/boost/taar/core/detail/io_deadline.hpp
        include/boost/taar/core/detail/move_only_function.hpp
        include/boost/taar/core/accept_encoding.hpp
        include/boost/taar/core/async_channel.hpp
        include/boost/taar/core/async_generator.hpp
        include/boost/taar/core/awaitable.hpp
        include/boost/taar/core/base64.hpp
        include/boost/taar/core/body_reader.hpp
        include/boost/taar/core/byte_range.hpp
        include/boost/taar/core/cancellation_signals.hpp
        include/boost/taar/core/chunk_body_from.hpp
        include/boost/taar/core/chunk_body_from_tag.hpp
        include/boost/taar/core/chunked_response.hpp
        include/boost/taar/core/chunked_response_meta.hpp
        include/boost/taar/core/constexpr_string.hpp
        include/boost/taar/core/context_store.hpp
        include/boost/taar/core/cookies.hpp
        include/boost/taar/core/cors.hpp
        include/boost/taar/core/entity_tag.hpp
        include/boost/taar/core/error.hpp
        include/boost/taar/core/form_kvp.hpp
        include/boost/taar/core/http_date.hpp
        include/boost/taar/core/ignore_and_rethrow.hpp
        include/boost/taar/core/interceptor.hpp
        include/boost/taar/core/is_async_generator.hpp
        include/boost/taar/core/is_awaitable.hpp
        include/boost/taar/core/is_chunked_response.hpp
        include/boost/taar/core/is_http_response.hpp
        include/boost/taar/core/json_schema.hpp
        include/boost/taar/core/member_function_of.hpp
        include/boost/taar/core/mime.hpp
        include/boost/taar/core/multipart.hpp
        include/boost/taar/core/rebind_executor.hpp
        include/boost/taar/core/request_attributes.hpp
        include/boost/taar/core/response_builder.hpp
        include/boost/taar/core/response_from.hpp
        include/boost/taar/core/response_from_tag.hpp
        include/boost/taar/core/segmented_body.hpp
        include/boost/taar/handler/constrained.hpp
        include/boost/taar/handler/context_inject.hpp
        include/boost/taar/handler/guard.hpp
        include/boost/taar/handler/htdocs.hpp
        include/boost/taar/handler/path_arg_check.hpp
        include/boost/taar/handler/rest.hpp
        include/boost/taar/handler/rest_arg.hpp
        include/boost/taar/handler/rest_arg_cast.hpp
        include/boost/taar/handler/rest_arg_cast_tag.hpp
        include/boost/taar/handler/rest_param.hpp
        include/boost/taar/handler/session_key.hpp
        include/boost/taar/handler/with_session.hpp
        include/boost/taar/jsonrpc/arg.hpp
        include/boost/taar/jsonrpc/bind.hpp
        include/boost/taar/jsonrpc/endpoint.hpp
        include/boost/taar/jsonrpc/envelope.hpp
        include/boost/taar/jsonrpc/error.hpp
        include/boost/taar/jsonrpc/request_scope.hpp
        include/boost/taar/jsonrpc/response.hpp
        include/boost/taar/matcher/detail/callable_with.hpp
        include/boost/taar/matcher/context.hpp
        include/boost/taar/matcher/cookie.hpp
        include/boost/taar/matcher/header.hpp
        include/boost/taar/matcher/metadata.hpp
        include/boost/taar/matcher/method.hpp
        include/boost/taar/matcher/operand.hpp
        include/boost/taar/matcher/route_shape.hpp
        include/boost/taar/matcher/target.hpp
        include/boost/taar/matcher/template_parser.hpp
        include/boost/taar/matcher/version.hpp
        include/boost/taar/mcp/arg.hpp
        include/boost/taar/mcp/content.hpp
        include/boost/taar/mcp/endpoint.hpp
        include/boost/taar/mcp/error.hpp
        include/boost/taar/openapi/generator.hpp
        include/boost/taar/server/tcp.hpp
        include/boost/taar/server/tls.hpp
        include/boost/taar/session/http.hpp
        include/boost/taar/session/https.hpp
        include/boost/taar/type_traits/always_false.hpp
        include/boost/taar/type_traits/callable.hpp
        include/boost/taar/type_traits/container_like.hpp
        include/boost/taar/type_traits/has_call_operator.hpp
        include/boost/taar/type_traits/numeric_type.hpp
        include/boost/taar/type_traits/specialization_of.hpp
        include/boost/taar/type_traits/string_like.hpp
        include/boost/taar/type_traits/super_type.hpp
)

target_link_libraries(${PROJECT_NAME} INTERFACE Boost::boost Boost::url Boost::json)

taar_interface_settings(${PROJECT_NAME})

install(TARGETS ${PROJECT_NAME} FILE_SET HEADERS)

if(ENABLE_CLANG_TIDY)
    include(${CMAKE_SOURCE_DIR}/cmake/clang-tidy.cmake)
    enable_clang_tidy_for(${PROJECT_NAME})
endif()

# enable testing
include(CTest)
add_subdirectory(test)

# examples
add_subdirectory(example)

