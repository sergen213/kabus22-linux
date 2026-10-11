# Kabus22 Linux

A reverse-engineered **native Linux engine, GTK installer, standalone GTK launcher and signed updater for Kabus 22**. It reads your own original game scripts and assets; it does not launch Wine, run the Windows engine, or bundle the game.

## Features and scope

- C++20 WDL runtime with campaign/A6 and offline Bonus/A7 compatibility paths, native archive/model/world loading, collision and ODE physics.
- SDL2 input/windowing, FFmpeg audio/video, FreeType/fontconfig text, and a default desktop OpenGL renderer.
- Explicit Vulkan raster or hardware-ray-query rendering. Unsupported explicitly requested features fail instead of silently switching backends.
- Optional graphics (current source): presets `original` (default, unchanged) / `enhanced` / `ultra` / `custom`, `--graphics KEY=VALUE`, PBR + HDR tone mapping, non-destructive HD texture packs (`tools/texture-upscaler`), shadow maps, SSAO, bloom, FXAA, TAA, volumetric fog, ray-traced shadows, GI and rough reflections with denoising. `kabus22 --list-capabilities` reports what this machine supports; presets disable unsupported features with a notice. See [docs/GRAPHICS_ROADMAP.md](docs/GRAPHICS_ROADMAP.md).
- Native GTK installer: installs from a user-owned ISO, the original `setup.exe`+`setup-1.bin`, or an installed Windows game folder (`--source`, auto-detected); `--verify` / `--repair`; versioned layout with atomic switching. Native widescreen for the original campaign is included without a patch folder. See [docs/INSTALLER.md](docs/INSTALLER.md).
- Separate GTK launcher: Install/Import, Verify, Repair, Play, renderer/preset/ray-tracing/texture-pack settings, FPS limit (original 72 cap, 30–240 or unlimited; `fps_limit` in [docs/GRAPHICS_ROADMAP.md](docs/GRAPHICS_ROADMAP.md)), controller settings, language, optional first-person camera, updates and diagnostic logs.
- `kabus22-updater`: GitHub Releases updates (stable/experimental), Ed25519-signed manifests, integrity checks, transactional install and rollback; game data, saves and settings are never touched. See [docs/RELEASE_PROCESS.md](docs/RELEASE_PROCESS.md).
- Modern controller mode (`~/.config/kabus22/controller.ini`; original mode is the default), atomic saves with rotating backups, and `kabus22 --import-saves WINDOWS_GAME_DIR` for original Windows saves.
- Scenario-based gameplay regression (`kabus22 --scenario`) over original saves: [docs/GAMEPLAY_TESTING.md](docs/GAMEPLAY_TESTING.md). Project status: [docs/PROGRESS.md](docs/PROGRESS.md), limits: [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md), reverse engineering: [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md).

Original-media installation, startup, scenario-driven gameplay from 27 original saves and real OpenGL/Vulkan GPU checks have been exercised privately. **A continuous full campaign/Bonus playthrough, complete original-Windows parity and execution on older CPUs are not established.** Nondefault stencil, software-world-light and VIEW render-to-bitmap routes remain unsupported. Ray tracing has a 1024-material-sampler-binding ceiling.

## AppImage prerelease download

Use the **[v0.1.1 experimental prerelease](https://github.com/sergen213/kabus22-linux/releases/tag/v0.1.1)** for integrated original-ISO widescreen and the recovered bitmap-font palette correction. Use assets and checksums from that exact release; historical private artifact hashes in the packaging notice do not identify this download.

The earlier [v0.1.0 release](https://github.com/sergen213/kabus22-linux/releases/tag/v0.1.0) is retained unchanged and predates these corrections.

The current-source standalone launcher and subsequent engine changes are **not included in the unchanged published v0.1.1 release**.

Download these assets together:

- `Kabus22-Linux-Installer-x86_64.AppImage` — native installer, engine and bundled non-host libraries.
- `Kabus22-Linux-Installer-x86_64.AppImage.sources.tar.gz` — matching distribution-library/FFmpeg/ODE corresponding sources, dependency build recipes and notices.
- `native-source.tar.gz` — native project/build/test sources and the reviewed `packaging/helper-innoextract/` and `packaging/runtime-uruntime/` source/notice inventories from the exact `v0.1.1` tagged Git commit, also available through its Git source links.
- `SHA256SUMS` — checksums of the three release assets above.

Helper/runtime corresponding sources are in those `packaging/` paths of `native-source.tar.gz`, and inside the extracted AppImage at `usr/share/licenses/kabus22/innoextract` and `usr/share/licenses/kabus22/uruntime`; they are **not** in the dependency sidecar. Uruntime's MIT license is distinct from its separately executed GPL-containing squashfuse/unsquashfs helpers (including GPL LZO and, in squashfuse, LGPL libfuse). Their complete retained sources/static build recipes provide the rebuild/relink route; the [packaging notice](packaging/NOTICE.txt) identifies these boundaries and the preserved component attributions.

From the download directory, verify the files before execution:

```sh
sha256sum --check SHA256SUMS
chmod +x Kabus22-Linux-Installer-x86_64.AppImage
./Kabus22-Linux-Installer-x86_64.AppImage
```

Run as your ordinary user, **not with sudo**. In the ISO picker, select your legally obtained original Kabus 22 ISO and choose a new installation directory. Leave community extras empty for native original-campaign widescreen; a separately obtained Kabus22Patch folder is optional for independently desired extras. No ISO, original game scripts/assets or patch is included in the release. The installer extracts media without running Windows code and creates application-menu and desktop shortcuts. OpenGL is the default; shortcuts also offer explicit Vulkan RT/raster actions.

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
  libvulkan-dev glslang-tools libgtk-3-dev libarchive-dev innoextract \
  libcurl4-openssl-dev libssl-dev nlohmann-json3-dev python3-cryptography

git clone https://github.com/sergen213/kabus22-linux.git
cd kabus22-linux
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 2
cmake -S installer -B build/installer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/installer --parallel 2
```

The pinned ODE **source** archive and BSD notices are included under `third_party/`; configuration checks its hash and applies two hash-checked recipes in `cmake/`: the contact merge (`KabusODEContact.cmake`) and zero-initialization of the LCP solver's `state[]` array (`KabusODELcp.cmake`). These commands build three separate executables: `build/native/kabus22`, `build/installer/kabus22-setup` and `build/installer/kabus22-launcher`. No original game data is needed to compile them. A local source build uses your distribution's libraries; it is not the separately pinned AppImage dependency closure described below.

### Tests

```sh
ctest --test-dir build/native --output-on-failure
ctest --test-dir build/installer --output-on-failure
python3 tools/check_wrs2.py
```

A source-only checkout registers three asset-free native CTests (`wdl_semantics`, `asset_boundaries`, `media_boundaries`) and two separate installer-directory tests (`installer_security_boundaries`, `launcher_settings_behavior`). The archive-tool check also generates its own fixtures.

Three additional native tests (`engine_boundaries`, `material_boundaries`, `physics_boundaries`) register automatically only when their private original-game fixtures exist at CMake's expected paths. They use `Normal_Mix.fx` and `Normal_SunMix.fx` under `reverse/media/iso/mode/samples/K22_mode.wrs/`, and `reverse/extracted/assets/yr.hmp`; engine needs both shader files and terrain, material needs both shader files, physics needs terrain. If present, the private wrapped-media fixture under `reverse/decompiled/runtime/campaign-media/riff-vorbis/` also extends `media_boundaries`. These files are intentionally not published. Reconfigure after supplying legally obtained fixtures. Default CTest success alone is not graphical or gameplay qualification.

## Install your own game

Run the installer as your ordinary user, **not with sudo**. A separately built installer does not search `PATH` for payloads: without overrides it expects `kabus22`, `kabus22-launcher` and `innoextract` beside its own executable. Supply all three explicitly for this source-build layout:

```sh
./build/installer/kabus22-setup \
  --engine "$PWD/build/native/kabus22" \
  --launcher "$PWD/build/installer/kabus22-launcher" --extractor /usr/bin/innoextract
```

In the window, select your original ISO and choose a **new** installation directory. **Current source includes native original-campaign widescreen by default; leave community extras empty for an ISO-only install.** A separately obtained Kabus22Patch folder is only needed if you independently want its extras. It is neither bundled nor automatically discovered, and installation does not rewrite WDL scripts or require patch artwork.

For an unattended install, substitute your own absolute media path:

```sh
./build/installer/kabus22-setup \
  --iso "/path/to/Kabus 22.iso" --install-dir "$HOME/Games/Kabus22" \
  --engine "$PWD/build/native/kabus22" \
  --launcher "$PWD/build/installer/kabus22-launcher" --extractor /usr/bin/innoextract \
  --unattended
```

Add `--patch "/path/to/Kabus22Patch"` only for separately desired community extras, not to enable native original-campaign widescreen. The installer refuses existing destinations and existing shortcuts; it stages extraction and preserves the source media on cancellation. The developer `--setup` route accepts the original loose `setup.exe` with its original sibling `setup-1.bin`, instead of `--iso`; it extracts them without running Windows code. `--help` lists options and `--self-check` runs asset-free CLI/security checks.

The current-source install contains `bin/kabus22-launcher`, `bin/kabus22` and `game/`. The examples above rely on installed host libraries; a bundled dependency payload additionally requires its corresponding notices. Application-menu and desktop shortcuts open the standalone settings launcher, not the engine directly. Explicit Vulkan RT/raster actions preselect the launcher's API choice; they **do not automatically Play**. Display adaptation remains engine-owned. Your desktop environment may require marking a desktop shortcut trusted before opening it.

### Standalone settings launcher (current source)

Unlike the installer, which requires your own ISO and a new destination, the launcher opens an **existing native installation** and needs no ISO or extraction:

```sh
./build/installer/kabus22-launcher --install-dir "$HOME/Games/Kabus22"
# Or use the installed application:
"$HOME/Games/Kabus22/bin/kabus22-launcher" --install-dir "$HOME/Games/Kabus22"
```

Its six tabs are Launch, Graphics, Audio, Keyboard, Joystick and Bonus. **Apply** writes settings without starting the game; **Play** saves and starts the separate native engine. Campaign and installed offline Bonus data are selectable. OpenGL, Vulkan raster and Vulkan hardware RT remain explicit choices without fallback.

Campaign controls retain original authored resolution and 16/32-bit color-depth compatibility records, lighting quality, completion buffering, mipmapping, anisotropy, reflections, blood color/amount and brightness/contrast/gamma. The color-depth record is a script/video compatibility choice, **not a promise of the old D3D framebuffer's physical format**. Blood amount and the three effects/music/dialogue volumes use 0–100; brightness/gamma use −100–100 and contrast −80–80. Original scripts may floor audio gains, so zero is not guaranteed exact mute. Ten keyboard actions and six joystick buttons are editable; joystick buttons use 1–12.

Bonus exposes its five source-consumed settings: authored resolution, shader level, anisotropy, music and stored brightness (50 is neutral; accepted native fixed-point range −2097102–2097151, not a recovered Windows UI range). Its separate AA choices request off/basic/4/9 samples, negotiated against native hardware. Campaign and Bonus text support `tr`/`en`/`de`; Campaign voice/movie audio supports `tr`/`en` (German text defaults to English voices). Saved patch locales are preserved unless intentionally changed; “Keep saved language / game default” also preserves patch indices not advertised as native locales. Patched/custom display remains game-owned. No speculative sensitivity, software-light, stencil-shadow or online-score controls are added.

Launcher choices live in `${XDG_CONFIG_HOME:-$HOME/.config}/kabus22/launcher.ini`. Game settings use the engine's existing save root, `${XDG_DATA_HOME:-$HOME/.local/share}/kabus22`, including private Campaign/Bonus records; original installed media and source defaults remain unchanged. Use private XDG directories for experiments. Native original-campaign display controls follow the engine limits below.

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

**Skipping intros.** Press Escape, Return, Space, the left mouse button or pad A/B/Start to skip the startup logos (FIP Publishing / Son Işık, about 15 s; the original cannot skip them), the New Game intro and full-screen movies (the original scripts accept only Escape there). The logos are fast-forwarded, not cut, so their script effects still run; the skipping press does not reach the menu or the game. Without a press, nothing changes.

### Original-campaign widescreen (v0.1.1)

For an automatically selected, recognized original `K22.wdl` campaign, the engine defaults to desktop-sized borderless display. It integrates the recovered Kabus22Patch resolution and state-dependent menu/HUD/text geometry using **original ISO artwork**, including the original menu illustration and eye animation. No proprietary patch scripts, replacement artwork or Windows DLLs are shipped. Already-patched `K22Widescreen.wdl` and custom `--script` entries retain their own behavior rather than receiving a second transform.

OpenGL, Vulkan raster and Vulkan RT consume the **same engine-prepared camera, UI and hit geometry**. Main-camera viewport dimensions track the native window; authored signed ARC/ASPECT behavior is preserved, and portal/mirror/custom VIEWs are not forced full-window. This is recovered resolution/UI integration, **not an inferred Hor+ projection or exact community-patch restyling**. The community patch does not supply a Bonus widescreen algorithm; Bonus is unchanged by this correction. Full campaign/Bonus acceptance and steady campaign CPU performance remain unestablished.

Ghidra recovery distinguishes indexed-PCX **FONT** decoding from ordinary BMAP decoding: FONT preserves glyph indices and uses the normalized **active RGB palette**, not the embedded PCX palette or index-as-alpha. Palette selection tries the supplied input's basename `.raw`, then `palette.raw`, then RGB332; normalization keeps index 0 black and 255 white and lifts dark nonzero entries to brightness 8. World-palette events refresh cached FONT images; ordinary BMAP embedded-palette decoding is unchanged. Font data still comes only from your supplied media.

Actual OpenGL, Vulkan raster and Vulkan RT glyph-pixel checks show all **eight genuine Font0 load-menu rows at 2560×1377** (the window-manager-clamped window), with source text alpha 50 unchanged. **5120×1440** was checked only through headless producer/API geometry, preserving authored load-row spacing, not actual GPU/window-system output. The extreme **16384×720** API check exercises a **selected-slot viewport with clipping**, not eight simultaneously visible rows or GPU/window-system proof at that size. This retains the recovered script-scale asymmetry rather than inventing a uniform fit-to-screen layout.

v0.1.1 qualification passed all six native CTests and the separate installer CTest, and exercised fresh shipping-library original-ISO standalone consumers in OpenGL, Vulkan raster and Vulkan RT: source-input menus/FONT rows and selected HUD, pause, map, fade, projection and extreme-display API paths. The exact corrected AppImage additionally passed ISO-only installation without payload overrides, all installed inventory hashes, both generated shortcuts, 2560×1440 startup/captures in all three backends, and an ABI-compatible shared-library loader/replacement check in an independent extracted tree. **These bounded checks are not full NewGame/playthrough acceptance or proof of a rebuilt/new-ABI replacement library.** Historical release/artifact receipts do not qualify this correction.

Use `--display authored` to restore original scripted display services and resolution presets. In native mode, `--resolution WIDTHxHEIGHT` chooses explicit dimensions for windowed/fullscreen use (minimum 1280×720, maximum 16384 per dimension); unset dimensions or either dimension zero use the desktop dimensions for both. `--window-mode windowed|fullscreen|borderless` selects the mode; borderless always uses desktop dimensions, even with an explicit resolution. These options do not change incidental headless/custom-script defaults or require changes to installer shortcuts.

```sh
# Explicit native windowed resolution for the original campaign
./build/native/kabus22 --data "$HOME/Games/Kabus22/game" \
  --display native --resolution 1920x1080 --window-mode windowed

# Original scripted display/preset behavior
./build/native/kabus22 --data "$HOME/Games/Kabus22/game" --display authored
```

Useful engine options:

| Option | Purpose |
| --- | --- |
| `--mode campaign\|bonus` | Campaign is default; Bonus uses your supplied Bonus data. |
| `--display native\|authored` | Native original-campaign display adaptation (default), or original scripted display services/presets. |
| `--resolution WIDTHxHEIGHT` | Native windowed/fullscreen dimensions; unset/either zero uses desktop, borderless always uses desktop. |
| `--window-mode windowed\|fullscreen\|borderless` | Native display mode; original campaign defaults to borderless. |
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

The image pins Ubuntu 24.04/snapshot packages, builds LGPL-only FFmpeg 6.1.1 and classic SDL2 dependencies, and provides Python 3.11+, `readelf`, `patchelf`, SquashFS tools, `glib-compile-schemas` and the updater's libcurl/OpenSSL/nlohmann-json development packages. Do not substitute Ubuntu's GPL-enabled FFmpeg binaries for this packaging closure. The driver builds the engine, GTK installer, separate GTK launcher and release (non-test) `kabus22-updater` with two parallel jobs using enumerated read-only source mounts, then passes the mandatory `--launcher` and `--updater` payloads to `tools/package_kabus_appimage.py`. It acquires authenticated corresponding dependency sources with network access, then assembles offline. The AppImage entry remains the installer; the installed settings launcher is a distinct application. ISO/assets/user data never enter the image. Output AppImage, JSON report and `.AppImage.sources.tar.gz` paths must be new; outputs are not overwritten or uploaded. Keep notices, matching dependency source sidecar and exact native project sources with any distributed artifact. The private qualified baseline required glibc 2.38 or newer; host GPU loaders/drivers are deliberately excluded. Update payload/manifest signing and rollback are described in [docs/RELEASE_PROCESS.md](docs/RELEASE_PROCESS.md).

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
