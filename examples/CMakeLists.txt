project(boost-web-examples)

find_package(Boost 1.83 COMPONENTS test REQUIRED)

add_executable(htdocs_server EXCLUDE_FROM_ALL)
set_target_properties(htdocs_server PROPERTIES CXX_STANDARD 23)
target_sources(htdocs_server PRIVATE htdocs_server.cpp)
target_link_libraries(htdocs_server PRIVATE boost-web)

add_executable(mixed_server EXCLUDE_FROM_ALL)
set_target_properties(mixed_server PROPERTIES CXX_STANDARD 23)
target_sources(mixed_server PRIVATE mixed_server.cpp)
target_link_libraries(mixed_server PRIVATE boost-web)

add_custom_target(
    examples DEPENDS
        htdocs_server
        mixed_server
)
