# Native port. Layers (notes/agents/platform.md):
#   platform-free:  port_render (assets, ModelRenderer front end, scenes), port_input_core (aim mapping, mouse / touch gun
#                   sources), port_play (gun glue, top level)
#   backends:       port_gpu_d3d11 (Windows), port_gpu_sdl (SDL_GPU), port_audio (XAudio2, Windows), port_audio_sdl,
#                   port_input (Windows raw input; also used by the hook)
#   platform layer: port_platform (SDL3: window, events, clock, input sources, device / audio creation, PNG)

add_library(port_input_core STATIC input/aim_map.cpp input/pointer_guns.cpp input/touch_guns.cpp input/wii_remote.cpp)
target_include_directories(port_input_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
  target_sources(port_input_core PRIVATE input/wii_remote_win.cpp)
  target_compile_definitions(port_input_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_input_core PUBLIC hid setupapi bthprops)
  add_library(port_input STATIC input/raw_input.cpp input/raw_input_guns.cpp)
  target_include_directories(port_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(port_input PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_input PUBLIC port_input_core)
endif()

# Optional asset pack lookup (HOTD2_ASSET_DIR; notes/agents/remaster.md), shared by the renderer and the audio backends.
add_library(port_assets STATIC assets/asset_override.cpp)
target_include_directories(port_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Online two-player: deterministic lockstep over UDP (net/netplay.h; notes/agents/netplay.md).
add_library(port_net STATIC net/netplay.cpp)
target_include_directories(port_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(port_net PUBLIC game)
if(WIN32)
  target_compile_definitions(port_net PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_net PUBLIC ws2_32)
endif()

# Asset loading + the backend-neutral model / sprite renderer on top of the native decoders in src/game.
add_library(port_render STATIC render/asset_store.cpp render/model_renderer.cpp scene/scene.cpp render/front_screens.cpp
                               render/png_read.cpp)
target_include_directories(port_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(port_render PUBLIC game port_assets)

if(WIN32)
  add_library(port_gpu_d3d11 STATIC render/d3d11_device.cpp)
  target_compile_definitions(port_gpu_d3d11 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_gpu_d3d11 PUBLIC port_render)

  # Sound output (XAudio2) for game::SoundDriver.
  add_library(port_audio STATIC audio/xaudio_backend.cpp)
  target_include_directories(port_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(port_audio PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_audio PUBLIC game_sound port_assets xaudio2 ole32)

  # input_test.exe - two-player crosshair test for mice / light guns (Win32 raw input, no SDL).
  add_executable(input_test WIN32 tools/input_test.cpp)
  target_link_libraries(input_test PRIVATE port_input)
endif()

if(HOTD2_SDL3)
  add_library(port_audio_sdl STATIC audio/sdl_audio_backend.cpp)
  target_include_directories(port_audio_sdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(port_audio_sdl PUBLIC game_sound port_assets hotd2_sdl3)
  # Channel semantics of the SDL sound backend on SDL's disk audio driver (tests/sdl_audio_test.cpp).
  add_executable(sdl_audio_test ${CMAKE_SOURCE_DIR}/tests/sdl_audio_test.cpp)
  target_link_libraries(sdl_audio_test PRIVATE port_audio_sdl)

  add_library(port_platform STATIC platform/platform_sdl.cpp)
  target_include_directories(port_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(port_platform PUBLIC port_render port_input_core port_audio_sdl hotd2_sdl3)
  if(WIN32)
    target_compile_definitions(port_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(port_platform PUBLIC port_input port_gpu_d3d11 port_audio)
  endif()
  if(HOTD2_SDL_GPU)
    add_library(port_gpu_sdl STATIC render/sdl_gpu_device.cpp)
    target_link_libraries(port_gpu_sdl PUBLIC port_render hotd2_sdl3)
    if(WIN32)
      target_compile_definitions(port_gpu_sdl PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    target_compile_definitions(port_platform PRIVATE HOTD2_HAVE_SDL_GPU)
    target_link_libraries(port_platform PUBLIC port_gpu_sdl)
  endif()

  # snd_play.exe - plays sound ids through the ported sound layer.
  add_executable(snd_play tools/snd_play.cpp)
  target_link_libraries(snd_play PRIVATE port_platform)

  # model_viewer.exe - renders pol models with textures (interactive or --screenshot).
  add_executable(model_viewer tools/model_viewer.cpp)
  target_link_libraries(model_viewer PRIVATE port_platform)

  # scene_viewer.exe - plays a scene (scenes/*.scene) with the game's camera paths (interactive or --screenshot).
  add_executable(scene_viewer tools/scene_viewer.cpp)
  target_link_libraries(scene_viewer PRIVATE port_platform)

  # play.exe - playable game on the native world (game mode 6): gun input, gameplay HUD, sound.
  add_executable(play tools/play.cpp)
  target_link_libraries(play PRIVATE port_platform game_hud port_play port_net)
  if(MSVC)
    # 32-bit: 4 GB of address space on 64-bit Windows (the asset pack's decoded textures are up to 16 MB each)
    target_link_options(play PRIVATE /LARGEADDRESSAWARE)
  endif()

  # hotd2.exe - the same program as a Windows GUI app (no console window when double-clicked). play.exe stays a console
  # program for scripts and headless runs (callers wait for it and read its output).
  if(WIN32)
    # Icon: the original's icon group from the local game\Hod2.exe when present, else our own crosshair
    # (tools/make_icon.py); written to the build folder only, never committed.
    set(HOTD2_ICON "${CMAKE_CURRENT_BINARY_DIR}/hotd2.ico")
    set(HOTD2_RC "${CMAKE_CURRENT_BINARY_DIR}/hotd2.rc")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    set(HOTD2_RC_SOURCES)
    if(Python3_Interpreter_FOUND)
      execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/make_icon.py" "${HOTD2_ICON}"
                              --from-exe "${PROJECT_SOURCE_DIR}/game/Hod2.exe"
                      RESULT_VARIABLE HOTD2_ICON_RESULT)
      if(HOTD2_ICON_RESULT EQUAL 0 AND EXISTS "${HOTD2_ICON}")
        file(WRITE "${HOTD2_RC}" "1 ICON \"${HOTD2_ICON}\"\n")
        set(HOTD2_RC_SOURCES "${HOTD2_RC}")
      endif()
    endif()
    add_executable(hotd2 WIN32 tools/play.cpp ${HOTD2_RC_SOURCES})
    target_link_libraries(hotd2 PRIVATE port_platform game_hud port_play port_net)
    target_compile_definitions(hotd2 PRIVATE PLAY_GUI=1)
    if(MSVC)
      # The GUI subsystem's own entry (WinMain in play.cpp forwards to main): a GUI exe entered through the console
      # CRT's mainCRTStartup trips Defender's heuristics. 4 GB address space as play.exe.
      target_link_options(hotd2 PRIVATE /LARGEADDRESSAWARE)
    endif()
  endif()
endif()
