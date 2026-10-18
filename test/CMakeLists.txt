project(boost-taar-test)

find_package(Boost 1.84 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)

set_target_properties(
    ${PROJECT_NAME} PROPERTIES UNITY_BUILD
    ${ENABLE_UNITY_BUILD})

target_sources(
    ${PROJECT_NAME}
    PRIVATE
        type_traits/test_callable.cpp
        type_traits/test_super_type.cpp
        type_traits/test_specialization_of.cpp
        e2e.hpp
        test.cpp
        test_accept_encoding.cpp
        test_async_channel.cpp
        test_async_generator.cpp
        test_byte_range.cpp
        test_callable_with.cpp
        test_cancellation.cpp
        test_chunk_body_from.cpp
        test_chunked_response.cpp
        test_connection_context.cpp
        test_constrained.cpp
        test_constexpr_string.cpp
        test_context_store.cpp
        test_cookies.cpp
        test_cors.cpp
        test_dual_surface.cpp
        test_e2e_context.cpp
        test_e2e_jsonrpc.cpp
        test_e2e_mcp.cpp
        test_e2e_operational.cpp
        test_e2e_streaming.cpp
        test_e2e_streaming_body.cpp
        test_e2e_tls.cpp
        test_entity_tag.cpp
        test_error.cpp
        test_htdocs.cpp
        test_http_date.cpp
        test_http_session.cpp
        test_interceptors.cpp
        test_is_http_response.cpp
        test_jsonrpc.cpp
        test_jsonrpc_endpoint.cpp
        test_matcher_cookie.cpp
        test_matcher_header.cpp
        test_matcher_metadata.cpp
        test_matcher_method.cpp
        test_matcher_operand.cpp
        test_matcher_target.cpp
        test_matcher_version.cpp
        test_mcp_endpoint.cpp
        test_mcp_stdio.cpp
        test_mcp_stream.cpp
        test_mime.cpp
        test_multipart.cpp
        test_openapi.cpp
        test_member_function_of.cpp
        test_method_fallbacks.cpp
        test_path_arg_check.cpp
        test_response_builder.cpp
        test_response_from.cpp
        test_response_from_user.cpp
        test_rest.cpp
        test_rest_arg.cpp
        test_rest_arg_cast_builtin.cpp
        test_rest_arg_cast_user.cpp
        test_routing_edges.cpp
        test_scenarios.cpp
        test_segmented_body.cpp
        test_tcp_server.cpp
        test_template_parser.cpp
        test_with_session.cpp
        to_response.h
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE boost-taar OpenSSL::SSL OpenSSL::Crypto
)

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

add_subdirectory(compile_fail)
