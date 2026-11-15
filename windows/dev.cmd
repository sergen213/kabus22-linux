@echo off
rem Runs one command inside the x64 developer environment (cl, link, cmake, ninja, dxc) of the newest Visual Studio
rem with the C++ x64 tools that vswhere finds (2022 or later).
rem   windows\dev.cmd cmake ...          desktop libraries
rem   windows\dev.cmd --uwp cmake ...    UWP store CRT and WindowsApp libraries (Xbox build)
setlocal
set "K22_PLATFORM="
if /i "%~1"=="--uwp" (set "K22_PLATFORM=uwp" & shift)
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -all -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "K22_VS=%%i"
if not defined K22_VS (echo kabus22: Visual Studio with the C++ x64 tools not found 1>&2 & exit /b 1)
call "%K22_VS%\VC\Auxiliary\Build\vcvarsall.bat" x64 %K22_PLATFORM% >nul || exit /b 1
if defined K22_PLATFORM (set "K22_ARGS=%*" & call set "K22_ARGS=%%K22_ARGS:*--uwp =%%") else set "K22_ARGS=%*"
%K22_ARGS%
exit /b %ERRORLEVEL%
