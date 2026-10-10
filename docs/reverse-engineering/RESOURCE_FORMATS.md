# Resource formats (original on-disk layouts)

Purpose: byte-level description of every file format shipped with or written by the original Kabus 22 (K22.exe, Acknex A6 6.31.4 campaign; A7 Bonus mode in `Kabus 22 Mode/`), with the evidence behind each statement.

Related: [REVERSE_ENGINEERING index](../REVERSE_ENGINEERING.md) · [ASSET_FORMATS (native consumption)](../ASSET_FORMATS.md) · [DATA_STRUCTURES](DATA_STRUCTURES.md) · [FUNCTION_MAP](FUNCTION_MAP.md) · [SCRIPT_SYSTEM](SCRIPT_SYSTEM.md) · [ORIGINAL_BEHAVIOR](ORIGINAL_BEHAVIOR.md) · [ENGINE_ARCHITECTURE](../ENGINE_ARCHITECTURE.md)

Confidence labels: **CONFIRMED** = decompiled code/ASM, file bytes or a recorded run; **HIGH CONFIDENCE** = strong indirect evidence; **HYPOTHESIS** = plausible, unverified; **UNKNOWN** = what is missing is stated. Addresses are K22.exe virtual addresses unless prefixed `acknex.dll` (A7 Bonus engine). "Census" means a read-only Python walk of the installed ISO-derived files in `~/.local/share/kabus22/game/` done while writing this document (2026-10-09); numbers quoted are its output.

All integers are little-endian unless stated otherwise.

---

## 1. Inventory: what each shipped file is

Census of every `.k22` / `.wrs` in the installed game root (ISO-extracted; installer allowlist `installer/media.cpp:selected`).

| File(s) | Actual format | Confidence / evidence |
|---|---|---|
| `K22.wrs` | WRS2 archive, 182 members, all `.wdl` scripts | CONFIRMED (census) |
| `Kaynak.k22` | WRS2 archive, 3005 members: 970 `.wmb`, 795 `.mdl`, 521 `.ogg`, 321 `.tga`, 247 `.pcx`, 103 `.wav`, 45 `.bmp`, 1 `.hmp`, 1 `.wdl`, 1 `PALETTE.RAW` | CONFIRMED (census; also `reverse/extracted/Kaynak.k22.index.json`, `reverse/extracted/recovery.json:archives`) |
| `yazi_tr.k22`, `yazi_en.k22`, `yazi_de.k22` | WRS2 archives, 52 members each (localized UI `.tga`/`.pcx`/`.bmp`, one `.wdl`, `PALETTE.RAW`) | CONFIRMED (census) |
| `ses_tr.k22`, `ses_en.k22` | WRS2 archives, 14 members (12 `.ogg` voice, one `.wdl`, `PALETTE.RAW`) | CONFIRMED (census) |
| `ve*.k22` (17 files), `vt*.k22` (24 files), `vkt.k22`, `kn_arg.k22` | RIFF/AVI movies (not archives) | CONFIRMED (`RIFF....AVI LIST` header bytes; §8) |
| `nti.k22`, `bti1.k22`, `bti2.k22` / `nei.k22`, `bei1.k22`, `bei2.k22` / `ndi.k22`, `bdi1.k22`, `bdi2.k22` | Plain CRLF text, one string per line (TR / EN / DE item and interaction texts) | CONFIRMED bytes; consumer `dil_ceviri_tr.wdl:dosyayukle_strng0..2` → `GenelAyarlar.wdl` `txt_load` (§11) |
| `options.k22` | 31-byte (original) or 32-byte (patched) binary settings | CONFIRMED (§10) |
| `ftz.k22` (written at runtime) | 3 bytes: playtime day/hour/minute | CONFIRMED (§11) |
| `mdc.k22`, `msc.k22` | 3 ASCII bytes `000` | Bytes CONFIRMED; consumer UNKNOWN (no reference found in extracted original/patch scripts, native code, CONTRACT.txt or the patched K22.exe) |
| `yoldata.DAT` | 117960-byte path-routing table | CONFIRMED layout (§12) |
| `Kabus 22 Mode/K22_mode.wrs` | WRS3 archive, 435 members (A7 Bonus) incl. 3 nested WRS3 `mode_*.k22` | CONFIRMED (census) |
| `Kabus 22 Mode/mode_tr.k22`, `mode_en.k22`, `mode_de.k22` | WRS3 archives, 85 members each | CONFIRMED (census) |
| `Kabus 22 Mode/mode_ayarlar.k22` | 5 CRLF text lines (Bonus settings) | CONFIRMED (§10.2) |
| `Kabus 22 Mode/ygm.k22` | 3 ASCII digits (Bonus launcher locale marker) | CONFIRMED bytes (§10.3) |
| `kayit<N>.sav`, `kysd.sav` (runtime) | Script-written save slot / save index | CONFIRMED writer (§11) |
| `K22.ini` | Text INI — community-patch artifact, not on the ISO | HIGH CONFIDENCE (§13) |

Not WRS: every census file whose first 4 bytes are not `WRS2`/`WRS3` is listed above with its real type. The `.k22` extension therefore carries no format meaning by itself.

---

## 2. WRS resource archives

### 2.1 Original A6 index loader (campaign)

Evidence: `reverse/decompiled/loaders/K22.exe.0041fd20.c` (index), `K22.exe.0041ff20` (open/register; summarized in `reverse/decompiled/RECOVERY.txt:42-57`), `reverse/decompiled/loaders/K22.exe.00420120.c` (lookup), `reverse/decompiled/decoder/K22.exe.00414bc0.c` (payload codec).

| Item | Value | Confidence / evidence |
|---|---|---|
| Registration | `RESOURCE` directive in parser `0041aef0` (`0x0041b4ab` refs string `0x005d4728`) → `0041ff20` opens the file, stores `FILE*` in table `0x006bafb8` | CONFIRMED, `RECOVERY.txt:39-46` |
| Archive limit | error after count `0x006bb1bc` exceeds `0x7f` → 128 archives | CONFIRMED, `RECOVERY.txt:45` |
| Magic | first 4 bytes compared (`_stricmp`) to `"WRS2"` | CONFIRMED, `K22.exe.0041fd20.c:36` |
| WRS2 name key / width | XOR `0xa5` (`DAT_006bb5f0`), pad constant `0x1f` | CONFIRMED, `0041fd20.c:38-39` |
| Legacy (no magic) | `rewind`, XOR key 0, pad constant `0x0c` | CONFIRMED, `0041fd20.c:34-35,42` |
| Name field size | WRS2 32 bytes, legacy 13 bytes: name read to on-disk NUL, then `fseek(pad - len)` | CONFIRMED (derived from control flow, `0041fd20.c:47-61`; `RECOVERY.txt:52`) |
| Size fields | two big-endian u32 built from four `getc`: stored (packed) length, then decoded length | CONFIRMED, `0041fd20.c:69-80` |
| Payload offset | `ftell` immediately after the header | CONFIRMED, `0041fd20.c:81-82` |
| Entry table | 0x30-byte records at `0x00665ea8`: name, +0x20 archive slot, +0x24 offset, +0x28 stored, +0x2c decoded | CONFIRMED, `0041fd20.c:63-84` |
| Entry limit | error `0x3ee` ("Too many WRS resources (%d max)!" at `0x005d72b8`) when total `0x006bb1b8` > `0xfff` → 4096 entries total | CONFIRMED, `0041fd20.c:85-87`, `reverse/decompiled/K22.exe.recovery.txt:1727` |
| Termination | name loop returns on `getc == -1` (EOF); loop condition tests the last byte read of the decoded-size field against `0xffffffff` (i.e. EOF from `getc`) | CONFIRMED code (`0041fd20.c:51-53,89`). `RECOVERY.txt:53` calls this a "0xffffffff termination marker"; **no on-disk sentinel record exists in any shipped archive** — every census archive is consumed exactly by 40-byte headers + payloads to EOF (CONFIRMED, census) |
| Lookup order | forward linear `_stricmp` scan of the entry table; **first (earliest-mounted) match wins**; decoded size must be ≤ the caller's limit, else "invalid size" | CONFIRMED, `K22.exe.00420120.c:99-126` |

### 2.2 On-disk layout

```
offset 0      : "WRS2" | "WRS3"        (absent in the legacy variant)
repeat until EOF:
  name[32]    : bytes XOR key until the first raw 0x00; rest raw zero padding   (legacy: name[13], key 0)
  u32be stored: packed payload length
  u32be size  : decoded length
  u8  payload[stored]
```

| Variant | Magic | Name key | Name bytes | Used by | Evidence |
|---|---|---|---|---|---|
| Legacy | none | 0 | 13 | no shipped file | `0041fd20.c` (CONFIRMED code path); no instance in census |
| WRS2 | `WRS2` | `0xa5` | 32 | all campaign archives | CONFIRMED (code + census) |
| WRS3 | `WRS3` | `0xb5` | 32 | all Bonus archives | CONFIRMED, A7 index `acknex.dll.10024860` (`MOV byte ptr [0x101ea600],0xb5` at `0x10024903`), `reverse/decompiled/runtime/bonus-core/CORE_CONTRACT.txt:47,67` |

Name padding is zero in every census header (CONFIRMED; native rejects nonzero padding). Names may contain `/` (directory-like). Every census member has `stored > 0` and `stored != decoded` — no member is stored raw (CONFIRMED, census: `pk==dec` count 0 for every archive).

### 2.3 Payload codec: LZSS

Every member payload is LZSS-coded, including already-compressed OGG (which therefore grows: `ses_en.k22` stored/decoded ratio 1.113, CONFIRMED census).

| Parameter | Value | Evidence |
|---|---|---|
| Window | 4096 bytes, pre-filled with `0x20` (space) | CONFIRMED, `00414bc0.c:21-29` (`0x3fb` dwords of `0x20202020` + one `0x2020` word) |
| Initial write cursor | `0xfee` (4078) | CONFIRMED, `00414bc0.c:26-28` |
| Flag byte | 8 flags, consumed LSB first; bit 1 = literal byte, bit 0 = back-reference | CONFIRMED, `00414bc0.c:40-56` |
| Reference | 2 bytes `lo, hi`: position = `lo | ((hi & 0xf0) << 4)`; length = `(hi & 0x0f) + 3` (code stores `+2`, copies `<=`) | CONFIRMED, `00414bc0.c:70-89` |
| Bounds | input counted against stored length (`param_4`), output against decoded length (`param_3`) | CONFIRMED, `00414bc0.c:52,69,87,104` |

Independent confirmation: `reverse/extracted/recovery.json:lzss` (initial cursor 4078 and space fill proven by original `K22.wdl`/`sub_titles_tr.wdl` decoding; all entries decode to their declared sizes) — CONFIRMED.

### 2.4 Known archive contents

| Archive | Notes | Evidence |
|---|---|---|
| ISO `K22.wrs` | 182 entries, 862026 bytes, SHA-256 `c9e32008…85c5` (installed inventory) | CONFIRMED (census, `~/.local/share/kabus22/.kabus22-inventory.json`) |
| Lutris `K22.wrs` | 179 entries, 804591 bytes, MD5 `7d8bad41…` — a different (older, Turkish-script) build; it is the source of `reverse/extracted/original/*.wdl` | CONFIRMED (`reverse/extracted/recovery.json:archives`); relationship to ISO HYPOTHESIS |
| `PALETTE.RAW` | present in `Kaynak.k22`, every `yazi_*.k22`, every `ses_*.k22`; 768 decoded bytes | CONFIRMED (census) |
| `K22_mode.wrs` nested | `mode_tr/en/de.k22` (WRS3) and `mode_ayarlar.k22` inside; RESOURCE mounts the **loose** same-named files, nested members are ordinary bytes | CONFIRMED, `bonus-core/CORE_CONTRACT.txt:46-48` |
| Patch `K22Widescreen.wrs` | WRS2, 190 entries (188 `.wdl`, 1 `.tga`, 1 `.bmp`) — community patch, not ISO | CONFIRMED, `reverse/extracted/recovery.json:archives` |

---

## 3. WMB6 world (`.wmb`)

Census: 970 `WMB6` in `Kaynak.k22`, 3 `WMB6` in `K22_mode.wrs`; **no `WMB7` file is shipped** (CONFIRMED; `reverse/extracted/recovery.json:native_3d_magic_counts` agrees).

### 3.1 Header

| Offset | Size | Field | Evidence |
|---|---|---|---|
| 0 | 4 | `"WMB6"` | CONFIRMED |
| 4 | 17 × 8 | `(u32 offset, u32 length)` per section; header ends at 140, first data (`legacy7`) starts at 140 | CONFIRMED, `reverse/extracted/asset_headers.json` (`header_layout_evidence`), bytes of `reverse/extracted/assets/level0.wmb` |

Section order (index: name as used by `asset_headers.json`): 0 palettes, 1 planes, 2 textures, 3 vertices, 4 pvs, 5 bsp_nodes, 6 materials, 7 faces, 8 legacy4, 9 aabb_hulls, 10 bsp_leafs, 11 bsp_blocks, 12 edges, 13 edgelist, 14 legacy7 (160-byte legacy hull records), 15 objects, 16 lightmaps — CONFIRMED (header bytes; native `Assets::decode_world` consumes the same indices). Reading WMB7-only trailing pairs would misinterpret section data (CONFIRMED, `asset_headers.json`).

### 3.2 Texture records (section 2)

Offset table (`u32 count`, `count × u32 offset`), then per record: `name[16]`, `u32 width`, `u32 height`, `u32 mip[4]` (offsets relative to the record), pixels.

| Profile | Pixel encoding | Evidence |
|---|---|---|
| Campaign A6 | base-level size `mip[1]-mip[0]` = 2×w×h (16-bit 565) or 3×w×h (24-bit); `width=height=0` → external image by name | HIGH CONFIDENCE (native `Assets::decode_world` accepts exactly these for all 970 shipped worlds; original reader body not exported here) |
| Bonus A7 | `+24` = format (`10` → 16-bit, `12` → 24-bit), pixels start at `+40`, exactly four mip levels; missing entries `0xffffffff` | CONFIRMED by citation in native source comment to `acknex.dll.10059e90` and uploader `1002cd4d..7a`; A7 export path not separately listed here |

### 3.3 Object records (section 15)

Offset table as above; record type is the first u32.

| Type | Content | Confidence / evidence |
|---|---|---|
| 1 | position, angle, 8 skipped bytes, `name[20]` | HIGH CONFIDENCE (native decoder; matches libgamestudio `wmb6.spec` cited in `asset_headers.json:sources`) |
| 2 | light: position, RGB (0–100 scale), range, flags | HIGH CONFIDENCE (as above) |
| 3 | legacy entity; native struct padded to 136 bytes | CONFIRMED, CONTRACT §24 (`CONTRACT.txt:810-826`) |
| 4 | sound: position, volume, range, flags, `file[33]` | HIGH CONFIDENCE |
| 5 | level info: sun azimuth/elevation floats at `+0x10/+0x14`, info flags, compiler version, gamma, lightmap size, packed sun colour at `+0x24`, ambient, fog colours | Layout HIGH CONFIDENCE; consumption CONFIRMED §19 |
| 6 | path: `name[20]`, point count (float), 36-byte points (position + 6 skills), 24-byte edges (1-based node pair, length, bezier, weight, skill) | HIGH CONFIDENCE |
| 7 | entity, verbatim 372-byte copy: `name` at 40, `file` at 73, `action` at 106 (33-byte slots), skills from 140 | CONFIRMED, CONTRACT §24 (`0045a358`) |
| 8 | region: min, max, 8 skipped bytes, `name[32]` | HIGH CONFIDENCE |

**Type 3 legacy filename rule** (CONFIRMED, `CONTRACT.txt:813-826`; `reverse/decompiled/runtime/entity-file-layout/contract.txt`, `original-asm.txt`): branch `0045a15a` of `00459eb0` uses name 40/20 bytes, filename 60/13, action 73/20. Byte 72 is tested at `0045a17d`: if zero, file (13) and action (20) are copied separately; if nonzero, `0045a1cc` calls `0045ccf0` with source 60, capacity 33 (≤32 chars + NUL) — the filename consumes the action slot and the action stays empty (zero-allocated by `0043a77a`). Skills 96..127 (8 floats), flags 128, ambient 132. Creation `004315f6` uses internal filename 73; `0043186e` checks internal action 106 and copies ≤19 chars for dispatch.

**Sun globals** (CONFIRMED, CONTRACT §19, `CONTRACT.txt:723-739`; `reverse/decompiled/runtime/sun-header-asm/contract.txt`): type-5 branch `0045a696-0045a6f2` assigns only `SUN_ANGLE` pan/tilt = record floats `+0x10/+0x14` × 1024, under load-mode predicate `007192e0`; the third SUN_ANGLE word is untouched. Packed sun colour `+0x24` is **not** consumed; `SUN_COLOR` (slot `00665c50`, zero xrefs) and `SUN_LIGHT` (`00665c1c`) are not assigned from the WMB.

### 3.4 Lightmaps (section 16)

Atlases of `256 << lightmapSize` square, 3 bytes per texel (`lightmapSize ≤ 2`). HIGH CONFIDENCE (native decoder accepts every shipped world). CPU storage and 565/mono LUTs: CONTRACT §20, §22 (`CONTRACT.txt:741-794`, `tex-lightmap-lut/contract.txt`) — CONFIRMED static.

---

## 4. MDL models (`.mdl`)

Census (first skin of each MDL4/MDL5 in `Kaynak.k22`): MDL4 334 (all skin format 2), MDL5 309 (format 2: 228, 4: 78, 5: 3), MDL7 152. `K22_mode.wrs`: MDL4 17, MDL5 35, MDL7 69. CONFIRMED.

### 4.1 MDL4 / MDL5 header (84 bytes)

| Field (order) | Evidence |
|---|---|
| magic `MDL4`/`MDL5`, version, scale vec3, offset vec3, (bounding/eye fields), `numskins`, `skinwidth`, `skinheight`, `numverts`, `numtris`, `numframes`, `numskinverts`, `flags`, `unused` | CONFIRMED values in `reverse/extracted/asset_headers.json` (e.g. `et_ari.MDL` MDL4 128×128, 145 verts; `yasin_deniz.mdl` MDL5 `skinwidth=0`, per-skin 512×512); layout per Conitec `prog_mdlhmp.html` (listed in `asset_headers.json:sources`) |

MDL4 skin dimensions come from the header; MDL5 stores width/height per skin (CONFIRMED by `yasin_deniz.mdl`: header 0×0, first skin 512×512).

### 4.2 Skin record type word

| `type & 7` | Meaning | Evidence |
|---|---|---|
| 0 | 8-bit indexed (palette) | HIGH CONFIDENCE (native; none observed as a first skin in census) |
| 1 | reference to an earlier skin index | HIGH CONFIDENCE |
| 2 | 16-bit RGB565 | CONFIRMED (census, decoded images) |
| 3 | 16-bit ARGB4444 | HIGH CONFIDENCE |
| 4 | 24-bit (B,G,R; `type & 128` swaps R/B) | CONFIRMED census |
| 5 | 32-bit BGRA (`type & 128` swap) | CONFIRMED census |
| 6 | embedded DDS of `width` bytes | HIGH CONFIDENCE |
| 7 | external filename, NUL-terminated | HIGH CONFIDENCE |
| flag 8 | 3 extra mip levels follow (dims must be multiples of 8) | HIGH CONFIDENCE |
| flag 16 / 32 | material block (4 colours + power) / length-prefixed extra data | HIGH CONFIDENCE (MDL7) |

### 4.3 UV and skin-selection rules (A6)

- Legacy signed pixel UVs are normalized by the **first raw skin's** dimensions rounded up to a power of two, minimum 8 (`00426aa0`, initialized `00413eaf`), in `00458500`. MDL7 float UVs are multiplied by raw/padded dimension. — CONFIRMED, CONTRACT §25 (`CONTRACT.txt:828-847`), `reverse/decompiled/runtime/model-uv-contract/contract.txt`.
- Empty skin descriptors consume zero bytes; zero skin count is malformed. — CONFIRMED §25.
- MDL4/5/7 and HMP5 use one model-wide skin bank; `004349f0` (`bmap_for_entity`) truncates/clamps the selector without mutating `ENTITY.SKIN`; draw preparation `0044a590` clamps and writes `ENTITY+0x40`. — CONFIRMED, CONTRACT §26 (`CONTRACT.txt:849-864`), `entity-skin-contract/contract.txt`.
- Raw MDL/HMP low-3 formats 0/2/3/4/5 map to CPU storage 1/2/2/3/4; formats 6/7 after MDL7 preprocessing give CPU4 (`00458b3b`); their decoding algorithm is UNKNOWN. — CONFIRMED §20.

### 4.4 MDL7

48-byte header (`version`, `bones_num`, `groups_num`, `mdl7data_size`, `entlump_size`, `medlump_size`) followed by declared structure sizes (bone 36, skin 28, colorvalue 16, material 68, skinpoint 8, triangle 12/16/26, mainvertex 16, framevertex 16, bonetrans 68, frame 24) and groups. CONFIRMED values for `bullet.mdl` in `asset_headers.json`; spec `libgamestudio/docs/mdl7.spec` (cited there).

A7 (Bonus) face reader `acknex.dll.101589eb` widens only UV index `0xffff` to `0xffffffff` (absent UV, float bytes `00000000` at `10187124`); other u16 values stay unsigned. — CONFIRMED, CONTRACT §30 (`CONTRACT.txt:1253-1260`), `reverse/decompiled/runtime/bonus-model-layout/contract.txt`.

---

## 5. HMP5 terrain (`.hmp`)

One shipped file (`yr.hmp` in `Kaynak.k22`). 84-byte header: scale, offset (−255.5), triangle size 15.96875, `mesh_vertices_x = 33`, `numskins 1`, `numverts 1089` (=33×33), `numframes 1`. CONFIRMED (`asset_headers.json`). Frames: packing word must be 2, then 4 bytes per vertex height (HIGH CONFIDENCE, native decoder). HMP generated UVs are separate from MDL UV normalization; loader `0045b630` reaches `00458500` for skins (CONFIRMED, §25–26).

---

## 6. Images

| Format | Shipped variants (census) | Original storage rule | Evidence / confidence |
|---|---|---|---|
| PCX | `Kaynak.k22`: 247 × 8-bit × 3 planes (RGB24). `yazi_*`: includes 1-plane indexed `Font0.pcx`. `K22_mode.wrs`: 13 × 3-plane, 1 × 1-plane | 1-plane stays CPU1; 3-plane converts CPU3→2 | CONFIRMED census + §20 |
| TGA | Types 2 (uncompressed) and 10 (RLE), 24 and 32 bpp (`Kaynak`: 104/157 RLE 24/32, 15/45 raw 24/32) | Default ALPHADEPTH 32: TGA24/32 stay CPU3/4; ALPHADEPTH ≤ 0x4000 raw converts to 2 | CONFIRMED census + §20 |
| BMP | `BM` magic; 45 in `Kaynak`, 5 per `yazi_*` | BMP8 CPU1; BMP24 CPU3→2 | Magic CONFIRMED; depth census not performed (UNKNOWN per file) |
| DDS | none shipped standalone; only possible as MDL skin format 6 | standalone DDS stays encoded CPU0 | §20 CONFIRMED; presence census CONFIRMED (no `.dds` member) |
| JPG | `Gallery/*.jpg`, `Help/**/*.jpg` — desktop wallpapers / HTML help, not engine assets | — | CONFIRMED (file names; no script reference checked) |

### 6.1 Palettes

| Item | Value | Evidence |
|---|---|---|
| `PALETTE.RAW` | 768 bytes RGB, starts as a gray ramp (`00 00 00 0f 0f 0f 1f 1f 1f …`) | CONFIRMED (census bytes) |
| Active-palette selection `00455240` | keep existing palette; else `<startup-input-basename>.raw`; else `palette.raw`; else RGB332: `R = i & 0xe0`, `G = uint8((i & 0xfc) << 3)`, `B = uint8(i << 6)` | CONFIRMED, second-numbered CONTRACT §40 (`CONTRACT.txt:2194-2197`) |
| Normalization `004550e0` | entry 0 → black; entries 1..255 with all RGB < 8 → (8,8,8); global active entry 255 → white | CONFIRMED, `CONTRACT.txt:2198-2200` |
| Separate consumers | D3D palette `006c4fb8`, mono palette `006c53b8` | CONFIRMED, `CONTRACT.txt:2202` |

No `<basename>.raw` other than `PALETTE.RAW` exists in any census archive (CONFIRMED).

---

## 7. Fonts

### 7.1 Bitmap FONT objects

| Item | Value | Evidence |
|---|---|---|
| Atlas geometry `0040eb60` | cell W at FONT+0x6c, H at +0x70; columns = imageWidth / W; glyphCount = w×h/(W×H) ≤ 256; W < 4 marks a system font (style bits) | CONFIRMED, `reverse/decompiled/runtime/FONT_LAYOUT.txt` |
| Advance `0040c8e0` | `W − (flags&2 ? 1 : 0) − (flags&0x2000 ? 2 : 0)`; full W×H quad always emitted; no proportional widths | CONFIRMED, `FONT_LAYOUT.txt` (ASM `0040ca78`..`0040ca87`) |
| Indexed FONT palette | FONT keeps indexed PCX plane data and uploads with the **active** palette `007192bc`, not the PCX's embedded palette | CONFIRMED, `CONTRACT.txt:2194-2204` |
| `Font0.pcx` (`yazi_en.k22`) | 352×144, 1 plane, 8-bit, stride 352, RLE gives 50688 indices, only 0 and 255; embedded 768-byte palette entirely black | CONFIRMED, `CONTRACT.txt:2181-2184` |

### 7.2 Built-in engine font `_a4font`

| Item | Value | Evidence |
|---|---|---|
| Location | embedded PCX in K22.exe at VA `0x0062130c` (file offset `0x22130c`), pointer at `0x00627310`; RLE `0x0062138c..0x006255b3` | CONFIRMED, `reverse/decompiled/runtime/font-embedded/K22.exe.0062130c.pcx.txt` |
| Header | PCX v5, RLE, 8 bpp, window 0,0..191,71, 3 planes, 192 bytes/line → RGB24 192×72, no palette | CONFIRMED (same file; native check `pe_font_detail::fontHeader` in `native/pe_font.hpp`) |
| Size / hash | 17063 bytes, SHA-256 `a16eb595…3215c41`, CRC32 `0xf5b48715`, decoded CRC `0x339585ef` | CONFIRMED (`font-embedded/*.pcx.txt`; `native/pe_font.hpp` constants) |
| Identity across builds | same bytes/offset in ISO `app/K22.exe`, patched K22.exe and 4 GB PE | CONFIRMED, `font-embedded/*.pcx.txt`, `native/pe_font.hpp` comment |

---

## 8. Audio

| Format | Shipped (census) | Evidence / confidence |
|---|---|---|
| WAV PCM (fmt tag 1) | `Kaynak.k22` 100 members: mono/stereo, 8/16-bit, 8000–44100 Hz (most: 22050 Hz mono, 43 × 16-bit, 38 × 8-bit). `K22_mode.wrs` 40 members, 22050/44100 Hz | CONFIRMED (census `fmt ` chunks) |
| OGG Vorbis (bare `OggS`) | 518 `.ogg` in `Kaynak.k22`, 108 in `K22_mode.wrs`, 12 per `ses_*.k22`; loose `Menu_sound/*.ogg`, `Kabus 22 Mode/Menu_ses/*.ogg` | CONFIRMED (magic) |
| RIFF/WAVE-wrapped Ogg (OggDS) | 3 `.ogg` members in `Kaynak.k22`: `v_tacikos_kapi.ogg` fmt tag `0x674f` ("Og"); `otokapi_acil.ogg`, `otokapi_zorlan.ogg` fmt tag `0x676f` ("og"). All three have a complete Ogg stream at the `data` chunk (offset 58 of the decoded member) | CONFIRMED (census; §33 for `v_tacikos_kapi`: wrapped 18382 bytes, DATA 18316 bytes, `CONTRACT.txt:1358-1377`, bytes in `reverse/decompiled/runtime/campaign-media/riff-vorbis/`) |

FFprobe reports codec `unknown` for the outer RIFF of all three wrapped members (CONFIRMED, run while writing this document). Consumers: `cinematic.wdl:848` (`v_tacikos_kapi`, CONTRACT §33) and `cinematic.wdl` `SOUND v_otokapiacil_ogg` / `v_otokapizorlan_ogg` declarations (CONFIRMED, script identifiers).

Original Windows decoding path for the wrapped files: UNKNOWN (no decoder export; presumably an installed OggDS DirectShow filter — HYPOTHESIS).

---

## 9. Video

| Item | Value | Evidence / confidence |
|---|---|---|
| Container | RIFF AVI (`hdrlavih`) in loose `.k22` files | CONFIRMED (bytes) |
| Streams | Indeo Video 5 (`IV50`) video; e.g. `vebi.k22` 640×480 @ 25 fps + MP3 mono 44100 Hz; `vkt.k22` 336×220 @ 10 fps, video only | CONFIRMED (ffprobe, read-only) |
| Naming | `ve*` 17 files, `vt*` 24 files; consistent with English / Turkish voiced cutscenes (`vt*` has extra scenes) | Counts CONFIRMED; language mapping HYPOTHESIS |
| `kn_arg.k22` | menu background, `media_loop("kn_arg.k22", …)` | CONFIRMED (script call in extracted original scripts) |
| `vkt.k22` | played onto an entity skin: `media_loop("vkt.k22", bmap_for_entity(my,0), 100)` | CONFIRMED (same) |
| Original playback | `MEDIA_PLAY 00405d10 → 00405ca0 → 00425730`: filename converted with `MultiByteToWideChar(CP_ACP)` and passed to a DirectShow graph; no engine path rewrite | CONFIRMED, CONTRACT §14 (`CONTRACT.txt:553-560`) |

---

## 10. Settings files

### 10.1 `options.k22` (campaign, binary)

Writer/reader: `GenelAyarlar.wdl` — sequence of `file_asc_read` / `file_asc_write` on `GA_Dosya` (CONFIRMED script). Each `file_asc_write` emits one byte. Original writes 31 bytes; the patched build appends a language byte (the original `file_asc_write(GA_Dosya, GA_Dil)` line is commented out). Validator: `installer/launcher_settings.cpp:validateCampaign` (CONFIRMED maxima below).

| Byte | Script variable | Stored as | Max | Evidence |
|---|---|---|---|---|
| 0 | `GA_Cozunurluk` (resolution) | raw | 3 | CONFIRMED |
| 1 | `GA_RenkKalitesi` (colour depth) | raw | 1 | CONFIRMED |
| 2 | `GA_IsikKalitesi` (light quality) | raw | 1 | CONFIRMED |
| 3 | `GA_Tamponlama` (buffering) | raw | 1 | CONFIRMED |
| 4 | `GA_Mipmapping` | raw | 1 | CONFIRMED |
| 5 | `GA_Anisotropy` | raw | 1 | CONFIRMED |
| 6 | `GA_Yansima` (reflection) | raw | 1 | CONFIRMED |
| 7 | `GA_KanRengi` (blood colour) | raw | 1 | CONFIRMED |
| 8 | `GA_KanMiktari` (blood amount) | raw | 100 | CONFIRMED |
| 9 | `GA_Parlaklik` (brightness) | value + 100 | 200 | CONFIRMED |
| 10 | `GA_Karsitlik` (contrast) | value + 80 | 160 | CONFIRMED |
| 11 | `GA_Gamma` | value + 100 | 200 | CONFIRMED |
| 12–14 | `GA_Efekt`, `GA_Muzik`, `GA_Dialog` (volumes) | raw | 100 | CONFIRMED |
| 15–24 | `GA_Tuslar[0..9]` keyboard bindings | key code; must be in launcher `keys()` table, no duplicates | — | CONFIRMED (validator); scan-code interpretation HIGH CONFIDENCE (installed default `48 50 4d 4b …` = DirectInput arrow keys) |
| 25–30 | `GA_JTuslar[0..5]` joystick buttons | 1..12 | 12 | CONFIRMED |
| 31 | patch language (0 tr, 1 en, 2 de; validator accepts ≤ 5) | patched builds only | 5 | CONFIRMED (validator + launcher save code); patch semantics of 3–5 UNKNOWN |

Example: installed default `00 01 01 01 01 01 01 00 64 64 50 82 41 5a 64 48 50 4d 4b 1f 20 1e 32 23 10 01 02 03 04 05 06` (31 bytes) — CONFIRMED bytes.

### 10.2 `mode_ayarlar.k22` (Bonus, text)

Five CRLF integer lines: resolution 0..3, shader level 0..2, anisotropy 0..1, music 0..1, brightness (50 neutral). CONFIRMED, `installer/launcher_settings.cpp:decodeBonus`; supplied default `2 2 0 1 50` (bytes) and archive copy `2 2 0 1 70` (nested member) — CONFIRMED census.

### 10.3 `ygm.k22` (Bonus launcher locale)

Three ASCII digits. The packed Bonus launcher embeds `rms0.txt='000'`, `rms1.txt='111'`, `rms2.txt='222'` and copies one to `ygm.k22` by `menu_dil`; contract mapping `000=en, 111=tr, 222=de` — CONFIRMED, `bonus-core/entry/entry-contract.txt:33`, `bonus-core/CORE_CONTRACT.txt` item 3. **Discrepancy:** native `installer/launcher_settings.cpp` accepts only `111` (tr), `222` (de), `333` (en) and rejects `000` (CONFIRMED code). Installed file is `111`.

---

## 11. Save and progress files (script-defined)

These formats are defined entirely by WDL scripts; no engine save format (`game_save`) is used for them (no `game_save`/`game_load` call found in extracted original scripts — CONFIRMED grep).

| File | Layout | Evidence / confidence |
|---|---|---|
| `kayit<N>.sav`, N = 0..98 | Ordered byte stream, one byte per `file_asc_write` in `Yeni_Oyun_Deger.wdl:YD_OyunuKaydet` (270 call sites incl. fixed loops of 4–150 iterations); read back by `YD_OyunuYukle`. First fields: `save_versiyon` (=100, `K22.wdl`), `LY_AktifLevelID`, `Aktif_Player`, `Aktif_Silah`, `akt_music_idno`, …; later `loc_gun`, `loc_saat`, `loc_dakka`, `aktif_kayit_sayisi`, `aktif_oldurme_sayisi`, `aktif_enerji_sayisi`, … | Writer order CONFIRMED (script). Lutris `kayit0.sav`: 2030 bytes, byte 0 = `0x64` (100) — CONFIRMED bytes. Full field map beyond the script order: see the script; per-field meaning partly HYPOTHESIS. Behaviour for values > 255 in the original engine: UNKNOWN |
| `kysd.sav` | Text: `file_var_write(Aktif_SaveNo)`, `file_var_write(SLG_KayitSayisi)`, then `SLG_KayitSayisi` label strings each followed by `"\n"`; max 99 slots (`YukleMenu.wdl: SLG_MaxKayit`) | CONFIRMED (`YukleMenu.wdl:SLG_Kaydet`); native regression expects e.g. `"1 1 <label>\n"` (`native/check_engine.cpp`) |
| `ftz.k22` | 3 bytes: day, hour, minute (`file_asc_write` in `CikisMenu.wdl`, read in `GenelAyarlar.wdl`) | CONFIRMED (script) |

TXT_LOAD / FILE_STR_READ tokenization of text files (initial bytes ≤ 0x1f skipped; token ends at EOF, any byte ≤ 0x1f, or `delimit_str[0]` = `,`; spaces kept; no comment handling; buffer `0063bc30`, limit 4095): CONFIRMED, CONTRACT §14 (`CONTRACT.txt:522-551`).

---

## 12. `yoldata.DAT` (AI path routing)

Reader: `yoldata.wdl` STARTER `DatYukle` reads 117960 bytes with `file_asc_read` into `YapayYol[117960]` (CONFIRMED script). Header comments: 52 levels, 2036 "boxes"; `LevelKutu[52]` = boxes per level; `Atlama[52]` = per-level offset.

| Property | Value | Evidence |
|---|---|---|
| Size | 117960 = Σ `LevelKutu[i]²` | CONFIRMED (arithmetic over script arrays) |
| Offsets | `Atlama[i]` = prefix sum of squares | CONFIRMED (all 52) |
| Per level | N×N byte matrix, N = `LevelKutu[i]`; diagonal `m[i][i] = i+1` (1-based box id) | CONFIRMED (bytes, level 0) |
| Meaning | all-pairs next-hop table between path boxes (row/column orientation not verified) | HIGH CONFIDENCE (diagonal identity, value range 0..136 ≤ max N) |

Included by `klasik_yara.wdl` (CONFIRMED).

---

## 13. `K22.ini` (community patch)

Present only in the patched Lutris install (`[Screen]` `Mode`, `X`, `Y` with a comment about the original 1920×1200 limit), not on the ISO; the installer allowlist does not extract `.ini` (CONFIRMED, `installer/media.cpp:selected`). The patch plugin `kabus_dll.dll` exports `INI_GetVariable` / `INI_SetVariable` / `INI_IsInitialized` (CONFIRMED, `RECOVERY.txt:102-104`). Original/patch video caps 1920×1200 vs 3840×2400: CONFIRMED, second-numbered CONTRACT §39 (`CONTRACT.txt:2147-2151`). The exact patch INI grammar is UNKNOWN; the native reimplementation's grammar is described in [ASSET_FORMATS](../ASSET_FORMATS.md).

---

## 14. Gaps

| Gap | Status |
|---|---|
| WMB6 campaign texture/object reader bodies (`00459eb0` beyond type 3/7, texture section) | only partially exported; layouts above marked HIGH CONFIDENCE where native acceptance is the evidence |
| MDL7 format 6/7 skin decoding after preprocessing | UNKNOWN (§20) |
| `mdc.k22`, `msc.k22` consumer | UNKNOWN |
| Original decoder for RIFF-wrapped Ogg | UNKNOWN |
| Save byte truncation for values > 255 | UNKNOWN for the original engine |
