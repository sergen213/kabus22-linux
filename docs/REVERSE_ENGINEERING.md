# Reverse engineering

How the original Kabus 22 binaries, scripts and data were analysed for the native Linux port, how to reproduce the analysis, and an index of the detailed documents.

Related: [ENGINE_ARCHITECTURE](ENGINE_ARCHITECTURE.md) · [GAMEPLAY_SYSTEMS](GAMEPLAY_SYSTEMS.md) · [ASSET_FORMATS](ASSET_FORMATS.md) · [CAMERA_SYSTEM](CAMERA_SYSTEM.md)

## Document index

| Document | Content |
|---|---|
| [reverse-engineering/EXECUTABLE_ANALYSIS.md](reverse-engineering/EXECUTABLE_ANALYSIS.md) | PE headers, sections, imports, compiler evidence, community-patch byte diff, SHA-256 inventory of the ISO edition |
| [reverse-engineering/ENGINE_IDENTIFICATION.md](reverse-engineering/ENGINE_IDENTIFICATION.md) | A6 6.31.4 campaign / A7 7.05.4 Bonus identification, APIs, compression, confidence-labelled |
| [reverse-engineering/FUNCTION_MAP.md](reverse-engineering/FUNCTION_MAP.md) | Recovered original functions (address, purpose, callers/callees, native counterpart) |
| [reverse-engineering/DATA_STRUCTURES.md](reverse-engineering/DATA_STRUCTURES.md) | In-memory layouts, tables, saved-file layouts |
| [reverse-engineering/SCRIPT_SYSTEM.md](reverse-engineering/SCRIPT_SYSTEM.md) | WDL compiler/runtime: 22.10 fixed point, WAIT/coroutines, locals, registry, native calls, input |
| [reverse-engineering/RESOURCE_FORMATS.md](reverse-engineering/RESOURCE_FORMATS.md) | Original on-disk formats with recovering addresses |
| [reverse-engineering/ORIGINAL_BEHAVIOR.md](reverse-engineering/ORIGINAL_BEHAVIOR.md) | Observed vs assumed behaviour, with evidence images/logs |
| [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md) | Native module map, frame loop, threading, backends |
| [GAMEPLAY_SYSTEMS.md](GAMEPLAY_SYSTEMS.md) | Characters, combat, enemies, inventory, puzzles, saves: script side and required engine services |
| [ASSET_FORMATS.md](ASSET_FORMATS.md) | How the native port loads each format; texture-pack format |
| [CAMERA_SYSTEM.md](CAMERA_SYSTEM.md) | Script-driven VIEW/camera, ARC/ASPECT, widescreen handling |

The primary evidence ledger is `reverse/decompiled/runtime/CONTRACT.txt` (numbered sections; cited as `CONTRACT.txt §N` or `CONTRACT.txt:LINE`). Paths under `reverse/` other than `reverse/scripts/*.java` are a private workspace and are not in the public repository (`.gitignore` lines 12-25, 512-513), because they contain decompiled proprietary code, extracted scripts and media.

## Confidence labels

All reverse-engineering documents use: **CONFIRMED** (direct evidence: bytes, decompiled code plus instructions, or recorded native runs), **HIGH CONFIDENCE** (strong indirect evidence), **HYPOTHESIS** (plausible, untested), **UNKNOWN** (not established; what is missing is stated). Native-port observations are never presented as observations of the original Windows game. Since 2026-10-09 the unmodified ISO `K22.exe` (SHA-256 `cc193c6b…4ebd`) has also been executed under Wine 11.19 (wined3d) as a reference tool only (see Dynamic analysis); before that, it had never been executed.

## Dynamic analysis (original under Wine)

`tools/wine-reference/wineref.py` (`setup` / `run`) extracts the ISO payload with innoextract into a private scratch tree, creates a private Wine prefix (X11 driver, drive `K:` mapped to the game copy, original saves copied in) and runs `K22.exe -nc -diag -d defiae_d3d -d defdil_en -d defses_en` windowed at the authored 1024×768 (`options.k22` byte 0 = 2, no `deftam_d3d`). It replays the same `tests/reference/*.k22s` cases the native engine runs (keys by X keysym through XTest, `#wine:` lines for timing) and captures the exact window with `ffmpeg -f x11grab -window_id`. Findings: the ISO `K22.exe` fails A6's own executable checksum (`0041fc00`/`00422af0`: stored 233761058, actual byte sum 233546425) and stops with `Error E356: Startup failure`; `-nc` is the stock A6 option that skips that check (`00412b90` sets `0x644848`), no binary is patched. A6 also stalls in its compile loop while its window is unfocused, so the driver re-activates the window. Nothing in the native build or runtime uses Wine; captures stay in `reverse/wine-reference/` (private).

## Inputs (read-only)

| Input | Use |
|---|---|
| `Kabus-22_Win_TR-EN-DE_ISO-Version/Kabus 22.iso` | ISO edition "Kabus 22 Multi 3" (Inno Setup 5.2.1); only source for native installs |
| `setup.exe` + `setup-1.bin` | different "Kabus 22 Test" build (Inno 5.1.10); not used for engine analysis |
| `Kabus22Patch/` | community "Bug Fix and Widescreen Support" patch; its `K22.exe` is the Ghidra-analysed A6 binary (6 `.text` bytes differ from ISO, see [EXECUTABLE_ANALYSIS](reverse-engineering/EXECUTABLE_ANALYSIS.md)) |
| `/mnt/1st4TB/Lutris Games/kabus22/…/Kabus 22/` | patched Windows install (source of `RenkAyar.dll` import and of `reverse/extracted/original/*.wdl`) |
| `~/.local/share/kabus22/game/` | native installation's verbatim ISO data |

Originals are never modified; mutation tests use private copies; `objcopy` must not be pointed at originals (it rewrites its input when no output is given).

## Tools

| Tool | Version / location | Purpose |
|---|---|---|
| Ghidra | 12.1.2, `/opt/ghidra` (`support/analyzeHeadless`) | headless import/analysis, read-only exports |
| Ghidra project | `reverse/ghidra/Kabus22.gpr` — programs `K22.exe` (patched), `kabus_dll.dll`, `RenkAyar.dll` (`reverse/ghidra/Kabus22.rep/idata/~index.dat`) | shared read-only analysis database; logs `reverse/ghidra/*.log` |
| Separate private projects | ISO `K22.exe` (`CONTRACT.txt:2144-2145`), A7 `acknex.dll` (`reverse/decompiled/runtime/bonus-core/CORE_CONTRACT.txt`) | keep original and patch/A7 analyses apart |
| innoextract | 1.9, `packaging/helper-innoextract/bin/amd64/innoextract` (provenance in `packaging/helper-innoextract/upstream/`) | list/extract/test Inno Setup payloads |
| 7-Zip, xorriso | system | ISO listing (`reverse/media/iso/iso-volume.txt`) |
| Python `pefile` | 2024.8.26 | PE headers, imports, Rich header, version resources |
| FFmpeg `ffprobe` | system | media probes (`reverse/media/iso/*.ffprobe.json`) |
| GDB (DAP) | system | live debugging of the native engine only (never the original) |
| Native checks | CTest: `wdl_semantics`, `asset_boundaries`, `media_boundaries`, `input_saves`, `engine_boundaries`, `material_boundaries`, `physics_boundaries` (`CMakeLists.txt` lines 73-108), `installer_security_boundaries` (`installer/CMakeLists.txt` line 52) | regression proof of recovered contracts |

## Methodology

1. **Acquire and fingerprint.** List the ISO, extract the Inno payload into a private directory with innoextract, hash every executable/archive and compare size+MD5 with the installer's own checksums (`reverse/media/iso/list-exact.txt`). Results: [EXECUTABLE_ANALYSIS](reverse-engineering/EXECUTABLE_ANALYSIS.md).
2. **Identify the engine** from embedded version strings and imports before reading code ([ENGINE_IDENTIFICATION](reverse-engineering/ENGINE_IDENTIFICATION.md)).
3. **Targeted static recovery.** Seed Ghidra exports from strings (e.g. `RESOURCE`, `WRS2`, `EXECUTE`), plugin handshakes (`A6_DLLAPI` resolver) and cross-references; export decompiled C **and** instruction listings, because the decompiler is sometimes wrong (example: the anisotropy sampler argument at `0x0043dc70` is a float bit pattern the decompiler printed as `0`, `CONTRACT.txt` §23). Every export run writes a manifest of candidates, timeouts and omissions (`reverse/decompiled/*/…recovery.txt`); timeouts are never presented as recovered code (`reverse/decompiled/RECOVERY.txt` lines 22-24).
4. **Script recovery.** Original game logic is WDL source inside WRS archives; the archive index/LZSS codec was recovered (`FUN_0041fd20`, `FUN_00414bc0`) and scripts were extracted to `reverse/extracted/original/` (Lutris build) and `reverse/media/iso/scripts/` (ISO build). Script names are cited as `file.wdl:identifier`; script text is not reproduced in documentation.
5. **Contracts.** Each recovered behaviour is written as a numbered section in `CONTRACT.txt` with the original address, the native symbol that implements it, and the exact verification performed. Later sections supersede earlier ones where stated.
6. **Native implementation and verification.** The C++20 engine in `native/` executes the original scripts; contracts are guarded by CTest suites and bounded real runs (`--frames N`, `--capture FILE.ppm`, private `XDG_*` directories, input ledgers). Evidence images/logs are kept under `reverse/` (e.g. `reverse/campaign-checkpoint-*.png`, `reverse/native-gl-bonus-*-evidence.txt`). These prove native behaviour, not original Windows parity.
7. **Boundaries.** No Wine/Proton at runtime; no redistribution of game assets, scripts or decompiled output. No clean-room or legal guarantee is claimed by these documents.

## Reproducing the Ghidra exports

Run from the repository root. All runs are `-readOnly -noanalysis` against the existing project, so the project database is not changed.

```sh
GH=/opt/ghidra/support/analyzeHeadless
# Baseline export of all three programs (RecoverKabus.java args: OUT [MAX_FUNCTIONS] [TIMEOUT_SECONDS] [ADDRESS ...])
"$GH" "$PWD/reverse/ghidra" Kabus22 -process '*' -noanalysis -readOnly \
  -scriptPath "$PWD/reverse/scripts" -postScript RecoverKabus.java "$PWD/reverse/decompiled/final" 180 20

# Loader follow-up (WRS index, resource registration, script loader, parser, version string)
"$GH" "$PWD/reverse/ghidra" Kabus22 -process K22.exe -noanalysis -readOnly \
  -scriptPath "$PWD/reverse/scripts" -postScript RecoverKabus.java "$PWD/reverse/decompiled/loaders" 120 20 \
  0041fd20 0041ff20 00420490 00419cb0 00419b10 00402d60 0041e180 0061ddb4

# Plugin escape helpers in the community kabus_dll.dll
"$GH" "$PWD/reverse/ghidra" Kabus22 -process kabus_dll.dll -noanalysis -readOnly \
  -scriptPath "$PWD/reverse/scripts" -postScript RecoverKabus.java "$PWD/reverse/decompiled/final" 180 20 \
  1008e240 1009ca20 1009cac0

# Embedded PCX (built-in font) export; args: OUT IMAGE_ADDRESS [POINTER_ADDRESS [PE_FILE ...]]
"$GH" "$PWD/reverse/ghidra" Kabus22 -process K22.exe -noanalysis -readOnly \
  -scriptPath "$PWD/reverse/scripts" -postScript ExportEmbeddedPcx.java /tmp/k22-font 0062130c 00627310

# Runtime follow-up (private script; args: OUT MAX_FUNCTIONS SEED_ADDRESS ...)
"$GH" "$PWD/reverse/ghidra" Kabus22 -process K22.exe -noanalysis -readOnly \
  -scriptPath "$PWD/reverse/decompiled/runtime" -postScript RecoverRuntime.java /tmp/k22-runtime 40 00414650
```

Commands 1-3 are recorded in `reverse/decompiled/RECOVERY.txt` lines 11-18; command 4 in the header of `reverse/scripts/ExportEmbeddedPcx.java`. A7 Bonus analysis uses private `reverse/decompiled/runtime/bonus-core/*.java` and `bonus-physics/*.java` scripts against a separate project containing `Kabus 22 Mode/acknex.dll` (SHA-256 `f96c7645…227b`). Expected outputs are Ghidra approximations that do not compile and are not engine source.

## Status of the analysis (summary)

| Area | State | Where |
|---|---|---|
| Engine identity, PE facts, hashes | complete for ISO edition | [EXECUTABLE_ANALYSIS](reverse-engineering/EXECUTABLE_ANALYSIS.md), [ENGINE_IDENTIFICATION](reverse-engineering/ENGINE_IDENTIFICATION.md) |
| WRS/LZSS, WMB6, MDL, HMP, images, fonts, audio, video | implemented natively; specific gaps listed | [RESOURCE_FORMATS](reverse-engineering/RESOURCE_FORMATS.md), [ASSET_FORMATS](ASSET_FORMATS.md) |
| WDL compiler/VM, scheduler, numeric | implemented; known gaps in `CONTRACT.txt` §8 | [SCRIPT_SYSTEM](reverse-engineering/SCRIPT_SYSTEM.md) |
| Gameplay | original scripts run on the native engine; no complete campaign or Bonus playthrough verified (README) | [GAMEPLAY_SYSTEMS](GAMEPLAY_SYSTEMS.md), [ORIGINAL_BEHAVIOR](reverse-engineering/ORIGINAL_BEHAVIOR.md) |
