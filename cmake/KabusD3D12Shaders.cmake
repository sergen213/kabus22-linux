# Direct3D 12 shaders (native/shaders/d3d12/*.hlsl) -> DXIL with the Windows SDK dxc at build time, embedded as byte arrays in
# the generated header d3d12/render_d3d12_shaders.hpp (namespace k22::d3d12::dxil). No shader files are read at runtime.
# Included from cmake/KabusWindows.cmake when K22_D3D12=ON; run with `cmake -P` by its own custom command for the embedding.
if(CMAKE_SCRIPT_MODE_FILE)
  # cmake -DK22_DXIL_LIST=<file of "symbol|path" lines> -DK22_DXIL_OUTPUT=<header> -P KabusD3D12Shaders.cmake
  # '|' cannot occur in a Windows path; the path is everything after the first one (build trees may contain spaces).
  file(STRINGS "${K22_DXIL_LIST}" entries)
  set(text "#pragma once\n#include <cstddef>\nnamespace k22::d3d12::dxil {\n")
  foreach(entry IN LISTS entries)
    string(FIND "${entry}" "|" separator)
    string(SUBSTRING "${entry}" 0 ${separator} symbol)
    math(EXPR separator "${separator} + 1")
    string(SUBSTRING "${entry}" ${separator} -1 path)
    file(READ "${path}" hex HEX)
    string(LENGTH "${hex}" digits)
    if(digits LESS 40 OR NOT hex MATCHES "^44584243")
      message(FATAL_ERROR "invalid DXIL container: ${path}")
    endif()
    # dxc signs the container (non-zero hash after the magic) when dxil.dll sits beside it; unsigned DXIL does not load.
    string(SUBSTRING "${hex}" 8 32 digest)
    if(digest STREQUAL "00000000000000000000000000000000")
      message(FATAL_ERROR "unsigned DXIL (dxil.dll missing beside dxc): ${path}")
    endif()
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "((0x..,){32})" "\\1\n" bytes "${bytes}")
    string(APPEND text "inline constexpr unsigned char ${symbol}[] = {\n${bytes}\n};\n")
  endforeach()
  string(APPEND text "}\n")
  file(WRITE "${K22_DXIL_OUTPUT}.tmp" "${text}")
  file(COPY_FILE "${K22_DXIL_OUTPUT}.tmp" "${K22_DXIL_OUTPUT}" ONLY_IF_DIFFERENT)
  file(REMOVE "${K22_DXIL_OUTPUT}.tmp")
  return()
endif()

find_program(K22_DXC dxc REQUIRED DOC "DirectX shader compiler (Windows SDK bin directory)")
set(K22_D3D12_SHADER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/native/shaders/d3d12")
set(K22_D3D12_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/d3d12")
set(K22_D3D12_SHADER_INCLUDES "${K22_D3D12_SHADER_DIRECTORY}/common.hlsli" "${K22_D3D12_SHADER_DIRECTORY}/pbr.hlsli" "${K22_D3D12_SHADER_DIRECTORY}/effects.hlsli")
# symbol|file|entry|profile|defines (comma separated)
set(K22_D3D12_SHADERS
  "sceneVS|scene|sceneVS|vs_6_0|" "sceneShadowVS|scene|sceneVS|vs_6_0|K22_SHADOW_MAPS"
  "scenePS|scene|scenePS|ps_6_0|" "sceneShadowPS|scene|scenePS|ps_6_0|K22_SHADOW_MAPS" "sceneCasterPS|scene|sceneCasterPS|ps_6_0|K22_SHADOW_MAPS,K22_CASTER"
  "shadowVS|shadow|shadowVS|vs_6_0|" "shadowGS|shadow|shadowGS|gs_6_0|" "shadowPS|shadow|shadowPS|ps_6_0|"
  "uiVS|ui|uiVS|vs_6_0|" "uiPS|ui|uiPS|ps_6_0|"
  "postVS|post|postVS|vs_6_0|" "presentVS|post|presentVS|vs_6_0|"
  "postPS|post|postPS|ps_6_0|K22_POST" "warpPS|post|warpPS|ps_6_0|K22_WARP" "skyPS|post|skyPS|ps_6_0|K22_SKY" "cloudPS|post|cloudPS|ps_6_0|K22_CLOUD" "mipPS|post|mipPS|ps_6_0|K22_MIP"
  "tonemapPS|effect_passes|tonemapPS|ps_6_0|K22_TONEMAP" "linearizePS|effect_passes|linearizePS|ps_6_0|K22_LINEARIZE" "linearizeMSPS|effect_passes|linearizePS|ps_6_0|K22_LINEARIZE_MS"
  "ssaoPS|effect_passes|ssaoPS|ps_6_0|K22_SSAO" "aoApplyPS|effect_passes|aoApplyPS|ps_6_0|K22_AO_APPLY" "fogPS|effect_passes|fogPS|ps_6_0|K22_FOG"
  "fogUpsamplePS|effect_passes|fogUpsamplePS|ps_6_0|K22_FOG_UPSAMPLE" "bloomPS|effect_passes|bloomPS|ps_6_0|K22_BLOOM" "compositePS|effect_passes|compositePS|ps_6_0|K22_COMPOSITE"
  "fxaaPS|effect_passes|fxaaPS|ps_6_0|K22_FXAA")
set(K22_D3D12_DXIL_FILES)
set(K22_D3D12_DXIL_LIST "")
foreach(shader IN LISTS K22_D3D12_SHADERS)
  string(REPLACE "|" ";" fields "${shader}")
  list(GET fields 0 symbol)
  list(GET fields 1 file)
  list(GET fields 2 entry)
  list(GET fields 3 profile)
  list(GET fields 4 defines)
  set(defineArguments)
  if(defines)
    string(REPLACE "," ";" defines "${defines}")
    foreach(define IN LISTS defines)
      list(APPEND defineArguments "-D${define}=1")
    endforeach()
  endif()
  set(output "${K22_D3D12_SHADER_OUTPUT}/${symbol}.dxil")
  add_custom_command(OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${K22_D3D12_SHADER_OUTPUT}"
    COMMAND "${K22_DXC}" -nologo -HV 2021 -O3 -Qstrip_debug -Qstrip_reflect -WX -T ${profile} -E ${entry} ${defineArguments}
      -I "${K22_D3D12_SHADER_DIRECTORY}" -Fo "${output}" "${K22_D3D12_SHADER_DIRECTORY}/${file}.hlsl"
    DEPENDS "${K22_D3D12_SHADER_DIRECTORY}/${file}.hlsl" ${K22_D3D12_SHADER_INCLUDES}
    COMMENT "dxc ${symbol}" VERBATIM)
  list(APPEND K22_D3D12_DXIL_FILES "${output}")
  string(APPEND K22_D3D12_DXIL_LIST "${symbol}|${output}\n")
endforeach()
file(CONFIGURE OUTPUT "${K22_D3D12_SHADER_OUTPUT}/shaders.txt" CONTENT "${K22_D3D12_DXIL_LIST}")
set(K22_D3D12_SHADER_HEADER "${K22_D3D12_SHADER_OUTPUT}/render_d3d12_shaders.hpp")
add_custom_command(OUTPUT "${K22_D3D12_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" "-DK22_DXIL_LIST=${K22_D3D12_SHADER_OUTPUT}/shaders.txt" "-DK22_DXIL_OUTPUT=${K22_D3D12_SHADER_HEADER}" -P "${CMAKE_CURRENT_LIST_FILE}"
  DEPENDS ${K22_D3D12_DXIL_FILES} "${K22_D3D12_SHADER_OUTPUT}/shaders.txt" "${CMAKE_CURRENT_LIST_FILE}"
  COMMENT "Embedding Direct3D 12 DXIL" VERBATIM)
target_sources(kabus22_core PRIVATE "${K22_D3D12_SHADER_HEADER}")
target_include_directories(kabus22_core PRIVATE "${K22_D3D12_SHADER_OUTPUT}")
