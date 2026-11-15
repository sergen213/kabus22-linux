include_guard(GLOBAL)
# Windows build of the engine (MSVC, dependencies from vcpkg: windows/vcpkg.json).
#   Desktop (x64-windows):  Direct3D 12 and OpenGL backends, console executable.
#   Xbox UWP Dev Mode (CMAKE_SYSTEM_NAME=WindowsStore, x64-uwp): Direct3D 12 only, packaged by windows/uwp (docs/XBOX.md).
# Vulkan, hardware ray tracing and FSR stay Linux-only. The CTest suites are POSIX-only and are not built here.
if(NOT MSVC)
  message(FATAL_ERROR "The Windows build requires MSVC (Visual Studio 2022 or later)")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  # clang-cl also sets MSVC, but it has no 64-significand-bit long double, which the patched ODE contact merge requires
  # outside cl.exe (cmake/KabusODEContact.cmake #error).
  message(FATAL_ERROR "The Windows build requires the MSVC compiler cl.exe; ${CMAKE_CXX_COMPILER_ID} (clang-cl) is not supported")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
  set(K22_UWP ON)
else()
  set(K22_UWP OFF)
endif()
# CMake's MSVC RelWithDebInfo uses /Ob1 (only __inline functions); the engine's interpreter and frame code depend on full
# inlining, so RelWithDebInfo gets Release's /Ob2 and keeps its PDBs.
string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
string(REPLACE "/Ob1" "/Ob2" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
option(K22_D3D12 "Direct3D 12 renderer backend" ON)
option(K22_OPENGL "OpenGL renderer backend (desktop only)" ON)
if(K22_UWP)
  set(K22_OPENGL OFF CACHE BOOL "OpenGL renderer backend (desktop only)" FORCE)
  if(NOT K22_D3D12)
    message(FATAL_ERROR "The UWP build renders only with Direct3D 12 (K22_D3D12=ON)")
  endif()
endif()
if(NOT K22_D3D12 AND NOT K22_OPENGL)
  message(FATAL_ERROR "Enable at least one renderer backend (K22_D3D12 or K22_OPENGL)")
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(Threads REQUIRED)
include(cmake/KabusODE.cmake)
if(K22_UWP)
  # ODE's Windows dError/dDebug/dMessage call MessageBox, which UWP lacks (config.h forces WIN32 on).
  set_source_files_properties("${kabus_ode_SOURCE_DIR}/ode/src/error.cpp" TARGET_DIRECTORY ODE PROPERTIES
    COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/ode_uwp_error.h")
endif()

set(K22_BACKEND_SOURCES)
set(K22_BACKEND_DEFINITIONS)
set(K22_BACKEND_LIBRARIES)
if(K22_OPENGL)
  find_path(K22_EPOXY_INCLUDE_DIR epoxy/gl.h REQUIRED)
  find_library(K22_EPOXY_LIBRARY NAMES epoxy REQUIRED)
  list(APPEND K22_BACKEND_SOURCES native/render_gl.cpp)
  list(APPEND K22_BACKEND_DEFINITIONS K22_HAVE_OPENGL=1)
  # render_gl.cpp embeds the PBR/effects GLSL shared with Vulkan; re-run configure when they change.
  foreach(k22_glsl IN ITEMS pbr effects)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/native/shaders/${k22_glsl}.glsl" K22_GLSL_TEXT)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/native/shaders/${k22_glsl}.glsl")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/render_${k22_glsl}_glsl.hpp" CONTENT "#pragma once\ninline constexpr const char* ${k22_glsl}GLSL=R\"K22(@K22_GLSL_TEXT@)K22\";\n" @ONLY)
  endforeach()
  list(APPEND K22_BACKEND_LIBRARIES "${K22_EPOXY_LIBRARY}" opengl32)
endif()
if(K22_D3D12)
  list(APPEND K22_BACKEND_SOURCES native/render_d3d12.cpp)
  list(APPEND K22_BACKEND_DEFINITIONS K22_HAVE_D3D12=1)
  list(APPEND K22_BACKEND_LIBRARIES d3d12 dxgi dxguid)
endif()

add_library(kabus22_core STATIC ${K22_CORE_SOURCES} ${K22_BACKEND_SOURCES})
target_compile_definitions(kabus22_core PUBLIC ${K22_BACKEND_DEFINITIONS} NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
  "$<$<BOOL:${K22_UWP}>:K22_UWP=1;WINAPI_FAMILY=WINAPI_FAMILY_APP>")
target_include_directories(kabus22_core PUBLIC native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(kabus22_core SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf" ${FFMPEG_INCLUDE_DIRS})
if(K22_OPENGL)
  target_include_directories(kabus22_core SYSTEM PRIVATE "${K22_EPOXY_INCLUDE_DIR}")
endif()
target_link_libraries(kabus22_core PUBLIC SDL2::SDL2 Freetype::Freetype ${FFMPEG_LIBRARIES} Threads::Threads ODE dwrite ${K22_BACKEND_LIBRARIES})
target_link_directories(kabus22_core PUBLIC ${FFMPEG_LIBRARY_DIRS})
# /utf-8: sources hold UTF-8 literals; /bigobj: engine.cpp exceeds the default section count.
set(K22_MSVC_OPTIONS /utf-8 /bigobj /permissive- /Zc:__cplusplus /Zc:preprocessor /W3 /MP)
target_compile_options(kabus22_core PRIVATE ${K22_MSVC_OPTIONS})
if(K22_D3D12)
  # Build-time HLSL -> DXIL (dxc from the Windows SDK), embedded like the Vulkan SPIR-V.
  include(cmake/KabusD3D12Shaders.cmake)
endif()

if(K22_UWP)
  include(cmake/KabusUWP.cmake)
else()
  add_executable(kabus22 native/main.cpp native/scenario.cpp)
  target_compile_options(kabus22 PRIVATE ${K22_MSVC_OPTIONS})
  target_link_libraries(kabus22 PRIVATE kabus22_core $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>)
  # Portable folder (docs/WINDOWS.md): `cmake --install BUILD --prefix DIR --component Kabus22` is what
  # windows/package-windows.ps1 zips and windows/installer/kabus22.iss packs. Every rule below defaults to that component;
  # the pinned ODE subproject's own rules (headers, static library, CMake/pkg-config files) stay outside it and are never
  # shipped. vcpkg's applocal step copies the SDL2/FFmpeg/FreeType/libepoxy DLLs next to kabus22.exe in the build tree and,
  # by x_vcpkg_install_local_dependencies (the toolchain's install() override exists only when X_VCPKG_APPLOCAL_DEPS_INSTALL
  # is set before it loads), into the install tree. The MSVC runtime DLLs are deployed app-locally (the UCRT is part of
  # Windows 10 and later). No game data is ever installed.
  set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Kabus22)
  install(TARGETS kabus22 RUNTIME DESTINATION .)
  if(COMMAND x_vcpkg_install_local_dependencies)
    x_vcpkg_install_local_dependencies(TARGETS kabus22 DESTINATION .)
  endif()
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
  include(InstallRequiredSystemLibraries)
  install(FILES windows/Kabus22.cmd windows/import-data.ps1 windows/prepare-data.ps1 DESTINATION .)
  install(FILES docs/WINDOWS.md DESTINATION . RENAME README.md)
  install(FILES LICENSE DESTINATION licenses RENAME kabus22-LICENSE.txt)
  install(FILES third_party/ode-LICENSE-BSD.TXT third_party/ode-ou-LICENSE-BSD.TXT DESTINATION licenses)
  install(FILES third_party/cgltf/LICENSE DESTINATION licenses RENAME cgltf-LICENSE.txt)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    # Every vcpkg port's notice (share/<port>/copyright) and the exact port versions; the release also carries their sources.
    file(GLOB K22_VCPKG_NOTICES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
    foreach(notice IN LISTS K22_VCPKG_NOTICES)
      get_filename_component(port "${notice}" DIRECTORY)
      get_filename_component(port "${port}" NAME)
      install(FILES "${notice}" DESTINATION licenses/vcpkg RENAME "${port}.txt")
    endforeach()
    file(GLOB K22_VCPKG_LISTS "${VCPKG_INSTALLED_DIR}/vcpkg/info/*_${VCPKG_TARGET_TRIPLET}.list")
    set(K22_VCPKG_PORTS "vcpkg ports in this build (port version), baseline windows/vcpkg.json:\n")
    foreach(list IN LISTS K22_VCPKG_LISTS)
      get_filename_component(name "${list}" NAME)
      string(REGEX REPLACE "_${VCPKG_TARGET_TRIPLET}\\.list$" "" name "${name}")
      string(REGEX REPLACE "^([^_]+)_" "\\1 " name "${name}")
      string(APPEND K22_VCPKG_PORTS "${name}\n")
    endforeach()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vcpkg-ports.txt" "${K22_VCPKG_PORTS}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vcpkg-ports.txt" DESTINATION licenses)
  endif()
endif()
