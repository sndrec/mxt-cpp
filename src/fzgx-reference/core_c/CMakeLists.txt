add_library(fzgx_sim_core STATIC
  src/fzgx_camera.c
  src/fzgx_replay.c
  src/fzgx_sim.c
)

target_include_directories(fzgx_sim_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/content_c/include
)

target_link_libraries(fzgx_sim_core PUBLIC fzgx_content m)

add_executable(fzgx_sim_core_tests
  tests/test_fzgx_sim.c
)

target_link_libraries(fzgx_sim_core_tests PRIVATE fzgx_sim_core)
target_compile_definitions(fzgx_sim_core_tests PRIVATE FZGX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

add_executable(fzgx_camera_tests
  tests/test_fzgx_camera.c
)

target_link_libraries(fzgx_camera_tests PRIVATE fzgx_sim_core)

add_executable(fzgx_replay_repro
  tools/fzgx_replay_repro.cpp
)

target_link_libraries(fzgx_replay_repro PRIVATE fzgx_sim_core)
target_compile_definitions(fzgx_replay_repro PRIVATE FZGX_REPO_ROOT="${CMAKE_SOURCE_DIR}")

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
  add_test(NAME fzgx_sim_core_tests COMMAND fzgx_sim_core_tests)
  add_test(NAME fzgx_camera_tests COMMAND fzgx_camera_tests)
endif()
