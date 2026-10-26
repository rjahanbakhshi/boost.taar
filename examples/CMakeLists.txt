project(boost-taar-examples)

find_package(Boost 1.84 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

set(TAAR_EXAMPLES
    cancellation_server
    connection_state_server
    cookie_session_server
    hello_server
    htdocs_server
    jsonrpc_server
    mcp_server
    mcp_stdio_server
    proxy_server
    rest_crud_server
    streaming_server
    taskboard_server
    tls_server
    websocket_server
)

# The stdio transport needs POSIX stream descriptors.
if(WIN32)
    list(REMOVE_ITEM TAAR_EXAMPLES mcp_stdio_server)
endif()

foreach(example IN LISTS TAAR_EXAMPLES)
    add_executable(${example} EXCLUDE_FROM_ALL ${example}.cpp)
    set_target_properties(${example} PROPERTIES CXX_STANDARD 23 UNITY_BUILD ${ENABLE_UNITY_BUILD})
    target_link_libraries(${example} PRIVATE boost-taar)
endforeach()

target_link_libraries(tls_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)

add_custom_target(examples DEPENDS ${TAAR_EXAMPLES})
