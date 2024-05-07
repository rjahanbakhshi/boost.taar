project(boost-web-test)

find_package(Boost 1.83 COMPONENTS test REQUIRED)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)
target_sources(${PROJECT_NAME} PRIVATE test_matcher.cpp)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE boost-web
)

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)
