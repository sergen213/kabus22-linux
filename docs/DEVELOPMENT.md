# Development

Notes for building, testing and packaging Kabus22 Linux from source. For the short build recipe see the [README](../README.md#build-from-source).

## Build notes

CMake 3.20+, a C++20 compiler and Python 3 are required. Vulkan headers and `glslangValidator` are build dependencies even if you only run OpenGL: shaders are compiled and embedded at build time.

The pinned ODE **source** archive and BSD notices are included under `third_party/`; configuration checks its hash and applies two hash-checked recipes in `cmake/`: the contact merge (`KabusODEContact.cmake`) and zero-initialization of the LCP solver's `state[]` array (`KabusODELcp.cmake`). These commands build three separate executables: `build/native/kabus22`, `build/installer/kabus22-setup` and `build/installer/kabus22-launcher`. No original game data is needed to compile them. A local source build uses your distribution's libraries; it is not the separately pinned AppImage dependency closure described below.

## Tests

```sh
ctest --test-dir build/native --output-on-failure
ctest --test-dir build/installer --output-on-failure
python3 tools/check_wrs2.py
python3 tools/package_kabus_appimage.py --self-check
```

A source-only checkout registers these asset-free tests:

- native: `wdl_semantics`, `asset_boundaries`, `media_boundaries`, `input_saves`, `graphics_policy`, and `texture_upscaler` when Python Pillow and numpy are installed;
- installer: `installer_security_boundaries`, `installer_cli_arguments`, `launcher_settings_behavior`, `updater_layout_signature_archive`, `updater_end_to_end`, `launcher_text_coverage`.

`launcher_text_coverage` fails when a launcher string has no Turkish entry in `installer/launcher_text.cpp`; add one for every new launcher string. The archive-tool check generates its own fixtures. The same commands run in CI (`.github/workflows/ci.yml`).

Three more native tests (`engine_boundaries`, `material_boundaries`, `physics_boundaries`) register only when their private original-game fixtures exist at the paths CMake expects. They use `Normal_Mix.fx` and `Normal_SunMix.fx` under `reverse/media/iso/mode/samples/K22_mode.wrs/`, and `reverse/extracted/assets/yr.hmp`; engine needs both shader files and the terrain, material needs both shader files, physics needs the terrain. If present, the private wrapped-media fixture under `reverse/decompiled/runtime/campaign-media/riff-vorbis/` also extends `media_boundaries`. These files are not published. Reconfigure after supplying legally obtained fixtures.

Gameplay tests need your installed game: configure with `-DK22_GAME_DATA=GAME_ROOT` to register `gameplay_scenarios`, add `-DK22_ORIGINAL_SAVES=WINDOWS_GAME_DIR` for `level_sweep` (and `level_sweep_demir|ebru|inzar`), and `-DK22_REFERENCE_DIR=DIR` for `reference_comparison`. See [GAMEPLAY_TESTING.md](GAMEPLAY_TESTING.md). Default CTest success alone is not graphical or gameplay qualification.

## Running a source build

Run the installer as your ordinary user, **not with sudo**. A separately built installer does not search `PATH` for its payloads: without overrides it expects `kabus22`, `kabus22-launcher`, `kabus22-updater` and `innoextract` beside its own executable. For an unattended install, substitute your own media path:

```sh
./build/installer/kabus22-setup \
  --source "/path/to/Kabus 22.iso" --install-dir "$HOME/Games/Kabus22" \
  --engine "$PWD/build/native/kabus22" \
  --launcher "$PWD/build/installer/kabus22-launcher" \
  --updater "$PWD/build/installer/kabus22-updater" \
  --extractor /usr/bin/innoextract --unattended
```

Native original-campaign widescreen is built in; no patch folder is needed. Add `--patch "/path/to/Kabus22Patch"` only if you separately want the community patch's extras; it is neither bundled nor discovered automatically. `--help` lists all options and `--self-check` runs asset-free CLI and security checks. Installation details: [INSTALLER.md](INSTALLER.md).

Use a private `XDG_DATA_HOME` when experimenting with the engine so your normal saves are not affected. `--frames N` bounds a run, `--capture FILE` saves rendered output (not with `--headless`), and `--headless` runs script and engine logic without a window.

## Widescreen and font history (v0.1.1)

These notes describe how the original-campaign widescreen and the bitmap-font palette were recovered and checked for the v0.1.1 prerelease. Design details are in [WIDESCREEN_IMPLEMENTATION.md](WIDESCREEN_IMPLEMENTATION.md).

For an automatically selected, recognized original `K22.wdl` campaign, the engine defaults to desktop-sized borderless display. It integrates the recovered Kabus22Patch resolution and state-dependent menu/HUD/text geometry using **original ISO artwork**, including the original menu illustration and eye animation. No proprietary patch scripts, replacement artwork or Windows DLLs are shipped. Already-patched `K22Widescreen.wdl` and custom `--script` entries retain their own behavior rather than receiving a second transform.

OpenGL, Vulkan raster and Vulkan RT consume the **same engine-prepared camera, UI and hit geometry**. Main-camera viewport dimensions track the native window; authored signed ARC/ASPECT behavior is preserved, and portal/mirror/custom VIEWs are not forced full-window. This is recovered resolution/UI integration, **not an inferred Hor+ projection or exact community-patch restyling**. The community patch does not supply a Bonus widescreen algorithm; Bonus is unchanged by this correction. Full campaign/Bonus acceptance and steady campaign CPU performance remain unestablished.

Ghidra recovery distinguishes indexed-PCX **FONT** decoding from ordinary BMAP decoding: FONT preserves glyph indices and uses the normalized **active RGB palette**, not the embedded PCX palette or index-as-alpha. Palette selection tries the supplied input's basename `.raw`, then `palette.raw`, then RGB332; normalization keeps index 0 black and 255 white and lifts dark nonzero entries to brightness 8. World-palette events refresh cached FONT images; ordinary BMAP embedded-palette decoding is unchanged. Font data still comes only from your supplied media.

Actual OpenGL, Vulkan raster and Vulkan RT glyph-pixel checks show all **eight genuine Font0 load-menu rows at 2560×1377** (the window-manager-clamped window), with source text alpha 50 unchanged. **5120×1440** was checked only through headless producer/API geometry, preserving authored load-row spacing, not actual GPU/window-system output. The extreme **16384×720** API check exercises a **selected-slot viewport with clipping**, not eight simultaneously visible rows or GPU/window-system proof at that size. This retains the recovered script-scale asymmetry rather than inventing a uniform fit-to-screen layout.

v0.1.1 qualification passed all six native CTests and the separate installer CTest, and exercised fresh shipping-library original-ISO standalone consumers in OpenGL, Vulkan raster and Vulkan RT: source-input menus/FONT rows and selected HUD, pause, map, fade, projection and extreme-display API paths. The exact corrected AppImage additionally passed ISO-only installation without payload overrides, all installed inventory hashes, both generated shortcuts, 2560×1440 startup/captures in all three backends, and an ABI-compatible shared-library loader/replacement check in an independent extracted tree. **These bounded checks are not full NewGame/playthrough acceptance or proof of a rebuilt/new-ABI replacement library.** Historical release/artifact receipts do not qualify this correction.

## Optional AppImage assembly

Building your own AppImage is optional; downloading a release does not require Docker or the development toolchain. **A clone alone is not immediately ready to assemble an AppImage.** Assembly needs the excluded upstream helper/runtime binaries, Docker with BuildKit, network access for authenticated provisioning, and the exact pinned toolchain/dependency evidence. The public prerelease includes matching dependency sources and exact native release sources; successful local assembly alone is not runtime qualification or legal certification.

The included `packaging/helper-innoextract/` and `packaging/runtime-uruntime/` inventories, referenced notices/provenance and licensed corresponding-source archives form the packaging metadata closure. Keep those members unchanged: the packager authenticates them. Historical notice references to private qualification reports are not files available in this checkout.

### Acquire the excluded pinned helper and runtime

On the host, install `curl` and `gnupg` and use a current Docker installation. Download the official **innoextract 1.9 amd64** helper, not a locally rebuilt or arbitrary distro binary, for AppImage assembly. [Helper provenance](../packaging/helper-innoextract/PROVENANCE.txt) records the upstream URLs, detached signatures, author fingerprint and limitations. Its statements about locally retained binary archives describe the private acquisition history; those binaries are excluded here.

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

The image pins Ubuntu 24.04/snapshot packages, builds LGPL-only FFmpeg 6.1.1 and classic SDL2 dependencies, and provides Python 3.11+, `readelf`, `patchelf`, SquashFS tools, `glib-compile-schemas` and the updater's libcurl/OpenSSL/nlohmann-json development packages. Do not substitute Ubuntu's GPL-enabled FFmpeg binaries for this packaging closure. The driver builds the engine, GTK installer, separate GTK launcher and release (non-test) `kabus22-updater` with two parallel jobs using enumerated read-only source mounts, then passes the mandatory `--launcher` and `--updater` payloads to `tools/package_kabus_appimage.py`. It acquires authenticated corresponding dependency sources with network access, then assembles offline. The AppImage entry remains the installer; the installed settings launcher is a distinct application. ISO/assets/user data never enter the image. Output AppImage, JSON report and `.AppImage.sources.tar.gz` paths must be new; outputs are not overwritten or uploaded. Keep notices, matching dependency source sidecar and exact native project sources with any distributed artifact. The private qualified baseline required glibc 2.38 or newer; host GPU loaders/drivers are deliberately excluded. Update payload/manifest signing and rollback are described in [docs/RELEASE_PROCESS.md](RELEASE_PROCESS.md).

## Ghidra tools

`reverse/scripts/RecoverKabus.java` exports selected address-backed symbols, strings, references and decompilation; `reverse/scripts/ExportEmbeddedPcx.java` exports an embedded PCX verbatim with provenance. Neither includes a game executable or image. Use Ghidra and its supported JDK with a program you own, imported/analyzed in your own project. In the GUI, add `reverse/scripts/` to Script Manager's search paths; the script headers describe their arguments.

For an existing analyzed project, a read-only headless export can be run as follows (set your own Ghidra/project/output locations):

```sh
"$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT_DIRECTORY" "$PROJECT_NAME" \
  -process K22.exe -noanalysis -readOnly -scriptPath "$PWD/reverse/scripts" \
  -postScript RecoverKabus.java "$OUTPUT_DIRECTORY" 120 20
```

The PCX script accepts `OUTPUT_DIRECTORY IMAGE_ADDRESS [POINTER_ADDRESS [PE_FILE ...]]`; addresses must match your exact program. Generated decompilations, reports and extracted images remain private and outside this publication. These scripts are not required to build or run the engine.

## What the repository publishes

This Git repository publishes implementation/build/test sources and the explicitly inventoried third-party source/notices. It excludes original ISOs, game assets/scripts/patches, proprietary SDKs, Windows executables, compiled helpers/runtimes, AppImages, build outputs, Ghidra databases, generated decompilations and private captures/acceptance receipts. The two authored Ghidra scripts are the exception within the otherwise excluded reverse-engineering workspace. Public binary/source release assets are distributed separately through GitHub Releases, not committed to the source tree.

`.gitignore` is a strict reviewed-source allowlist, not a general build-output blacklist. New files remain ignored until explicitly reviewed and added to that allowlist; do not bypass it to publish game data or generated/private outputs.
