# Windows and Xbox Series X|S

Kabus22 builds on Windows with MSVC and runs on a retail Xbox Series X|S in **Developer Mode** as a UWP app, rendering with the native Direct3D 12 backend (`native/render_d3d12.cpp`). The game data is your own copy, extracted from your ISO and bundled into a private, self-signed package that is sideloaded through Windows Device Portal. Nothing here works with, or needs, the GDK: the public GDK targets PC only, console GDK titles need the licensed GDKX and a dev kit, and Developer Mode runs only UWP apps.

## What runs where

| Build | Backends | Entry | Data and saves |
|---|---|---|---|
| Windows desktop (`x64-windows`) | Direct3D 12 (default), OpenGL | console `kabus22.exe`, same command line as Linux | `--data GAME_ROOT`; saves, mods, packs, `config\` under `%LOCALAPPDATA%\Kabus22` |
| Xbox UWP (`x64-uwp`) | Direct3D 12 | `native/uwp_main.cpp` builds the command line | game data inside the package (`<package>\game`); saves and config in the app's `LocalState` |

Vulkan, hardware ray tracing and FSR stay Linux-only. Direct3D 12 admits exactly the graphics options OpenGL admits; its captures match OpenGL within a mean of 0.08 per channel on the same GPU (menu and movie frames are bit-identical). The CTest suites are POSIX-only and are not built on Windows.

Xbox limits (UWP Dev Mode): Direct3D 12 feature level 11_0 with shader model 6.0; only Xbox-compatible controllers; no Discord presence; the in-game launcher-style backend probe is unavailable (an app cannot start child processes).

Display: the UWP CoreWindow is always 1920×1080, but the engine renders the whole frame (3D and UI layout) at the console's HDMI output mode (`HdmiDisplayInformation`; 3840×2160 on a 4K TV), like a desktop of that size. At start it asks the console for the highest refresh rate the TV offers at that resolution, up to 120 Hz (Series X|S outputs at most 120 Hz), and paces to 120 fps unless the graphics menu chose an FPS limit (`kabus22: display 3840x2160 at 119.88 Hz` in the log). Graphics settings from the in-game menu are saved to `LocalState\config\launcher.ini` and applied at the next start by the same rules as the Linux launcher (preset; `[Graphics]` keys under the custom preset, `fps_limit` and `seamless_doors` under every preset). Renderer, window mode and resolution are fixed on the console.

## Prerequisites (build PC)

- Visual Studio 2026 with **Desktop development with C++** and **Universal Windows Platform development** (C++ UWP tools: the store CRT in `VC\Tools\MSVC\<ver>\lib\x64\store` and the `Microsoft.VCLibs` framework package).
- Windows SDK 10.0.26100 (`dxc.exe`, `makeappx.exe`, `signtool.exe`). Visual Studio's bundled vcpkg, CMake and Ninja are used.
- The Xbox in Developer Mode with Device Portal enabled (Dev Home → Remote Access: set a username and password). Note the console's IP; the portal is `https://<ip>:11443`.

All build commands run through `windows\dev.cmd`, which loads the Visual Studio x64 environment (`--uwp` selects the store libraries). In Git Bash pass paths with forward slashes.

## 1. Game data from your ISO

```
powershell -ExecutionPolicy Bypass -File windows/prepare-data.ps1 -Iso "D:/.../Kabus 22.iso" -Output D:/kabus22/xbox-work/game
```

It mounts the ISO, extracts the Inno setup with innoextract 1.9 (downloaded once, SHA-256 pinned) and keeps the files the Linux installer selects (`*.k22`, `*.wrs`, `*.dat`, help/gallery images, menu sounds), about 2.2 GB. `K22.exe` is copied as data only: the engine reads its embedded `_a4font` and never runs it.

## 2. Dependencies (vcpkg, once per triplet)

```
cd windows
"C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/vcpkg.exe" install --triplet x64-windows --x-install-root=D:/kabus22/xbox-work/vcpkg/x64-windows
"C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/vcpkg.exe" install --triplet x64-uwp --x-install-root=D:/kabus22/xbox-work/vcpkg/x64-uwp
```

`windows/vcpkg.json` pins the baseline: SDL2, FreeType, FFmpeg (avcodec, avformat, swresample, swscale) and, for the desktop OpenGL backend, libepoxy.

## 3. Windows desktop build

```
VCPKG="C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
./windows/dev.cmd cmake -S . -B D:/kabus22/xbox-work/build/win64 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_TOOLCHAIN_FILE=$VCPKG" -DVCPKG_MANIFEST_DIR=$PWD/windows -DVCPKG_MANIFEST_INSTALL=OFF \
  -DVCPKG_INSTALLED_DIR=D:/kabus22/xbox-work/vcpkg/x64-windows -DVCPKG_TARGET_TRIPLET=x64-windows
./windows/dev.cmd ninja -C D:/kabus22/xbox-work/build/win64 kabus22
D:/kabus22/xbox-work/build/win64/kabus22.exe --data D:/kabus22/xbox-work/game            # Direct3D 12
D:/kabus22/xbox-work/build/win64/kabus22.exe --data D:/kabus22/xbox-work/game --renderer opengl
```

Options: `-DK22_D3D12=OFF` or `-DK22_OPENGL=OFF` drop a backend. On hybrid-graphics laptops the executable asks NVIDIA Optimus / AMD PowerXpress for the discrete GPU; Direct3D 12 picks the high-performance adapter itself. `K22_D3D12_DEBUG=1` enables the D3D12 debug layer with GPU-based validation (desktop only).

## 4. Xbox build, package and deploy

```
./windows/dev.cmd --uwp cmake -S . -B D:/kabus22/xbox-work/build/uwp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 \
  "-DCMAKE_TOOLCHAIN_FILE=$VCPKG" -DVCPKG_MANIFEST_DIR=$PWD/windows -DVCPKG_MANIFEST_INSTALL=OFF \
  -DVCPKG_INSTALLED_DIR=D:/kabus22/xbox-work/vcpkg/x64-uwp -DVCPKG_TARGET_TRIPLET=x64-uwp
./windows/dev.cmd --uwp ninja -C D:/kabus22/xbox-work/build/uwp kabus22
powershell -ExecutionPolicy Bypass -File windows/package-uwp.ps1 -Build D:/kabus22/xbox-work/build/uwp -Game D:/kabus22/xbox-work/game -Out D:/kabus22/xbox-work/package
powershell -ExecutionPolicy Bypass -File windows/deploy-xbox.ps1 -Console 192.168.2.28 -User NAME -Password SECRET -Package D:/kabus22/xbox-work/package
```

- `package-uwp.ps1` lays out `kabus22.exe`, the vcpkg DLLs, `AppxManifest.xml` (from `windows/uwp/AppxManifest.xml.in`, identity `Kabus22.Native`, publisher `CN=Kabus22 Dev`), generated tiles and the game data (hard links), packs `Kabus22.msix` and signs it with a self-signed certificate it creates in `Cert:\CurrentUser\My` (exported as `Kabus22-dev.cer`). Every package gets the next fourth version field (`<build>\package-revision.txt`), so an install updates in place. `-NoGame` makes a small package without data for smoke tests.
- `deploy-xbox.ps1` turns on the console setting `DefaultUWPContentTypeToGame`, so the package installs as a **Game** (about 5 GB of memory, exclusive CPU cores and the full GPU, instead of the 1 GB App budget); the engine logs the limit it got (`kabus22: UWP memory limit 5120 MB`). It stops the running game, uploads the package with its certificate and the VCLibs framework over the installed one (no uninstall, so saves, `launcher.ini` and `controller.ini` in LocalState survive), waits for the install and starts the game. You can also set App type: Game by hand in Dev Home (select the game, View button → View details).

Testing on the console:

| Command | Effect |
|---|---|
| `deploy-xbox.ps1 ... ` (no `-Package`) | start the installed game again |
| `deploy-xbox.ps1 ... -EngineArgs "--language en --mode bonus"` | write `LocalState\args.txt`: extra engine arguments, one per line, appended to the defaults (`--data <package>\game --renderer d3d12 --window-mode fullscreen`) |
| `deploy-xbox.ps1 ... -EngineArgs "--standalone --list-capabilities"` | `--standalone` replaces the defaults (commands that take no other options) |
| `deploy-xbox.ps1 ... -EngineArgs ""` | clear `args.txt` |
| `deploy-xbox.ps1 ... -EngineArgs "K22_FRAME_TIMING=local:frames.txt"` | a `K22_NAME=value` line sets that diagnostic environment variable; `local:` makes the value a path in LocalState (fetch it with Device Portal's file explorer) |
| `deploy-xbox.ps1 ... -Log` | print `LocalState\kabus22.log`: the engine's stdout and stderr |
| `deploy-xbox.ps1 ... -Screenshot shot.png` | save the console's current screen (Device Portal `/ext/screenshot`) |

The controller's B button is handled by the game (`SDL_HINT_WINRT_HANDLE_BACK_BUTTON`), not as system "back". Controllers connect a moment after start-up; the engine opens them when SDL reports them.

Controls: without a `controller.ini` the Xbox build uses the **modern controller mode** with its default bindings (LT aim, RT fire, LB run, Start menu, Back map, Y spell, A/B/X/RB/LS/RS the remaining original buttons 7–12; left stick moves, right stick turns). To change them, put a `controller.ini` into `LocalState\config\` (Device Portal file explorer); `mode=original` there gives the scripts the raw pad instead. In original mode on Windows and Xbox, a recognised controller is presented in the original game's DirectInput layout (JOY_1–10 = A, B, X, Y, LB, RB, Back, Start, LS, RS; axes left stick, LT−RT, right stick), as SDL's Windows drivers number raw buttons and axes differently.

In 2 PLAYER mode the second pad drives player 2 with the same default buttons; a player-2 Inzar opens her spell menu with Start (d-pad or stick to move, A to select, B to go back) and player 2 has no inventory or map ([COOP.md](COOP.md#player-2s-input)).

## Build notes

- The pinned ODE source is patched at configure time (`cmake/KabusODELcp.cmake`, `cmake/KabusODEContact.cmake`); the patched files are written byte-exact with `file(CONFIGURE ... NEWLINE_STYLE UNIX)` because `file(WRITE)` translates newlines on Windows. MSVC has no 64-bit-significand `long double`, so the contact-merge predicate computes in `double` there: the differences, squares and products of the float inputs are exact in double, only the three-term sums can round differently from x87 PC64, which matters solely at a threshold tie. In the UWP build ODE's error reporting goes to stderr (`cmake/ode_uwp_error.h`), as UWP has no MessageBox.
- `windows/dev.cmd` uses `vswhere -all` so it also finds an instance the Visual Studio Installer is modifying.
