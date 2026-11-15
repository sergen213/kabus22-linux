include_guard(GLOBAL)
# Xbox UWP Dev Mode executable (docs/XBOX.md). Configure inside `windows\dev.cmd --uwp` (store CRT libraries) with
# -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 and the x64-uwp vcpkg triplet; windows/package-uwp.ps1
# turns the build directory into a signed .msix with the game data.
add_executable(kabus22 WIN32 native/main.cpp native/scenario.cpp native/uwp_main.cpp)
target_compile_options(kabus22 PRIVATE ${K22_MSVC_OPTIONS})
target_link_libraries(kabus22 PRIVATE kabus22_core WindowsApp.lib)
target_link_options(kabus22 PRIVATE /APPCONTAINER /SUBSYSTEM:WINDOWS)
