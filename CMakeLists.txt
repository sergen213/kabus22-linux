cmake_minimum_required(VERSION 3.20)
project(kabus22_native VERSION 0.4.6 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(K22_VERSION="${PROJECT_VERSION}")
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(K22_GLSLANG_VALIDATOR glslangValidator REQUIRED)
# Shader bytecode is embedded into the ELF, never compiled or located at runtime.
set(K22_SHADER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/native/shaders")
set(K22_SHADER_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(K22_SHADER_INCLUDES
  "${K22_SHADER_DIRECTORY}/raster_uniform.glsl"
  "${K22_SHADER_DIRECTORY}/ray_query.glsl"
  "${K22_SHADER_DIRECTORY}/pbr.glsl"
  "${K22_SHADER_DIRECTORY}/effects.glsl")
set(K22_SHADER_FILES scene.vert scene.frag shadow.vert shadow.geom shadow.frag
  ui.vert ui.frag post.vert post.frag sky.frag warp.frag cloud.frag deform.comp tonemap.frag
  effects.frag rt_signals.comp rt_filter.comp scene.tesc scene.tese)
# OpenGL embeds the same PBR source text as Vulkan; re-run configure when it changes.
file(READ "${K22_SHADER_DIRECTORY}/pbr.glsl" K22_PBR_GLSL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${K22_SHADER_DIRECTORY}/pbr.glsl")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/render_pbr_glsl.hpp" CONTENT "#pragma once\ninline constexpr const char* pbrGLSL=R\"K22(@K22_PBR_GLSL@)K22\";\n" @ONLY)
file(READ "${K22_SHADER_DIRECTORY}/effects.glsl" K22_EFFECTS_GLSL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${K22_SHADER_DIRECTORY}/effects.glsl")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/render_effects_glsl.hpp" CONTENT "#pragma once\ninline constexpr const char* effectsGLSL=R\"K22(@K22_EFFECTS_GLSL@)K22\";\n" @ONLY)
set(K22_SHADER_OUTPUTS)
set(K22_SHADER_EMBED_ARGUMENTS)
foreach(shader IN LISTS K22_SHADER_FILES)
  string(REPLACE "." "_" symbol "${shader}")
  set(output "${K22_SHADER_OUTPUT_DIRECTORY}/${shader}.spv")
  add_custom_command(OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${K22_SHADER_OUTPUT_DIRECTORY}"
    COMMAND "${K22_GLSLANG_VALIDATOR}" --target-env vulkan1.2 -V
      "-I${K22_SHADER_DIRECTORY}" "${K22_SHADER_DIRECTORY}/${shader}" -o "${output}"
    DEPENDS "${K22_SHADER_DIRECTORY}/${shader}" ${K22_SHADER_INCLUDES}
    VERBATIM)
  list(APPEND K22_SHADER_OUTPUTS "${output}")
  list(APPEND K22_SHADER_EMBED_ARGUMENTS "${symbol}" "${output}")
endforeach()
set(K22_RT_SHADER "${K22_SHADER_OUTPUT_DIRECTORY}/scene.rt.frag.spv")
add_custom_command(OUTPUT "${K22_RT_SHADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${K22_SHADER_OUTPUT_DIRECTORY}"
  COMMAND "${K22_GLSLANG_VALIDATOR}" --target-env vulkan1.2 -V
    "-I${K22_SHADER_DIRECTORY}" -DK22_RAY_QUERY=1 "${K22_SHADER_DIRECTORY}/scene.frag" -o "${K22_RT_SHADER}"
  DEPENDS "${K22_SHADER_DIRECTORY}/scene.frag" ${K22_SHADER_INCLUDES}
  VERBATIM)
list(APPEND K22_SHADER_OUTPUTS "${K22_RT_SHADER}")
list(APPEND K22_SHADER_EMBED_ARGUMENTS scene_rt_frag "${K22_RT_SHADER}")
set(K22_SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/render_vulkan_spirv.hpp")
add_custom_command(OUTPUT "${K22_SHADER_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/native/embed_spirv.py"
    "${K22_SHADER_HEADER}" ${K22_SHADER_EMBED_ARGUMENTS}
  DEPENDS ${K22_SHADER_OUTPUTS} "${CMAKE_CURRENT_SOURCE_DIR}/native/embed_spirv.py"
  VERBATIM)
include(cmake/KabusODE.cmake)
# AMD FidelityFX SDK v1.1.4 / FSR 3.1.4 upscaler + Vulkan backend (MIT; third_party/FidelityFX-SDK/KABUS-PROVENANCE.txt).
# tools/k22_fsr3_blobs.py replaces the SDK's Windows-only FidelityFX_SC: SPIR-V + descriptor reflection.
set(K22_FFX "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FidelityFX-SDK")
set(K22_FFX_BLOBS "${CMAKE_CURRENT_BINARY_DIR}/k22_fsr3_blobs.h")
file(GLOB_RECURSE K22_FFX_GPU CONFIGURE_DEPENDS "${K22_FFX}/sdk/include/FidelityFX/gpu/*.h" "${K22_FFX}/sdk/src/backends/vk/shaders/*.glsl")
add_custom_command(OUTPUT "${K22_FFX_BLOBS}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/k22_fsr3_blobs.py" --glslang "${K22_GLSLANG_VALIDATOR}"
    --sdk "${K22_FFX}" --output "${K22_FFX_BLOBS}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/k22_fsr3_blobs.py" ${K22_FFX_GPU}
  VERBATIM)
set(K22_FFX_SOURCES "${K22_FFX}/sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp" "${K22_FFX}/sdk/src/backends/vk/ffx_vk.cpp"
  "${K22_FFX}/sdk/src/shared/ffx_assert.cpp" "${K22_FFX}/sdk/src/shared/ffx_message.cpp"
  "${K22_FFX}/sdk/src/shared/ffx_object_management.cpp" "${K22_FFX}/sdk/src/shared/ffx_breadcrumbs_list.cpp")
add_library(kabus22_fsr3 STATIC ${K22_FFX_SOURCES} native/fsr_vk.cpp "${K22_FFX_BLOBS}")
set_source_files_properties(${K22_FFX_SOURCES} PROPERTIES COMPILE_OPTIONS "-w") # upstream sources (local patches: KABUS-PROVENANCE.txt)
target_include_directories(kabus22_fsr3 SYSTEM PRIVATE "${K22_FFX}/sdk/include" "${K22_FFX}/sdk/src/shared"
  "${K22_FFX}/sdk/src/backends/shared" "${K22_FFX}/sdk/src/components" "${CMAKE_CURRENT_BINARY_DIR}" ${Vulkan_INCLUDE_DIRS})
target_include_directories(kabus22_fsr3 PRIVATE native)
target_compile_options(kabus22_fsr3 PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/k22-ffx-posix/k22_ffx_posix.h")
install(FILES "${K22_FFX}/LICENSE.txt" DESTINATION share/doc/kabus22/third_party RENAME FidelityFX-SDK-LICENSE.txt)
add_library(kabus22_core native/engine.cpp native/wdl.cpp native/assets.cpp native/render.cpp
  native/render_gl.cpp native/render_vulkan.cpp native/render_vulkan_raster.cpp native/render_vulkan_rt.cpp native/vulkan_loader.cpp
  native/media.cpp native/physics.cpp native/texture_pack.cpp native/controller.cpp native/saves.cpp native/console.cpp native/seamless_doors.cpp
  native/character.cpp native/coop_menu.cpp native/coop.cpp "${K22_SHADER_HEADER}")
target_include_directories(kabus22_core PUBLIC native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" ${Vulkan_INCLUDE_DIRS})
target_link_libraries(kabus22_core PUBLIC PkgConfig::SDL2 PkgConfig::EPOXY PkgConfig::AV PkgConfig::FREETYPE PkgConfig::FONTCONFIG Threads::Threads ODE ${CMAKE_DL_LIBS} PRIVATE kabus22_fsr3)
target_compile_options(kabus22_core PRIVATE -Wall -Wextra -Wpedantic)
add_executable(kabus22 native/main.cpp native/scenario.cpp)
target_link_libraries(kabus22 PRIVATE kabus22_core)
install(TARGETS kabus22 RUNTIME DESTINATION bin)
include(CTest)
if(BUILD_TESTING)
  add_executable(wdl_regression native/wdl_regression.cpp)
  target_link_libraries(wdl_regression PRIVATE kabus22_core)
  add_test(NAME wdl_semantics COMMAND wdl_regression)
  add_executable(check_assets native/check_assets.cpp)
  target_link_libraries(check_assets PRIVATE kabus22_core)
  add_test(NAME asset_boundaries COMMAND check_assets)
  add_executable(check_media native/media.cpp native/assets.cpp)
  target_include_directories(check_media PRIVATE native)
  target_compile_definitions(check_media PRIVATE K22_MEDIA_SELF_CHECK=1)
  target_link_libraries(check_media PRIVATE PkgConfig::SDL2 PkgConfig::AV Threads::Threads)
  set(K22_TEST_MEDIA "${CMAKE_SOURCE_DIR}/reverse/decompiled/runtime/campaign-media/riff-vorbis/v_tacikos_kapi-wrapped.bin")
  set(K22_TEST_MATERIALS "${CMAKE_SOURCE_DIR}/reverse/media/iso/mode/samples/K22_mode.wrs")
  set(K22_TEST_TERRAIN "${CMAKE_SOURCE_DIR}/reverse/extracted/assets/yr.hmp")
  if(EXISTS "${K22_TEST_MEDIA}")
    add_test(NAME media_boundaries COMMAND check_media "${K22_TEST_MEDIA}")
  else()
    add_test(NAME media_boundaries COMMAND check_media)
    message(STATUS "Private wrapped media unavailable; media_boundaries uses asset-free coverage")
  endif()
  add_executable(check_engine native/check_engine.cpp)
  target_link_libraries(check_engine PRIVATE kabus22_core)
  add_test(NAME input_saves COMMAND check_engine --input-saves)
  if(EXISTS "${K22_TEST_MATERIALS}/Normal_Mix.fx" AND EXISTS "${K22_TEST_MATERIALS}/Normal_SunMix.fx" AND EXISTS "${K22_TEST_TERRAIN}")
    add_test(NAME engine_boundaries COMMAND check_engine "${K22_TEST_MATERIALS}" "${K22_TEST_TERRAIN}")
  else()
    message(STATUS "Skipping engine_boundaries: private shader or terrain fixtures unavailable")
  endif()
  add_executable(k22-render-check native/check_render.cpp)
  target_link_libraries(k22-render-check PRIVATE kabus22_core)
  # Asset-free: presets, --graphics/--texture-pack parsing, Contract E packs, tangents, tone curve.
  add_test(NAME graphics_policy COMMAND k22-render-check --policy)
  if(EXISTS "${K22_TEST_MATERIALS}/Normal_Mix.fx" AND EXISTS "${K22_TEST_MATERIALS}/Normal_SunMix.fx")
    add_test(NAME material_boundaries COMMAND k22-render-check "${K22_TEST_MATERIALS}")
  else()
    message(STATUS "Skipping material_boundaries: private shader fixtures unavailable")
  endif()
  add_executable(check_physics native/check_physics.cpp)
  target_link_libraries(check_physics PRIVATE kabus22_core)
  if(EXISTS "${K22_TEST_TERRAIN}")
    add_test(NAME physics_boundaries COMMAND check_physics "${K22_TEST_TERRAIN}")
  else()
    message(STATUS "Skipping physics_boundaries: private terrain fixture unavailable")
  endif()
  # Real-data gameplay scenarios (docs/GAMEPLAY_TESTING.md); registered only when an installed game directory is supplied.
  set(K22_GAME_DATA "" CACHE PATH "Original game directory for the gameplay_scenarios test (empty: not registered)")
  set(K22_ORIGINAL_SAVES "" CACHE PATH "Original Windows game directory with kayit*.sav/kysd.sav for save-based scenarios")
  if(K22_GAME_DATA)
    add_test(NAME gameplay_scenarios COMMAND "${CMAKE_COMMAND}" "-DK22=$<TARGET_FILE:kabus22>" "-DGAME=${K22_GAME_DATA}" "-DSAVES=${K22_ORIGINAL_SAVES}"
      "-DSCENARIOS=${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gameplay-scenarios" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios/run.cmake")
    set_tests_properties(gameplay_scenarios PROPERTIES TIMEOUT 7200)
    # Every campaign level (tests/levels/levels.tsv) entered through the original LevelYukle path, 600 headless frames each (~5 min).
    if(K22_ORIGINAL_SAVES)
      add_test(NAME level_sweep COMMAND "${CMAKE_COMMAND}" "-DK22=$<TARGET_FILE:kabus22>" "-DGAME=${K22_GAME_DATA}" "-DSAVES=${K22_ORIGINAL_SAVES}"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/level-sweep" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/levels/sweep.cmake")
      set_tests_properties(level_sweep PROPERTIES TIMEOUT 1200)
      # The same sweep with each character lock (docs/CHARACTER_LOCK.md): every level's player is the locked character.
      foreach(character demir ebru inzar)
        add_test(NAME level_sweep_${character} COMMAND "${CMAKE_COMMAND}" "-DK22=$<TARGET_FILE:kabus22>" "-DGAME=${K22_GAME_DATA}" "-DSAVES=${K22_ORIGINAL_SAVES}"
          "-DCHARACTER=${character}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/level-sweep-${character}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/levels/sweep.cmake")
        set_tests_properties(level_sweep_${character} PROPERTIES TIMEOUT 1200)
      endforeach()
    endif()
    # Native captures of tests/reference/*.k22s against the original's captures (tools/wine-reference/, docs/GAMEPLAY_TESTING.md#reference-comparison).
    # Renders with OpenGL in a window, so it needs DISPLAY; thresholds in tests/reference/thresholds.tsv.
    set(K22_REFERENCE_DIR "" CACHE PATH "Reference captures of the original game (wineref.py output) for reference_comparison (empty: not registered)")
    if(K22_REFERENCE_DIR AND K22_ORIGINAL_SAVES)
      add_test(NAME reference_comparison COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/wine-reference/compare.py"
        "${K22_REFERENCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/reference-comparison" --thresholds "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference/thresholds.tsv"
        --pairs "${CMAKE_CURRENT_BINARY_DIR}/reference-comparison/pairs" --ref-gamma 30
        --run "$<TARGET_FILE:kabus22>" "${K22_GAME_DATA}" "${K22_ORIGINAL_SAVES}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference")
      set_tests_properties(reference_comparison PROPERTIES TIMEOUT 3600)
    endif()
  endif()
  find_program(K22_UPSCALER_PYTHON python3)  # the tool runs as `python3`; may differ from the shader-embedding interpreter
  execute_process(COMMAND "${K22_UPSCALER_PYTHON}" -c "import PIL, numpy" RESULT_VARIABLE K22_UPSCALER_DEPS OUTPUT_QUIET ERROR_QUIET)
  if(K22_UPSCALER_DEPS EQUAL 0)
    add_test(NAME texture_upscaler COMMAND "${K22_UPSCALER_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/texture-upscaler/test_upscale.py")
  else()
    message(STATUS "Skipping texture_upscaler: Python Pillow/numpy unavailable")
  endif()
endif()
