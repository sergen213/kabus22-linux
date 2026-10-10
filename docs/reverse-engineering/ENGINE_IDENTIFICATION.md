# Engine identification

Which engine generations, compilers, APIs and container formats the original Kabus 22 binaries use, each statement labelled by confidence.

Related: [REVERSE_ENGINEERING index](../REVERSE_ENGINEERING.md) · [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md) · [SCRIPT_SYSTEM](SCRIPT_SYSTEM.md) · [RESOURCE_FORMATS](RESOURCE_FORMATS.md) · [FUNCTION_MAP](FUNCTION_MAP.md) · [ENGINE_ARCHITECTURE](../ENGINE_ARCHITECTURE.md)

Labels: **CONFIRMED** = read directly from bytes/decompiled code; **HIGH CONFIDENCE** = strong indirect evidence; **HYPOTHESIS** = plausible, unverified; **UNKNOWN** = not established. Hashes are listed in [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md).

## Summary

| Component | Identification | Label | Evidence |
|---|---|---|---|
| Campaign engine | Conitec Acknex / 3D GameStudio **A6, version 6.31.4**, statically linked into `K22.exe` | CONFIRMED | strings `A6 engine` at VA `0x0061dda8` and `6.31.4` at `0x0061ddb4` in ISO `K22.exe` (computed, same VAs as patched build); logged by `FUN_00413880` ("Log of %s %s run at %s"): `reverse/decompiled/final/K22.exe.00413880.c`, `reverse/decompiled/RECOVERY.txt` "Engine version" |
| Bonus mode engine ("Kabus 22 Mode") | Conitec **A7 engine 7.05.4** in `Kabus 22 Mode/acknex.dll`, driven by stub `K22_mode.exe` | CONFIRMED | strings `A7 Engine` VA `0x101aa084`, `7.05.4` VA `0x101aa090`; version resource `FileVersion 7, 0, 5, 1`, `CompanyName Conitec` (computed); `reverse/decompiled/runtime/bonus-core/CORE_CONTRACT.txt` |
| Edition | Inno Setup "Kabus 22 Multi 3" (EN/DE/TR), media dated 2008-03 | CONFIRMED | `reverse/media/iso/info.txt` |
| Campaign executable packing | ISO `K22.exe` is **not** packed (plain `.text/.rdata/.data/.rsrc`); only the launchers `Kabus22.exe` / `Kabus22_mode.exe` are UPX-packed Multimedia Builder programs | CONFIRMED | pefile sections; [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md) |
| Analysed binary vs ISO binary | Ghidra project analyses the community-patched `K22.exe` (`1a6a0dbc…`). It differs from ISO `K22.exe` (`cc193c6b…`) in 6 `.text` bytes (start-up/archive branches), 19 `.data` bytes and resources; `.rdata` identical | CONFIRMED | byte comparison in [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md#community-patch-binaries-not-part-of-the-iso); `reverse/decompiled/runtime/CONTRACT.txt:2147-2154` |
| Compiler | Microsoft Visual C++ .NET 2003 (7.1), static CRT, for `K22.exe` and `acknex.dll` | HIGH CONFIDENCE | linker version 7.10, Rich-header build 3077 entries, no `MSVCR*` import |
| Rendering API | Direct3D 9 (`Direct3DCreate9` only `d3d9.dll` import); D3DX9 statically linked in A6 (`D3DX9 Shader Compiler 5.04.00.2904`), dynamically (`d3dx9_30.dll`, 9.12.589.0000) in A7; shader profiles up to `vs_2_0`/`ps_3_0` named in A6 | CONFIRMED | imports/strings; [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md#k22exe-imports-255-functions) |
| Target DirectX runtime | DirectX 9.0c (string `DirectX 9.0c`; ISO ships the DirectX redistributable with d3dx9 24…34 cabs) | HIGH CONFIDENCE | `K22.exe` string; `7z l` of ISO `DirectX/` |
| Input / audio APIs | DirectInput 8 (`DirectInput8Create`), WINMM joystick (`joyGetPosEx`), DirectSound (`DSOUND` ordinals) | CONFIRMED | imports |
| Physics | ODE (Open Dynamics Engine) embedded in A6/A7 (`ODE INTERNAL ERROR %d`, `ODE Message` strings; `ph_*` script API) | HIGH CONFIDENCE | `K22.exe` file offset `0x20dcd0`; `acknex.dll` strings. Native port also uses ODE (`native/physics.cpp`) |
| Collision for A6 movement | A6 BSP/hull queries for `c_move`/`trace`, not ODE | HIGH CONFIDENCE | `CONTRACT.txt` §36, §46, §48 (lines 1456-1489, 1878-1910, 1979-2006) |
| Networking | WinSock (`WSOCK32`) session layer in engine; HTTP plugin `K22_0.dll` (Delphi) for Bonus online features | CONFIRMED (imports/exports); not used by native port |

## Scripting

| Statement | Label | Evidence |
|---|---|---|
| Game logic is C-Script/WDL source, compiled by the engine at load time (no precompiled bytecode on disk) | CONFIRMED | parser driver `FUN_0041aef0` handles `BIND/LEVEL/SAVEDIR/PLUGINDIR/INCLUDE/PRINT/HALT/RESOURCE`; `FUN_00419cb0` loads text and passes it to the parser; `FUN_0041b5d0` parses built-in C-Script text at `0x005d3348` (`reverse/decompiled/RECOVERY.txt` lines 39-68) |
| Statement compiler emits opcodes through `FUN_004012d0`; `FUN_00402fe0` is the statement dispatcher (WHILE/BREAK/CONTINUE/locals/EXECUTE) | CONFIRMED | `reverse/decompiled/RECOVERY.txt` line 72 |
| Numbers are fixed-point 22.10 (`var`), range ±2097151.999, quantum 1/1024 | CONFIRMED | `CONTRACT.txt` §5 (lines 172-225), `reverse/decompiled/runtime/numeric/` |
| Coroutines: `wait(n)` / `waitt(n)` suspend a script instance; the time base is 16 ticks per second | CONFIRMED | `CONTRACT.txt` §1-§3, §7 line 291-292 |
| A6 API surface: 317 native call rows; A7: 394 rows / 392 normalised names (312 common, 5 A6-only, 80 A7-only) | CONFIRMED | `CONTRACT.txt:1650-1652`; `reverse/decompiled/runtime/api-descriptors.json`; `native/native_calls.inc` |
| `proc_status`, kill-all and `proc_kill2` are later-version APIs and absent from 6.31.4 | HIGH CONFIDENCE | public manual version notes cited in `CONTRACT.txt:301-304, 327-329` |
| Plugins use `A6_DLLAPI` (A6) and `A5_DLLAPI` (Delphi HTTP DLL); the engine passes an interface pointer whose resolver lives at `+0xa30` | CONFIRMED | `reverse/decompiled/RECOVERY.txt` lines 74-97; exports in [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md) |
| Bonus entry is the configured name `K22_mode` (static string `0x101ad0b0`), with preprocessor defines `defmode_d3d`, `defdil_*`, `defanti_*` from the launcher | CONFIRMED | `reverse/decompiled/runtime/bonus-core/entry/entry-contract.txt` lines 15-28 |

Details: [SCRIPT_SYSTEM](SCRIPT_SYSTEM.md).

## Containers and compression

| Statement | Label | Evidence |
|---|---|---|
| Campaign archives use `WRS2`: XOR-0xA5 filenames in 32-byte fields, big-endian stored/decoded lengths, `0xffffffff` terminator; legacy (non-`WRS2`) archives use 13-byte unkeyed names | CONFIRMED | `FUN_0041fd20`; `reverse/decompiled/RECOVERY.txt` lines 48-57 |
| Archive/entry limits: 128 archives (`0x7f`), 4096 entries (`0xfff`) | CONFIRMED | `FUN_0041ff20`, `FUN_0041fd20` (same source) |
| A7 Bonus archives (`K22_mode.wrs`, `mode_*.k22`) use `WRS3`; native reader uses name key `0xb5` for WRS3 | CONFIRMED (magic bytes, computed); key: CONFIRMED in native reader `native/assets.cpp` `Assets::mount` (line 259), original A7 key-derivation code not cited here (**UNKNOWN** address) |
| `.k22` is only an extension: `Kaynak.k22`, `ses_*.k22`, `yazi_*.k22` are WRS2 archives, `v*.k22`/`kn_arg.k22` are RIFF AVI (Indeo 5 `IV50`, MP3 audio), others are text/binary settings | CONFIRMED | magic bytes of `~/.local/share/kabus22/game/*.k22`; `reverse/media/iso/vtto.k22.ffprobe.json` |
| WRS member payloads are LZSS (4096-byte window initialised with spaces, start position `0xfee`, flag byte LSB-first, 12-bit offset / 4-bit length+3) | CONFIRMED | `reverse/decompiled/decoder/K22.exe.00414bc0.c` (window fill `0x20202020`, `0xfee`); native `Assets::decompress` in `native/assets.cpp` |
| zlib 1.1.4 is linked into `K22.exe` but is not the WRS codec | CONFIRMED (string + LZSS above); its actual consumer UNKNOWN |
| Level format is `WMB6` in both campaign and Bonus (A6 binary also knows `WMB2-5`; no `WMB7` string in either engine) | CONFIRMED | strings in both binaries (computed); native accepts only `WMB6` (`native/assets.cpp` line 448) |
| Model/terrain formats handled: `MDL4/MDL5/MDL7`, `HMP5` | CONFIRMED for the native loader (`native/assets.cpp` lines 1188-1374); which magic each original binary accepts is HIGH CONFIDENCE only (magic compared as integers, no strings) |
| Audio: Ogg Vorbis (`OggS` in engine; Bonus/campaign music `.ogg`), RIFF-wrapped Vorbis sound effects, WAV | CONFIRMED | `CONTRACT.txt` §33-§34 (lines 1358-1432); `native/media.cpp` |
| Video: AVI with Indeo 5 video and MP3 audio, played through the engine's media layer (`MEDIA_PLAY`) | CONFIRMED (file probes); the Windows playback path (DirectShow/VfW) is HYPOTHESIS | `reverse/media/iso/*.ffprobe.json`; `CONTRACT.txt` §14 |
| `acknex.wdf` is an engine/publish definition file (`WDF\0` magic, 107 KB) loaded at start-up by A7 `engine_open` `0x10034fe0` | CONFIRMED (magic, load call); content semantics UNKNOWN | `entry-contract.txt` line 15 |

Details: [RESOURCE_FORMATS](RESOURCE_FORMATS.md), [ASSET_FORMATS](../ASSET_FORMATS.md).

## Script provenance caveat

The WDL scripts used for most citations in `reverse/extracted/original/` were extracted from the Lutris install's `K22.wrs`, not the ISO `K22.wrs` (`c9e32008…`); ISO copies live in `reverse/media/iso/scripts/` and `reverse/media/iso/script-comparison.json` reports 173 of 182 files identical. **CONFIRMED** (comparison file). The native port ships no scripts; it reads them from the user's installed ISO data.

## Open questions

- **UNKNOWN**: the exact engine build of `setup.exe` ("Kabus 22 Test") — different `K22.exe` MD5, not analysed.
- **UNKNOWN**: what the `.data` fields changed at `0x621190` by the community patch control.
- **UNKNOWN**: A6 consumer of the statically linked zlib.
- **HYPOTHESIS**: Kabus22.exe (Multimedia Builder) launches `K22.exe`; the packed launcher was not unpacked.
