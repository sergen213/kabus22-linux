include_guard(GLOBAL)
include(FetchContent)

# Official odedevs/ode 0.16.6, commit a5fc0bc282703ab34d7d3d2500bafae7428eeaa9.
# Acquired from https://bitbucket.org/odedevs/ode/get/a5fc0bc282703ab34d7d3d2500bafae7428eeaa9.tar.gz
# Select the BSD license; ship third_party/ode-LICENSE-BSD.TXT with binaries.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(ODE_DOUBLE_PRECISION OFF CACHE BOOL "Use float32 ODE" FORCE)
set(ODE_WITH_DEMOS OFF CACHE BOOL "Disable ODE demos" FORCE)
set(ODE_WITH_TESTS OFF CACHE BOOL "Disable upstream ODE tests" FORCE)
set(ODE_WITH_OPCODE ON CACHE BOOL "Enable OPCODE trimesh collisions" FORCE)
set(ODE_NO_TRIMESH OFF CACHE BOOL "Keep trimesh collision support" FORCE)
set(ODE_WITH_GIMPACT OFF CACHE BOOL "Use OPCODE instead of GIMPACT" FORCE)
set(ODE_WITH_LIBCCD OFF CACHE BOOL "Disable unused libccd colliders" FORCE)

# Keep the pinned upstream's 2.8.12 minimum usable with CMake 4.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM OR CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS 3.5)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(kabus_ode
    URL "${CMAKE_CURRENT_LIST_DIR}/../third_party/ode-0.16.6.tar.gz"
    URL_HASH SHA256=05c0a0ea84c58c213d05e220adbc6559c473227505d5668a51493eed21b6c2f8
    # Populate first; the recovered query boundary must be applied on every
    # configure, even when FetchContent reuses an already populated source tree.
    SOURCE_SUBDIR kabus-deferred-contact-patch
)
FetchContent_MakeAvailable(kabus_ode)
include("${CMAKE_CURRENT_LIST_DIR}/KabusODEContact.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/KabusODELcp.cmake")
add_subdirectory("${kabus_ode_SOURCE_DIR}" "${kabus_ode_BINARY_DIR}")
# Source-local options preserve live PC64 operation order without changing the
# rest of ODE or the engine's compiler/solver defaults.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "Kabus ODE contact precision requires GCC or Clang")
endif()
set_source_files_properties(
    "${kabus_ode_SOURCE_DIR}/ode/src/collision_trimesh_box.cpp"
    TARGET_DIRECTORY ODE
    PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off"
)
# Upstream target ODE exports its generated/source includes and dIDESINGLE PUBLIC.
# Consumers exposing <ode/ode.h> in their headers must link ODE PUBLIC.
