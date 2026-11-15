// Forced include for ODE's error.cpp in the UWP build (cmake/KabusWindows.cmake): UWP has no MessageBox, so ODE's
// Windows dError/dDebug/dMessage report to stderr, which the Xbox entry redirects to LocalState/kabus22.log.
#pragma once
#include <windows.h>
#include <cstdio>
#undef MessageBox
#define MessageBox(owner, text, title, flags) (std::fprintf(stderr, "%s: %s\n", (title), (text)), 0)
