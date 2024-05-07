include_guard()

find_program(
  CLANG_TIDY_EXE
  NAMES "clang-tidy"
  DOC "Path to clang-tidy executable"
)

if(NOT CLANG_TIDY_EXE)
  message(WARNING "clang-tidy not found.")
else()
  message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
  set(
    DO_CLANG_TIDY "${CLANG_TIDY_EXE}"
    "-config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
  )
endif()

function(enable_clang_tidy_for)
  if(CLANG_TIDY_EXE)
    foreach(arg IN ITEMS ${ARGN})
      set_target_properties(${arg} PROPERTIES CXX_CLANG_TIDY "${DO_CLANG_TIDY}")
    endforeach()
  endif()
endfunction()

