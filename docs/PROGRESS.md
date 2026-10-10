# Progress

Status of the native Linux port against the master objective. Each milestone lists confirmed discoveries, source changes, tests, build results, blockers and the next task. Evidence paths under `reverse/` are private (not published); docs cite them for reproducibility on the maintainer's machine.

Approach (CONFIRMED, unchanged since v0.1.0): `native/` reimplements the Acknex A6.31 (campaign) / A7.05 (Bonus) runtime and executes the **original WDL scripts and assets** from the user's own media. Gameplay — characters, combat, enemies, inventory, puzzles, saves — therefore comes from the original scripts; correctness work is engine fidelity, verified against Ghidra recovery of `K22.exe` / `acknex.dll` and against real runs.

Deliberate deviations from the objective's suggested stack: SDL2 instead of SDL3 (already bundled/qualified; no SDL3-only feature needed), GTK3 launcher instead of Qt 6 (existing qualified launcher; separate executable as required). macOS ARM64 not started (optional).

## Phase 0–4 — inspection, community patch, binary diff, WRS, A6.31 interfaces

- Confirmed: ISO `K22.exe` is A6 6.31.4, MSVC 7.10 (VS.NET 2003), Direct3D 9 + static D3DX9, not packed; Bonus runs on `acknex.dll` A7 7.05. WRS payloads are LZSS. Docs: [reverse-engineering/](reverse-engineering/), [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md), [ASSET_FORMATS.md](ASSET_FORMATS.md), [WRS_FORMAT.md](WRS_FORMAT.md), [ACKNEX_A631.md](ACKNEX_A631.md).
- Community patch 1.8.5 and iseeeva/kabus22 analysed: 6 patched `.text` bytes (3 jumps skipping integrity checks), resolution cap 1920x1200→3840x2400, startup name `K22Widescreen`; widescreen is **not Hor+** (Vert- windowed, stretched exclusive fullscreen, per-axis 2D rescale). Docs: [COMMUNITY_PATCH_ANALYSIS.md](COMMUNITY_PATCH_ANALYSIS.md), [PATCH_BINARY_DIFF.md](PATCH_BINARY_DIFF.md), [WIDESCREEN_IMPLEMENTATION.md](WIDESCREEN_IMPLEMENTATION.md), [BUG_FIX_PORTING.md](BUG_FIX_PORTING.md), [COMMUNITY_CREDITS.md](COMMUNITY_CREDITS.md). The iseeeva repository has no license; nothing from it is copied or shipped.

## Phase 5–7 — engine, original rendering/widescreen, gameplay

Engine fixes this cycle (each with an asset-free regression check; addresses are `K22.exe` unless noted):

| Fix | Original evidence | Regression |
| --- | --- | --- |
| Exit-question highlight inverted in native widescreen | CikisMenu bottom = H-16 | `check_engine --campaign-display` |
| 1024-tier menu/HUD art selected for native display (GA_Cozunurluk slot 2), user's authored resolution not overwritten | script tier rule | display check, options byte-compare |
| RIFF-wrapped Vorbis tag 0x676f decoded | Kaynak.k22 members | `media_boundaries` |
| Archive precedence: loose → first-mounted archive | 00420120, 0041fd20 | `asset_boundaries` |
| `mouse_force` normalised, y-up, clamped | 00422000; acknex.dll 10027844 | `input_saves` |
| `vec_for_vertex` index clamp (every Ebru save crashed) | 00449dc0 | `vertex_index_consumers` |
| vec_* on `my.skillN` addresses contiguous skills (Lain and AI froze) | 0043e390 | `skill_vector_consumers` |
| Legacy `trace` always rewrites YOU (AA-gun bullets never hit) | 004546f0 | `legacy_trace_consumers` |
| IF/WHILE condition is the function result accumulator | 00410a80 | `wdl_regression` |
| `vec_to_screen` ignored VISIBLE incorrectly (duma enemies never fought) | 00404020 | `screen_projection_consumers` |
| A6 collision hulls (NARROW/FAT, square XY) | 00435730 | `entity_bounds_consumers` |
| Placed entity lift by SCALE_Z·min_z (save icons unreachable) | 004315d0 | scenario `world_save1_point` |
| MDL collision box from first frame only (duma floated) | 00457d10/00457d70/00458500 | `entity_bounds_consumers` |
| SYS_* date/time updated each frame | 00414650 | scenario save label |
| Sprite strip `+N` wider than texture limit (Seytan_k could not load) | 00459a90 | `sprite_strip_consumers` |
| Legacy `emit` particle SIZE is screen pixels at video-mode width (blood bursts filled the screen) | 00443950, 0043fc10, 0044b770 | `legacy_particle_size_consumers` |
| Scenario runs deterministic (randomize keeps seeded stream, manual media clock) — test mode only | scripts call randomize() before random() | `media_boundaries`, two identical CTest runs |
| WMB legacy entity flags decoded through an invented table (level5 props UNLIT/invisible, chair missing) | 00459eb0; Wine entity dump | `asset_boundaries` |
| Material-less entities ignored the floor-light sample; default MATERIAL binding | 0042fa20, 00430920/0044f970, 00415ad0 | `engine_boundaries` |
| Sun direction sign (models lit from below) | 00437800, 00438470 | `engine_boundaries` |
| 32-bit TGA sprites without TRANSPARENT drawn opaque (Start4 fence hid the vans) | 0040e2b0, 00440e90; Wine capture | `engine_boundaries` |
| BRIGHT alone drawn additively (ev wall lamp) | 0043ee40, 00443350, 0043b040 | `engine_boundaries` |

New features: modern controller mode (`controller.ini`; SDL GameController, radial deadzone, sensitivity, remap, optional damage rumble — original mode default; both modes verified live with a virtual uinput Xbox 360 pad, `tools/virtual-gamepad/`), save hardening (atomic writes, 3 rotating backups, `.meta` sidecar with migration hook), `--import-saves` for original Windows saves, optional first-person camera for Demir/Ebru (`--camera first-person`, patch-derived), patch languages es/ru/pl when installed, modern-mode load-menu joystick navigation.

Gameplay verification: scenario harness `kabus22 --scenario` drives the original menus/input; all 27 original Windows saves load (16 worlds, all three characters). 24 deterministic scenarios cover combat with all 8 weapons, spells, enemy AI at three sites, Lain boss defeat, game over, pickup/use, item combination, locked→key→unlocked door with Video_Cam cutscene, the kizic coordinate puzzle, movie A/V timing, save-point save→reload→restart, Bonus new game. `level_sweep` enters all 101 campaign levels through the original `LevelYukle` path and plays 600 frames each (101/101). Ledger: [GAMEPLAY_TESTING.md](GAMEPLAY_TESTING.md); systems: [GAMEPLAY_SYSTEMS.md](GAMEPLAY_SYSTEMS.md).

Original-vs-native parity milestone (2026-10-09): the unmodified ISO `K22.exe` runs under Wine as a reference (`tools/wine-reference/`); seven cases (`tests/reference/*.k22s`: startup/menus, level5, kriz, cam1, ev, tnel24, New Game→Start4) are captured on both sides and compared by the fixture-gated `reference_comparison` CTest against per-image thresholds (`tests/reference/thresholds.tsv`, original-side noise from two Wine passes). Lighting, models and framing match by measurement and inspection (Demir, lockers, whiteboard within 1–2% mean; camera offset 0–2 px); the remaining whole-frame gap is texture detail and the original's own AI randomness in kriz. REA evidence IDs and the full table: [GAMEPLAY_TESTING.md](GAMEPLAY_TESTING.md#reference-comparison-original-under-wine), [ORIGINAL_BEHAVIOR §15](reverse-engineering/ORIGINAL_BEHAVIOR.md#15-wmb-entity-flags-model-lighting-and-draw-state).

## Phase 8–10 — Vulkan, PBR/HD textures, ray tracing

- Presets original/enhanced/ultra/custom, `--graphics KEY=VALUE`, `--list-capabilities` (real GPU probe), automatic disabling of unsupported features with a notice; explicit `--renderer` remains fail-hard.
- Texture packs (non-destructive, `${XDG_DATA_HOME}/kabus22/packs`), PBR metallic-roughness with normal/AO maps, HDR + calibrated tone mapping; `tools/texture-upscaler` (Scale2x for UI/fonts/text, Lanczos or optional Real-ESRGAN for world/model, consistency guard). [TEXTURE_RESTORATION.md](TEXTURE_RESTORATION.md), [GRAPHICS_ROADMAP.md](GRAPHICS_ROADMAP.md).
- Ray query: RT shadows (soft), RT GI with temporal + à-trous denoise, roughness-dependent reflections; raster shadow maps, SSAO, bloom, FXAA, TAA, volumetric fog. Default RT lighting fixed to match the original non-shadowing D3D9 lights (byte-identical to Vulkan raster on four test views). Timing table and validation results: [RAYTRACING.md](RAYTRACING.md).
- AMD FSR 3.1.4 upscaling (`fsr=…`, Vulkan raster and RT, `quality` in `ultra`; FidelityFX SDK v1.1.4 subset vendored with its provenance) at render resolution with the HUD at display resolution; RT frame pacing fixed (recording overlaps the in-flight frame) and volumetric fog at half resolution: level5 `ultra` on `vulkan-rt` 122 -> 306 FPS at 1080p (RX 7900 XTX). [RAYTRACING.md](RAYTRACING.md#performance)
- Default captures (no graphics flags) byte-identical before/after for OpenGL and Vulkan raster; zero Vulkan validation errors for raster, RT, enhanced, ultra.

## Phase 11–12 — launcher, installer, updates, packaging

- Installer: `--source` auto-detects ISO, `setup.exe`+`setup-1.bin` or an installed Windows directory; edition detection; `--verify`/`--repair`; versioned layout `versions/<v>` + atomic `current` switch. [INSTALLER.md](INSTALLER.md).
- Launcher (GTK3): Install/Import, Verify, Repair, Play, renderer, presets, dynamic Custom/RT pages from engine capabilities, texture packs, controller page, language, camera, updates (check/apply/rollback), diagnostic logs.
- Updater: GitHub Releases over HTTPS, stable/experimental channels, Ed25519-signed manifest, sha256/size check, hostile-archive rejection, transactional commit, rollback; saves/settings/original assets untouched. CI and release workflows. [RELEASE_PROCESS.md](RELEASE_PROCESS.md).
- 0.2.0 AppImage candidate (local, not published): built with the pinned toolchain, ISO-only install, `--verify` clean, installed engine passes 600-frame starts in OpenGL/Vulkan raster/Vulkan RT and real gameplay scenarios; signed update payload applied and rolled back on a 0.2.0 install and on a flat v0.1.1 install with game/saves/options hashes unchanged. Hashes in [RELEASE_PROCESS.md](RELEASE_PROCESS.md).

## Tests and build

Native CTest: `wdl_semantics`, `asset_boundaries`, `media_boundaries`, `input_saves`, `engine_boundaries`, `graphics_policy`, `material_boundaries`, `physics_boundaries`, `texture_upscaler`, plus `gameplay_scenarios` and `level_sweep` (registered with `-DK22_GAME_DATA` / `-DK22_ORIGINAL_SAVES`) and `reference_comparison` (also `-DK22_REFERENCE_DIR`, rendered). Installer CTest: `installer_security_boundaries`, `installer_cli_arguments`, `launcher_settings_behavior`, `updater_layout_signature_archive`, `updater_end_to_end`. GPU suites: `k22-render-check --gl|--vulkan|--vulkan-rt`.

Sanitizers (Debug `-fsanitize=address,undefined`, `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`): on the 2026-10-10 tree (render/asset parity and controller fixes), native fixture CTests 9/9 pass. On the 2026-10-09 tree, installer CTest 5/5 passed, and so did headless scenarios `world_save1_point`, `combat_demir_duma_gun` and `inzar_spells` (fresh private XDG, imported original saves) and level-sweep rows 1 (start4, Demir), 27 (ev, Ebru), 39 (tnel24, Inzar), 62 (seytan_k, Inzar, scripted), 66 (tnel8, Ebru) and 101 (boss, Demir, scripted). No sanitizer report in any run. The one finding was in pinned ODE: `dSolveLCP` allocated `state[]` uninitialized and `swapProblem` swapped unassigned entries (UBSan invalid `bool` load in `check_physics yr.hmp`). `cmake/KabusODELcp.cmake` now zero-initializes it, with hash-checked original and modified sources like the contact recipe. Evidence: `reverse/wave2/Packaging/sanitizers/`.

## Remaining blockers / next tasks

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md). Next: publish 0.2.0 only after the maintainer reviews and authorizes release (and replaces the development signing key if desired); extend scenarios through each level's story sequence beyond entry; real Windows/Direct3D 9 reference captures (the current reference is Wine/wined3d); recover the A7 legacy particle path for Bonus.
