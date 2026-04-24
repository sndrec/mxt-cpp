set(GODOT_CPP_BINDINGS_DIR "${CMAKE_SOURCE_DIR}/../godot-cpp")

if(NOT EXISTS "${GODOT_CPP_BINDINGS_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "godot-cpp not found at ${GODOT_CPP_BINDINGS_DIR}")
endif()

add_subdirectory("${GODOT_CPP_BINDINGS_DIR}" "${CMAKE_BINARY_DIR}/godot-cpp")

add_library(fzgx_godot_bridge SHARED
  src/fzgx_godot_bridge.cpp
)

target_include_directories(fzgx_godot_bridge PRIVATE
  ${CMAKE_SOURCE_DIR}/core_c/include
  ${CMAKE_SOURCE_DIR}/content_c/include
)

target_link_libraries(fzgx_godot_bridge PRIVATE
  fzgx_sim_core
  godot::cpp
)

set_target_properties(fzgx_godot_bridge PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/fzgx-port/bin"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/fzgx-port/bin"
  OUTPUT_NAME "fzgx_godot_bridge"
)

if(WIN32)
  function(fzgx_resolve_mingw_runtime_dll out_var dll_name)
    get_filename_component(MINGW_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    execute_process(
      COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${dll_name}
      OUTPUT_VARIABLE MINGW_PRINT_PATH
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(MINGW_DLL_CANDIDATES
      "${MINGW_TOOLCHAIN_BIN}/${dll_name}"
      "${MINGW_PRINT_PATH}"
    )

    foreach(runtime_candidate IN LISTS MINGW_DLL_CANDIDATES)
      if(EXISTS "${runtime_candidate}")
        set(${out_var} "${runtime_candidate}" PARENT_SCOPE)
        return()
      endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
  endfunction()

  fzgx_resolve_mingw_runtime_dll(MINGW_LIBSTDCPP_DLL "libstdc++-6.dll")
  fzgx_resolve_mingw_runtime_dll(MINGW_LIBGCC_DLL "libgcc_s_seh-1.dll")
  fzgx_resolve_mingw_runtime_dll(MINGW_WINPTHREAD_DLL "libwinpthread-1.dll")

  foreach(runtime_dll IN ITEMS
    "${MINGW_LIBSTDCPP_DLL}"
    "${MINGW_LIBGCC_DLL}"
    "${MINGW_WINPTHREAD_DLL}"
  )
    if(EXISTS "${runtime_dll}")
      add_custom_command(TARGET fzgx_godot_bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${runtime_dll}"
          "${CMAKE_SOURCE_DIR}/fzgx-port/bin"
      )
    endif()
  endforeach()
endif()
