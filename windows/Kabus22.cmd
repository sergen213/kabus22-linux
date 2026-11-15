@echo off
rem Starts Kabus 22 with the game data in the "game" folder beside it (docs/WINDOWS.md); imports that data first if it is
rem missing. Extra arguments go to kabus22.exe (kabus22.exe --help lists them).
setlocal
if not exist "%~dp0game\K22.exe" powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0import-data.ps1"
if not exist "%~dp0game\K22.exe" exit /b 1
"%~dp0kabus22.exe" --data "%~dp0game" %*
if errorlevel 1 pause
