project(boost-taar-test)

find_package(Boost 1.83 COMPONENTS test REQUIRED)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)
set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD ${ENABLE_UNITY_BUILD})
target_sources(
    ${PROJECT_NAME}
    PRIVATE
        test.cpp
        test_callable_traits.cpp
        test_callable_with.cpp
        test_constexpr_string.cpp
        test_header.cpp
        test_http_session.cpp
        test_is_http_response.cpp
        test_member_function_of.cpp
        test_method.cpp
        test_operand.cpp
        test_pack_element.cpp
        test_response_builder.cpp
        test_response_from_built_in.cpp
        test_response_from_user.cpp
        test_rest.cpp
        test_rest_arg.cpp
        test_specialization_of.cpp
        test_super_type.cpp
        test_target.cpp
        test_template_parser.cpp
        test_version.cpp
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
