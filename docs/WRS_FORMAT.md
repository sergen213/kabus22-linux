# WRS resource archive format (3D GameStudio A6/A7, as used by Kabus 22)

Labels: **CONFIRMED** / **HIGH CONFIDENCE** / **HYPOTHESIS** / **UNKNOWN**. Related: [ASSET_FORMATS](ASSET_FORMATS.md) §2 · [reverse-engineering/RESOURCE_FORMATS](reverse-engineering/RESOURCE_FORMATS.md) · [COMMUNITY_PATCH_ANALYSIS](COMMUNITY_PATCH_ANALYSIS.md) · [COMMUNITY_CREDITS](COMMUNITY_CREDITS.md).

Implementations compared: `tools/wrs2.py` (this project, Python), `native/assets.cpp` `Assets::mount`/`Assets::decompress`/`Assets::read` (this project, C++), iseeeva `kabus_tools` (`reverse/community/iseeeva-kabus22/kabus_tools/src/index.ts` + QuickBMS script `src/external/gamestudio.bms` by Luigi Auriemma), and the original engine file opener `K22.exe` `00420120` (`reverse/decompiled/K22.exe.00420120.c`).

## 1. Layout

```
offset 0   char[4]  magic        "WRS2" (A6 campaign) | "WRS3" (A7 Bonus)
then, repeated until end of file (no directory, no count, no trailer):
  +0       u8[32]   name         each byte XOR key; terminated by a raw 0x00, remaining bytes 0x00 (not XOR-ed)
  +32      u32 BE   packed size  bytes of LZSS stream that follow
  +36      u32 BE   output size  decoded size
  +40      u8[packed] LZSS stream
```

| Property | Value | Evidence | Label |
|---|---|---|---|
| Name key | `0xA5` for WRS2, `0xB5` for WRS3 | `gamestudio.bms`; `native/assets.cpp:258-265`; `tools/wrs2.py` | CONFIRMED |
| Byte order | big-endian sizes (unusual for an x86 engine) | all three implementations; every Kabus archive decodes | CONFIRMED |
| Empty member | packed = output = 0; mixed zero/non-zero rejected by both project readers | `tools/wrs2.py`, `native/assets.cpp:267` | CONFIRMED (readers); engine behaviour UNKNOWN |
| Name length | ≤ 32 bytes; longest observed 27 (`Kaynak.k22`) / 21 (`K22Widescreen.wrs`) | `wrs2.py list --json` | CONFIRMED |
| Separators | names are flat in every Kabus archive (no `/` or `\`); readers still normalise `\` → `/` and refuse `..`, absolute, device names | `tools/wrs2.py:safe_name`, `native/assets.cpp` `safe_name` | CONFIRMED |
| Integrity | none: no checksum, no signature, no encryption of member data; the name XOR is the only obfuscation | format above | CONFIRMED |

Other GameStudio variants handled only by QuickBMS (none occurs in Kabus 22): version byte `< '2'` derives a one-byte key from the first name assuming a 4-character `.ext` (`key = name[len-4] ^ '.'`); `"WRS4"` stores a big-endian index offset after the magic, an LZSS-compressed index of `{name[32]^0xAA, packed, size}` entries, and member data packed from offset 8. **CONFIRMED** (script text), unverified against real files.

## 2. Compression (LZSS)

Okumura LZSS, per member, no shared dictionary:

| Parameter | Value | Label |
|---|---|---|
| Ring buffer | 4096 bytes, initialised to `0x20` (space) | CONFIRMED |
| Initial write position | 4078 (= 4096 − 18) | CONFIRMED |
| Flag byte | 8 tokens, least-significant bit first; 1 = literal byte, 0 = reference | CONFIRMED |
| Reference | 2 bytes `lo, hi`: absolute ring position `lo | (hi & 0xF0) << 4`, length `(hi & 0x0F) + 3` (3…18) | CONFIRMED |
| Termination | stream ends when the packed byte count is consumed; output must equal the declared size | CONFIRMED |

Evidence: `tools/wrs2.py:decode_lzss`, `native/assets.cpp:278-295`; every member of all seven ISO archives (`K22.wrs` 182, `Kaynak.k22` 3 005, `yazi_{tr,en,de}.k22` 52 each, `ses_{tr,en}.k22` 14 each), the patch `K22Widescreen.wrs` (190) and the Lutris `K22.wrs` (179) decoded to the declared sizes with `tools/wrs2.py` during this analysis (`reverse/community/reports/wrs-index-*.txt`, `reverse/community/wrs/`). Compression ratio: `Kaynak.k22` 2 251 119 916 → 976 156 068 bytes; `K22Widescreen.wrs` 8 264 697 → 2 185 549. **CONFIRMED.**

## 3. Engine use

| Operation | Original A6 (`K22.exe`) | Label |
|---|---|---|
| Start-up archive | the executable's embedded base name (`.data 0x621240`: `K22`, patched `K22Widescreen`) selects `<name>.wrs` holding `<name>.wdl` | CONFIRMED (bytes + `0041fc00`) |
| Mounting | WDL `resource "file";` statements register more archives, in source order (`K22.wdl` line 1: `kaynak.k22`, then `yazi_xx.k22`, `ses_xx.k22` under `IFDEF defdil_*/defses_*`) | CONFIRMED (scripts) |
| Lookup | `00420120`: for non-`.WDL` names, a case-insensitive (`_stricmp`) scan of the registered table (entries of 32-byte name + data pointer, stride 0x24, `DAT_00695eb0`, count `DAT_006bb1c0`) returns the **first** match; only if none matches is a loose file opened (`_findfirst`) | HIGH CONFIDENCE (decompiled control flow) |
| Scripts | `.WDL` names take a separate branch controlled by start-up flags `0x62119c`/`0x621268` (archive mode vs protected loose sources, `"- script modified"`) | HIGH CONFIDENCE |

Native (`native/assets.cpp`, `native/main.cpp`):

| Operation | Native | Difference | Label |
|---|---|---|---|
| Loose files | every regular file under the data root is indexed case-insensitively at start (`Assets::Assets`, symlinks ignored) | — | CONFIRMED |
| Mount | `Assets::mount` scans headers, validates names/sizes/padding, rejects duplicates inside one archive and file/directory conflicts, then `insert_or_assign` (campaign) → **later archive wins**; Bonus uses `try_emplace` (first wins) | campaign precedence is the reverse of the original | CONFIRMED |
| Read | loose file first, archive entry second (`Assets::read`) | original: archive first | CONFIRMED |
| Mount order | `K22.wrs`, `Kaynak.k22`, `K22Widescreen.wrs` (each if present), then script `resource` statements (idempotent) | — | CONFIRMED |
| Observable today | duplicates across ISO + v1.8.5 archives: `PALETTE.RAW` (byte-identical in all six ISO archives) and `LY_Fade.bmp` (the patch copy wins in both models); no loose file shadows a member | none | CONFIRMED |

## 4. Tool comparison

| Capability | `tools/wrs2.py` | native `Assets` | iseeeva `kabus_tools` |
|---|---|---|---|
| List | yes (`list`, `--json`) | internal | via QuickBMS (`export` only) |
| Extract | yes, streaming, exclusive create, symlink-safe, 128 MiB default member cap | n/a | QuickBMS `-o` (overwrites) |
| Pack | no | no | yes: WRS2 only, flat directories only, names XOR `0xA5`, BE sizes, LZSS via QuickBMS `lzss_compress` |
| Versions | WRS2 | WRS2, WRS3 | read WRS1…4, write WRS2 |
| Validation | magic, truncation, padding, duplicates (casefold), unsafe paths, LZSS bounds | same class + per-asset size limit | none |
| Platform | any Python 3 | Linux build | Windows (`quickbms.exe`), Node ≥ 18 |

Observations on `kabus_tools` (all **CONFIRMED** from source unless noted):

- `import` runs QuickBMS with `-o` on each input file, writing the compressed result back over the **input file in the source directory**, then reads it from there: packing destroys the unpacked tree it was given. **HIGH CONFIDENCE** (QuickBMS writes `FNAME` into the output folder, which is the input folder).
- The name-length check rejects > 32 bytes but the message says "36 bytes or fewer"; a 32-byte name is written without terminator (both project readers accept it).
- An empty directory produces a zero-length file without magic.
- QuickBMS `lzss_compress` output compatibility with the engine's decoder is assumed by the author's workflow; no project test covers it. **HYPOTHESIS.**

Decision: no new Linux tool was added. The only capability missing from `tools/wrs2.py` is packing, and the native engine does not need repacked archives — loose files under the game root already take precedence over archive members (`Assets::read`). **CONFIRMED** (code); see [COMMUNITY_PATCH_ANALYSIS](COMMUNITY_PATCH_ANALYSIS.md) §7.2 for the precedence caveat.

## 5. Verification run

```
$ python3 tools/check_wrs2.py
WRS2 checks passed            (exit 0, 2026-10-09)
```

`tools/check_wrs2.py` covers LZSS literal/reference decoding, space-initialised window, truncated/oversized streams, magic, padding, duplicate and case-folded names, unsafe paths and exclusive extraction. **CONFIRMED.**
