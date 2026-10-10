# Known issues and limits

Honest list of what is not established or not implemented. See [PROGRESS.md](PROGRESS.md) for what is.

## Gameplay coverage

- No continuous full-campaign or full-Bonus playthrough. Verification uses scenarios starting from 27 original Windows saves (16 worlds) plus New Game→Start4, and `level_sweep`, which enters all 101 campaign levels through the original `LevelYukle` path and plays 600 frames in each; each level's story sequence beyond its entry is not covered. Ledger: [GAMEPLAY_TESTING.md](GAMEPLAY_TESTING.md).
- Not exercised by scenarios: `video_topal_t` subtitle timing (another movie, `veca.k22`, was measured to match), Bonus score/kills/game over. Item combination (`world_item_combine`) and the kizic coordinate puzzle (`world_kizic_kordinat`) pass.
- Reference comparison with the original (Wine 11.19 + wined3d, windowed, not real Windows/Direct3D 9; `tools/wine-reference/`): startup/menus and six in-game cases (`tests/reference/*.k22s`). The reference set is `reverse/wine-reference/set/` (private, not in Git); `reference_comparison` (configure with `-DK22_REFERENCE_DIR`) checks 28 images against `tests/reference/thresholds.tsv`. In-game native captures repeat run to run (≤ 0.001 SSIM); menu and movie frames do not, because the `kn_arg.k22` backdrop and movie phase follow the wall clock on both sides. Pixel parity is not certified; results and the remaining gap are in [GAMEPLAY_TESTING](GAMEPLAY_TESTING.md#reference-comparison-original-under-wine). Parity findings:
  - The original under Wine does not apply the `RenkAyar.dll` `SetGammaRamp` (GA_Gamma 30), because a windowed D3D9 device does not present it [INFERENCE]. Native applies it, so comparisons use `compare.py --ref-gamma 30`. After that correction, whole-frame brightness matches within 1–4% in all six in-game cases.
  - Model lighting, fixed 2026-10-09 ([GAMEPLAY_TESTING](GAMEPLAY_TESTING.md#bugs-fixed) 19–21): the Demir model now matches the original in level5 and Start4 (shirt, legs and Start4 torso within 1.5% mean), and the level5 right-hand lockers and whiteboard, formerly about 1.33× too bright, are within 1%. Residual: the level5 ceiling fan (`tavanpervane.mdl`, AMBIENT −150; hub mean 52.7 against 59.8) and the kriz boat (`tekne_loop.mdl`) stay darker than the original. The floor-light sample is ruled out: the fan's trace misses the floor (`FLOOR_RANGE` 100, as in A6 `00437b90`, so both sides reset to 127 and the ambient term is 0), and the boat's sample is valid (distance 86, TEX_LIGHT 51). The cause, sun/dynamic-light terms or the surface hit under the boat, is open ([ORIGINAL_BEHAVIOR §15](reverse-engineering/ORIGINAL_BEHAVIOR.md#15-wmb-entity-flags-model-lighting-and-draw-state)).
  - The level5 wooden chair in front of the whiteboard (`FFR9.MDL`), formerly only in the original, was the WMB entity flag decode (FLAG1 read as INVISIBLE); fixed, the chair is drawn (fix 19).
  - Start4's chain-link fence (`telbasla.tga`, 32-bit TGA sprites) hid the vans behind it, and level `ev`'s BRIGHT wall lamp (`lalam.mdl`) drew additively; both fixed (fixes 22–23).
  - kriz is not repeatable on the original itself: when `atilgan_ai` attacks depends on `random()` waypoint choices (first attack 43, 51 and 114 ticks after load in three Wine runs; native with scenario seed 0 at 44, with the same knock-back pan 107° and damage as the 43-tick original run). Captures after the first attack, and whether a knock-back carries the player onto the thrown mine, differ run to run; `kriz_loaded` of two Wine passes agree (0.99) but differ from a third (0.66), and native (0.60) shows Demir at another pose and position. Damage per contact and run/turn speed match (fix 18).
  - Remaining whole-frame SSIM gap in static scenes (ev 0.74, tnel24 0.69, cam1 0.77, against original-side noise ≥ 0.97): high-frequency texture detail differs (texture filtering/mip selection under wined3d versus native), lighting and geometry match by inspection. Not investigated further.
  - Not resolved statically: which D3D state the original's world-sprite draw (`00443350`) uses for a 32-bit-alpha bitmap; native blends it, matching the Wine captures (fix 22; the cam1 save circle is round). Also open: the sprite colour gain. `00443350` pre-lights sprites (no MATERIAL; AMBIENT + floor scalar + 64, MODULATE), but native draws them through `mat_sprite`, which is closer to the Wine capture than a prototype of the literal formula.
  - The cam1 yellow leaf decals (`gazel1.tga`, FLARE|OVERLAY floor sprites) that earlier builds showed are gone since fix 20 (default `mat_sprite` material), not fixes 22–23. This is not a regression: the original under Wine does not show them either, and the cam1 save circle matches within about 1 level of mean RGB. The solid-red HUD heart also matches the original.
- A6 entity collision boxes are not recomputed when a script changes SCALE after creation (`K22.exe` 00432cb0 not ported). Every such SCALE write in the original campaign scripts targets a PASSABLE entity (weapon attachments, effects, decals, e.g. `oyuncu2_ebru.wdl:62`, `koru_buyuler.wdl`, `efect2.wdl`, `boss.wdl:1456`), and every script `trace`/`ent_move` uses IGNORE_PASSABLE, so it has no gameplay effect there.
- Legacy `emit` particle SIZE follows A6 `00443950` (screen pixels at the selected video mode's width, `CONTRACT.txt` §43) in the campaign only. Not reproduced: the original's per-pixel truncation of very small particles, and per-view ARC (all views use the camera's ARC). Bonus (A7 `acknex.dll`) emit SIZE stays in quants because its legacy path has not been recovered.
- Sprite strips wider than 16384 px are re-laid out as a frame grid (one original asset, `s_alev+61.tga`); GPUs with a smaller texture limit are not handled.

## Rendering

- Nondefault stencil shadows, software world light and VIEW render-to-bitmap remain unsupported.
- Ray tracing needs ray queries, acceleration structures, buffer device addresses, descriptor indexing and shader int64; 1024-material-sampler ceiling. Presets disable unsupported features with a notice; explicit `--renderer vulkan` / RT requests fail instead of falling back.
- Bloom works in LDR thresholds when PBR/HDR is off, so bright white paper can glow like a light source.
- Upscaling (FSR-like) is not implemented and not offered.
- IBL is an ambient-only approximation (no sky cubemaps in the original data).

## Controls

- No physical gamepad has been tested. Modern and original controller modes were verified live in the rendered engine with a virtual Xbox 360 pad (uinput, xpad identity; `tools/virtual-gamepad/`, [GAMEPLAY_TESTING.md](GAMEPLAY_TESTING.md#controller-live-virtual-pad)): deadzone, walking, analog turn scaling with sensitivity, aim/fire, Start/Back menu and map, `[Buttons]` remap, hotplug, damage rumble with vibration 1 (none with vibration 0 or in original mode), and the original raw `JOY_*` buttons. Not live-tested: real rumble motors, PlayStation/Switch and other layouts (only via SDL's mapping database), Bluetooth reconnects, two pads, pad menu navigation, Vulkan renderers with a pad.
- In original mode the analog stick walks only when pushed past about 52%, as in A6. `joy_force` follows A6 `00422000` (fix 24): dead zone 32/255, ×`JOY_SENSE`·0.02, clamped to ±2. `Joy_Takip` halves it to `JOY_Ileri`, and the walk needs `Key_Ileri==1`, so only a saturated stick gives exactly 1; smaller deflections do nothing. The d-pad (arrow keys) also walks. Not checked on Windows: DirectInput's axis range on a real pad (`(pos·255)/range − 255`; native maps SDL's ±32767 onto it). Vibration is an added feature (the original has no force feedback), triggered by player damage in modern mode.
- SDL ignores pad input while the game window is unfocused (default `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=0`).

## Platform and packaging

- SDL2 rather than SDL3; GTK3 launcher rather than Qt 6 (see [PROGRESS.md](PROGRESS.md)). macOS ARM64 is not started.
- Requires glibc ≥ 2.38, x86-64. Older CPUs not tested.
- Version 0.2.0 is built and qualified locally only; it is not published. The update signing key is a development key held by the maintainer; key custody/rotation is in [RELEASE_PROCESS.md](RELEASE_PROCESS.md). The Docker release workflow has not run on GitHub.

## Rights

- No project-wide license is assigned. The game, its media and the community patch are not distributed; users supply their own. The iseeeva/kabus22 repository has no license; nothing from it is copied. See [COMMUNITY_CREDITS.md](COMMUNITY_CREDITS.md).
