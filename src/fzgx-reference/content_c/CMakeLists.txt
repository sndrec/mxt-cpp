add_library(fzgx_content STATIC
  src/fzgx_content.c
  src/fzgx_builtin_content.c
  src/fzgx_random_track.c
)

target_include_directories(fzgx_content
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(fzgx_content
  PUBLIC
    m
)

add_executable(fzgx_content_tests
  tests/test_fzgx_content.c
)

target_link_libraries(fzgx_content_tests PRIVATE fzgx_content)
target_compile_definitions(fzgx_content_tests PRIVATE FZGX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
  add_test(NAME fzgx_content_tests COMMAND fzgx_content_tests)
endif()
