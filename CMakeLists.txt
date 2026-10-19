cmake_minimum_required(VERSION 3.24)
project(hotd2_re LANGUAGES C CXX)

option(HOTD2_ALLOW_64BIT "Experimental: allow a 64-bit build (port / tests only, no hook; notes/agents/platform.md)" OFF)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT HOTD2_ALLOW_64BIT)
  message(FATAL_ERROR "hotd2-re must be built as 32-bit: cmake -A Win32 ...")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# SDL assertions may otherwise embed absolute build-machine source paths.
# Apply before FetchContent so the bundled SDL library gets the same settings.
# Keep function names and line numbers for diagnostics, without local filenames.
add_compile_definitions("$<$<CONFIG:Release>:SDL_FILE=\"\">"
                        "$<$<CONFIG:Release>:SDL_ASSERT_FILE=\"\">")

# SDL3: platform layer of the native port (window, events, clock, keyboard / mouse / touch, audio, SDL_GPU renderer).
# Default: FetchContent of the pinned official release tarball. Offline: -DFETCHCONTENT_SOURCE_DIR_SDL3=<SDL3 source dir>
# (an extracted SDL3-3.4.18 release), or -DHOTD2_SDL3_SYSTEM=ON to use an installed SDL3 (find_package CONFIG).
option(HOTD2_SDL3 "Build the SDL3 platform layer and the port tools that need it (play, scene_viewer, model_viewer)" ON)
option(HOTD2_SDL3_SYSTEM "Use an installed SDL3 (find_package) instead of fetching the pinned release" OFF)
option(HOTD2_SDL_GPU "Build the SDL_GPU renderer backend (play / viewers: --renderer sdlgpu)" ON)
if(HOTD2_SDL3)
  if(HOTD2_SDL3_SYSTEM)
    find_package(SDL3 REQUIRED CONFIG)
    if(TARGET SDL3::SDL3-static)
      add_library(hotd2_sdl3 ALIAS SDL3::SDL3-static)
    else()
      add_library(hotd2_sdl3 ALIAS SDL3::SDL3)
    endif()
  else()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
      URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3)
    add_library(hotd2_sdl3 ALIAS SDL3-static)
  endif()
endif()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
# Microsoft Detours, built from the submodule as a static lib (hook only).
set(DETOURS_SRC ${CMAKE_SOURCE_DIR}/src/hook/third_party/Detours/src)
add_library(detours STATIC
  ${DETOURS_SRC}/detours.cpp
  ${DETOURS_SRC}/modules.cpp
  ${DETOURS_SRC}/disasm.cpp
  ${DETOURS_SRC}/image.cpp
  ${DETOURS_SRC}/creatwth.cpp
  ${DETOURS_SRC}/disolx86.cpp
  ${DETOURS_SRC}/disolx64.cpp
  ${DETOURS_SRC}/disolia64.cpp
  ${DETOURS_SRC}/disolarm.cpp
  ${DETOURS_SRC}/disolarm64.cpp)
target_include_directories(detours PUBLIC ${DETOURS_SRC})
target_compile_definitions(detours PRIVATE WIN32_LEAN_AND_MEAN _X86_)
endif()

# Rewritten game code shared by the hook DLL and the port.
add_library(game STATIC src/game/prs.cpp src/game/texture.cpp src/game/exe_image.cpp src/game/palette_bank.cpp src/game/model.cpp src/game/anim.cpp src/game/camera.cpp src/game/event.cpp src/game/stage.cpp src/game/environment.cpp src/game/sprite.cpp src/game/shooting.cpp src/game/enemy.cpp src/game/rng.cpp src/game/effects.cpp src/game/task.cpp src/game/props.cpp src/game/event_runner.cpp src/game/skin.cpp src/game/shoot_camera.cpp src/game/mstack.cpp src/game/world.cpp src/game/world_enemy.cpp src/game/world_enemy_ai.cpp src/game/world_civilian.cpp src/game/world_player.cpp src/game/world_gameover.cpp src/game/world_actors.cpp src/game/world_collision.cpp src/game/world_scene1.cpp src/game/world_st2.cpp src/game/world_st2_actors.cpp src/game/world_st2_enemy.cpp src/game/world_st2_actor31.cpp src/game/world_st2_bats.cpp src/game/world_st2_komono.cpp src/game/world_st2_misc.cpp src/game/world_npc.cpp src/game/world_st1b.cpp src/game/world_routes.cpp src/game/world_plan.cpp src/game/world_routes_fish.cpp src/game/world_st1b_bats.cpp src/game/world_st1b_held.cpp src/game/world_st1b_civ.cpp src/game/world_st1b_boss.cpp src/game/world_enemy_beh.cpp src/game/menu_text.cpp)
target_sources(game PRIVATE src/game/world_st3.cpp src/game/world_st3_actors.cpp src/game/world_st3_enemy.cpp src/game/world_st3_fish.cpp src/game/world_st3_vehicle.cpp src/game/world_st3_birds.cpp)  # stage 3 (notes/agents/stage3.md)
target_sources(game PRIVATE src/game/world_st3_boss.cpp)  # stage-3 boss, continues (notes/agents/stage3b.md)
target_sources(game PRIVATE src/game/world_st4_enemy.cpp src/game/world_st4_birds.cpp src/game/world_st4_actors.cpp)  # stage 4 (notes/agents/stage4.md)
target_sources(game PRIVATE src/game/world_st5_actors.cpp src/game/world_st5_car.cpp src/game/world_st5_enemy.cpp src/game/world_st5_boss.cpp)  # stage 5 (notes/agents/stage5.md)
target_sources(game PRIVATE src/game/world_st4_boss.cpp src/game/world_st4_held.cpp src/game/world_st4_vehicle.cpp)  # stage-4 boss (notes/agents/stage4b.md)
target_sources(game PRIVATE src/game/world_st6.cpp src/game/world_st6_enemy.cpp src/game/world_st6_boss.cpp src/game/world_ending.cpp)  # stage 6, final boss, ending (notes/agents/stage6.md)
target_sources(game PRIVATE src/game/world_config_io.cpp)  # WorldConfig file: world_test --save-config / play --config (notes/agents/routes.md)
target_sources(game PRIVATE src/game/world_front.cpp src/game/world_front_tut.cpp)  # boot / title / attract / mode select (notes/agents/frontend.md)
target_sources(game PRIVATE src/game/world_light.cpp)  # env interpolator, light globals, light table (notes/agents/lighting.md)
target_sources(game PRIVATE src/game/world_st2c_enemy.cpp src/game/world_st2c_boss.cpp src/game/world_thrown.cpp)  # stage 2 continued (notes/agents/stage2c.md)
target_sources(game PRIVATE src/game/save_data.cpp src/game/world_modes_menu.cpp src/game/world_modes_rank.cpp src/game/world_modes_options.cpp src/game/world_modes_boss.cpp src/game/world_modes_end.cpp src/game/world_modes_train.cpp src/game/world_modes_orig.cpp src/game/world_modes_items.cpp src/game/world_modes_story.cpp src/game/world_modes_net.cpp src/game/world_modes_weapons.cpp src/game/world_st2_water.cpp)  # game types / menu modes (notes/agents/modes.md)
target_sources(game PRIVATE src/game/world_scene_select.cpp)  # port only: the mode select's Scene Select entry and page (WorldConfig::portFeatures)
target_sources(game PRIVATE src/game/world_horde.cpp)  # port only: the Horde mode (notes/agents/horde.md)
target_sources(game PRIVATE src/game/world_randomizer.cpp)
target_sources(game PRIVATE src/game/world_prototype.cpp)
add_executable(prototype_test tests/prototype_test.cpp)
target_link_libraries(prototype_test PRIVATE game)
target_sources(game PRIVATE src/game/stage_results.cpp src/game/world_stage_results.cpp)
target_include_directories(game PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Sound tables + channel logic (Snd_PlayGameSound / Snd_PlayFile).
add_library(game_sound STATIC src/game/sound_table.cpp src/game/sound_driver.cpp)
target_link_libraries(game_sound PUBLIC game)
add_executable(sound_test tests/sound_test.cpp)
target_link_libraries(sound_test PRIVATE game_sound)

add_executable(prs_test tests/prs_test.cpp)
target_link_libraries(prs_test PRIVATE game)

add_executable(tex_test tests/tex_test.cpp)
add_executable(bank_test tests/bank_test.cpp)
add_executable(model_test tests/model_test.cpp)
add_executable(anim_test tests/anim_test.cpp)
add_executable(cam_test tests/cam_test.cpp)
add_executable(evt_test tests/evt_test.cpp)
add_executable(stage_test tests/stage_test.cpp)
add_executable(env_test tests/env_test.cpp)
add_executable(sprite_test tests/sprite_test.cpp)
add_executable(pass1_test tests/pass1_test.cpp)
target_link_libraries(pass1_test PRIVATE game)
add_executable(enemy_test tests/enemy_test.cpp)
target_link_libraries(enemy_test PRIVATE game)
add_executable(effects_test tests/effects_test.cpp)
target_link_libraries(effects_test PRIVATE game)
add_executable(task_test tests/task_test.cpp)
add_executable(props_test tests/props_test.cpp)
target_link_libraries(task_test PRIVATE game)
target_link_libraries(props_test PRIVATE game)
add_executable(world_test tests/world_test.cpp)
target_link_libraries(world_test PRIVATE game game_hud)
# The game from boot (title, attract, mode select) vs boot captures (notes/agents/frontend.md)
add_executable(frontend_test tests/frontend_test.cpp)
target_link_libraries(frontend_test PRIVATE game game_hud)
add_executable(menu_pointer_test tests/menu_pointer_test.cpp)
target_link_libraries(menu_pointer_test PRIVATE game game_hud)
add_executable(options_pointer_test tests/options_pointer_test.cpp)
target_link_libraries(options_pointer_test PRIVATE game game_hud)
add_executable(selection_pointer_test tests/selection_pointer_test.cpp)
target_link_libraries(selection_pointer_test PRIVATE game game_hud)

add_executable(lobby_stage_test tests/lobby_stage_test.cpp)
target_link_libraries(lobby_stage_test PRIVATE game game_hud)
add_executable(network_room_test tests/network_room_test.cpp)
add_executable(randomizer_test tests/randomizer_test.cpp)
target_link_libraries(randomizer_test PRIVATE game game_hud game_sound)
add_executable(stage_results_test tests/stage_results_test.cpp)
target_link_libraries(stage_results_test PRIVATE game game_hud)
add_executable(credits_skip_test tests/credits_skip_test.cpp)
target_link_libraries(credits_skip_test PRIVATE game game_hud)
target_link_libraries(network_room_test PRIVATE game game_hud)
add_executable(event_runner_test tests/event_runner_test.cpp)
target_link_libraries(event_runner_test PRIVATE game)
target_link_libraries(sprite_test PRIVATE game)
add_executable(shooting_test tests/shooting_test.cpp)
target_link_libraries(shooting_test PRIVATE game)
target_link_libraries(env_test PRIVATE game)
target_link_libraries(stage_test PRIVATE game)
target_link_libraries(evt_test PRIVATE game)
target_link_libraries(cam_test PRIVATE game)
target_link_libraries(anim_test PRIVATE game)
target_link_libraries(model_test PRIVATE game)
target_link_libraries(bank_test PRIVATE game)
target_link_libraries(tex_test PRIVATE game)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  add_subdirectory(src/hook)  # the hook DLL for the original exe (Windows only)
endif()
add_subdirectory(src/port)

if(WIN32)
  add_executable(render_depth_test tests/render_depth_test.cpp)
  target_link_libraries(render_depth_test PRIVATE port_render port_gpu_d3d11)
  if(TARGET port_gpu_sdl)
    target_link_libraries(render_depth_test PRIVATE port_gpu_sdl)
    target_compile_definitions(render_depth_test PRIVATE HOTD2_TEST_SDL_GPU)
  endif()
endif()

if(WIN32 AND HOTD2_SDL3)
  add_executable(randomizer_scene_test tests/randomizer_scene_test.cpp)
  target_link_libraries(randomizer_scene_test PRIVATE game port_render port_gpu_d3d11)
  if(MSVC)
    target_link_options(randomizer_scene_test PRIVATE /LARGEADDRESSAWARE)
  endif()
endif()

# Port gameplay glue (device input -> gun records / trigger), pure; tests/gun_input_test.cpp.
add_library(port_play STATIC src/port/play/gun_input.cpp src/port/play/pause_menu.cpp src/port/play/display_menu.cpp src/port/play/wii_menu.cpp)
target_link_libraries(port_play PUBLIC game port_input_core)
add_executable(wii_remote_test tests/wii_remote_test.cpp)
target_link_libraries(wii_remote_test PRIVATE port_play)
add_executable(gun_input_test tests/gun_input_test.cpp)
target_link_libraries(gun_input_test PRIVATE port_play)
# play.exe's pause menu state machine, the display settings page and the settings file (src/port/play/pause_menu.h,
# display_menu.h).
add_executable(pause_menu_test tests/pause_menu_test.cpp)
target_link_libraries(pause_menu_test PRIVATE port_play)
# The port's PNG reader (asset pack) against Pillow's decode: tools/png_hashes.py writes the list.
add_executable(png_test tests/png_test.cpp)
target_link_libraries(png_test PRIVATE port_render)
target_include_directories(png_test PRIVATE src/port)
# Touch / mouse gun sources of the platform layer (platform-free logic); tests/touch_guns_test.cpp.
add_executable(touch_guns_test tests/touch_guns_test.cpp)
target_link_libraries(touch_guns_test PRIVATE port_input_core)
# The game image's placement in the window and the aim mapping over it (src/port/input/aim_map.h FitImage /
# WindowToImage); tests/display_map_test.cpp.
add_executable(display_map_test tests/display_map_test.cpp)
target_link_libraries(display_map_test PRIVATE port_input_core)

# Skinned attachments: every definition's vertex offsets must address vertex records.
add_executable(skin_test tests/skin_test.cpp)
target_link_libraries(skin_test PRIVATE port_render)
# Gameplay 2D layer (HUD, credits, continue, game over, subtitles); tests/hud_test.cpp.
add_library(game_hud STATIC src/game/hud.cpp)
target_link_libraries(game_hud PUBLIC game)
add_executable(hud_test tests/hud_test.cpp)
target_link_libraries(hud_test PRIVATE game_hud)

# play.exe's raw-input join logic with scripted reports (fake device handles), through the world front end.
if(WIN32)
  add_executable(play_join_test tests/play_join_test.cpp)
  target_link_libraries(play_join_test PRIVATE port_play port_input)
endif()
