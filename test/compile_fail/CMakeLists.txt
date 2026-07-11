# Compile-fail probes: each target must fail to compile for the reason noted
# at the top of its source file. ctest runs the build and expects failure.

function(taar_compile_probe name must_fail)
    add_library(compile_probe_${name} OBJECT EXCLUDE_FROM_ALL ${name}.cpp)
    set_target_properties(compile_probe_${name} PROPERTIES CXX_STANDARD 23)
    target_link_libraries(compile_probe_${name} PRIVATE boost-taar)
    add_test(
        NAME compile_fail.${name}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target compile_probe_${name})
    set_tests_properties(compile_fail.${name} PROPERTIES WILL_FAIL ${must_fail})
endfunction()

taar_compile_probe(control_compiles FALSE)
taar_compile_probe(context_after_provider TRUE)
taar_compile_probe(context_not_in_scope TRUE)
taar_compile_probe(keyed_context_without_with_session TRUE)
taar_compile_probe(mcp_on_initialize_wrong_signature TRUE)
taar_compile_probe(openapi_method_negated TRUE)
taar_compile_probe(openapi_negation TRUE)
taar_compile_probe(openapi_or_between_paths TRUE)
taar_compile_probe(provider_count_mismatch TRUE)
taar_compile_probe(return_type_without_response_from TRUE)
