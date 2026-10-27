# Native port. Layers (notes/agents/platform.md):
#   platform-free:  port_render (assets, ModelRenderer front end, scenes), port_input_core (aim mapping, mouse / touch gun
#                   sources), port_play (gun glue, top level)
#   backends:       port_gpu_d3d11 (Windows), port_gpu_sdl (SDL_GPU), port_audio (XAudio2, Windows), port_audio_sdl,
#                   port_input (Windows raw input; also used by the hook)
#   platform layer: port_platform (SDL3: window, events, clock, input sources, device / audio creation, PNG)

add_library(port_input_core STATIC input/aim_map.cpp input/aim_calibration.cpp input/lightguns.cpp input/pointer_guns.cpp input/touch_guns.cpp input/wii_remote.cpp)
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

# Optional startup release check, independent of SDL and deterministic game / net state.
add_library(port_update STATIC update/update_check.cpp update/update_install.cpp)
target_include_directories(port_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
if(WIN32)
  # Embed the helper so existing packaging picks it up with hotd2.exe. No extra
  # installed scripts or runtime dependencies beyond Windows PowerShell/.NET.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    update/update_transaction.ps1 update/update_runner.ps1)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/update/update_transaction.ps1" HOTD2_TRANSACTION_SCRIPT)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/update/update_runner.ps1" HOTD2_RUNNER_SCRIPT)
  # MSVC limits individual string tokens; adjacent per-line literals retain the
  # exact script bytes without requiring a separate distributed helper file.
  foreach(script HOTD2_TRANSACTION_SCRIPT HOTD2_RUNNER_SCRIPT)
    string(REPLACE "\n" ")HOTD2UPD\"\nR\"HOTD2UPD(\n" ${script} "${${script}}")
  endforeach()
  configure_file(update/update_scripts.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/update_scripts.h" @ONLY)
  target_include_directories(port_update PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(port_update PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_update PUBLIC winhttp shell32 ole32)
endif()
add_executable(update_check_test ${PROJECT_SOURCE_DIR}/tests/update_check_test.cpp)
target_link_libraries(update_check_test PRIVATE port_update)

# Online two-player: deterministic lockstep over UDP (net/netplay.h; notes/agents/netplay.md).
add_library(port_net STATIC net/netplay.cpp net/leaderboard.cpp)
target_include_directories(port_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(port_net PUBLIC game)
if(WIN32)
  target_compile_definitions(port_net PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_net PUBLIC ws2_32)
endif()

# Asset loading + the backend-neutral model / sprite renderer on top of the native decoders in src/game.
add_library(port_render STATIC render/asset_store.cpp render/model_renderer.cpp render/render_pose.cpp scene/scene.cpp render/front_screens.cpp
                               render/png_read.cpp render/ui_font.cpp)
target_include_directories(port_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(port_render PUBLIC game port_assets)

if(WIN32)
  target_link_libraries(port_render PUBLIC gdi32)
  add_library(port_gpu_d3d11 STATIC render/d3d11_device.cpp)
  target_compile_definitions(port_gpu_d3d11 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_gpu_d3d11 PUBLIC port_render)
  add_library(port_gpu_d3d12 STATIC render/d3d12_device.cpp)
  target_compile_definitions(port_gpu_d3d12 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(port_gpu_d3d12 PUBLIC port_render d3d12 dxgi d3dcompiler)
  target_sources(port_gpu_d3d12 PRIVATE render/dlss_super_resolution.cpp render/ngx_context.cpp render/dlss_frame_generation.cpp)
  target_sources(port_gpu_d3d12 PRIVATE render/d3d12_ray_scene.cpp)
  target_sources(port_gpu_d3d12 PRIVATE render/d3d12_final_frames.cpp)
  target_sources(port_gpu_d3d12 PRIVATE render/d3d12_gpu_timer.cpp)
  target_sources(port_gpu_d3d12 PRIVATE render/fsr_super_resolution.cpp)
  target_sources(port_gpu_d3d12 PRIVATE render/fsr_frame_generation.cpp)
  target_sources(port_gpu_d3d12 PRIVATE render/xess_super_resolution.cpp)
  set(HOTD2_XESS_SDK "" CACHE PATH "Optional external official Intel XeSS SDK checkout (x64)")
  if(HOTD2_XESS_SDK)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
      message(FATAL_ERROR "The optional Intel SDK requires a 64-bit build")
    endif()
    target_include_directories(port_gpu_d3d12 PRIVATE "${HOTD2_XESS_SDK}/inc")
    target_compile_definitions(port_gpu_d3d12 PRIVATE HOTD2_XESS_SDK)
    target_link_libraries(port_gpu_d3d12 PRIVATE wintrust crypt32)
  endif()
  set(HOTD2_FSR_SDK "" CACHE PATH "Optional external official AMD FidelityFX SDK checkout (x64)")
  if(HOTD2_FSR_SDK)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
      message(FATAL_ERROR "The optional AMD SDK requires a 64-bit build")
    endif()
    target_include_directories(port_gpu_d3d12 PRIVATE
      "${HOTD2_FSR_SDK}/Kits/FidelityFX/api/include" "${HOTD2_FSR_SDK}/Kits/FidelityFX/upscalers/include"
      "${HOTD2_FSR_SDK}/Kits/FidelityFX/framegeneration/include")
    target_compile_definitions(port_gpu_d3d12 PRIVATE HOTD2_FSR_SDK PLATFORM_WINDOWS)
    target_link_libraries(port_gpu_d3d12 PRIVATE wintrust crypt32)
  endif()
  set(HOTD2_DXC_SDK "" CACHE PATH "Optional external official Microsoft DXC SDK directory")
  if(HOTD2_DXC_SDK)
    if(NOT EXISTS "${HOTD2_DXC_SDK}/inc/dxcapi.h")
      message(FATAL_ERROR "HOTD2_DXC_SDK must contain inc/dxcapi.h")
    endif()
    target_include_directories(port_gpu_d3d12 PRIVATE "${HOTD2_DXC_SDK}/inc")
    target_compile_definitions(port_gpu_d3d12 PRIVATE HOTD2_DXC_SDK)
  endif()
  set(HOTD2_DLSS_SDK "" CACHE PATH "Optional external official NVIDIA DLSS SDK checkout (x64 only)")
  if(HOTD2_DLSS_SDK)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
      message(FATAL_ERROR "The optional DLSS SDK requires a 64-bit build")
    endif()
    if(NOT EXISTS "${HOTD2_DLSS_SDK}/include/nvsdk_ngx.h")
      message(FATAL_ERROR "HOTD2_DLSS_SDK must point to an official DLSS SDK checkout")
    endif()
    target_include_directories(port_gpu_d3d12 PRIVATE "${HOTD2_DLSS_SDK}/include")
    target_compile_definitions(port_gpu_d3d12 PRIVATE HOTD2_NGX_SDK)
    target_link_libraries(port_gpu_d3d12 PRIVATE
      "$<$<CONFIG:Debug>:${HOTD2_DLSS_SDK}/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib>"
      "$<$<NOT:$<CONFIG:Debug>>:${HOTD2_DLSS_SDK}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib>" wintrust crypt32)
  endif()
  add_executable(d3d12_device_test ${PROJECT_SOURCE_DIR}/tests/d3d12_device_test.cpp)
  target_link_libraries(d3d12_device_test PRIVATE port_gpu_d3d12 port_gpu_d3d11)
  add_executable(dlss_device_test ${PROJECT_SOURCE_DIR}/tests/dlss_device_test.cpp)
  target_link_libraries(dlss_device_test PRIVATE port_gpu_d3d12)
  add_executable(fsr_device_test ${PROJECT_SOURCE_DIR}/tests/fsr_device_test.cpp)
  target_link_libraries(fsr_device_test PRIVATE port_gpu_d3d12)
  add_executable(vendor_upscale_test ${PROJECT_SOURCE_DIR}/tests/vendor_upscale_test.cpp)
  target_link_libraries(vendor_upscale_test PRIVATE port_gpu_d3d12)
  add_executable(dlss_frame_generation_test ${PROJECT_SOURCE_DIR}/tests/dlss_frame_generation_test.cpp)
  target_link_libraries(dlss_frame_generation_test PRIVATE port_gpu_d3d12)
  add_executable(d3d12_raytracing_test ${PROJECT_SOURCE_DIR}/tests/d3d12_raytracing_test.cpp)
  target_link_libraries(d3d12_raytracing_test PRIVATE port_gpu_d3d12)
  add_executable(d3d12_ray_world_test ${PROJECT_SOURCE_DIR}/tests/d3d12_ray_world_test.cpp)
  target_link_libraries(d3d12_ray_world_test PRIVATE port_gpu_d3d12)
  add_executable(d3d12_final_frames_test ${PROJECT_SOURCE_DIR}/tests/d3d12_final_frames_test.cpp)
  target_link_libraries(d3d12_final_frames_test PRIVATE port_gpu_d3d12)
  add_executable(d3d12_gpu_timing_test ${PROJECT_SOURCE_DIR}/tests/d3d12_gpu_timing_test.cpp)
  target_link_libraries(d3d12_gpu_timing_test PRIVATE port_gpu_d3d12)
  add_executable(fsr_frame_generation_test ${PROJECT_SOURCE_DIR}/tests/fsr_frame_generation_test.cpp)
  target_link_libraries(fsr_frame_generation_test PRIVATE port_gpu_d3d12)
  add_executable(d3d12_window_frame_generation_test ${PROJECT_SOURCE_DIR}/tests/d3d12_window_frame_generation_test.cpp)
  target_link_libraries(d3d12_window_frame_generation_test PRIVATE port_gpu_d3d12)
  if(HOTD2_DXC_SDK)
    add_executable(d3d12_rayquery_test ${PROJECT_SOURCE_DIR}/tests/d3d12_rayquery_test.cpp)
    target_include_directories(d3d12_rayquery_test PRIVATE "${HOTD2_DXC_SDK}/inc")
    target_link_libraries(d3d12_rayquery_test PRIVATE port_gpu_d3d12)
  endif()

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
  add_executable(audio_loop_region_test ${CMAKE_SOURCE_DIR}/tests/audio_loop_region_test.cpp)
  target_link_libraries(audio_loop_region_test PRIVATE port_audio_sdl)

  add_library(port_platform STATIC platform/platform_sdl.cpp)
  target_include_directories(port_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(port_platform PUBLIC port_render port_input_core port_audio_sdl hotd2_sdl3)
  if(WIN32)
    target_compile_definitions(port_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(port_platform PUBLIC port_input port_gpu_d3d11 port_gpu_d3d12 port_audio)
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
  target_link_libraries(play PRIVATE port_platform game_hud port_play port_net port_update)
  if(WIN32)
    add_executable(lightgun_platform_test ${CMAKE_SOURCE_DIR}/tests/lightgun_platform_test.cpp)
    target_link_libraries(lightgun_platform_test PRIVATE port_platform)
    add_executable(startup_window_test ${CMAKE_SOURCE_DIR}/tests/startup_window_test.cpp)
    target_link_libraries(startup_window_test PRIVATE port_platform)
    add_executable(device_menu_render_test ${CMAKE_SOURCE_DIR}/tests/device_menu_render_test.cpp)
    target_link_libraries(device_menu_render_test PRIVATE port_platform port_play port_render)
    add_executable(scroll_hint_render_test ${CMAKE_SOURCE_DIR}/tests/scroll_hint_render_test.cpp)
    target_link_libraries(scroll_hint_render_test PRIVATE port_platform port_play port_render)
    add_executable(online_name_render_test ${CMAKE_SOURCE_DIR}/tests/online_name_render_test.cpp)
    target_link_libraries(online_name_render_test PRIVATE port_platform port_play port_render)
  endif()

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
    target_link_libraries(hotd2 PRIVATE port_platform game_hud port_play port_net port_update)
    target_compile_definitions(hotd2 PRIVATE PLAY_GUI=1)
    if(MSVC)
      # The GUI subsystem's own entry (WinMain in play.cpp forwards to main): a GUI exe entered through the console
      # CRT's mainCRTStartup trips Defender's heuristics. 4 GB address space as play.exe.
      target_link_options(hotd2 PRIVATE /LARGEADDRESSAWARE)
    endif()
  endif()
endif()

# DXC is an optional local SDK dependency. Keep its required licenses with local
# binaries; never add compiler binaries to the source repository.
if(WIN32 AND HOTD2_DXC_SDK)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(HOTD2_DXC_ARCH x64)
  else()
    set(HOTD2_DXC_ARCH x86)
  endif()
  foreach(dxc_target play hotd2 d3d12_device_test dlss_device_test dlss_frame_generation_test d3d12_raytracing_test d3d12_ray_world_test fsr_frame_generation_test d3d12_window_frame_generation_test)
    if(TARGET ${dxc_target})
      add_custom_command(TARGET ${dxc_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${HOTD2_DXC_SDK}/bin/${HOTD2_DXC_ARCH}/dxcompiler.dll"
          "${HOTD2_DXC_SDK}/bin/${HOTD2_DXC_ARCH}/dxil.dll" "$<TARGET_FILE_DIR:${dxc_target}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${dxc_target}>/dxc-licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${HOTD2_DXC_SDK}/LICENSE-MS.txt" "${HOTD2_DXC_SDK}/LICENSE-LLVM.txt" "${HOTD2_DXC_SDK}/LICENCE-MIT.txt"
          "$<TARGET_FILE_DIR:${dxc_target}>/dxc-licenses")
    endif()
  endforeach()
endif()

# Optional vendor runtime files stay outside Git. Local builds carry the exact
# SDK license/notice alongside copied binaries so the normal Options choices
# work without requiring developer-only absolute SDK paths.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  foreach(vendor_app play hotd2)
    if(TARGET ${vendor_app})
      if(HOTD2_DLSS_SDK)
        add_custom_command(TARGET ${vendor_app} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HOTD2_DLSS_SDK}/lib/Windows_x86_64/rel/nvngx_dlss.dll"
            "${HOTD2_DLSS_SDK}/lib/Windows_x86_64/rel/nvngx_dlssg.dll" "$<TARGET_FILE_DIR:${vendor_app}>"
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${vendor_app}>/vendor-licenses/nvidia"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HOTD2_DLSS_SDK}/LICENSE.txt"
            "$<TARGET_FILE_DIR:${vendor_app}>/vendor-licenses/nvidia/LICENSE.txt")
      endif()
      if(HOTD2_FSR_SDK)
        add_custom_command(TARGET ${vendor_app} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HOTD2_FSR_SDK}/Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_dx12.dll"
            "${HOTD2_FSR_SDK}/Kits/FidelityFX/signedbin/amd_fidelityfx_framegeneration_dx12.dll" "$<TARGET_FILE_DIR:${vendor_app}>"
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${vendor_app}>/vendor-licenses/amd"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HOTD2_FSR_SDK}/Kits/FidelityFX/docs/license.md" "${HOTD2_FSR_SDK}/3rdpartynotice.md"
            "$<TARGET_FILE_DIR:${vendor_app}>/vendor-licenses/amd")
      endif()
      if(HOTD2_XESS_SDK)
        add_custom_command(TARGET ${vendor_app} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HOTD2_XESS_SDK}/bin/libxess.dll" "$<TARGET_FILE_DIR:${vendor_app}>"
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${vendor_app}>/vendor-licenses/intel"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HOTD2_XESS_SDK}/LICENSE.txt" "${HOTD2_XESS_SDK}/third-party-programs.txt"
            "$<TARGET_FILE_DIR:${vendor_app}>/vendor-licenses/intel")
      endif()
    endif()
  endforeach()
endif()
