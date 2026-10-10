# Widescreen: community patch mechanism and native implementation

Labels: **CONFIRMED** / **HIGH CONFIDENCE** / **HYPOTHESIS** / **UNKNOWN**. Related: [COMMUNITY_PATCH_ANALYSIS](COMMUNITY_PATCH_ANALYSIS.md) · [PATCH_BINARY_DIFF](PATCH_BINARY_DIFF.md) · [CAMERA_SYSTEM](CAMERA_SYSTEM.md) · [BUG_FIX_PORTING](BUG_FIX_PORTING.md).

Short answer: the community patch is **not Hor+**. It raises the engine's mode cap, lets `K22.ini` choose any resolution ≥ 1280×720, and re-scales 2D art and HUD with separate X/Y factors (non-uniform stretch). The 3D projection is untouched: the engine keeps the horizontal FOV (`camera.arc`) and derives the vertical extent from the window, so wider windows show **less** vertically (Vert-) in windowed/borderless mode, while exclusive fullscreen keeps the 4:3 vertical extent and stretches the image horizontally. The native engine reproduces the same model for the original scripts; it adds no Hor+.

## 1. How the patch does it

| Layer | Mechanism | Evidence | Label |
|---|---|---|---|
| Executable | largest mode-table entry 1920×1200 → 3840×2400; start-up archive `K22Widescreen.wrs` | `.data 0x627ab8`, `0x621243` ([PATCH_BINARY_DIFF](PATCH_BINARY_DIFF.md) §2.2) | CONFIRMED |
| Projection code | unchanged (`.text` differs only at three start-up/integrity sites; `.rdata` identical) | `CONTRACT.txt:2147-2163`; `reverse/community/reports/pe-compare.json` | CONFIRMED |
| Resolution choice | `dbg_screen_reset(0)` in `K22Widescreen.wdl`: `Screen:Mode` 0/1/2 from `K22.ini` via `INI_GetVariable`; `X`/`Y` = INI values, or `sys_metrics(0/1)` when 0 or borderless; rejects < 1280×720 with an error and exits; `video_window(...,16|0|1,title)` + `video_set(W,H,depth, mode 2|1|2)`; `GA_Cozunurluk` pinned to 0 and `GA_Cozunurlukler[0..1]=W,H` | `reverse/community/wrs/patch-K22Widescreen/K22Widescreen.wdl` lines 626-668 | CONFIRMED |
| 2D scale families | `GA_YeniScaleOranlari`=(W/1366, H/768) main-menu background `kan_1366.tga`; `…2`=(W/1024, H/768) loading/intro panels; `…3`=(W/640, H/480) inventory/pause backgrounds, fades, cinematic bars, boss bar, map fade; X and Y independent → non-uniform stretch | same file 648-656; consumers `Kabusdismenu`, `YeniMenu`, `Envantermenu`, `Smartmenu`, `cinematic`, `kotu_koruyucu`, `LevelYonetim`, `BUL_Kordinat` (`reverse/community/diffs/*.diff`) | CONFIRMED |
| HUD/menu/text state switching | main loop: in gameplay `dbg_screen_reset(1)` sets `GA_ScaleOranlari{,2}`=(W/640, H/480); in menus/game-over/pause `dbg_screen_reset(2)` sets them and `GA_FontOranlari` to (W/1280, H/720) and `(3)` rescales ~40 TEXT objects, fades, subtitle Y = H − H/16 | `K22Widescreen.wdl` 672-770, 838-878 | CONFIRMED |
| Camera/FOV | no `camera.arc`/`camera.aspect` assignment added for widescreen (arc writes 155 → 156, the extra one is the F5 camera `arc=70`); viewport already `screen_size` in original `cam.wdl` | `reports/script-identifier-counts.json`; `diffs/cam.wdl.diff` | CONFIRMED |
| View-space weapon preview | `SilahPanel` default `Y=-70,Z=-37.5`; `Z=-52` if `Screen:Mode==1` (fullscreen); cinematic crate variant −40/−30 | `diffs/silah.wdl.diff`, `diffs/cinematic.wdl.diff` | CONFIRMED |
| Blood bursts | particle size forced to 0 (`kan_blood_scale=0`, scaled formula commented out) | `K22Widescreen.wdl:690`, `diffs/efect2.wdl.diff` | CONFIRMED |

### 1.1 Resulting 3D field of view

A6 projection (recovered, `CONTRACT.txt:2156-2163`; native `native/render_policy.hpp:33-38`): ARC is the horizontal FOV; vertical half-tangent = tan(ARC/2) · H/W · |pixelAspect|, with pixelAspect = 1/(ASPECT·Q), Q = (H·320)/(W·240) in exclusive fullscreen and Q = 1 otherwise; scripts keep ASPECT = 1.

Vertical FOV in degrees for representative ARC values (derived from that formula; **CONFIRMED** as arithmetic over the recovered contract, not measured in a Windows run):

| Aspect | ARC 60 windowed/borderless | ARC 80 windowed/borderless | any ARC, exclusive fullscreen | fullscreen horizontal stretch |
|---|---|---|---|---|
| 4:3 | 46.8 | 64.4 | = 4:3 value | ×1.000 |
| 5:4 | 49.6 | 67.7 | = 4:3 value | ×0.938 |
| 16:10 | 39.7 | 55.3 | = 4:3 value | ×1.200 |
| 16:9 | 36.0 | 50.5 | = 4:3 value | ×1.333 |
| 21:9 (64:27) | 27.4 | 39.0 | = 4:3 value | ×1.778 |
| 32:9 | 18.4 | 26.6 | = 4:3 value | ×2.667 |

So: windowed/borderless = **vertical cut (Vert-)**, square pixels; exclusive fullscreen = 4:3 vertical extent displayed across the wide screen = **anamorphic horizontal stretch**. **HIGH CONFIDENCE.** Independent corroboration: the patch's weapon-preview offsets are −37.5 (windowed/borderless: less vertical room, entity raised) and −52 (fullscreen: same vertical room as 4:3, essentially the original −50). **HIGH CONFIDENCE.**

The patch's default `K22.ini` is `Mode = 1` (exclusive fullscreen), i.e. the stretched variant. **CONFIRMED** (file).

### 1.2 UI positioning

Background art and full-screen panels are scaled to fill the window independently in X and Y; menus/HUD use the 1280×720 or 640×480 basis per state; subtitles sit at H − H/16. Nothing is pillar-boxed and no aspect-correct anchor exists. **CONFIRMED** (formulas above).

## 2. How native implements it

Native applies a recovered equivalent of the patch's display model to the **original ISO scripts**, without shipping patch scripts, art or DLLs (`CONTRACT.txt:2214-2223`). When the user's game directory contains `K22Widescreen.wrs`, native runs the patch's own scripts and leaves display handling to them.

| Element | Native code (working tree 2026-10-09) | Label |
|---|---|---|
| Admission | `Engine::configure_display` (`native/engine.cpp:693-749`): only the auto-selected original campaign `K22.wdl` whose `GA_Cozunurlukler`/`GA_ScaleOranlari{,2}`/`GA_FontOranlari` tables match the authored values and which lacks `dbg_screen_reset` (i.e. not already patched) | CONFIRMED |
| Mode/size | default borderless at desktop size; `--resolution` 1280×720 … 16384×16384; `--window-mode windowed|fullscreen|borderless`; `--display authored` restores original resolution services | CONFIRMED (`native/main.cpp` help text) |
| Art tier (`GA_Cozunurluk`) | Unlike the patch (which pins `GA_Cozunurluk` to 0), native owns slot 2 of the authored tables. The original scripts pick 1024-tier art when `GA_Cozunurluk>1` (`Kabusdismenu` `kan_1024`/eye positions, `YeniMenu`, `K22.wdl`, `PlayerEnerji`, `KoruView`, `kotu_koruyucu`), and every native size is ≥ 1280×720, so native sets `GA_Cozunurluk`=`GM_CursorCozunurluk`=2. It writes `GA_Cozunurlukler[4..5]`=W,H, `GA_ScaleOranlari[4..5]`, `GA_ScaleOranlari2[4..5]`=(W/1024, H/768) in gameplay and (W/2048, H/1152) in menus, and `GA_FontOranlari[2]`=W/1280. The font arrays copy slot 0 into slot 2, so text keeps the tier-0 fonts. Leaving native display restores the authored slot-2 values and `GA_Cozunurluk` | CONFIRMED (code; `check_engine` display graph and `--campaign-display` real-data assertions) |
| Saved settings | The tier-2 override exists in memory only. `options.k22` byte 0 (`GA_Cozunurluk`, written by `GenelAyarlar.wdl GA_AyarlariKaydet`) keeps the user's authored value: native records it when the script reads `options.k22` and puts it back when the script writes the file (`native/engine.cpp` `file_open_read`/`file_close`). A later `--display authored` run therefore starts at the user's resolution | CONFIRMED (unit test + live run, §3) |
| Scale tables | `Engine::prepare_display` (`native/engine.cpp:750-840`): menu/font (W/1280, H/720), gameplay (W/640, H/480), quantized to 22.10 `var` | CONFIRMED |
| Per-object roles | full-screen fills scaled to window (`Fill`), map pixels, intro titles centred on the 1024×768 basis, eyes at the slot-2 positions (662,601)/(327,603) in 1024×768 space, load list/cursor with scroll budget, credit frame/text, inventory labels, cinematic bars (W/640,H/480 like the patch), boss bars on 1024-tier `BEP1_*` art (W/1024, H/768), `PEMP_`/`KVMP_` HUD on W/1024, pause menu, bottom-anchored menu panels, `Cer_*` highlight frames re-anchored on resize, exit-question frame bottom = H−16, subtitle Y = H·15/16 | CONFIRMED |
| Projection | `Engine::display_aspect` (`native/engine.cpp:841-846`): pixelAspect = 1/(ASPECT·Q), Q only in exclusive fullscreen; `Camera::widescreen=false` for every view | CONFIRMED; asserted by `native/check_engine.cpp` `campaign_display` projection phase |
| Weapon preview | `SilahPanel` view entity gets Y=−70, Z=−37.5 (−52 fullscreen; crate −30/−40) in native display mode (`native/engine.cpp:2836`) — the patch values | CONFIRMED |
| Hor+ | not implemented; renderer supports `widescreen=true` with `referenceAspect` 4:3 (`native/render.hpp:53-55`, `native/render_policy.hpp:35`) but nothing selects it | CONFIRMED |
| Blood bursts | original size 300 kept for the original scripts | CONFIRMED |

## 3. Native run results

Engine: installed `~/.local/share/kabus22/bin/kabus22` ("0.1.1", SHA-256 `d2e08fd3…cd44`, local repair build of 2026-10-08, `CONTRACT.txt` §42), because the current working tree did not build during this analysis (a concurrently edited shader was missing). The working tree has no uncommitted change touching display code (`git diff native/engine.cpp`: no hunk in `configure_display`/`prepare_display`/`display_aspect`/`SilahPanel`), so the binary's widescreen path is expected to match the tree. **HIGH CONFIDENCE.** Data: `~/.local/share/kabus22/game` (ISO only, no patch) read-only. Environment: private `XDG_DATA_HOME/CONFIG/STATE`, private 0700 `XDG_RUNTIME_DIR`, `SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=pipewire`, `--language en --display native --window-mode windowed`, `timeout`. Script: `reverse/community/aspect/run.sh`; tables `reverse/community/aspect/results-*.tsv`; downscaled captures `reverse/community/aspect/cap-*.png`.

| Requested | Aspect | 120 frames (host X, RX 7900 XTX) | 1500 frames (host X) | 1500 frames (private Xvfb 7680×2160, llvmpipe) | Visual check (1500) | Label |
|---|---|---|---|---|---|---|
| 1024×768 | 4:3 | exit 1, "requires 1280x720 through 16384x16384" | same | — | not runnable: below native minimum | CONFIRMED |
| 1600×1200 | 4:3 | exit 0, 1600×1200 | exit 0, 1600×1200 | — | main menu, art fills window | CONFIRMED |
| 1280×1024 | 5:4 | exit 0, 1280×1024 | exit 0, 1280×1024 | — | main menu, art fills, slight vertical stretch | CONFIRMED |
| 1366×768 | ≈16:9 | exit 0, 1366×768 | exit 0, 1366×768 | — | main menu | CONFIRMED |
| 1920×1080 | 16:9 | exit 0, 1920×1080 | exit 0, 1920×1080 | — | main menu; buttons bottom-centred | CONFIRMED |
| 1920×1200 | 16:10 | exit 0, 1920×1200 | exit 0, 1920×1200 | — | main menu | CONFIRMED |
| 2560×1080 | 21:9 | exit 0, 2560×1080 | exit 0, 2560×1080 | exit 0, 2560×1080 | logo/art visibly stretched horizontally; menu intact | CONFIRMED |
| 3840×1080 | 32:9 | exit 0, **2560×1080** | exit 0, **2560×1080** | exit 0, **3840×1080** | host window clamped by the window manager to monitor width; Xvfb unclamped: strong horizontal stretch, menu intact | CONFIRMED |
| 3440×1440 | 43:18 | exit 0, **2560×1377** | exit 0, **2560×1377** | exit 0, 3440×1440 | host clamp as above; Xvfb correct | CONFIRMED |
| 5120×1440 | 32:9 | — | — | exit 0, 5120×1440 | menu with animated background video, stretched | CONFIRMED |
| 3840×2160 | 16:9 | — | — | exit 0, 3840×2160 | menu | CONFIRMED |

Notes:

- All 120-frame captures are black (mean luminance 0): frame 120 is still inside the start-up fade; 600 frames show the FIP publisher logo; ≈1500 frames reach the main menu. **CONFIRMED.** The engine reports the drawable it actually got; capture size = actual window size.
- Montages: `reverse/community/aspect/montage-1500-windowed.png`, `montage-1500-xvfb.png`.
- Only start-up/menu presentation was exercised. No in-game 3D frame was captured, so the FOV table in §1.1 is not run-verified natively. **UNKNOWN** (runtime) / **CONFIRMED** (code + unit assertion).
- Exit-confirmation menu captured at 1920×1080 and 2560×1080 after XTest key input on Xvfb (`exitmenu-2300-xvfb-*.png`) shows the misplaced highlight frame described in [COMMUNITY_PATCH_ANALYSIS](COMMUNITY_PATCH_ANALYSIS.md) §7.1. **CONFIRMED** (capture, before the fix below).
- **Exit frame, fixed (2026-10-09).** The original bottom `480*GA_ScaleOranlari[y]-DM_AltPay` (`CikisMenu.wdl:47`) equals `screen_size.y-16` in every authored table. The native H/720 menu scale broke that identity, so the frame was drawn inverted above the question. While `CMP_Soru` is visible, `prepare_display` now sets `Cer_hY2 = H-16`. Scenario captures on the current tree, native windowed, settled after 90 frames: 1920×1080 and 2560×1080 both give `Cer_hY1` 866 and `Cer_hY2` 1064 (before the fix: 866 and 704, inverted). The frame now covers the question down to the NO button. Captures: `reverse/wave2/EngineFixes2/exit-1920x1080.png`, `exit-2560x1080.png`, before/after crops `exit-1920x1080-prefix-crop.png`/`exit-1920x1080-crop.png`. For authored 1024×768, a build with the fix line removed and the current build produce **byte-identical** captures (`Cer_hY2` 751.812 in both; `exit-authored-1024x768.png`), because authored mode never runs `prepare_display`. **CONFIRMED** (capture).
- **Settings persistence (2026-10-09).** In a private XDG, the Windows `options.k22` (byte 0 = 0) was put in place. A native 1920×1080 run opened Settings and left with Esc, which runs `GA_AyarlariKaydet`. Bytes 0-30 stayed identical; only byte 31, the patch language byte the original scripts never write, was dropped, as the original game does. In memory, `GA_Cozunurluk` stayed 2. A following `--display authored` run started with `GA_Cozunurluk` 0 and `video_mode` 6 (640×480), and options.k22 stayed byte-identical. Without a user file, the shipped `options.k22` (byte 0 = 2) is the authored value. **CONFIRMED** (live run).

## 4. Supported aspect ratios

| Aspect | Native (original ISO scripts) | Patch on Windows | Label |
|---|---|---|---|
| 4:3, 5:4 | yes, ≥ 1280×720 only (so 1280×960, 1600×1200, 1280×1024 …; 1024×768 rejected) | ≥ 1280×720 only | CONFIRMED |
| 16:10, 16:9 | yes | yes (≤ 3840×2400) | CONFIRMED |
| 21:9, 32:9 | yes (to 16384 px); 2D art stretched; Vert- 3D | yes up to 3840 px wide; same stretch/Vert- | CONFIRMED (native run), patch HIGH CONFIDENCE |
| custom | any W,H within limits; borderless always uses desktop size | any via `K22.ini` | CONFIRMED |

Not provided by either: Hor+ 3D, aspect-preserving (pillar-boxed) 2D art, safe-area HUD anchoring. A Hor+ option would only need the engine to set `Camera::widescreen=true` per view (renderer path exists) behind an explicit setting; it is a deliberate departure from original behaviour and is not implemented. **CONFIRMED** (code state).
