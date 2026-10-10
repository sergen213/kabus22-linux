# Original Behavior Catalog

Purpose: catalog what is known about the behavior of the original Kabus 22 game and what the native port has actually been observed doing when it runs the original scripts. Each item keeps **Observed**, with an evidence path, separate from **Assumed**.

Related: [Reverse-engineering index](../REVERSE_ENGINEERING.md) · [Engine architecture](../ENGINE_ARCHITECTURE.md) · [Gameplay systems](../GAMEPLAY_SYSTEMS.md) · [Camera system](../CAMERA_SYSTEM.md) · [Script system](SCRIPT_SYSTEM.md) · [Executable analysis](EXECUTABLE_ANALYSIS.md) · [Function map](FUNCTION_MAP.md)

## How to read this catalog

Until 2026-10-09 the original Windows executable was never run for this project; since then the unmodified ISO `K22.exe` has been run under Wine as a reference tool (rows labelled **Original observed (Wine)**, evidence under `../../reverse/wine-reference/`, method in [REVERSE_ENGINEERING](../REVERSE_ENGINEERING.md#dynamic-analysis-original-under-wine)). Everything stated about the original binary (`K22.exe`, Acknex A6 6.31.4, campaign; and the A7 runtime behind Bonus mode `Kabus 22 Mode/K22_mode.wrs`) comes from three kinds of source:

- **Static analysis.** This covers read-only Ghidra exports and paired C/ASM files under `../../reverse/decompiled/`, summarized in [`CONTRACT.txt`](../../reverse/decompiled/runtime/CONTRACT.txt). The contract's own header states: "No game execution … runtime observation". Static analysis was done on the patched `K22.exe` (SHA-256 `1a6a0dbc…`). The digest-qualified ISO `K22.exe` (`cc193c6b…`) differs from it only in 6 startup/archive-path bytes of `.text` (`CONTRACT.txt:2147-2149`).
- **Original script text.** The reference copy is in `../../reverse/extracted/original/`. The ISO edition is in `../../reverse/media/iso/scripts/`, and 173 of its 182 files are byte-identical to the reference copy.
- **Community material.** Kabus22Patch 1.8.5 and a third-party repository are inventoried under `../../reverse/community/` (reports in `../../reverse/community/reports/summary.json`). Their behavior was not independently executed on Windows either.

"Native observed" means a recorded run of the Linux native engine against original game data. Most walked campaign runs used an **ISO + Kabus22Patch** installation, whose entry archive is the patch `K22Widescreen.wrs` (`../../reverse/campaign-pointer-fixed-input-ledger.json`, `asset_archive_identity`). ISO-only runs are marked as such. The label "(ISO+patch)" means the patched scripts were driving the game.

Labels:

- **CONFIRMED**: direct evidence.
- **HIGH CONFIDENCE**: strong indirect evidence.
- **HYPOTHESIS**: plausible but not checked.
- **UNKNOWN**: the evidence needed to decide is missing.

## 1. Startup and bootstrap

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (static) | The engine compiles scripts, then enumerates plugins (`004133c0` called from `00413990`), then queues MAIN first and STARTER functions in reverse first-creation order. MAIN runs until its first yield, then the starters run. | `CONTRACT.txt:475-499` (§13) | CONFIRMED (static) |
| Original (script) | `K22.wdl main()` runs in this order: `sleep(1)`; `anactrl_ok` gate (defined through `IFDEF defiae_d3d`); `wait(1)`; `GA_AyarlariYukle` (reads `options.k22`); `video_switch(GA_Cozunurluk+6,…)`; volume globals; `dll_open("RenkAyar.dll")` with `dl_PaKaGa`; `d3d_*` settings; `Level_Load(analevel_wmb)` (`level0.wmb`); `fps_max=72`, `fps_min=24`; `Menu_AcilisEkrani()`. | `../../reverse/extracted/original/K22.wdl:646-689` | CONFIRMED |
| Original (static) | `level_load` only records a pending command (bit 8). The load runs after the current frame's script phase and does not yield. | `CONTRACT.txt:501-508` | CONFIRMED (static) |
| Native observed | ISO-only installation, 600 frames: 179 WDL files, 1857 functions and 93139 statements parsed. The run exits 0 after showing the authentic publisher bitmap PANEL ("FIP PUBLISHING") at 1024×768. | `CONTRACT.txt:1773-1779`, `:2099-2104` (receipts in a private cache, not in the repository) | CONFIRMED |
| Native observed | Before the logo, the screen is black because `Menu_AcilisEkrani` sleeps before showing the logo. A GDB snapshot at frame 239 shows the process waiting in that `SLEEP`, with no movie and no UI. | `CONTRACT.txt:1691-1700` | CONFIRMED |
| Native observed | `../../reverse/native-iso-campaign-start.png` (1024×768) shows the "A SON ISIK Studios game" logo. `../../reverse/native-iso-menu.png` is entirely black and is **not** menu evidence. | image files | CONFIRMED (contents inspected) |
| Native design | `Engine::initialize_plugins` runs after `Runtime::load` and before `Engine::configure_language` and `Runtime::bootstrap` (`native/main.cpp`). The PE DLLs are replaced by compiled providers. | `CONTRACT.txt:480-486`; grep | CONFIRMED |
| Original observed (Wine) | ISO `K22.exe` under Wine 11.19/wined3d, 1024×768 windowed: FIP PUBLISHING fades in over 1.1 s, holds 2.9 s, fades out over 2.0 s; 0.9 s black; SON ISIK fades in 1.1 s, holds 1.9 s, fades out 2.0 s (mean-luma timeline of the recording). This matches the script arithmetic (12.5 s) and native (logo visible frame 177, menu frame 925). The main menu then appears ~7 s later under Wine (DirectShow start of `kn_arg.k22`), versus ~1 s natively. | `../../reverse/wine-reference/explore1/startup.mp4`, `startup-sheet.png`, `acklog.txt` | CONFIRMED |
| Original observed (Wine) | Main menu layout (logo, NEW GAME…EXIT, eyes) matches native `--display authored` 1024×768; the red background is the looping movie, so frames differ by loop phase. | `../../reverse/wine-reference/menus/pair-*.png` | CONFIRMED (inspected) |

The logo sequence comes from `Menu_AcilisEkrani` (`K22.wdl:586`) on PANEL `Menu_Acilis` (layer 100). It shows `Vestel_Kabus22` (`Vestel_sunar.tga`) and then `LastLight_Kbs22` (`SONISIK_LOGO.tga`), with fades driven by `sleep` and `wait` (`K22.wdl:511-515`, `:586-645`). The patch variant does the same in `K22Widescreen.wdl:566-624` (`CONTRACT.txt:1691-1694`). **CONFIRMED**

## 2. Menus

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (script) | The out-of-game menu is `Kabusdismenu.wdl DisMenuAc` (:137). It loops the `kn_arg.k22` movie as backdrop (:182), loops `DM_Music` (:183), and includes every sub-menu (:205-219). | script | CONFIRMED |
| Native observed (ISO+patch) | The original main menu, New Game and Normal selection were rendered at 2560×1440. | `../../reverse/native-gl-campaign-integrated-menu.png`, `…-difficulty.png`; `CONTRACT.txt:988-995` | CONFIRMED |
| Native observed (ISO+patch) | The EXIT confirmation is keyboard-polled. Down → NO → Return goes back to the main menu. EXIT → YES → Return quits normally. A physical mouse click did not open the confirmation. | `../../reverse/native-main-bitmap-af06-result.txt`, `…-input-ledger.json`, `…-return-confirmation.png`, `…-no-returned-menu.png`; `CONTRACT.txt:1273-1283` | CONFIRMED |
| Native observed, earlier defect | `CikisMenu.wdl:46:17 bitmap object required` occurred before the shared bitmap-handle fix. The cause was the raw-address arithmetic `DMIL_CikisSoru[...]+Cer_Kenar`. | `../../reverse/campaign-pointer-fixed-exit-error.txt`; `CONTRACT.txt:1224-1230`, `:1239-1251` | CONFIRMED (fixed per §30-31) |
| Native observed | A fresh mouse warp or click did not move an inherited EXIT highlight. Pressing Up ×4 selected New Game. | `../../reverse/campaign-checkpoint-input-ledger.json` (`initial_menu_error`) | CONFIRMED |
| Assumed | The original menu ignores the mouse in the same way. | script is keyboard-polled | HIGH CONFIDENCE (script), UNKNOWN (Windows) |

## 3. Intro, briefing and movies

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (script) | Movies are `.k22` containers played through `media_play` (`media_layer`=900). Subtitles are frame-indexed and use `media_playing` frame numbers (`sub_titles_*.wdl`). The ISO edition selects voice through `defses_tr`/`defses_en` and the `video_ismi_strNN` names. | `../../reverse/media/iso/scripts/K22.wdl:22-50`; `LevelYonetim.wdl:772ff` | CONFIRMED |
| Native observed (ISO+patch) | After Normal, the English-subtitle intro plays. Escape continues to the headquarters scene; a second Escape reaches Start4 gameplay. | `../../reverse/campaign-agent-input-ledger.json` (`preledger_inputs`); `../../reverse/campaign-pointer-fixed-english-intro.png`, `…-headquarters.png`; `../../reverse/native-gl-campaign-integrated-intro.png` | CONFIRMED |
| Native observed (ISO+patch) | The sequence New Game → Normal → intro → Start4 Loading → English briefing "vest" → travel "veyo" → third-person world was observed. | `CONTRACT.txt:881-890`; `../../reverse/native-global-skin-{intro,briefing,travel,travel-skipped}.png` | CONFIRMED |
| Native | Movies are decoded with FFmpeg (`Media::movie_play`, `native/media.cpp`). Pixel parity with the original Windows codec path has not been checked. | grep | CONFIRMED / parity UNKNOWN |

## 4. Difficulty

- **Original (script).** `YeniMenu.wdl:135 YeniOyunBaslat(_Zorluk)` takes 0 for Normal and 1 for Hard. Enemy health and damage scale by `(OyunZorluk+1)`. See [Gameplay systems §11](../GAMEPLAY_SYSTEMS.md#11-difficulty). **CONFIRMED**
- **Native observed.** The Normal selection screen and the chosen Normal path were captured: `../../reverse/campaign-checkpoint-normal-difficulty.png`, `../../reverse/native-gl-resume2-difficulty.png`. **CONFIRMED**
- **Hard.** No run recorded. **UNKNOWN**

## 5. FIP publisher panel at 1024×768

- **Native observed, ISO-only historical artifact.** The installed engine kept the scripts' authored 4:3 mode and captured a 1024×768 framebuffer showing the FIP bitmap PANEL (`CONTRACT.txt:1776-1782`, `:2103-2104`). **CONFIRMED**
- **Native observed, v0.1.1 and later.** Native desktop borderless display captured the same panel at 2560×1440 in GL, Vulkan raster and Vulkan RT (`CONTRACT.txt:2253-2256`, `:2091-2094`). With the patch, 5120×2880 and 3840×2160 captures were also made (`:1592-1593`, `:1624`). **CONFIRMED**
- **Assumed.** 1024×768 comes from `GA_Cozunurluk` in the shipped `options.k22` (31 bytes in the installed game directory) through `video_switch(GA_Cozunurluk+6,…)`. **HIGH CONFIDENCE**: the script path is confirmed, but the value decoded from that file is not recorded in this repository.

## 6. Frame pacing and time

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (static) | The nominal tick is 1/16 s. The raw quantum is 1/1024 tick, derived from QPC with a `timeGetTime` fallback. TIME is clamped between 51 raw and 16/FPS_MIN, scaled by TIME_FACTOR, and smoothed with TIME_SMOOTH (default 0.5 in this binary). | `CONTRACT.txt:103-123` (§3) | CONFIRMED (static) |
| Original (static) | `WAIT(n<=0)` is a one-frame yield. `freeze_mode` ≥1 lets only MY==NULL records run; ≥2 stops scheduling. | `CONTRACT.txt:71-133` | CONFIRMED (static) |
| Original (static) | `00414650` sleeps and rechecks against FPS_MAX before publishing TIME_FRAME, whether or not FPS_LOCK is set. | `CONTRACT.txt:1526-1528` (§38) | CONFIRMED (static) |
| Original (script) | The campaign sets `fps_max=72` and `fps_min=24`. | `K22.wdl` `main` | CONFIRMED |
| Original observed (Wine) | The campaign runs at the FPS_MAX 72 cap (TIME 0.223 every frame in cam1/kriz). Player run speed (~9.9 quants/tick), turn rate (12.6°/tick) and atilgan contact damage (4·TIME·(OyunZorluk+1) per wing trace) are per tick and equal to native at 60 fps (TIME 0.268); with the same pan, cam1 glide legs end within 0.6 quants of native. Under Wine every key hold reaches the game about one frame long (release latency); the driver compensates. | Per-frame `diag` instrumentation, [GAMEPLAY_TESTING](../GAMEPLAY_TESTING.md#bugs-fixed) fix 18 | CONFIRMED (Wine) |
| Native | `native/main.cpp` sleeps until `floor(1000·1024·1024 / max(FPS_MAX raw,1024))` raw milliseconds after the frame start, without accumulating catch-up debt. `Engine::tick` reproduces the TIME arithmetic. | grep; `CONTRACT.txt:1528-1534` | CONFIRMED |
| Native observed | An asset-free consumer gave 50.049 ms at FPS_MAX 20, 25.024 ms at FPS_MAX 40, and saturated at 1000 ms for zero or negative values. A dynamic change from 20 to 40 was exercised. The packaged build was also checked. | `CONTRACT.txt:1566-1569`, `:1686-1689`, `:2106-2108`; tool `tools/check_frame_pacing.py` | CONFIRMED |
| Not established | Steady campaign FPS and CPU use; comparison with Windows frame timing. | `CONTRACT.txt:1549-1551`, `:1861-1863` | UNKNOWN |

## 7. Audio

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (script) | Positional sound is computed in WDL (`Ses_Sistemi.wdl sterio_ses3d`) and played with `snd_play(vol*Vol_Efekt, pan)`. Music is `snd_loop` with `snd_tune` fades. Default volumes are effects 65, music 90, dialogue 100. | [Gameplay systems §7](../GAMEPLAY_SYSTEMS.md#7-sound-and-music) | CONFIRMED |
| Original (data) | `v_tacikos_kapi.ogg` in `Kaynak.k22` is Ogg/Vorbis wrapped in RIFF/WAVE with format tag 0x674f, not a bare Ogg stream. | `CONTRACT.txt:1365-1371`; `../../reverse/decompiled/runtime/campaign-media/riff-vorbis/` | CONFIRMED (bytes) |
| Native observed | Before the fix, Level4 stopped at `cinematic.wdl:848:23` with "decoder unavailable". After the fix to the shared RIFF/Ogg ingress (`media_payload`), the replay passed this point. | `CONTRACT.txt:1360-1364`, `:1381-1393`, `:1480` | CONFIRMED |
| Native observed | The game's PipeWire stream was at 100%, unmuted, and produced nonzero PCM: 49152 frames, peak 32332, RMS 9587. An earlier "no sound" report was traced to a persisted 4% per-application volume, not to engine behavior. | `CONTRACT.txt:2326-2331`, `:2394-2395` | CONFIRMED |
| Not established | Original Windows mixer levels and 3D pan; every sound event. | — | UNKNOWN |

## 8. Input

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (static) | Keyboard state uses scancode = `(lParam>>16)&0x7f`. A down edge fires only when the key goes from up to held, so auto-repeat is ignored. A short tap is published as 1024 for one frame. Joystick codes are 256-279; mouse codes are 280-282. | `CONTRACT.txt:433-449` (§12) | CONFIRMED (static) |
| Original (static) | Embedded `engine_internal.wdl` binds F2 save, F3 load, F5 video, F6 screenshot, F10/ESC/CLOSE exit, F12 sound toggle and ENTER screen toggle. Game scripts may replace these bindings. | `CONTRACT.txt:468-470`; `../../reverse/extracted/engine_internal.wdl` | CONFIRMED (static) |
| Original (script) | Keys can be remapped through `GA_Tuslar`. The default map key is slot 8, scancode 35 (H). | `GenelAyarlar.wdl:37,50,115` | CONFIRMED; H mapping HIGH CONFIDENCE |
| Native observed | Holding H set native `Input` bit 11, then `KEY_H`=`Key_Harita`=1024 one frame later, and the original map opened (`h_menu_aktifmi`=1, `freeze_mode`=1). | `../../reverse/native-gl-input-debug-held-map.png`; `CONTRACT.txt:926-931` | CONFIRMED |
| Native observed | The map closes on Escape, as in the original loop `yeni_harita.wdl:2343-2359`, not on a second H press. M opens the inventory. Enter confirmations take effect after the key is released (`Envantermenu.wdl:177-180`). | `../../reverse/native-gl-resume2-map-escape.png`, `…-map-closed.png`, `../../reverse/native-gl-campaign-integrated-inventory.png`; `CONTRACT.txt:898-906`, `:996-1001` | CONFIRMED |
| Native observed | Held A+Up drove movement through `key_ileri`/`key_kosma`. | `CONTRACT.txt:2312-2321` | CONFIRMED |
| Test-method limit | Short synthetic key taps (xdotool `--delay`) often missed the original held/release loops. This is not evidence of an input defect. | `CONTRACT.txt:892`, `:904-906`, `:936-937` | CONFIRMED (methodology) |
| Original (static) | Joystick polling `00422000` (`ev_f91758961be5402cfccd9f4a5eff19d358738d56a3a07a9dd4273842401e3008`; keyword table: `JOY_FORCE` → `0x665d98`, `JOY_RAW` → `0x665864`, `JOY_ROT` → `0x665850`, `JOY_SENSE` → `0x665d60`, default 1). `JOY_RAW`/`JOY_ROT` axes are `(pos·255)/range − 255`, i.e. −255…255. `JOY_FORCE.x` = trunc(dz(raw)·`JOY_SENSE`·0.02f) (constant `0x5d63ac` = 0.02f), `.y` the same negated, with a dead zone dz of 32 (raw − 32 sign-wise, 0 inside), then clamped to ±2·`JOY_SENSE`. `.z` is never written. A full stick gives 2.0, so `Joy_Takip`'s `JOY_Ileri = joy_force.y/2` (`GenelAyarlar.wdl:75`) is exactly 1 and `Key_Ileri==1` walks; below about 52% deflection (raw < 132) it is fractional and does not. | REA ID at left; `K22.exe` keyword table at file offset `0x1d5500` | CONFIRMED (static) |
| Native observed | Live virtual pad, original mode: half stick (raw −102) gives `JOY_Ileri` 0.699 and no movement; full stick gives `JOY_Ileri` 1 and walks within 3 frames (fix 24). | `tools/virtual-gamepad/live/original.k22s`; `check_engine --input-saves` | CONFIRMED |

## 9. Level transitions

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (static) | Level transition `00434670` stops level slots, tears down the old level and constructs the new one. Removing an entity cancels processes whose MY is that entity and clears YOU where it matches (`00431920` → `00410550`). Registered pointer cells equal to a removed entity are set to NULL (`0041f050`). New entity actions run in the next scheduler phase. STARTERs are not rerun. | `CONTRACT.txt:510-520`, `:1139-1147` | CONFIRMED (static) |
| Native observed (ISO+patch) | Walked transitions: Start4 → Start5 → Start3 → Start5 → Start4 → SD → Level4 → NStart7 → Mekke → Start2. | `../../reverse/campaign-checkpoint-replay-input-ledger.json` (`route`); images `../../reverse/campaign-checkpoint-replay-*-{arrival,authentic-transition}.png` | CONFIRMED |
| Native observed, earlier defect | SD → Start4 return failed at patched `cam.wdl:499:7` ("invalid WDL object handle"): `Strt4_cam_kontrol` read `player.y` from a stale global. After the pointer-cleanup fix, Start4 → SD → Start4 succeeded and the returned player moved. | `../../reverse/campaign-source-pointer-before-unwind.txt`; `../../reverse/campaign-pointer-fixed-Start4-return-interactive.png`; `CONTRACT.txt:919-924`, `:1201-1222` | CONFIRMED |
| Native observed | The replay ended in Start2 with an external SIGKILL of unknown cause: no C++ throw and no OOM record. | `../../reverse/campaign-checkpoint-stop-evidence.json` | CONFIRMED; cause UNKNOWN |
| Not established | The other roughly 93 levels and the ending. | — | UNKNOWN |

## 10. Saves

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (script) | Saves are written by script: index `kysd.sav` and slot `kayit<N>.sav`, using `file_asc_write`/`file_var_*`. Save points are the actions `save_tga` and `otosave_fish`. The slot file does not store the player's XYZ, so loading respawns at the level entry. | `YukleMenu.wdl`, `Yeni_Oyun_Deger.wdl`; `CONTRACT.txt:1880-1882` | CONFIRMED (script) |
| Original (data) | Settings live in a 31-byte `options.k22` in the game directory. The patch language protocol extends the record to 32 bytes. | `~/.local/share/kabus22/game/options.k22` is 31 bytes; comment in `Engine::configure_language` | CONFIRMED |
| Native observed | The original Save/Load menus, writer, reader, world and actor birth ran through the SourceAPI consumer and exited 0 in 22.7 s. Start6 entry 0 keeps the authored birth position. | `CONTRACT.txt:1682-1685`, `:1878-1900`, `:1997-2002` | CONFIRMED |
| Native observed | No walked run reached a save point. The replays wrote only `options.k22`. | `../../reverse/campaign-checkpoint-stop-evidence.json` (`private_save_files`); `CONTRACT.txt:1377` | CONFIRMED (absence) |
| Native design | All file writes are confined to `$XDG_DATA_HOME/kabus22` (`Engine::Engine`, `svc_private`). | grep | CONFIRMED |
| Not established | Walked save → reload → continue. | — | UNKNOWN |

## 11. Bonus mode (A7)

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (static) | `PLAYER_NAME` is a live STRING over 32 zero bytes (A7 `101f64b0`). FILTER is PANEL/TEXT mask 0x100000. A NULL fourth argument to `effect` means zero velocity. Static ENTITY partners carrying flag 0x200 are rejected before ODE contact (`1007bc40`). MDL UV 0xffff is widened to 0xffffffff (`101589eb`). | `CONTRACT.txt:1172-1188`, `:1318-1336`, `:1253-1256` | CONFIRMED (static) |
| Native observed | The genuine Bonus menu ran for 600 frames, showing Demolition Day art, NEW GAME/CREDITS/EXIT, the software cursor and "No connection". | `../../reverse/native-gl-bonus-predefined-entry.png`; `CONTRACT.txt:1190-1199` | CONFIRMED |
| Native observed | New Game → loadout → Ready/Go → turret moved with A/D and mouse aim; both guns fired, ammunition 950 → 868; the authored timer ran to single digits with no LCP warning. | `../../reverse/native-gl-bonus-1b-gameplay-evidence.txt`, `…-1b-*.png`, `…-1b-live-gameplay.mp4`, `…-1b-straight-live.mp4`; `CONTRACT.txt:1477-1479` | CONFIRMED |
| Native observed, earlier defects | Failures that occurred before their fixes: `atom_kutu.MDL` missing UV; `silah_mode.wdl:1725` NULL effect; ODE LCP stops. | `../../reverse/native-gl-bonus-gameplay-evidence.txt`, `../../reverse/native-gl-bonus-53-continuous-evidence.txt`; `CONTRACT.txt:1232-1237`, `:1426-1431` | CONFIRMED |
| Not established | Correct settling of every crate; a complete Bonus session; parity with the original solver. | `CONTRACT.txt:1353-1356`, `:1401-1405` | UNKNOWN |

## 12. Bitmap-font palette

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (data) | `Font0.pcx` (from `yazi_en.k22`) is a 352×144 indexed-8 PCX that uses only indices 0 and 255. Its embedded palette is entirely black. | `CONTRACT.txt:2181-2184` | CONFIRMED (bytes) |
| Original (static) | FONT keeps the indices and uses the active palette (`007192bc`). Palette selection tries `<startup basename>.raw`, then `palette.raw`, then an RGB332 fallback (`00455240`). Normalization (`004550e0`) sets entry 0 to black, lifts dark entries to (8,8,8) and makes entry 255 white. | `CONTRACT.txt:2194-2204` | CONFIRMED (static) |
| Native observed, before the fix | 304 Font0 draw commands rendered with no glyph ink on the 8 save rows. TrueType and bitmap labels rendered normally. | `CONTRACT.txt:2174-2179` | CONFIRMED |
| Native observed, after the fix | `Engine::ensure_font_palette` follows the same order. All 8 Font0 rows contribute glyph pixels in GL, Vulkan raster and Vulkan RT at 2560×1377. | `CONTRACT.txt:2222-2223`, `:2229-2230`; grep `native/engine.cpp` | CONFIRMED |

## 13. Widescreen

| | Statement | Evidence | Label |
|---|---|---|---|
| Original (static) | The projection code is byte-identical between the original and the community patch. Exclusive fullscreen uses `Q=(H·320)/(W·240)`; windowed uses `Q=1`. The mode cap is 1920×1200 in the original and 3840×2400 in the patch. | `CONTRACT.txt:2147-2163` | CONFIRMED (static) |
| Original (script) | Only 4:3 and 5:4 presets exist (`GA_Cozunurlukler`). Patch widescreen is implemented in script (resolution and menu/HUD/text geometry). | `GenelAyarlar.wdl:3`; `CONTRACT.txt:2167-2169` | CONFIRMED |
| Native observed | v0.1.1 runs native borderless at desktop size with the recovered geometry and original ISO art, in three backends. The 600-frame startup at 2560×1440 was captured. | `CONTRACT.txt:2214-2256` | CONFIRMED |
| Not claimed | A Hor+ projection, or exact restyling to match the patch. | `CONTRACT.txt:2161-2163`; `README.md` | CONFIRMED (explicit non-claim) |

See [Camera system](../CAMERA_SYSTEM.md) for the projection formula.

## 14. Physics and collision behavior in the campaign

- **Native observed.** In Start4, held A+Up stopped against a corpse entity. Against a wall with normal (0,-1,0) the player glided. At a two-plane corner the velocity dropped to zero with no passthrough, and Down escaped the corner (`CONTRACT.txt:2312-2324`). **CONFIRMED**
- **Original A6.** The `c_trace USE_BOX` input preparation was recovered from `00472030` and `00470a50` (`CONTRACT.txt:1988-1993`). A static replay of the legacy BSP reproduces a Start6 trace result of raw -1024 with Normal 0 (`:1999-2002`). **CONFIRMED (static)**. Bit-level parity of the oriented solver is not claimed.

## 15. WMB entity flags, model lighting and draw state

REA evidence IDs are from `rea function` on the ISO `K22.exe` (Ghidra provider, snapshot `reverse/rea/K22-iso.snapshot`, outputs `reverse/rea/fn-<address>.json`, private).

| | Statement | Evidence | Label |
|---|---|---|---|
| Original observed (Wine) | level5 (`kayit0.sav`): a diagnostic starter in a private, size-identical `K22.wrs` logged every entity. Action-less WMB entities carry exactly the flags stored in their record (legacy type 3 too); no entity had a NULL MATERIAL (plain MDLs `mat_model`, UNLIT `ampul.mdl` `mat_unlit`, sprites `mat_sprite`). Demir is lit by his floor sample; the lockers above a dark floor are dark; the wooden chair `FFR9.MDL` stands in front of the whiteboard. | `../../reverse/wine-reference/parity-lighting/`; [GAMEPLAY_TESTING](../GAMEPLAY_TESTING.md#bugs-fixed) 19 | CONFIRMED (dynamic, Wine/wined3d) |
| Original observed (Wine) | After the gamma correction (`--ref-gamma 30`) native matches the original within 1–2% mean on Demir (level5 shirt/legs, Start4 torso) and the level5 lockers/whiteboard; Ebru (`ev`) and Inzar (`tnel24`) match. | `../../reverse/wine-reference/set/` vs native captures; [GAMEPLAY_TESTING](../GAMEPLAY_TESTING.md#reference-comparison-original-under-wine) | CONFIRMED (dynamic) |
| Original observed (Wine) | Start4's `telbasla.tga` fence sprites (32-bit TGA, `sabit_tga`, no TRANSPARENT) show thin wires with the vans behind them. `ev`'s `lalam.mdl` wall lamp (flags PASSABLE\|BRIGHT) is drawn opaque with its dark cap. | `set/newgame/start4_control.png`, `set/ev/ev_loaded.png` | CONFIRMED (dynamic) |
| Original (static) | `00459eb0` copies type-3 and type-7 entity flags verbatim. | `ev_c3ef2a5d908f692fd0cae3cc99d605d5de96250669fd90df4c9dbe098f269146` | CONFIRMED (static) |
| Original (static) | `0042fa20` (`ev_db63dc891de4f0a46236385d61305cba312c2a4fe59bbba933a46321f9a57479`), called from `00430920` and `0044f970` (`ev_3f79be3e4ecaf3ed04f6e3cc031329e8d44ca2efda6785dbb328f162582f591b`), gives a NULL ENTITY.MATERIAL a persistent built-in: METAL → `mat_metal`, UNLIT → `mat_unlit`, HMP → `mat_terrain`, sprite → `mat_sprite`, MDL → `mat_model` (`00415ad0`: `mat_model` ambient 200, diffuse 255, albedo 50; `mat_unlit` emissive 128). | REA IDs at left | CONFIRMED (static) |
| Original (static) | Hardware MDL draws `0044ad00` → `00442770` → `0043ee40` (`ev_5d63209d2b1901349bd2510d51f5e9f8768d82982c2ae4cf1c28120704aa447f`) set `D3DRS_AMBIENT` from AMBIENT + VIEW ambient + the floor sample (`004370c0`/`00437190`), the albedo-scaled sun (`00437630`, `ev_beccbe88c3bb9efce9f4f43faaaa3a84b2816f130477a40f89ba8f13968cacbf`) and MODULATE2X. | REA IDs at left | CONFIRMED (static) |
| Original (static) | The sun's `D3DLIGHT9.Direction` is `0 − unit(SUN_ANGLE)` (`00437800`, `ev_846e2c5191b7564d3510cabfd506edaaf61e7a845f4e7feed4e0d5b335d7011f`; stored by `00438470`, `ev_8a6906e9bfc103e74ea4173983d3ce983c8868749c5219d9098b6862e0ea454d`): the light travels away from the sun. | REA IDs at left | CONFIRMED (static + Wine measurement) |
| Original (static) | Models (`0043ee40`) and world sprites (`00443350`, `ev_750567c7cd6d8bbb028586e1452e35c0bc8a1742bce5e591276d801fbff94177`) enable alpha blending only for FLARE (0x8000), TRANSPARENT (0x400) or a view opacity below 255 (`0043b040`, `ev_a15798587dc8a6593d2d8ce469e9207eef08099053dc52ab8fd261312a16085d`); BRIGHT alone draws opaque. The model alpha test is on only for OVERLAY without BMAP flag 0x100. | REA IDs at left | CONFIRMED (static) |
| Original (static) | `0040e2b0` sets BMAP flag 0x100 for 32-bit TGA files; `0043bb10` (`ev_ee8924291186e41a972d59d2ab0f2f693628bc9873be3bc7bae0bfb68779449f`) creates such textures with format flag 0x20 instead of the OVERLAY key format 0x200. VIEW sprites (`00440e90`, `ev_1b8eee2c77a2273c206b248c343beb864f008f3919c1b269952a36f5d1abf9da`) blend a 0x100 bitmap. | `../../reverse/decompiled/runtime/font-layout/K22.exe.0040e2b0.c`; REA IDs | CONFIRMED (static) |
| Unknown | The D3D state `00443350` uses for a world sprite with a 0x100 bitmap and no TRANSPARENT: the decompile shows neither blending nor alpha test, yet the Wine capture shows the sprite transparent. Native blends it (matches the capture). | as above | UNKNOWN |
| Original observed (Wine) | cam1 (`kayit1.sav`): the `gazel1.tga` leaf decals (32-bit TGA, FLARE\|OVERLAY, tilt 90, no MATERIAL) are not visible at load, and the `k22_save.tga` floor circle (32-bit TGA, OVERLAY, no TRANSPARENT) is drawn round, not as a square, and matches native within about 1 level of mean RGB (native 25.1/24.8/20.2, original 25.3/25.1/20.4 at gamma 30). Native before fix 20 drew the leaves bright yellow and the circle redder (R 27.4). | `set/cam1/cam1_loaded.png` | CONFIRMED (dynamic) |
| Original (static) | World sprites in `00443350` are pre-transformed (FVF `0x2c4` = XYZRHW\|DIFFUSE\|SPECULAR\|TEX2), so no D3D lighting or MATERIAL applies to them. Their diffuse is `00437190`(clamp(`004370c0`() + 64, 0, 255)) (`ev_5a9c8381a3af7629e91895377a51aefa2d8c9bcd8f3d968976f9933336e056d5`, `ev_d7f644c4b58326907d9876ef6c0a1474b2686dadeb21719e4834b96ae6202133`): AMBIENT + VIEW ambient + floor scalar + 64 plus the `00438030` channel deltas, dynamic lights and LIGHT tint, with stage-0 COLOROP MODULATE (4); UNLIT sprites use base 0 with MODULATE2X (5). Native draws sprites through the default `mat_sprite` material instead. A prototype of the literal formula was further from the Wine capture than the current path on all four cam1 decal regions, and the UNLIT branch (base 0) does not explain the visible UNLIT lamp glows, so the effective sprite gain is not resolved; native was left unchanged. | REA IDs at left; `../../reverse/decompiled/runtime/entity-ambient-source/contract.txt` | CONFIRMED (static) / UNKNOWN (gain) |
| Original (static) | Floor-light miss: `00437b90` resets the cache to 127/50/0 (`00434700`) when the legacy trace from the entity origin down `FLOOR_RANGE` (100, `K22.wdl`) hits nothing. The level5 ceiling fan (`tavanpervane.mdl`, z 51, AMBIENT −150) traces to z −49, above the floor at about −67, and misses natively too (`floor_dist` −1), so its ambient term is clamp(−150) + 127 = 0 on both sides. The kriz boat (`tekne_loop.mdl`, AMBIENT −11) has a valid native sample (distance 86.3, TEX_LIGHT 51, B/G/R 53/56/45). Both stay darker than the original (fan hub mean 52.7 against 59.8); the floor sample is not the cause. Remaining candidates are the sun/dynamic-light terms and, for the boat, which surface the legacy trace hits under it. | `../../reverse/decompiled/runtime/bonus-core/entity-light-cache/a6/contract.txt` §5; native `list near` `floor_dist` | CONFIRMED (static) / UNKNOWN (residual) |
| Native | Follows the rows above (GAMEPLAY_TESTING fixes 19–23), except that world sprites take the `mat_sprite` material path rather than the `00443350` pre-lit colour (see the sprite row); shared by OpenGL, Vulkan raster and RT through the engine-prepared frame. | `native/engine.cpp make_frame`; `check_engine` (includes the cam1 leaf-decal case), `check_assets` | CONFIRMED |

## Gaps summary

| Area | Status |
|---|---|
| Execution of original `K22.exe` or of the A7 runtime on real Windows/Direct3D 9 | Not performed. The ISO `K22.exe` runs under Wine/wined3d as a reference (rows "Original observed (Wine)"): startup, menus and six in-game cases; the A7 runtime was not run. Pixel parity with real Direct3D 9 is unknown. |
| Full campaign playthrough, ending, Ebru and Inzar sections | Not established |
| Walked save and reload | Not established |
| Hard difficulty, puzzles, enemy combat outcomes | Not established |
| Full Bonus session and crate physics acceptance | Not established |
| Steady-state CPU and FPS on older CPUs | Not established (`README.md`) |
| Nondefault stencil, software world light, VIEW render-to-bitmap | Unsupported in the native port (`README.md`; `CONTRACT.txt:1850-1852`) |
