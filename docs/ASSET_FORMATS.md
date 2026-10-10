# Asset formats in the native port

Purpose: developer reference for how the native Linux engine (`native/`) and installer/launcher (`installer/`) read, validate, extract and store each Kabus 22 asset format, and which variants are unsupported.

Related: [REVERSE_ENGINEERING index](REVERSE_ENGINEERING.md) · [RESOURCE_FORMATS (original byte layouts)](reverse-engineering/RESOURCE_FORMATS.md) · [ENGINE_ARCHITECTURE](ENGINE_ARCHITECTURE.md) · [DATA_STRUCTURES](reverse-engineering/DATA_STRUCTURES.md) · [FUNCTION_MAP](reverse-engineering/FUNCTION_MAP.md) · [GAMEPLAY_SYSTEMS](GAMEPLAY_SYSTEMS.md)

Byte layouts and original-executable addresses live in [RESOURCE_FORMATS](reverse-engineering/RESOURCE_FORMATS.md); this file covers native behaviour only. Confidence labels as defined there. Native citations are `file:symbol`; line numbers are omitted for files that were being edited concurrently when this was written (2026-10-09). "Census" = read-only walk of the installed game files done for these docs.

---

## 1. Resolution pipeline

| Step | Native implementation | Confidence / evidence |
|---|---|---|
| Root scan | `native/assets.cpp:Assets::Assets` walks the data root recursively (symlinks skipped) into a loose-file map keyed by `safe_name` (case-folded, trailing `.`/space stripped, absolute/empty rejected); case-insensitive collisions are errors | CONFIRMED (code) |
| Archive mount | `Assets::mount`; campaign `native/main.cpp` mounts `K22.wrs`, `Kaynak.k22`, `K22Widescreen.wrs` (each only if present); Bonus mounts `K22_mode.wrs`. Script `RESOURCE` (`native/wdl.cpp` parser `resource` callback) and `add_resource` (`native/engine.cpp:Engine::builtin`) also call `Assets::mount` | CONFIRMED (code) |
| Lookup | `Assets::read`: loose file first, then archive entry | CONFIRMED (code) |
| Duplicate archive names | Bonus: first-mounted wins (`try_emplace`), matching A7 (`bonus-core/CORE_CONTRACT.txt:46`). Campaign: **last-mounted wins** (`insert_or_assign`), whereas original A6 lookup `00420120` returns the first match | CONFIRMED divergence (code vs `reverse/decompiled/loaders/K22.exe.00420120.c:99-126`). Census: the only cross-archive duplicate among the campaign base archives is `palette.raw` (same stored size in all); effect for patch archives is UNKNOWN |
| Size limits | 128 MiB per member/image (`assetLimit`), 64 MiB PE read for the font (`fontPeLimit`), 1 GiB decoded cache (`cacheLimit`) | CONFIRMED (`native/assets.cpp` constants) |

## 2. WRS archives

`Assets::mount` / `Assets::decompress` / `Assets::read_entry` (`native/assets.cpp`).

| Behaviour | Native | Confidence |
|---|---|---|
| Magic | `WRS2` (name XOR `0xa5`) or `WRS3` (XOR `0xb5`); anything else, including the legacy no-magic variant, is rejected | CONFIRMED (code). Legacy variant unsupported; none shipped (CONFIRMED census) |
| Header | 40 bytes: 32-byte name + big-endian stored/decoded u32; nonzero name padding, `stored > remaining`, `stored`/`decoded` > 128 MiB, or exactly one of them zero → error | CONFIRMED (code) |
| End of index | exact EOF; no sentinel record expected | CONFIRMED (code; all shipped archives parse) |
| Conflicts | duplicate member name and file-vs-directory prefix conflicts (`a` and `a/b`) → error | CONFIRMED (code) |
| Codec | LZSS, 4096-byte space-filled window, cursor 4078, LSB-first flags (1 = literal), 12-bit position, length `(hi&15)+3`; output must equal declared size exactly | CONFIRMED (code matches `K22.exe.00414bc0`) |
| Original limits | 128 archives / 4096 entries are **not** enforced natively | CONFIRMED (code: no such check) |

## 3. Worlds (`.wmb`)

`Assets::world` → `Assets::decode_world` (`native/assets.cpp`), cached per name; `World` in `native/assets.hpp`.

| Item | Native behaviour | Confidence |
|---|---|---|
| Supported | `WMB6` only; 17 section pairs, sections must lie inside the file and not overlap; unindexed bytes kept in `World::metadata` | CONFIRMED (code) |
| WMB7 | rejected ("unsupported world format") | CONFIRMED; no WMB7 is shipped (census) |
| Textures | campaign: 16-bit (565) or 24-bit embedded, or external by name when `width=height=0`; Bonus profile: A7 layout (format word `10`/`12`, four mips, missing `0xffffffff` entries) | CONFIRMED (code) |
| Objects | types 1–8 decoded to `WorldEntity`/`Light`/`Path`; unknown types are errors; type 3 legacy filename/action rule per CONTRACT §24; type 7 372-byte records | CONFIRMED (code; `reverse/decompiled/runtime/CONTRACT.txt:810-826`) |
| Sun | `Engine` sets only `sun_angle` pan/tilt from `World::sunAzimuth/sunElevation`; parsed `World::sunColor` is not written to `SUN_COLOR` | CONFIRMED (`native/engine.cpp`, matches CONTRACT §19) |
| Lightmaps | atlas `256 << lightmapSize`, 24-bit; storage bytes chosen by `Assets::set_native_shadow_depth`; 565/mono LUTs from `Assets::initialize_native_lightmap_tables` (startup-captured, original `004555e0`/`00455240`) | CONFIRMED (code; CONTRACT §22) |

## 4. Models (`.mdl`, `.hmp`)

`Assets::model` → `Assets::load_model` → `Assets::decode_model` (`native/assets.cpp`).

| Item | Native behaviour | Confidence |
|---|---|---|
| Magics | `MDL4`, `MDL5`, `MDL7`, `HMP5`; others rejected | CONFIRMED (code) |
| Skins | `type&15`: 7 external file (`Assets::image_ptr`), 1 skin reference, 0/2/3/4/5/6 embedded via `Assets::embedded_image`; flag 8 mip levels skipped; flag 16 material block; flag 32 length-prefixed extra data | CONFIRMED (code) |
| Indexed skins (format 0) | palette from `palette.raw` (must be 768 bytes) else last 768 bytes of `palette.pcx` (requires `0x0c` marker) | CONFIRMED (code); no shipped first skin uses format 0 (census) |
| UV normalization | geometry UVs untouched; renderer compensates the selected image by padded/raw ratio (`mdl_texture_extent`: next power of two, min 8) for formats 4/5/7 only | CONFIRMED (code; CONTRACT §25) |
| Skin bank | model-wide bank: `model_skin_index`, `model_material`, `model_has_entity_skins` (`native/assets.hpp`) | CONFIRMED (CONTRACT §26) |
| Bonus MDL7 | UV index `0xffff` widened to "absent UV" at the face boundary | CONFIRMED (CONTRACT §30) |
| Shadow LODs | `name_0.mdl` loads `_1`, `_2`, `_3` companions if present (A6 `0045b810`, A7 `1005ee90`) | CONFIRMED (code comments cite these addresses) |
| HMP5 | grid from header, frame packing must be 2, heights 4 bytes/vertex | CONFIRMED (code) |
| Limits | skins 1..256, vertices 1..65535, frames 1..4096; MDL7 vertex stride 16 or 26; triangle stride 6/12/16/26; matrix stride 68; non-triangle groups rejected | CONFIRMED (code) |
| MDL7 skin formats 6/7 after original preprocessing | not reproduced beyond the paths above | UNKNOWN original algorithm (CONTRACT §20) |

## 5. Images

`native/media.cpp:decode_image` (internal `decode_bitmap`, FFmpeg `libavcodec` + `libswscale`), wrapped by `native/assets.cpp:Assets::image_ptr`. Output: `k22::Image` (`native/assets.hpp`), straight-alpha RGBA8, top row first, plus `sourceDepth`, `nativeStorageBytes`, `nativeTextureAlpha`.

| Extension | FFmpeg decoder | Native storage metadata | Confidence |
|---|---|---|---|
| `.pcx` | PCX | depth = bpp × planes; 8 → CPU1, 24 → CPU2 | CONFIRMED (code; CONTRACT §20) |
| `.bmp` | BMP | header depth; 8 → 1, 24 → 2 | CONFIRMED |
| `.tga` | TARGA | 24/32 → 3/4, or 2 when ALPHADEPTH ≤ 16 (`Assets::set_native_alpha_depth`) | CONFIRMED |
| `.dds` | DDS | storage 0 (encoded); DXT1 alpha exception preserved | CONFIRMED |
| `.jpg/.jpeg/.png/.gif/.tif/.tiff/.webp` | MJPEG/PNG/GIF/TIFF/WEBP | depth from header/pixel format | CONFIRMED (code); not used by shipped engine assets (census) |
| other | FFmpeg probe (`Decoder`) | — | CONFIRMED (code) |

`Assets::purge_image` drops cache entries (never the engine font). Bonus profile alone honours `nativeTextureAlpha`.

## 6. Fonts and palettes

| Item | Native implementation | Confidence |
|---|---|---|
| `_a4font` | `Assets::image_ptr("engine/_a4font.pcx")`: loose `game/engine/_a4font.pcx`, else archive member, else extracted from a loose `K22.exe`/`K22_4GB.exe` via `extract_engine_font`; always checked by `validate_engine_font` (`native/pe_font.hpp`: length 17063, RGB24 192×72 header, CRC32 `0xf5b48715`, decoded CRC `0x339585ef`); PE parsing in `pe_font_detail::peSections` scans only initialized-data sections | CONFIRMED (code) |
| Built-in FONT object | `Engine::resolve_builtin_font` requires 192×72; `_a4font` FONT created with style 6, size 9 | CONFIRMED (`native/engine.cpp`) |
| Indexed bitmap FONT | `decode_bitmap_font` keeps the index plane and expands with the caller's 768-byte active palette (opaque alpha), not the PCX palette | CONFIRMED (`native/media.hpp`; CONTRACT second §40–41) |
| Active palette | `Engine::ensure_font_palette`: `<entry-basename>.raw`, else `palette.raw`, else RGB332; `Engine::set_font_palette` normalizes (original `00455240`/`004550e0`) | CONFIRMED (code) |

The installer never ships the font: `installer/setup.cpp:installFont` extracts `app/K22.exe` from the user's media into a private scratch directory (`installer/media.cpp:extractFontSource`, only that exact listed member, ≤ 64 MiB), writes `game/engine/_a4font.pcx` and provenance `game/engine/_a4font-source.json` (`"format":"A6 _a4font RGB24 PCX 192x72"`, source SHA-256), and discards the EXE. An optional patch's `K22.exe` may be the font source instead (`origin = "user-patch/K22.exe"`). CONFIRMED (code; installed JSON observed).

## 7. Audio

`native/media.cpp:audio` → `Decoder` (FFmpeg `libavformat`/`libavcodec`, `libswresample` to float stereo), played by `Media::sound_play` / `Media::sound_play_at` through SDL.

| Input | Native behaviour | Confidence |
|---|---|---|
| WAV PCM, bare Ogg Vorbis, MP3 | FFmpeg probe/decoder | CONFIRMED (code); shipped variants listed in RESOURCE_FORMATS §8 |
| RIFF/WAVE fmt tag `0x674f` with `OggS` data | `media_payload` validates RIFF/chunk bounds and borrows the `data` chunk zero-copy as a plain Ogg stream | CONFIRMED (code; CONTRACT §34, `CONTRACT.txt:1379-1393`) |
| RIFF/WAVE fmt tag `0x676f` (`otokapi_acil.ogg`, `otokapi_zorlan.ogg`) | **not** matched by `media_payload` (it tests only `0x674f`); falls through to FFmpeg, which reports codec `unknown` for these files | Code path CONFIRMED; ffprobe result CONFIRMED; resulting runtime failure ("Native decoder unavailable") when `cinematic.wdl` plays them is HYPOTHESIS — not run |
| Volumes | `Media::master_volume` (SOUND_VOL / MIDI_VOL; zero mutes), `Media::sound_tune` | CONFIRMED (`native/media.hpp`) |

## 8. Video

`Media::movie_play` / `Media::movie_frame` / `Media::update` (`native/media.cpp`): bytes via `Assets::read` (movies are loose `.k22` files, not archive members), video through `Decoder` + `sws_scale` to RGBA, audio track through `audio`; movie frames can be bound to a bitmap target (used for `media_loop(..., bmap_for_entity(...))`).

| Item | Status | Confidence |
|---|---|---|
| Container/codec | RIFF AVI, Indeo 5 (`IV50`) video, MP3 or no audio | CONFIRMED (ffprobe) |
| Indeo 5 availability | depends on the FFmpeg build linked into the engine; the bundled FFmpeg configure flags were not found in the repository | UNKNOWN (not re-verified here) |
| Original behaviour | DirectShow handoff, no engine path rewrite | CONFIRMED (CONTRACT §14) |

## 9. Settings, saves and other script files

All runtime-written files go to the private save root `${XDG_DATA_HOME:-$HOME/.local/share}/kabus22` (`Engine::Engine` in `native/engine.cpp`; `native/saves.cpp:root`), never into the installed `game/` directory. `svc_private` (`native/engine.cpp`) rejects symlink escapes; writes are atomic temp + rename. Reads prefer the private copy, then the original asset (`file_open_read` handling in `Engine::builtin`). README states the same location (`README.md`, "Saves/configuration").

| File | Native handling | Confidence |
|---|---|---|
| `options.k22` | engine: reads the private `<root>/options.k22` if present (must be 31 or 32 bytes), else the shipped asset; scripts write the private copy; launcher `installer/launcher_settings.cpp:validateCampaign` (31 or 32 bytes, per-byte maxima, key codes from `keys()`, no duplicate keys, joystick 1..12, byte 31 ≤ 5) | CONFIRMED (code) |
| `mode_ayarlar.k22`, `ygm.k22` (Bonus) | launcher `decodeBonus` (5 lines, ranges), locale marker accepted only as `111`/`222`/`333` → tr/de/en | CONFIRMED (code). Mismatch with recovered A7 launcher (`000` = en) — see RESOURCE_FORMATS §10.3 |
| `kayit<N>.sav`, `kysd.sav`, `ftz.k22` | produced by the original scripts through native `file_*` builtins; `file_asc_write` appends `uint8(raw >> 10)` (the integer part, low 8 bits) | CONFIRMED (`native/engine.cpp`); original/script regression in `native/check_engine.cpp` (`LV_LevelKaydeta`, `kysd.sav` text `"N N <label>\n"`) |
| Save rotation (in progress) | `native/saves.cpp`: `rotated()` = `kayit0..98.sav` or `kysd.sav`; `backup()` keeps numbered copies under `<root>/backups/<relative>.N`; optional sidecar `<save>.meta` starting `kabus22-save-meta <format>` with `engine=`, `written=`, `entry=`, `script=`, `origin=` keys | CONFIRMED present in working tree at time of writing; uncommitted, may change |
| `_data/saves/<name>` reads | if absent privately, fall back to the same basename in assets (patched scripts' save folder) | CONFIRMED (code) |
| `yoldata.DAT` | ordinary asset read by the original `yoldata.wdl` starter | CONFIRMED (installer allowlists `.dat`) |
| `K22.ini` | only when patched scripts bind `kabus_dll.dll`: `svc_ini_load` / `svc_ini_parse` read private then asset `K22.ini`; grammar: `;`/`#` comments, `#include` (depth ≤ 16), `[section]` or `{section}`, `key = value` or `key: value`, `\`/`/` line continuation; keys stored `section:key`; `Screen:Mode` 0..2, `Screen:X` ≤ 3840, `Screen:Y` ≤ 2160 else `0`; `svc_ini_save` rewrites `<root>/K22.ini` | CONFIRMED (code). This is native grammar, not a recovered patch-DLL grammar |
| Text catalogues (`nti.k22` etc.) | `txt_load` with original delimiter rules (CONTRACT §14) | HIGH CONFIDENCE (CONTRACT section integrated in native; not re-traced here) |

## 10. Installer extraction and installed layout

`installer/media.cpp` (namespace `k22::setup::media`) and `installer/setup.cpp:install`.

| Stage | Behaviour | Confidence |
|---|---|---|
| Source detection | ISO9660 image, original Inno `setup.exe` with sibling `<stem>-1.bin`…, or (new, in progress) an installed Windows game directory with `K22.wrs` + `K22.exe` (`installer/setup.hpp:SourceKind`) | CONFIRMED (code) |
| ISO | `prepare`: libarchive ISO9660 only; rejects links/special/unsized members; exactly one installer with contiguous `.bin` slices (≤ 128); slices staged into a private `0700` directory; source identity rechecked | CONFIRMED |
| Inno listing | external `innoextract` helper: `--data-version` (must be `N.N.N`), full `--list` path validation (duplicates only allowed for excluded Windows EXEs), `--list --list-sizes --list-checksums` (MD5 per member) | CONFIRMED |
| Allowlist (`selected`) | `app/**/*.k22|.wrs|.dat`; `app/Help/*.jpg|.html`; `app/Gallery/*.jpg`; `app/Menu_sound/*.ogg`, `app/Kabus 22 Mode/Menu_ses/*.ogg`. Excluded: `K22.exe`, DLLs, `kayit*.sav`, `.url`, drivers, `acknex.wdf` | CONFIRMED (code + `selfCheck` cases) |
| Extraction | `innoextract --test --extract --collisions=error --include /<path>…`; every file re-hashed (MD5) and size-checked, no links/hardlinks, mode `0600` | CONFIRMED |
| Optional patch | directory snapshot; root accepts only `.wrs`/`.ini`; `_data/**` accepts `.wdl .wrs .ini .wmb .mdl .pcx .bmp .tga .jpg .jpeg .png .dds .ogg .wav .mp3 .avi .txt .fx .dat .k22 .ttf`; Windows components validated but never installed; case changes and conflicts rejected | CONFIRMED (`installer/setup.cpp:dataAsset`, `PatchSnapshot`) |
| Observed installed root (current release layout) | `bin/`, `lib/`, `share/`, `game/` (526 inventoried files incl. 137 original media), `kabus22.png`, `.kabus22-owned` (`Kabus22 native installation 1`), `.kabus22-inventory.json` (`format`, `root`, `source`, `files[path,size,sha256]`, `shortcuts`) | CONFIRMED (observed `~/.local/share/kabus22/`) |
| Versioned layout (in progress) | `installer/layout.hpp`: `versions/<semver>/{bin,lib,share}`, `current` symlink, compat symlinks `bin`, `lib`, `share` → `current/*`; `migrate_flat` converts old roots; `game/`, saves and options never touched | CONFIRMED present in working tree; uncommitted |

## 11. Texture packs (format contract)

**Status:** format contract owned by the Graphics/TextureTool work. At the time of writing the working tree contains `native/texture_pack.hpp`, `native/texture_pack.cpp` (in the root `CMakeLists.txt` source list) and `native/check_texture_pack.hpp`, implementing `texture_pack_root`, `load_texture_pack`, `discover_texture_packs`, `TexturePackSet::find`, `texture_key`, `decode_png`/`encode_png` and `dump_textures` — CONFIRMED (code read). `grep` found **no** use of these symbols in `native/main.cpp`, `native/engine.cpp` or `native/render*.cpp`, so engine/renderer consumption is **not yet implemented** in the tree that was read. The launcher already persists `texture-packs` in `launcher.ini`, reads a `texture_packs` capabilities list and passes `--texture-pack <NAME>` (`installer/launcher_settings.cpp`). Re-check before relying on it.

| Rule | Contract | Implemented in `texture_pack.cpp` |
|---|---|---|
| Location | `${XDG_DATA_HOME:-~/.local/share}/kabus22/packs/<NAME>/pack.txt` (relative `XDG_DATA_HOME` ignored) | yes (`texture_pack_root`, checks) |
| Header | first line exactly `kabus22-texture-pack 1` | yes |
| Metadata | `name<TAB>value`, `license<TAB>value`; both required, before entries, non-empty, no duplicates | yes |
| Entry | `<key>\t<kind>\t<relative/path.png>`; duplicate key/kind rejected | yes |
| Kind | `albedo`, `normal`, `roughness`, `metallic`, `ao` | yes |
| Key | lowercase 16-hex FNV-1a-64 (offset `0xcbf29ce484222325`, prime `0x100000001b3`) over width as LE u32, height as LE u32, then the decoded RGBA8 bytes top row first, i.e. `k22::Image::rgba` | yes (`texture_key`) |
| File | PNG only; path relative, no `..`, canonical path confined to the pack directory; regular file ≤ 64 MiB; ≤ 8192 px per side | yes |
| Dimensions | original × s, s ∈ {1, 2, 4, 8}; all kinds of one key must match | yes (`TexturePackSet::find`, `load_texture_pack`) |
| Normal maps | tangent space, OpenGL convention (+Y up); alpha forced to 255 on load | contract; alpha forcing yes |
| Roughness / metallic / AO | R channel used; packed into an ORM image R = AO, G = roughness, B = metallic (defaults AO 1, roughness 1, metallic 0) | yes |
| Priority | first pack (launcher priority order) containing the key supplies all kinds; invalid entries logged once and ignored | yes |
| Dump | `dump_textures` writes `<key>.png` for decoded originals; refuses output inside the game data directory | yes |

## 12. Known unsupported cases

| Case | Evidence |
|---|---|
| Legacy (magic-less) WRS, WMB7, unknown WMB object types, non-triangle MDL7 groups | code (§2–4) |
| RIFF-wrapped Ogg with fmt tag other than `0x674f` | code (§7) |
| Nondefault stencil, software-world-light, VIEW render-to-bitmap | `README.md` (feature/limit statement) |
| Ray tracing beyond 1024 material sampler bindings; no automatic backend fallback | `README.md` |
| Full campaign/Bonus playthrough, Windows parity, older CPUs | not established (`README.md`) |
