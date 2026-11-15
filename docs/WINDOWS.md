# Kabus22 on Windows

The native Kabus22 engine also runs on 64-bit Windows 10 and 11, rendering with Direct3D 12 (default) or OpenGL. As on Linux, no game data is included: you need your own copy of Kabus 22 (the ISO image, the original `setup.exe` with its `setup-1.bin`, the disc, or an installed Windows copy of the game). The Windows original's `K22.exe` is only read as data for its font; it is never run.

## Requirements

- Windows 10 or 11, x64.
- A GPU with a Direct3D 12 driver (feature level 11_0), or desktop OpenGL 3.3 for `--renderer opengl`.
- About 2.5 GB of disk space for the extracted game data.
- An internet connection the first time game data is read from an ISO or setup `.exe`: the extraction downloads the innoextract 1.9 tool once (SHA-256 pinned). An installed game folder needs no download.

## Download

Each [release](https://github.com/sergen213/kabus22-linux/releases) carries:

| File | Content |
| --- | --- |
| `Kabus22-<version>-Windows-Setup-x64.exe` | Setup: installs for your user, no administrator rights needed. Recommended. |
| `kabus22-<version>-windows-x64.zip` | The same files as a portable folder. |
| `kabus22-<version>-windows-x64-sources.zip` | Sources of the bundled third-party libraries (FFmpeg, SDL2, FreeType, libepoxy and their dependencies) with the vcpkg recipes that built them. |

Check a download against the release's `SHA256SUMS` in PowerShell:

```
Get-FileHash -Algorithm SHA256 .\Kabus22-0.4.18-Windows-Setup-x64.exe
```

The files are not code-signed, so Windows SmartScreen may show "Windows protected your PC" when you start the setup or the game. Choose **More info**, then **Run anyway**, after checking the hash.

## Installing with the setup

1. Run `Kabus22-<version>-Windows-Setup-x64.exe`. It installs to `%LOCALAPPDATA%\Programs\Kabus22` by default.
2. On the **Game data** page choose where the game files come from:
   - the ISO image or the original setup `.exe` (its `setup-1.bin` must be in the same folder),
   - an installed Kabus 22 folder (the one holding `K22.exe`) or the Kabus 22 disc, or
   - **Later**: install without game data and import it afterwards with **Import Kabus 22 game data** in the Start menu.
3. Setup copies the game files (about 2.2 GB) into the `game` folder of the installation before it installs anything else. If the copy fails, it shows the reason and you can go back and choose another source.

Start the game from **Kabus 22** in the Start menu (or the optional desktop shortcut). A console window opens beside the game with the engine's log; if the engine stops with an error, that window stays open so you can read it.

If Windows refuses to mount the ISO, double-click the ISO in Explorer (this mounts it as a drive) and choose that drive as the folder source.

Updating: run the setup of the newer version; it installs over the old one in the same folder and can keep the game data already installed. There is no launcher or automatic updater on Windows.

Uninstalling (Settings → Apps, or the uninstaller in the installation folder) removes the program and its `game` folder. Your saves and settings are never removed.

## Portable zip

1. Extract `kabus22-<version>-windows-x64.zip` anywhere you can write, for example `D:\Games\Kabus22`.
2. Double-click `Kabus22.cmd`. The first time it asks for your ISO, setup `.exe` or game folder and copies the game data into `game` beside it (`import-data.ps1`), then starts the game.

From a terminal:

```
powershell -ExecutionPolicy Bypass -File prepare-data.ps1 -Source "D:\...\Kabus 22.iso" -Output game
kabus22.exe --data game                      # Direct3D 12
kabus22.exe --data game --renderer opengl    # OpenGL
```

`prepare-data.ps1 -Source` accepts the ISO, the setup `.exe`, a folder holding the setup (a mounted ISO or the disc) or an installed game folder. `kabus22.exe --help` lists every option; `kabus22.exe --list-capabilities` shows which renderers and graphics options this PC supports.

## Saves, settings and mods

Saves, the in-game graphics settings (`config\launcher.ini`), controller settings, mods and texture packs live in `%LOCALAPPDATA%\Kabus22`, the same layout as `~/.local/share/kabus22` on Linux ([README](../README.md#usage), [MODDING.md](MODDING.md)). Setup refuses to install into that folder, and neither the uninstaller nor an update touches it. `kabus22.exe --import-saves "C:\...\Kabus 22"` imports the saves of the original Windows game.

Vulkan, hardware ray tracing and FSR are Linux-only; Direct3D 12 offers exactly the graphics options of OpenGL.

## Licences

The `licenses` folder of the installation or zip holds the Kabus22 licence, the ODE and cgltf licences, the notice of every bundled vcpkg library (`licenses\vcpkg`) and their exact versions (`licenses\vcpkg-ports.txt`). FFmpeg is built without GPL components and used under the LGPL; its sources are in the release's `-sources.zip`. The Microsoft Visual C++ runtime DLLs are redistributed app-locally.

## Building

The Windows build (MSVC, vcpkg) and the Xbox package are described in [XBOX.md](XBOX.md). `windows/package-windows.ps1` turns a desktop build directory into the zip and, with Inno Setup 6.3 or newer, the setup (`windows/installer/kabus22.iss`); the GitHub Actions workflow `.github/workflows/windows-build.yml` does the same on every push and pull request.
