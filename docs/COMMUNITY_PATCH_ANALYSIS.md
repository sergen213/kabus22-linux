# Community patch analysis (iseeeva "Bug Fix and Widescreen Support", v1.8.5)

Purpose: describe what the community patch and its author's tool repository contain, what changes they make to the original Kabus 22 ISO edition, and how this relates to the native Linux engine.

Labels: **CONFIRMED** (byte/hash comparison, source text or run output), **HIGH CONFIDENCE** (static reasoning over confirmed facts), **HYPOTHESIS**, **UNKNOWN**.

Related: [PATCH_BINARY_DIFF](PATCH_BINARY_DIFF.md) · [WIDESCREEN_IMPLEMENTATION](WIDESCREEN_IMPLEMENTATION.md) · [BUG_FIX_PORTING](BUG_FIX_PORTING.md) · [ACKNEX_A631](ACKNEX_A631.md) · [WRS_FORMAT](WRS_FORMAT.md) · [COMMUNITY_CREDITS](COMMUNITY_CREDITS.md) · [REVERSE_ENGINEERING](REVERSE_ENGINEERING.md) · [reverse-engineering/EXECUTABLE_ANALYSIS](reverse-engineering/EXECUTABLE_ANALYSIS.md)

Evidence lives in the private working tree `reverse/community/` (git-ignored; holds proprietary material and is never published). Paths below are relative to the repository root.

## 1. Targets

| ID | Target | Location | Identity | Label |
|---|---|---|---|---|
| A | Original ISO edition | `Kabus-22_Win_TR-EN-DE_ISO-Version/Kabus 22.iso` → `JPN-K22.exe` (Inno Setup) extracted with `innoextract 1.9` into a temporary dir (deleted after inventory) | 149 files, 2 347 708 649 bytes; `K22.exe` SHA-256 `cc193c6b…4ebd`, `K22.wrs` `c9e32008…c5c5` | CONFIRMED |
| A′ | Native install of A | `~/.local/share/kabus22/game` (read only) | 137 files; `K22.wrs` byte-identical to A | CONFIRMED |
| B | Community patch v1.8.5 | `Kabus_22_Multi_Version_Patch_1.8.5.7z` (MD5 `bf273ed8…2334` = ModDB published MD5; SHA-256 `9ddb5186…3670`) extracted to `reverse/community/patch-1.8.5/` | 438 files, 1 070 097 081 bytes | CONFIRMED |
| B′ | `Kabus22Patch/` | repo root | all 438 files byte-identical to B | CONFIRMED |
| B″ | Lutris Windows install | `/mnt/1st4TB/Lutris Games/kabus22/drive_c/Program Files (x86)/Merscom/Kabus 22/` (read only) | 817 files: ISO files + B overlay + leftovers of an older mod release (`_data/languages/…`, `turkce.k22`, `rmo/rms.k22`, `cutscene_files/`) + ReShade; its `K22.wrs` (`5bae0b5e…`) is **not** the ISO archive | CONFIRMED |
| C | Author's repository | `https://github.com/iseeeva/kabus22`, cloned to `reverse/community/iseeeva-kabus22/` at `8d7ac45b9474a2d30c5612d57d101ff76bbba7f1` (2025-12-24, single commit) | 47 files | CONFIRMED |

Inventories (path, size, SHA-256): `reverse/community/reports/inventory-{A-iso-setup,A-installed-native-game,B-patch-1.8.5-7z,B-Kabus22Patch-dir,B-lutris-install,C-iseeeva-repo}.tsv`; A↔B comparison with B′/B″ columns: `reports/inventory-compare.tsv`; generator: `reverse/community/analyze.py`; summary: `reports/summary.json`.

## 2. What the author's repository is (target C)

| Path | Kind | Content | Label |
|---|---|---|---|
| `kabus_tools/` | **Author source** (TypeScript, Node) | WRS export via bundled `quickbms.exe` + `gamestudio.bms`; WRS import (pack) re-implemented in TS, compression delegated to `quickbms` + `lzssBig.bms`. See [WRS_FORMAT](WRS_FORMAT.md) §5 | CONFIRMED |
| `kabus_tools/src/external/quickbms.exe`, `*.bms` | **Third-party binary/scripts** (Luigi Auriemma's QuickBMS) | not author code | CONFIRMED |
| `kabus_vs/Kabus.Acknex.Plugin/Index.hpp`, `Utils/Common/*.hpp`, `Utils/Ini/Ini.hpp`, `Utils/Discord/Rpc.hpp` | **Author source** (C++17, MSVC v143, Win32 DLL) | exactly the 8 exported functions of the shipped `kabus_dll.dll`: `DiscordRPC_TryConnect/IsConnected/TryDisconnect/SetStateDetails`, `INI_IsInitialized/GetVariable/SetVariable`, `Parse_EscapeChar` | CONFIRMED (export table of B `kabus_dll.dll` = these 8 + SDK `A6_DLLAPI`; PDB path `…\Tools\kabus_vs\Debug\Kabus.Acknex.Plugin.pdb`) |
| `kabus_vs/…/Acknex/include/*.h`, `Acknex/lib/adll.lib` | **Conitec A6 plugin SDK** (headers + import library) | see [ACKNEX_A631](ACKNEX_A631.md) | CONFIRMED |
| `kabus_vs/…/Utils/Ini/include/ini.hpp` | third party, MIT (Borovik Alexey 2015) | INI parser | CONFIRMED |
| `kabus_vs/…/Utils/Discord/include/*.h`, `lib/discord-rpc.lib` | third party (Discord `discord-rpc`) | RPC client | CONFIRMED |
| `kabus_vs/Kabus.Acknex.Plugin.vcxproj` | build config | references `kabus_lib\resolution_funcs.h`, which is **absent** from the repo; no resolution export exists in the shipped DLL | CONFIRMED (missing file); purpose UNKNOWN |
| `exe/K22.exe`, `K22_4GB.exe`, `K22_NoWRS.exe`, `K22_NoWRS_4GB.exe` | **Binary-patched Conitec/original executables** | see [PATCH_BINARY_DIFF](PATCH_BINARY_DIFF.md) | CONFIRMED |
| `oyun_icin_fikirler/` | ideas: `fps_mode_cam/cam_code.wdl` (prototype camera), `k22_cam.cfg`, `demir_fps.mdl` (25 MB model), `cooptest.png` | not used by v1.8.5 | CONFIRMED (no reference in B) |

Licence: the repository has **no LICENSE file** and GitHub reports `"license": null` (API `repos/iseeeva/kabus22`, 2026-10-09). Default copyright applies; see [COMMUNITY_CREDITS](COMMUNITY_CREDITS.md). **CONFIRMED.**

Not in C: the patch's WDL scripts, translations, fonts, textures and videos. Patch content (B) is distributed only as the ModDB archive. **CONFIRMED.**

## 3. What the patch contains (target B)

| Item | Nature | Label |
|---|---|---|
| `K22.exe`, `K22_4GB.exe` | original A6 engine executable with 6 code bytes + 19 data bytes changed and an icon resource added | CONFIRMED |
| `K22Widescreen.wrs` (190 members) | complete replacement script set: 178 of the 182 ISO `K22.wrs` members (132 byte-identical, 46 modified), 10 new WDL, 2 new images (`kan_1366.tga`, `LY_Fade.bmp`) | CONFIRMED (`reports/wrs-diff-original-vs-patch1.8.5.tsv`) |
| `kabus_dll.dll` | author plugin (source in C), Debug build, VS 2022 (linker 14.43) | CONFIRMED |
| `kabus_rpc.dll` | Discord `discord-rpc` 2018 build | CONFIRMED |
| `K22.ini` | `[Screen] Mode/X/Y` (defaults `1/0/0`) | CONFIRMED |
| `_data/language/<lang>/{subtitle,sound,texture,video}` × 6 languages + `_resource` fonts | loose language packs | CONFIRMED |
| `_data/video/{kn_arg,vkt}.k22`, `_data/saves/` | relocated media and save directory | CONFIRMED |

Original material inside the patch (byte-identical to ISO data): 12 English and 12 Turkish voice clips (= members of `ses_en.k22`/`ses_tr.k22`), 9 item-text files (= `nti/bti1/bti2/nei/bei1/bei2/ndi/bdi1/bdi2.k22` renamed to `.txt`), `kn_arg.k22`, and 142 of 260 language images (= members of `yazi_{tr,en,de}.k22`). Re-encoded cutscene videos have different sizes from the ISO originals. **CONFIRMED** (hash matching in this analysis). Consequence for redistribution: [COMMUNITY_CREDITS](COMMUNITY_CREDITS.md) §3.

The German and Spanish `sound/` folders are byte-identical to the English folder. **CONFIRMED.**

## 4. Script-level change summary (A `K22.wrs` vs B `K22Widescreen.wrs`)

Totals: identical 132, changed 46, added 12, removed 4 (`k22.wdl`, `dil_ceviri_en.wdl`, `dil_ceviri_de.wdl`, `sub_titles_en.wdl`). **CONFIRMED.** Per-member size, SHA-256 and added/removed line counts: `reports/wrs-diff-original-vs-patch1.8.5.tsv`; unified diffs (private, contain proprietary text): `reverse/community/diffs/*.diff`.

Change families (details and native status: [BUG_FIX_PORTING](BUG_FIX_PORTING.md)):

| Family | Members (examples) | Label |
|---|---|---|
| New entry point `K22Widescreen.wdl` replacing `K22.wdl`: DLL load, `K22.ini` display set-up `dbg_screen_reset(0..3)`, run-time HUD/menu scale switching | `K22Widescreen.wdl` | CONFIRMED |
| Resolution-independent UI scale families `GA_YeniScaleOranlari{,2,3}` (W/1366·H/768, W/1024·H/768, W/640·H/480) | `GenelAyarlar`, `Kabusdismenu`, `YeniMenu`, `Envantermenu`, `Smartmenu`, `cinematic`, `kotu_koruyucu`, `LevelYonetim`, `BUL_Kordinat` | CONFIRMED |
| Run-time language index `DD_Dil` (0 TR, 1 EN, 2 DE, 3 ES, 4 RU, 5 PL) replacing `GA_Dil` indexing in ~20 menu/image scripts; language menu re-enabled | `AyarlarMenu`, `DilMenu`, `dil_degistir`, `DMI_*`, `*MenuImage` | CONFIRMED |
| Language-specific voice clips `voice_variables[DD_Dil*12+k]` | `BUL_Salter`, `LevelYonetim`, `Ornek_Yasin`, `boss`, `koruyucu`, `kotu_koruyucu` | CONFIRMED |
| FPS (close over-shoulder) camera on F5 for Demir/Ebru, guards on legacy camera writers | `cam.wdl`, `GenelAyarlar` | CONFIRMED |
| Debug/cheat keys F1 (overlay), F6 (all weapons), F7 (health/mana) | `k22_dbg.wdl` | CONFIRMED |
| Discord Rich Presence | `discord.wdl` | CONFIRMED |
| Saves moved to `_data\saves\`, video paths to `_data\video\` | `YukleMenu`, `Yeni_Oyun_Deger`, `korku_sahneleri`, `Kabusdismenu` | CONFIRMED |
| Blood-burst particle size `300` → `kan_blood_scale` (always `0`) | `efect2` + 10 enemy scripts | CONFIRMED |
| Weapon-preview view entity Y/Z offsets dependent on `K22.ini` `Screen:Mode` | `silah`, `cinematic` | CONFIRMED |
| Exit-menu highlight-frame formula, credits text height, joystick in load menu, smart-menu fire-key repeat | `CikisMenu`, `CreditsMenu`, `YukleMenu`, `Smartmenu` | CONFIRMED |

Script identifier census (counts per file; no source text): `reports/script-identifier-counts.json`. `camera.arc` writes 155 → 156 (the single new one is the FPS camera `arc=70`); `camera.aspect` usage unchanged (2, mirror only); `video_switch` 3 → 0; `video_set` 0 → 4; `sys_metrics` 0 → 2. **CONFIRMED.**

## 5. Configuration

| File | Original (A) | Patch (B) | Label |
|---|---|---|---|
| `options.k22` | 31 one-byte values: 7 graphics, 2 blood, brightness+100, contrast+80, gamma+100, 3 volumes, 10 keys, 6 joystick buttons (`GenelAyarlar.wdl` `GA_AyarlariKaydet`) | same 31 + byte 32 = `DD_Dil` (0..5) | CONFIRMED (scripts; ISO file 31 bytes, Lutris file 32 bytes with byte 32 = 1) |
| `K22.ini` | absent | `[Screen] Mode` 0 windowed/1 fullscreen/2 borderless, `X`, `Y` (0 = desktop size); validated by `kabus_dll.dll` (`Ini.hpp`: digits only, ranges 0..2, 0..3840, 0..2160, otherwise reset to 0 and rewritten) | CONFIRMED |

`K22.ini`'s comment states an engine limit of 1920×1200; the repo `exe/readme.txt` states the patch raised "1920×1080 to 3840×2160". The actual table in the executable changes 1920×1200 → 3840×2400 ([PATCH_BINARY_DIFF](PATCH_BINARY_DIFF.md) §2). **CONFIRMED.** The readme is inaccurate; the INI comment matches the original value.

Native mapping: `Engine::configure_language` accepts 31/32-byte records and appends/updates byte 32 (`native/engine.cpp:670-686`); `K22.ini` is parsed and normalized by `svc_ini_parse/svc_ini_load/svc_ini_normalize` into the private save root (`native/engine.cpp:1111-1138`). **CONFIRMED.** (Line numbers: working tree of 2026-10-09; other work is editing these files, so search by function name if they drift.)

## 6. Reconciliation with earlier recovered knowledge

| Earlier statement | Result of this analysis | Label |
|---|---|---|
| `CONTRACT.txt` §39 (second, line 2139): patch `.text` differs in six startup/archive bytes; `.rdata` identical; cap 1920×1200 → 3840×2400; projection path unchanged | Reproduced byte-for-byte; the six bytes are now individually explained ([PATCH_BINARY_DIFF](PATCH_BINARY_DIFF.md) §2) | CONFIRMED |
| §39: "scripts, not a new projection routine, provide resolution/menu/HUD/text geometry" | Confirmed; also no `camera.arc`/`aspect` widescreen assignment | CONFIRMED |
| §41 (line 2208): native ships no patch scripts/art/DLLs, no invented Hor+ | Matches native source (`display_aspect`, `prepare_display`) | CONFIRMED |
| `reverse/extracted/patch_analysis.json`, `reverse/extracted/original/` used as "original" | That baseline was extracted from the **Lutris** `K22.wrs`, which differs from the ISO archive in 6 members and lacks 3 (`reports/wrs-diff-original-vs-lutrisK22wrs.tsv`). Its "45/46 changed" counts coincide with the ISO-based count (46) because the Lutris-modified members are also modified by B | CONFIRMED |
| `docs/reverse-engineering/EXECUTABLE_ANALYSIS.md` `.rsrc` "icons/manifest (HYPOTHESIS)" | `.rsrc` holds 6 `RT_ICON` + `RT_GROUP_ICON "KABUS22"`, no manifest/version | CONFIRMED |

## 7. Findings that imply native work (status updated 2026-10-09)

Each item: evidence → consequence. When this analysis was written, no `native/` code had been changed. Since then, items 1 (exit frame), 2 (archive order), 3 (ES/RU/PL CLI), 5 (modern-controller load list) and 6 (1024 art tier) have been fixed in native. Item 4 (Hor+) is deliberately not implemented. Current status: [BUG_FIX_PORTING](BUG_FIX_PORTING.md) §5.

1. **Exit-confirmation highlight frame is misplaced in native widescreen.** Original `CikisMenu.wdl:47` uses the absolute bottom `480*GA_ScaleOranlari[y]-DM_AltPay`, valid only when the scale table maps 480 lines to the full screen height (original 4:3 tables). Native feeds `H/720` (`Engine::prepare_display`, `native/engine.cpp:753`), so at 1920×1080 the target rectangle becomes y1 = `CMP_Soru.pos_y` = 1080−48−16−150 = 866 (`Cer_Kenar`=0) > y2 = 480·1.5−16 = 704 and renders as an empty box above the question. Observed at 1920×1080 and 2560×1080: `reverse/community/aspect/exitmenu-2300-xvfb-{1920x1080,2560x1080}.png`. The patch fixes it (`CMP_Soru.pos_y + 480*sy`, `DM_AltPay=0`). Native `prepare_display` re-anchors `Cer_*` only on resize (`native/engine.cpp:816-824`). **HIGH CONFIDENCE** (capture + arithmetic). Suggested fix: in native display mode, set the exit-menu `Cer_hY2` to the screen bottom minus the original 16-pixel margin, as the 4:3 original produces.
2. **Archive member precedence: corrected reading.** The original lookup (`K22.exe` `00420120`) for ordinary assets checks, in order: the in-memory buffer cache (table `00695eb0`), the loose file, the `PATH` directories, and finally the archive table `00665ea8`, where the **first** match wins (`0041fd20` appends members without de-duplicating). An earlier version of this item said "archives first, loose afterwards"; it mistook the memory-cache table for the archive index. `.WDL` scripts are read from archives only when flag `0062119c` is set (it is in both the ISO executable, 0x70aa, and the patch executable, 0xdf02). Native was already loose-first, but made the **last** mounted archive win. It is now first-mounted-wins, and `native/main.cpp` mounts the start-up archive first (K22Widescreen.wrs → K22.wrs → Kaynak.k22). With ISO + v1.8.5 data the only cross-archive duplicates are `PALETTE.RAW` (identical bytes) and `LY_Fade.bmp` (the patch copy wins). **CONFIRMED** (static + `check_assets`); see [BUG_FIX_PORTING](BUG_FIX_PORTING.md) §5.
3. **CLI cannot select the patch's Spanish/Russian/Polish languages.** `--language` accepts only `tr|en|de` (`native/main.cpp:83`, `Engine::configure_language` `native/engine.cpp:673`); indices 3–5 are reachable only through the in-game language menu (byte 32 ≤ 5 accepted, `native/engine.cpp:684`). **CONFIRMED.** Launcher/CLI gap, not an engine defect.
4. **No Hor+ option.** Both the patch and native produce horizontal-locked (Vert-) 3D at wide aspects and stretch 4:3 art non-uniformly ([WIDESCREEN_IMPLEMENTATION](WIDESCREEN_IMPLEMENTATION.md) §2, §4). The renderer already has `Camera::widescreen`/`referenceAspect` (`native/render.hpp:53-55`, `native/render_policy.hpp:35`) but the engine always sets `widescreen=false`. **CONFIRMED.** Enhancement candidate, not parity.
5. **Load menu has no controller navigation in original scripts** (patch adds `JOY_*` to `YukleMenu.wdl`). Relevant to the controller work. **CONFIRMED.**
6. **640-tier art is used at every resolution.** `prepare_display` pins `GA_Cozunurluk` to 0, so original branches such as `Kabusdismenu.wdl` (`if(GA_Cozunurluk<=1) DMP_Arka.BMAP=Diablo_640` → `kan_640.tga`) and `YeniMenu.wdl` (`load640_tr.tga` vs `load_tr.tga`) pick the low-resolution variants, upscaled to the window, although the ISO ships the 1024-tier files (`kan_1024.tga` in `yazi_*.k22`, `gozsag1024_*.tga` in `Kaynak.k22`). The patch instead ships a new 1366-wide background. **HIGH CONFIDENCE** (code); image-quality only. Choosing the higher tier would require matching its separate position/scale tables (`gozsag_pos_*[GA_Cozunurluk]`).

## 8. Reproduction

```
cd reverse/community
7z x ../../Kabus_22_Multi_Version_Patch_1.8.5.7z -opatch-1.8.5
git clone https://github.com/iseeeva/kabus22 iseeeva-kabus22
7z x "../../Kabus-22_Win_TR-EN-DE_ISO-Version/Kabus 22.iso" -o.iso-tmp/iso 'JPN-K22*'
../tools/innoextract/usr/bin/innoextract -s -d .iso-tmp/out .iso-tmp/iso/JPN-K22.exe
python3 ../../tools/wrs2.py extract .iso-tmp/out/app/K22.wrs wrs/original-K22
python3 ../../tools/wrs2.py extract patch-1.8.5/K22Widescreen.wrs wrs/patch-K22Widescreen
python3 ../../tools/wrs2.py extract "<lutris>/K22.wrs" wrs/lutris-K22
python3 analyze.py        # writes reports/*
```
