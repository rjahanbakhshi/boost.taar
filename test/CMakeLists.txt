project(boost-taar-test)

find_package(Boost 1.84 CONFIG REQUIRED)

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
        test.cpp
        test_callable_with.cpp
        test_constexpr_string.cpp
        test_cookies.cpp
        test_tcp_server.cpp
        test_http_session.cpp
        test_is_http_response.cpp
        test_matcher_cookie.cpp
        test_matcher_header.cpp
        test_matcher_method.cpp
        test_matcher_operand.cpp
        test_matcher_target.cpp
        test_matcher_version.cpp
        test_member_function_of.cpp
        test_response_builder.cpp
        test_response_from.cpp
        test_response_from_user.cpp
        test_rest.cpp
        test_rest_arg.cpp
        test_rest_arg_cast_builtin.cpp
        test_rest_arg_cast_user.cpp
        test_template_parser.cpp
        to_response.h
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE boost-taar
)

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)
