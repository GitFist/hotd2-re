# Pure hook logic (no game memory access), shared with unit tests.
add_library(hook_logic STATIC gun_state.cpp)
target_include_directories(hook_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hook_logic PUBLIC port_input)

# hotd2_hook.dll - injected into the original game; swaps in rewritten functions.
add_library(hotd2_hook SHARED
  dllmain.cpp
  hooks.cpp
  input_hook.cpp
  auto_play.cpp
  display_hook.cpp
  model_check.cpp
  anim_check.cpp
  scene_capture.cpp
  log.cpp
  hotd2_hook.def)
target_link_libraries(hotd2_hook PRIVATE detours game hook_logic)
target_compile_definitions(hotd2_hook PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# hotd2_launch.exe - starts the game with hotd2_hook.dll loaded.
add_executable(hotd2_launch launcher.cpp)
target_link_libraries(hotd2_launch PRIVATE detours)
target_compile_definitions(hotd2_launch PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Unit tests for coordinate conversion and device/gun state.
add_executable(input_test_unit ${CMAKE_SOURCE_DIR}/tests/input_test_unit.cpp)
target_link_libraries(input_test_unit PRIVATE hook_logic)
