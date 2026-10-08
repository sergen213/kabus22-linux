# Kabus22 Linux

A reverse-engineered **native Linux engine and installer for Kabus 22**. It reads your own original game scripts and assets; it does not launch Wine, run the Windows engine, or bundle the game.

## Features and scope

- C++20 WDL runtime with campaign/A6 and offline Bonus/A7 compatibility paths, native archive/model/world loading, collision and ODE physics.
- SDL2 input/windowing, FFmpeg audio/video, FreeType/fontconfig text, and a default desktop OpenGL renderer.
- Explicit Vulkan raster or hardware-ray-query rendering, including geometry-based visibility and reflections. Unsupported requested features fail instead of silently switching backends.
- Native GTK installer: extracts a user-owned ISO, optionally applies a separately selected Kabus22Patch folder, recovers artwork/fonts from that media, and creates application-menu and desktop shortcuts.

Original-media installation, startup, selected original-script/world consumers and real OpenGL/Vulkan GPU checks have been exercised privately. **A full campaign/Bonus playthrough, complete original-Windows parity and execution on older CPUs are not established.** Nondefault stencil, software-world-light and VIEW render-to-bitmap routes remain unsupported. Ray tracing has a 1024-material-sampler-binding ceiling.

## AppImage prerelease download

Download the **v0.1.0 experimental prerelease** from [GitHub Releases](https://github.com/sergen213/kabus22-linux/releases/tag/v0.1.0). Use the assets on that exact release page; the older private artifact hashes in the packaging notice do not identify this fresh build.

Download these assets together:

- `Kabus22-Linux-Installer-x86_64.AppImage` — native installer, engine and bundled non-host libraries.
- `Kabus22-Linux-Installer-x86_64.AppImage.sources.tar.gz` — matching dependency/helper/runtime corresponding sources, build recipes and notices.
- `native-source.tar.gz` — native project/build/test sources from the exact Git commit tagged `v0.1.0`, also available through the release's Git source links.
- `SHA256SUMS` — checksums of the three release assets above.

From the download directory, verify the files before execution:

```sh
sha256sum --check SHA256SUMS
chmod +x Kabus22-Linux-Installer-x86_64.AppImage
./Kabus22-Linux-Installer-x86_64.AppImage
```

Run as your ordinary user, **not with sudo**. In the ISO picker, select your legally obtained original Kabus 22 ISO, choose a new installation directory, and optionally select your separately obtained Kabus22Patch folder. No ISO, original game scripts/assets or patch is included in the release. The installer extracts the media without running Windows code and creates application-menu and desktop shortcuts. OpenGL is the default; the installed shortcuts also offer explicit Vulkan RT/raster actions. ISO-only installation retains authored legacy resolution modes; the optional patch supplies its authored widescreen UI, not automatic Hor+ camera conversion.

**Minimum runtime requirements:** x86-64 Linux with **glibc 2.38 or newer**, a graphical desktop and a working host GPU driver/desktop OpenGL 3.3 or newer. GPU drivers, GL/Vulkan loaders and Wayland libraries are host-owned, not bundled. Optional Vulkan needs a host Vulkan loader/driver supporting Vulkan 1.3 and the requested features; hardware RT additionally needs ray queries, acceleration structures, buffer device addresses, descriptor indexing and shader int64. Unsupported explicit Vulkan/RT requests fail without automatic fallback. Generic x86-64 compilation does not establish execution on older CPUs.

This is an experimental prerelease, **not certification of a complete campaign/Bonus playthrough, original-Windows parity, older-CPU compatibility or legal compliance**. See the scope limitations above and the release notes for checks actually performed on the published build. Keep both source archives and notices with any redistributed AppImage; component licenses apply, and no project-wide license is assigned.

## Build from source (Ubuntu 24.04, x86-64)

CMake 3.20+, a C++20 compiler, Python 3 and these development packages are required. Vulkan headers and `glslangValidator` are build dependencies even when you run only OpenGL; shaders are compiled and embedded at build time.

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends \
  git build-essential cmake ninja-build pkg-config python3 \
  libsdl2-dev libepoxy-dev libavformat-dev libavcodec-dev libavutil-dev \
  libswscale-dev libswresample-dev libfreetype6-dev libfontconfig1-dev \
  libvulkan-dev glslang-tools libgtk-3-dev libarchive-dev innoextract

git clone https://github.com/sergen213/kabus22-linux.git
cd kabus22-linux
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 2
cmake -S installer -B build/installer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/installer --parallel 2
```

The pinned ODE **source** archive and BSD notices are included under `third_party/`; configuration checks its hash and applies the contact recipe in `cmake/`. No original game data is needed to compile either executable. A local source build uses your distribution's libraries; it is not the separately pinned AppImage dependency closure described below.

### Tests

```sh
ctest --test-dir build/native --output-on-failure
ctest --test-dir build/installer --output-on-failure
python3 tools/check_wrs2.py
```

A source-only checkout registers three asset-free native CTests (`wdl_semantics`, `asset_boundaries`, `media_boundaries`) and one separate installer test (`installer_security_boundaries`). The archive-tool check also generates its own fixtures.

Three additional native tests (`engine_boundaries`, `material_boundaries`, `physics_boundaries`) register automatically only when their private original-game fixtures exist at CMake's expected paths. They use `Normal_Mix.fx` and `Normal_SunMix.fx` under `reverse/media/iso/mode/samples/K22_mode.wrs/`, and `reverse/extracted/assets/yr.hmp`; engine needs both shader files and terrain, material needs both shader files, physics needs terrain. If present, the private wrapped-media fixture under `reverse/decompiled/runtime/campaign-media/riff-vorbis/` also extends `media_boundaries`. These files are intentionally not published. Reconfigure after supplying legally obtained fixtures. Default CTest success alone is not graphical or gameplay qualification.

## Install your own game

Run the installer as your ordinary user, **not with sudo**. A separately built installer does not search `PATH` for payloads: without overrides it expects `kabus22` and `innoextract` beside its own executable. Supply the engine and extractor explicitly for this source-build layout:

```sh
./build/installer/kabus22-setup \
  --engine "$PWD/build/native/kabus22" --extractor /usr/bin/innoextract
```

In the window, select your original ISO, choose a **new** installation directory, and optionally select your own Kabus22Patch folder. The patch is neither bundled nor automatically discovered. ISO-only installation retains the original scripted resolution modes; the optional patch supplies its authored widescreen/borderless/menu/HUD layout, not an automatic Hor+ camera conversion.

For an unattended install, substitute your own absolute media path:

```sh
./build/installer/kabus22-setup \
  --iso "/path/to/Kabus 22.iso" --install-dir "$HOME/Games/Kabus22" \
  --engine "$PWD/build/native/kabus22" --extractor /usr/bin/innoextract \
  --unattended
```

Add `--patch "/path/to/Kabus22Patch"` if wanted. The installer refuses existing destinations and existing shortcuts; it stages extraction and preserves the source media on cancellation. The developer `--setup` route accepts the original loose `setup.exe` with its original sibling `setup-1.bin`, instead of `--iso`; it extracts them without running Windows code. `--help` lists options and `--self-check` runs asset-free CLI/security checks.

The install contains `bin/kabus22` and `game/`. The examples above rely on installed host libraries; a bundled dependency payload additionally requires its corresponding notices. Shortcuts launch native OpenGL by default and offer explicit Vulkan RT/raster actions. Your desktop environment may require marking a desktop shortcut trusted before opening it.

## Run the native engine

```sh
# Default OpenGL (desktop OpenGL 3.3 or newer)
./build/native/kabus22 --data "$HOME/Games/Kabus22/game"

# Explicit Vulkan hardware ray tracing
./build/native/kabus22 --data "$HOME/Games/Kabus22/game" --renderer vulkan

# Explicit Vulkan raster, without ray tracing
./build/native/kabus22 --data "$HOME/Games/Kabus22/game" \
  --renderer vulkan --ray-tracing off
```

You can instead run the installed `"$HOME/Games/Kabus22/bin/kabus22"` with the same arguments. Vulkan requires a working host Vulkan loader/driver, Vulkan 1.3 and the requested device features. RT additionally requires ray queries, acceleration structures, buffer device addresses, descriptor indexing and shader int64. There is no automatic fallback on unsupported hardware. GPU drivers and GL/Vulkan loaders are not shipped here.

Useful engine options:

| Option | Purpose |
| --- | --- |
| `--mode campaign\|bonus` | Campaign is default; Bonus uses your supplied Bonus data. |
| `--language tr\|en\|de` | Select text language. |
| `--audio-language tr\|en` | Select campaign audio language. |
| `--script ENTRY` | Select a supplied WDL entry instead of automatic selection. |
| `--define SYMBOL` | Add a compiler definition; Bonus AA uses `defanti_0` through `defanti_3`. |
| `--frames N` | Bound a run to N engine frames. |
| `--capture FILE` | Capture actual rendered output; not available with `--headless`. |
| `--headless` | Run script/engine logic without a window; not Vulkan rendering. |

Saves/configuration live under `${XDG_DATA_HOME:-$HOME/.local/share}/kabus22`, separate from the supplied game data. Use a private `XDG_DATA_HOME` when running experiments so they do not affect your normal saves. Run `./build/native/kabus22 --help` for the complete CLI.

## Optional AppImage assembly — additional inputs required

Building your own AppImage is optional; downloading a release does not require Docker or the development toolchain. **A clone alone is not immediately ready to assemble an AppImage.** Assembly needs the excluded upstream helper/runtime binaries, Docker with BuildKit, network access for authenticated provisioning, and the exact pinned toolchain/dependency evidence. The public prerelease includes matching dependency sources and exact native release sources; successful local assembly alone is not runtime qualification or legal certification.

The included `packaging/helper-innoextract/` and `packaging/runtime-uruntime/` inventories, referenced notices/provenance and licensed corresponding-source archives form the packaging metadata closure. Keep those members unchanged: the packager authenticates them. Historical notice references to private qualification reports are not files available in this checkout.

### Acquire the excluded pinned helper and runtime

On the host, install `curl` and `gnupg` and use a current Docker installation. Download the official **innoextract 1.9 amd64** helper, not a locally rebuilt or arbitrary distro binary, for AppImage assembly. [Helper provenance](packaging/helper-innoextract/PROVENANCE.txt) records the upstream URLs, detached signatures, author fingerprint and limitations. Its statements about locally retained binary archives describe the private acquisition history; those binaries are excluded here.

```sh
sudo apt-get install --no-install-recommends curl gnupg
mkdir -p build-appimage/cache packaging/helper-innoextract/bin/amd64
curl --fail --location --proto '=https' --proto-redir '=https' \
  https://constexpr.org/innoextract/files/innoextract-1.9-linux.tar.xz \
  -o build-appimage/cache/innoextract-1.9-linux.tar.xz
printf '%s\n' '008efe5011476ccc4aae17c3e22038b5a1bc5c7aad2b9d4d869537bf3874d21f  build-appimage/cache/innoextract-1.9-linux.tar.xz' | sha256sum --check
mkdir -m 700 build-appimage/cache/innoextract-keyring
gpg --homedir "$PWD/build-appimage/cache/innoextract-keyring" --batch \
  --import packaging/helper-innoextract/upstream/dscharrer.asc
gpg --homedir "$PWD/build-appimage/cache/innoextract-keyring" --batch \
  --verify packaging/helper-innoextract/upstream/innoextract-1.9-linux.tar.xz.sig \
  build-appimage/cache/innoextract-1.9-linux.tar.xz
tar --extract --xz --file build-appimage/cache/innoextract-1.9-linux.tar.xz \
  --directory packaging/helper-innoextract/bin/amd64 --strip-components=3 \
  innoextract-1.9-linux/bin/amd64/innoextract
printf '%s\n' '7a8ad941deeb0de8d1f804830f339fd17e27cfbce473615d79d1d65c106884e9  packaging/helper-innoextract/bin/amd64/innoextract' | sha256sum --check
chmod +x packaging/helper-innoextract/bin/amd64/innoextract

curl --fail --location --proto '=https' --proto-redir '=https' \
  https://github.com/VHSgunzo/uruntime/releases/download/v0.8.1/uruntime-appimage-squashfs-lite-x86_64 \
  -o build-appimage/cache/uruntime-appimage-squashfs-lite-x86_64
printf '%s\n' '32c6024a14ff375749ee3bed3330c1586a6dd2bfeddbbd7ceacc053aec2b27a3  build-appimage/cache/uruntime-appimage-squashfs-lite-x86_64' | sha256sum --check
```

Proceed only if all hash/signature checks succeed. The retained author's primary fingerprint is `ADE9653703D4ADE0E997758128555A66D7E1DEC9`; a signature against that official key is not independent web-of-trust certification. The helper's default packaging path is `packaging/helper-innoextract/bin/amd64/innoextract`; the Python packager also accepts `--innoextract PATH`. The baseline shell driver uses the default path and requires the unmodified pinned runtime at the cache path above. Its fixed-width runtime policy patch is applied during assembly, not to your downloaded input.

### Build the isolated toolchain and assemble locally

Send only the Dockerfile on stdin, **never the full repository as Docker build context**:

```sh
docker build --platform linux/amd64 -t kabus22-ubuntu24-toolchain \
  - < tools/Dockerfile.ubuntu24
sh tools/build_kabus_baseline.sh "$PWD/build-baseline" kabus22-ubuntu24-toolchain
```

The image pins Ubuntu 24.04/snapshot packages, builds LGPL-only FFmpeg 6.1.1 and classic SDL2 dependencies, and provides Python 3.11+, `readelf`, `patchelf`, SquashFS tools and `glib-compile-schemas`. Do not substitute Ubuntu's GPL-enabled FFmpeg binaries for this packaging closure. The driver builds with two parallel jobs using enumerated read-only source mounts, acquires authenticated corresponding dependency sources with network access, then assembles offline. ISO/assets/user data never enter the image. Output AppImage, JSON report and `.AppImage.sources.tar.gz` paths must be new; outputs are not overwritten or uploaded. Keep notices, matching dependency source sidecar and exact native project sources with any distributed artifact. The private qualified baseline imported GLIBC 2.38; generic compilation does not prove older-CPU portability.

## Ghidra tools

`reverse/scripts/RecoverKabus.java` exports selected address-backed symbols, strings, references and decompilation; `reverse/scripts/ExportEmbeddedPcx.java` exports an embedded PCX verbatim with provenance. Neither includes a game executable or image. Use Ghidra and its supported JDK with a program you own, imported/analyzed in your own project. In the GUI, add `reverse/scripts/` to Script Manager's search paths; the script headers describe their arguments.

For an existing analyzed project, a read-only headless export can be run as follows (set your own Ghidra/project/output locations):

```sh
"$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT_DIRECTORY" "$PROJECT_NAME" \
  -process K22.exe -noanalysis -readOnly -scriptPath "$PWD/reverse/scripts" \
  -postScript RecoverKabus.java "$OUTPUT_DIRECTORY" 120 20
```

The PCX script accepts `OUTPUT_DIRECTORY IMAGE_ADDRESS [POINTER_ADDRESS [PE_FILE ...]]`; addresses must match your exact program. Generated decompilations, reports and extracted images remain private and outside this publication. These scripts are not required to build or run the engine.

## Publication and rights

This Git repository publishes implementation/build/test sources and the explicitly inventoried third-party source/notices. It excludes original ISOs, game assets/scripts/patches, proprietary SDKs, Windows executables, compiled helpers/runtimes, AppImages, build outputs, Ghidra databases, generated decompilations and private captures/acceptance receipts. The two authored Ghidra scripts are the exception within the otherwise excluded reverse-engineering workspace. Public binary/source release assets are distributed separately through GitHub Releases, not committed to the source tree.

`.gitignore` is a strict reviewed-source allowlist, not a general build-output blacklist. New files remain ignored until explicitly reviewed and added to that allowlist; do not bypass it to publish game data or generated/private outputs.

**No project-wide license has been assigned.** Public source visibility is not an open-source license or a clean-room certification. Existing file-specific third-party notices remain applicable: preserve the libgamestudio MIT attribution in `native/assets.cpp`, the ODE/OU BSD texts and dependency notices/source inventories. They do not grant rights to Kabus 22 or its original media. Obtain and use the game/optional patch yourself under their applicable terms. [Packaging notice](packaging/NOTICE.txt) preserves historical private qualification and the public prerelease policy, including corresponding sources and shared-library replacement. Historical private reports and artifact paths referenced there are not downloadable releases.
