# Community credits, licences and redistribution boundaries

Labels: **CONFIRMED** (text of a licence/notice or a hash comparison) / **HIGH CONFIDENCE** / **HYPOTHESIS** / **UNKNOWN**. This page records facts and project policy; it is not legal advice and makes no clean-room or legal-compliance claim.

Related: [COMMUNITY_PATCH_ANALYSIS](COMMUNITY_PATCH_ANALYSIS.md) · [ACKNEX_A631](ACKNEX_A631.md) · [WRS_FORMAT](WRS_FORMAT.md) · [INSTALLER](INSTALLER.md).

## 1. Attribution

- **iseeeva** — author of the "Kabus 22 Bug Fix and Widescreen Support" mod (ModDB `mods/kabus-22-bug-fix-and-widescreen-support`, Multi Version Patch 1.8.3 and 1.8.5) and of the tool repository `https://github.com/iseeeva/kabus22` (WRS tool, A6.31 plugin `kabus_dll`). This project learned from that work:
  - the WRS member layout used by `tools/wrs2.py` cites the repository's `gamestudio.bms` as its format reference (`tools/wrs2.py` docstring);
  - the native display model reproduces the patch's resolution/HUD formulas and weapon-preview offsets for the original scripts ([WIDESCREEN_IMPLEMENTATION](WIDESCREEN_IMPLEMENTATION.md));
  - the native `K22.ini`, `Parse_EscapeChar` and Discord services re-implement the interface of `kabus_dll.dll` so that patch scripts run unmodified ([ACKNEX_A631](ACKNEX_A631.md) §4).
  **CONFIRMED** (code comments/contents).
- **El Blog de Jackic** — Spanish translation (patch `readme.txt` credits). **CONFIRMED.**
- **Discord Acknex Community** — credited by the patch readme. **CONFIRMED.**
- Polish and Russian translations: added in v1.8.5; individual translators are not named in the readme. **UNKNOWN.**
- **Luigi Auriemma** — QuickBMS and `gamestudio.bms` (bundled in the tool repository). **CONFIRMED.**
- **Conitec** — 3D GameStudio A6/A7 engine and plugin SDK. **Borovik Alexey** — `ini.hpp` (MIT). **Discord** — `discord-rpc`. **CONFIRMED** (file headers).

## 2. Licences found

| Item | Licence / terms | Evidence | Label |
|---|---|---|---|
| iseeeva tool repository (author code: `kabus_tools/src/*.ts`, `kabus_vs/…/Index.hpp`, `Utils/**` except third-party headers) | **none stated**: no LICENSE file; GitHub API `license: null` | `reverse/community/iseeeva-kabus22/` at `8d7ac45`; `api.github.com/repos/iseeeva/kabus22` (2026-10-09) | CONFIRMED |
| Community patch archive contents (scripts, translations, fonts, textures, videos, DLLs, patched executables) | **none stated** in `readme.txt` | `reverse/community/patch-1.8.5/readme.txt` | CONFIRMED |
| Conitec A6 plugin SDK headers/lib (in the tool repository) | `readme_acknex.txt`: free for plugins if complete product source is provided (unless Pro/SDK licence); `.cpp/.h` redistributable, **`.lib` not**; no wrapper DLLs | `kabus_vs/Kabus.Acknex.Plugin/readme_acknex.txt` | CONFIRMED |
| `ini.hpp` | MIT | file header | CONFIRMED |
| `discord-rpc` headers/lib, `kabus_rpc.dll` | upstream `discord-rpc` is MIT-licensed; no licence text is shipped with the patch or repository | HIGH CONFIDENCE (upstream knowledge, not verified in shipped files) |
| QuickBMS (`quickbms.exe`, `.bms` scripts) | upstream GPL-2.0; no licence/source offer shipped in the repository | HIGH CONFIDENCE (upstream knowledge) |
| Kabus 22 game (scripts, art, audio, video, executables) | proprietary (Merscom/FIP/Kabus 22 developers); no redistribution grant known | CONFIRMED absence of any grant in the media examined |

Without a stated licence, default copyright applies to iseeeva's own code and patch content: it may be read and studied, but copying it into this project or redistributing it requires the author's permission. **HIGH CONFIDENCE** (general copyright default).

## 3. What the native project may and may not redistribute

| Material | Status in this project | Reason | Label |
|---|---|---|---|
| Original game assets (ISO, `K22.wrs`, `Kaynak.k22`, `yazi_*`, `ses_*`, videos, `K22.exe`, `RenkAyar.dll`) | **never redistributed**; the installer extracts them from the user's own ISO/installation | proprietary | CONFIRMED (project policy, README) |
| Patch scripts (`K22Widescreen.wrs` and its WDL), translations, fonts, re-drawn textures, re-encoded videos | **not redistributed**; the installer can import a user-supplied patch directory as optional extras | no licence; also contains original game material (below) | CONFIRMED (policy) |
| Patch files that are byte-identical copies of ISO content (24 voice clips, 9 item-text files, `kn_arg.k22`, 142 of 260 language images) | the patch itself redistributes original copyrighted material; that cannot be re-licensed by anyone but the rights holders | hash matches in [COMMUNITY_PATCH_ANALYSIS](COMMUNITY_PATCH_ANALYSIS.md) §3 | CONFIRMED (facts) |
| Patched executables (`K22*.exe`), incl. `K22_NoWRS*.exe` | **never redistributed** | modified proprietary Conitec/game binaries; the NoWRS build additionally carries a different A6 licensee's registration block ([PATCH_BINARY_DIFF](PATCH_BINARY_DIFF.md) §3) | CONFIRMED (facts) |
| `kabus_dll.dll`, `kabus_rpc.dll` | **never redistributed**, never executed | no licence; Windows DLLs | CONFIRMED |
| iseeeva source code | **not copied**; native services are independent implementations of the observed interface (export names, argument/return types, INI keys/ranges/defaults, escape table) | interface facts recorded with attribution | HIGH CONFIDENCE (identifier search of `native/`, `tools/`, `installer/` finds none of the plugin's own identifiers; one comment names `Index.hpp`'s `IniVar` as the behaviour being matched, `native/engine.cpp:1143`) |
| Conitec SDK headers / `adll.lib` | **not included** | SDK terms; `.lib` not redistributable | CONFIRMED |
| Project documentation describing formats/behaviour (this `docs/` set) | published | facts, hashes, counts and short identifiers only; no proprietary script text, translation text or art | CONFIRMED (authoring rule) |
| Analysis working data (`reverse/community/`: clones, extractions, diffs, captures) | **private**, git-ignored | contains proprietary and unlicensed material | CONFIRMED (`.gitignore` `/reverse/*`) |

Translations: the patch's Spanish/Polish/Russian texts and Cyrillic/Polish font atlases are the translators' and iseeeva's work built on proprietary game text; the project neither ships nor converts them. Users who own both the game and the patch can install the patch themselves; the native engine then runs it ([BUG_FIX_PORTING](BUG_FIX_PORTING.md) §4). **CONFIRMED** (policy).

Tools: `tools/wrs2.py` is project code (format knowledge credited above); it does not bundle QuickBMS or any `.bms` script. **CONFIRMED.**

## 4. Rights separation summary (HIGH CONFIDENCE, from §2–§3)

1. Game rights holders: all original content, including copies embedded in the patch.
2. iseeeva and translators: patch scripts, new art (`kan_1366.tga`, `LY_Fade.bmp`, redrawn fonts/textures), translations, plugin and tool source — unlicensed, all rights reserved by default.
3. Third parties: Conitec SDK (restricted), QuickBMS (GPL-2.0), discord-rpc (MIT), ini.hpp (MIT).
4. This project: native engine, installer, tools and documentation under the project's own terms (see the repository README; no project-wide licence is asserted here).

If permission is ever sought from iseeeva (e.g. to ship translations), the request must also respect item 1: the translations and the patch contain derived or copied game material.
