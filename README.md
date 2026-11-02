# Kabus22 Linux

A native Linux engine, installer, launcher and updater for the Turkish game **Kabus 22**, which runs your own original game files without Wine.

## What it is

Kabus 22 was built on the 3D GameStudio A6 engine (campaign) and A7 (the Bonus mode). This project reimplements that engine in C++20 from reverse engineering. It runs the game's **original WDL scripts and assets unmodified**, so the gameplay comes from the original game. No Windows code is run; Windows files are only read as data.

It includes:

- **Engine** (`kabus22`): campaign and offline Bonus mode, OpenGL renderer (default), Vulkan raster and Vulkan hardware ray tracing (ray-traced shadows, GI, reflections), AMD FSR 3.1 upscaling and optional frame generation, graphics presets, HD texture packs and native widescreen. The original look is the default.
- **Installer** (`kabus22-setup`): installs from your original ISO, the original `setup.exe` + `setup-1.bin`, or an installed Windows copy. It can also verify and repair an installation. Its interface is in German, English or Turkish.
- **Launcher** (`kabus22-launcher`): settings, Play, mods, logs and an Updates page. Its interface is in English or Turkish.
- **Updater** (`kabus22-updater`): signed updates from GitHub Releases with rollback. It never touches game data, saves or settings.
- **Native additions**: local two-player co-op, playing the campaign as Demir, Ebru or Inzar, save anywhere (F5/F9), a developer console, a modern controller mode, an optional first-person camera, seamless doors and a mod manager. Apart from seamless doors (on by default, can be turned off), they stay inactive until you use them.

**Status:** playable, but not finished. Testing uses scripted scenarios from original saves and a sweep that enters every campaign level. A full continuous playthrough and exact parity with the Windows original are not established. See [Known issues](docs/KNOWN_ISSUES.md).

## You need the original game

No game data is included: no ISO, scripts, models, textures, sounds or patches. You must own Kabus 22 and supply its files yourself. Kabus 22 and its media belong to their respective owners. This project is not affiliated with or endorsed by them.

## Download

Get the latest release from [GitHub Releases](https://github.com/sergen213/kabus22-linux/releases). Download `Kabus22-Linux-Installer-x86_64.AppImage` and `SHA256SUMS`, then:

```sh
sha256sum --check --ignore-missing SHA256SUMS
chmod +x Kabus22-Linux-Installer-x86_64.AppImage
./Kabus22-Linux-Installer-x86_64.AppImage
```

Run it as your normal user, **not with sudo**. Pick your Kabus 22 ISO (or `setup.exe`, or an installed Windows game folder) and an installation folder. The default is `~/.local/share/kabus22`. The installer adds an application-menu entry and a desktop shortcut that open the launcher.

Each release also carries `Kabus22-Linux-Installer-x86_64.AppImage.sources.tar.gz` and `native-source.tar.gz`. They hold the corresponding sources and notices of the bundled third-party components; keep them with the AppImage if you pass it on.

Existing installations update from the launcher's **Updates** page (Check, then Apply). The launcher restarts into the new version.

## Requirements

- x86-64 Linux with **glibc 2.38 or newer** and a graphical desktop.
- A working GPU driver with desktop **OpenGL 3.3** or newer.
- Optional Vulkan renderer: a **Vulkan 1.3** driver and loader.
- Optional ray tracing: additionally ray queries, acceleration structures, buffer device addresses, descriptor indexing and shader int64.

GPU drivers and GL/Vulkan loaders come from your system; they are not bundled. If you explicitly ask for Vulkan or ray tracing and your GPU lacks a feature, the engine stops with an error instead of falling back. Graphics presets instead turn unsupported features off with a notice.

## Usage

Start the game from the **Kabus 22** shortcut. It opens the launcher; choose your settings there and press **Play**. See [Launcher](docs/LAUNCHER.md).

You can also run the engine directly:

```sh
# Default OpenGL
~/.local/share/kabus22/bin/kabus22 --data ~/.local/share/kabus22/game

# Vulkan with hardware ray tracing
~/.local/share/kabus22/bin/kabus22 --data ~/.local/share/kabus22/game --renderer vulkan

# Vulkan without ray tracing
~/.local/share/kabus22/bin/kabus22 --data ~/.local/share/kabus22/game --renderer vulkan --ray-tracing off

# What this machine supports (backends, graphics options, texture packs)
~/.local/share/kabus22/bin/kabus22 --list-capabilities
```

Common engine options:

| Option | Purpose |
| --- | --- |
| `--mode campaign\|bonus` | Campaign (default) or the Bonus mode. |
| `--renderer opengl\|vulkan` | OpenGL (default) or Vulkan. Vulkan uses ray tracing unless `--ray-tracing off`. |
| `--preset original\|enhanced\|ultra\|custom` | Graphics preset; `original` (default) keeps the original look. |
| `--graphics KEY=VALUE` | One graphics option, e.g. `fsr=quality`, `frame_generation=on`, `fps_limit=144`. Repeatable. |
| `--texture-pack NAME` | Use an HD texture pack from `~/.local/share/kabus22/packs/NAME`. |
| `--display native\|authored` | Native widescreen (default) or the original resolution settings. |
| `--resolution WIDTHxHEIGHT` | Window size in native display mode (1280×720 to 16384×16384). |
| `--window-mode windowed\|fullscreen\|borderless` | Window mode; borderless (default) always uses the desktop size. |
| `--language tr\|en\|de` | Text language. |
| `--audio-language tr\|en` | Voice and movie language. |
| `--camera original\|first-person` | Optional first-person camera for Demir and Ebru. |
| `--character original\|demir\|ebru\|inzar` | Play the whole campaign as one character. |
| `--mod ID` | Enable a mod. Repeatable; the first listed wins. |
| `--import-saves WINDOWS_GAME_DIR` | Import saves from a Windows installation. |

Run `kabus22 --help` for the full list. Saves and settings live in `~/.local/share/kabus22` (or `$XDG_DATA_HOME/kabus22`); launcher settings are in `~/.config/kabus22/launcher.ini`.

In game: **F5** quicksaves (also SAVE in the Escape menu), **F9** quickloads, and **`` ` ``** or **F10** opens the console. Escape, Enter, Space, the left mouse button or pad A/B/Start skips the startup logos, intro and movies. **2 PLAYER** in the main menu starts local co-op.

## Build from source

On Ubuntu 24.04 (x86-64):

```sh
sudo apt-get install --no-install-recommends \
  git build-essential cmake ninja-build pkg-config python3 \
  libsdl2-dev libepoxy-dev libavformat-dev libavcodec-dev libavutil-dev \
  libswscale-dev libswresample-dev libfreetype6-dev libfontconfig1-dev \
  libvulkan-dev glslang-tools libgtk-3-dev libarchive-dev innoextract \
  libcurl4-openssl-dev libssl-dev nlohmann-json3-dev python3-cryptography

git clone https://github.com/sergen213/kabus22-linux.git
cd kabus22-linux
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native
cmake -S installer -B build/installer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/installer
```

This builds `build/native/kabus22` and `build/installer/kabus22-setup`, `kabus22-launcher` and `kabus22-updater`. No game data is needed to compile. To install from a source build, point the installer at those programs:

```sh
./build/installer/kabus22-setup \
  --engine "$PWD/build/native/kabus22" \
  --launcher "$PWD/build/installer/kabus22-launcher" \
  --updater "$PWD/build/installer/kabus22-updater" \
  --extractor /usr/bin/innoextract
```

Tests, AppImage assembly and the Ghidra scripts are covered in [Development](docs/DEVELOPMENT.md).

## Documentation

For players:

- [Installer](docs/INSTALLER.md) — supported game sources, verify and repair, installation layout.
- [Launcher](docs/LAUNCHER.md) — tabs, settings and where they are stored.
- [Graphics](docs/GRAPHICS_ROADMAP.md) — presets and every graphics option.
- [Ray tracing](docs/RAYTRACING.md) — the Vulkan ray-traced renderer and its limits.
- [Texture packs](docs/TEXTURE_RESTORATION.md) — building and using HD texture packs.
- [Mods](docs/MODDING.md) — mod format, mod manager and the 3-D model viewer.
- [Save anywhere and console](docs/CONSOLE.md) — quicksave and console commands.
- [Co-op](docs/COOP.md) — local two-player mode.
- [Character lock](docs/CHARACTER_LOCK.md) — playing as Demir, Ebru or Inzar throughout.
- [Seamless doors](docs/SEAMLESS_DOORS.md) — walking through doors without a loading pause.
- [Known issues](docs/KNOWN_ISSUES.md) — what is missing or not verified.

For contributors:

- [Development](docs/DEVELOPMENT.md) — tests, AppImage assembly, Ghidra scripts.
- [Release process](docs/RELEASE_PROCESS.md) — versioning, signing, updates and rollback.
- [Progress](docs/PROGRESS.md) — project status by phase.
- [Engine architecture](docs/ENGINE_ARCHITECTURE.md) and [gameplay systems](docs/GAMEPLAY_SYSTEMS.md).
- [Gameplay testing](docs/GAMEPLAY_TESTING.md) — scenario tests over original saves.
- [Reverse engineering](docs/REVERSE_ENGINEERING.md), [asset formats](docs/ASSET_FORMATS.md), [WRS archives](docs/WRS_FORMAT.md), [A6.31 plugin interface](docs/ACKNEX_A631.md).
- [Widescreen](docs/WIDESCREEN_IMPLEMENTATION.md), [camera system](docs/CAMERA_SYSTEM.md).
- Community patch: [analysis](docs/COMMUNITY_PATCH_ANALYSIS.md), [binary diff](docs/PATCH_BINARY_DIFF.md), [bug-fix porting](docs/BUG_FIX_PORTING.md), [credits](docs/COMMUNITY_CREDITS.md).

## Contributing

Changes are accepted only as pull requests to this repository, reviewed and merged by the maintainer. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

The project's own code is © sergen213, all rights reserved, and source-available under [LICENSE](LICENSE): you may read it, build it for personal use and fork it only to send pull requests ([CONTRIBUTING.md](CONTRIBUTING.md)). Redistribution, published modified versions and commercial use need written permission. Public source visibility is not an open-source license or a clean-room certification.

Third-party components keep their own licenses: preserve the libgamestudio MIT attribution in `native/assets.cpp`, the ODE/OU BSD texts and the dependency notices and source inventories (see the [packaging notice](packaging/NOTICE.txt)). None of this grants rights to Kabus 22 or its original media; obtain and use the game and any optional patch yourself under their terms.
