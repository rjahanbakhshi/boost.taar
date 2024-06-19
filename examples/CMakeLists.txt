project(boost-web-examples)

find_package(Boost 1.83 COMPONENTS test REQUIRED)

add_executable(http_server EXCLUDE_FROM_ALL)
set_target_properties(http_server PROPERTIES CXX_STANDARD 23)
target_sources(http_server PRIVATE http_server.cpp)
target_link_libraries(http_server PRIVATE boost-web)

