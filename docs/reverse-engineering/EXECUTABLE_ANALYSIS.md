# Executable analysis

Static PE facts for the original Kabus 22 ISO edition and the community patch, with SHA-256 values computed for this document (2026-10-09).

Related: [REVERSE_ENGINEERING index](../REVERSE_ENGINEERING.md) · [ENGINE_IDENTIFICATION](ENGINE_IDENTIFICATION.md) · [FUNCTION_MAP](FUNCTION_MAP.md) · [RESOURCE_FORMATS](RESOURCE_FORMATS.md) · [ENGINE_ARCHITECTURE](../ENGINE_ARCHITECTURE.md)

Confidence labels: **CONFIRMED** (file bytes / decompiled code), **HIGH CONFIDENCE**, **HYPOTHESIS**, **UNKNOWN**. Paths under `reverse/` are private workspace evidence (only `reverse/scripts/*.java` is published, see `.gitignore`). No original executable was run; all facts below come from reading bytes.

## Method (reproducible)

1. ISO members were listed with `7z l "Kabus-22_Win_TR-EN-DE_ISO-Version/Kabus 22.iso"` and copied into a private temporary directory (deleted afterwards).
2. Inno Setup payload was inspected and selectively extracted with the pinned helper `packaging/helper-innoextract/bin/amd64/innoextract` (innoextract 1.9): `-i JPN-K22.exe`, `--list --list-sizes`, `--extract -I app/K22.exe …`.
3. PE headers, sections, imports/exports, Rich header and version resources were parsed with Python `pefile` 2024.8.26; hashes with `hashlib.sha256`. `objcopy` was never run on originals.
4. Integrity check: MD5 and size of every hashed file were compared with the installer's own checksums recorded in `reverse/media/iso/list-exact.txt` (innoextract `--list-checksums`). **CONFIRMED** 79/79 match.

## Edition identification

| Item | Value | Confidence / evidence |
|---|---|---|
| Disc image | `Kabus 22.iso`, 2,089,629,632 data bytes in 76 files; volume id `K22_Y_K_MG`, App Id `NERO BURNING ROM` | CONFIRMED: `reverse/media/iso/iso-volume.txt`, `7z l` |
| ISO SHA-256 | `0fb722388128a9c2ebd7e15849ec0392eab752b5622dd4375ff0580a336b5df1` | CONFIRMED (computed) |
| Distribution zip SHA-256 | `Kabus-22_Win_TR-EN-DE_ISO-Version.zip` `e303ed28e86934bbd920ee5480a77dfca5ba17bb720f240ab7ce48cca420550e` | CONFIRMED (computed) |
| Installer | Inno Setup "Kabus 22 Multi 3", setup data version 5.2.1, languages english/german/turkce | CONFIRMED: `reverse/media/iso/info.txt`, `data-version.txt` |
| Installer build dates | ISO members dated 2008-03-08 (`JPN-K22*.bin/.exe`); bundled DirectX redistributable cabs up to `JUN2007_*` | CONFIRMED: `7z l` |
| Copyright string | `KABUS 22 © 2008 SON ISIK LTD.` in `Kabus22.exe` version resource | CONFIRMED (pefile) |

ISO installer sources (computed):

| ISO member | Size | SHA-256 |
|---|---|---|
| `JPN-K22.exe` | 344953 | `15c0d8d10003bebddb5e1606e9fb5965e1d7dea28bf5cf4d42db6924ac9d425e` |
| `JPN-K22-1.bin` | 729654912 | `06786294f991e67a767f171bb12f9405bfef8827550068b3dc6d1c3335e9730e` |
| `JPN-K22-2.bin` | 730000000 | `0a502121ac99c91dc1f02d9e5656ca104fd57b5ff3aae56debe90d78c778d008` |
| `JPN-K22-3.bin` | 565412946 | `d46b07f10be2a12749098f69b5155b6bf53665a1151d8d78b08809fdfa5445f2` |
| `autorun.inf` | 48 | `e0e351d8e4ff64095b69dcb5eb24ea7487ad56a39575fc7d9e7f59518d92eac3` |
| `K22_icon.ICO` | 2238 | `931b6e877fe6a17bec5ac570ee0a175518fb4fc47d116cf4903974aec93e8e68` |

Other local media (not the ISO edition):

| File | SHA-256 | Notes |
|---|---|---|
| `setup.exe` | `afbabd42f2a5e6cbc886aec664e266cd2537a1ab5c6f6a18bbc65d8d258860ef` | Inno "Kabus 22 Test", data version 5.1.10, Turkish only (`reverse/media/setup-info.txt`). Its `app/K22.exe` MD5 `03134ed048c0e002acc7448e62191d50` differs from the ISO `K22.exe` MD5 `e3fb53c9482bc2ad45c99e1edfa58656` (`reverse/media/setup-list.txt`, `reverse/media/iso/windows-pe-research-manifest.json`). **CONFIRMED** different build; not analysed further (**UNKNOWN** engine version). |
| `setup-1.bin` | `c41cc0d702aab9d473521962c3ea632d6c8706c047fb87bc4d578b4c96ba8c08` | payload of `setup.exe` |
| `Kabus_22_Multi_Version_Patch_1.8.5.7z` | `9ddb51861824dba454b1fb21123eecfe2af606fc4d39a0a5600346bfb8b63670` | community patch archive (see below) |

## PE summary (ISO edition)

All binaries are PE32, machine `0x14c` (i386), subsystem 2 (Windows GUI). **CONFIRMED** (pefile).

| File | Size | Link timestamp (UTC) | Linker | Entry VA | Sections | Role |
|---|---|---|---|---|---|---|
| `K22.exe` | 2347008 | 2004-11-18T19:33:46Z | 7.10 | `0x5bb38b` | .text, .rdata, .data, .rsrc | A6 campaign engine (static) |
| `Kabus22.exe` | 1244972 | 2005-11-07T13:10:28Z | 6.0 | `0x58c7e0` | UPX0, UPX1, .rsrc | UPX-packed Multimedia Builder launcher |
| `RenkAyar.dll` | 49152 | 2006-07-14T16:30:32Z | 6.0 | `0x100023e8` | .text, .rdata, .data, .reloc | colour/gamma plugin (A6_DLLAPI) |
| `OggDS0995.exe` | 475844 | 2002-04-28T23:26:07Z | 6.0 | `0x404670` | .text, .rdata, .data, .rsrc | third-party Ogg DirectShow filter installer |
| `Kabus 22 Mode/acknex.dll` | 2007040 | 2007-08-14T10:37:33Z | 7.10 | `0x10146d24` | .text, .rdata, .data, .rsrc, .reloc | A7 Bonus engine DLL |
| `Kabus 22 Mode/K22_mode.exe` | 23552 | 2007-03-20T11:48:45Z | 7.10 | `0x40105b` | .text, .rdata, .data, .rsrc | A7 engine stub (engine_open/frame/close) |
| `Kabus 22 Mode/Kabus22_mode.exe` | 1549860 | 2005-11-07T13:10:28Z | 6.0 | `0x58c7e0` | UPX0, UPX1, .rsrc | UPX-packed Bonus launcher |
| `Kabus 22 Mode/K22_0.dll` | 320512 | 1992-06-19T22:22:17Z | 2.25 | `0x447268` | CODE, DATA, BSS, .idata, .edata, .reloc, .rsrc | Delphi HTTP plugin (A5_DLLAPI + HTTP_*) |
| `Kabus 22 Mode/K22_1.dll` | 49152 | 2006-07-14T16:30:32Z | 6.0 | `0x100023e8` | .text, .rdata, .data, .reloc | byte-identical copy of RenkAyar.dll |
| `Kabus 22 Mode/d3dx9_30.dll` | 2388176 | 2006-03-31T19:32:20Z | 7.10 | `0x614f6c` | .text, .data, .rsrc, .reloc | Microsoft D3DX9 runtime (FileVersion 9.12.589.0000) |

Role notes:
- `K22.exe` is the campaign engine itself (`A6 engine` / `6.31.4` strings, see [ENGINE_IDENTIFICATION](ENGINE_IDENTIFICATION.md)). It has no version resource (`.rsrc` virtual size `0x10`) and no debug directory. **CONFIRMED**. Its entry `0x5bb38b` is the MSVC CRT start-up exported as `reverse/decompiled/K22.exe.005bb38b.c` / `reverse/decompiled/runtime/K22.exe.005bb38b.c`. **CONFIRMED**.
- `Kabus22.exe`: `UPX0/UPX1` sections, `2.01\0UPX!` marker, version resource `Comments="Created with Multimedia Builder version 4.9.7.8"`, `FileVersion 6.11.0000.1`, `CompanyName SON ISIK LTD.`. **CONFIRMED** packed menu/autorun launcher, not the engine. That it starts `K22.exe` is **HYPOTHESIS** (packed code not unpacked; `reverse/decompiled/RECOVERY.txt` "Engine version" paragraph explicitly does not generalise to it).
- `Kabus 22 Mode/Kabus22_mode.exe` (`FileVersion 1.0.4.0`) is the same Multimedia Builder/UPX family; its zlib overlay at file offset `0x7f618` decodes to launcher records that start `<SrcDir>\K22_mode.exe`. **CONFIRMED**: `reverse/decompiled/runtime/bonus-core/entry/entry-contract.txt` lines 7-14.
- `K22_mode.exe` `WinMain 0x00401000` calls `engine_open`, loops `engine_frame`, then `engine_close`, imported from `acknex.dll` (IAT `0x004050c4/c8/cc`). **CONFIRMED**: imports above + `entry-contract.txt` line 13.
- `K22_0.dll`: sections `CODE/DATA/BSS`, linker 2.25, timestamp `0x2A425E19` (1992-06-19) — the fixed Borland Delphi stamp; exports `A5_DLLAPI`, `HTTP_Create/Free/Get/IsWorking/Post/Results/SaveToFile`. **CONFIRMED** exports; Delphi origin **HIGH CONFIDENCE**. Online Bonus features use it; the native port implements no network path (`entry-contract.txt` line 30).
- `RenkAyar.dll` and `Kabus 22 Mode/K22_1.dll` are byte-identical (SHA-256 `9c9e9ea6…a09a3e`); exports `A6_DLLAPI`, `dl_PaKaGa`, `dl_Reset`. **CONFIRMED**. Behaviour: `reverse/decompiled/RenkAyar.behavior.txt`.
- `OggDS0995.exe` (2002) is the third-party OggDS DirectShow filter installer shipped for Ogg playback. **HIGH CONFIDENCE** (filename; not unpacked).

### K22.exe sections

| Name | RVA | Virtual size | Raw size | Entropy |
|---|---|---|---|---|
| `.text` | 0x1000 | 0x1ce7bd | 0x1cf000 | 6.78 |
| `.rdata` | 0x1d0000 | 0x4a976 | 0x4b000 | 5.61 |
| `.data` | 0x21b000 | 0x118938 | 0x21000 | 4.33 |
| `.rsrc` | 0x334000 | 0x10 | 0x1000 | 0.0 |

ImageBase `0x400000`, OS version 4.0, characteristics `0x10f` (relocs stripped, executable, 32-bit; **not** large-address-aware). `.data` virtual size (`0x118938`) is ~8.5× its raw size, i.e. large zero-initialised globals (registry/entry tables such as `0x00665ea8`, see [DATA_STRUCTURES](DATA_STRUCTURES.md)). **CONFIRMED**.

### K22.exe imports (255 functions)

| DLL | Count | Examples |
|---|---|---|
| `DSOUND.dll` | 2 | `#1`, `#2` |
| `WINMM.dll` | 18 | `timeEndPeriod`, `timeBeginPeriod`, `timeKillEvent`, `timeGetTime`, `mciSendCommandA`, `joyGetPosEx`, `joyGetDevCapsA`, `joyGetNumDevs` … |
| `DINPUT8.dll` | 1 | `DirectInput8Create` |
| `WSOCK32.dll` | 3 | `gethostbyname`, `gethostname`, `inet_ntoa` |
| `d3d9.dll` | 1 | `Direct3DCreate9` |
| `KERNEL32.dll` | 128 | `QueryPerformanceFrequency`, `WideCharToMultiByte`, `CloseHandle`, `VirtualProtect`, `VirtualFree`, `VirtualQuery`, `GetSystemInfo`, `WaitForMultipleObjects` … |
| `USER32.dll` | 54 | `MsgWaitForMultipleObjects`, `GetQueueStatus`, `PostThreadMessageA`, `SetMenu`, `ClipCursor`, `PostQuitMessage`, `GetMenu`, `SystemParametersInfoA` … |
| `GDI32.dll` | 38 | `CreateCompatibleBitmap`, `SetBkMode`, `GetTextMetricsA`, `CreateSolidBrush`, `SetTextColor`, `GetTextExtentPointA`, `SetStretchBltMode`, `SetDIBitsToDevice` … |
| `ADVAPI32.dll` | 4 | `GetUserNameA`, `RegOpenKeyA`, `RegQueryValueExA`, `RegCloseKey` |
| `ole32.dll` | 6 | `CoCreateInstance`, `CoUninitialize`, `CoTaskMemAlloc`, `CoFreeUnusedLibraries`, `CoTaskMemFree`, `CoInitialize` |

DirectX evidence for `K22.exe` (**CONFIRMED** unless noted):
- Only `Direct3DCreate9` from `d3d9.dll`, `DirectInput8Create` from `DINPUT8.dll`, `DSOUND.dll` ordinals 1/2 (`DirectSoundCreate`/`DirectSoundEnumerateA` by ordinal — **HIGH CONFIDENCE** ordinal mapping).
- D3DX9 is statically linked: strings `Microsoft (R) D3DX9 Shader Compiler 5.04.00.2904`, `Microsoft (R) D3DX9 Shader Assembler 5.04.00.2904`, `Direct3DShaderValidatorCreate9`, shader profiles `vs_1_0…vs_2_0`, `ps_1_0…ps_3_0`; no `d3dx9_*.dll` import. Mapping of D3DX 5.04.00.2904 to a specific DirectX SDK release is **HYPOTHESIS** (2004-era DirectX 9.0c SDK).
- String `DirectX 9.0c` present. Joystick via WINMM `joyGetPosEx`/`joyGetDevCapsA` and DirectInput8; CD/MCI via `mciSendCommandA`; timing via `timeGetTime`/`timeSetEvent`/`timeBeginPeriod`. **CONFIRMED** imports.
- `WSOCK32` (`gethostbyname`, `gethostname`, `inet_ntoa`) for the engine's multiplayer/session layer (`Acknex Session` string). Not used by the native port. **CONFIRMED** imports; use **HIGH CONFIDENCE**.
- zlib `inflate 1.1.4 Copyright 1995-2002 Mark Adler` is statically linked (`zlib error` strings). Its consumer inside the engine is **UNKNOWN** (WRS payloads use LZSS, not zlib: [RESOURCE_FORMATS](RESOURCE_FORMATS.md)).
- ODE strings `ODE INTERNAL ERROR %d` (file offset `0x20dcd0`) and `ODE Message` plus `ph_setgravity`, `ph_setautodisable`, `ph_selectgroup` script-API names. **CONFIRMED** strings; that A6 physics is built on the Open Dynamics Engine is **HIGH CONFIDENCE**.

### Compiler / linker evidence

- `K22.exe`, `acknex.dll`, `K22_mode.exe`, `d3dx9_30.dll`: linker version 7.10. `K22.exe` Rich header (prodid,build)×count: (105,2067)×10, (15,3077)×65, (95,2179)×3, (93,2067)×2, (11,8966)×1, (95,3077)×213, (29,9466)×7, (10,8966)×21, (93,4035)×2, (95,4035)×3, (95,2067)×12, (18,8444)×6, (96,4035)×146, (28,9178)×2, (93,2179)×14, (25,9210)×5, (1,0)×289, (96,3077)×162, (94,3052)×1, (90,3077)×1. Build 3077 entries with linker 7.10 ⇒ Microsoft Visual C++ .NET 2003 (7.1). **HIGH CONFIDENCE**. Other build numbers come from statically linked libraries/import libraries (D3DX, CRT, SDK); their individual products are **UNKNOWN**.
- No `MSVCR*.dll` import, `Microsoft Visual C++ Runtime Library` string present ⇒ static CRT. **CONFIRMED**.
- `acknex.dll` Rich header begins (105,2067)×12, (15,3077)×61, (95,2179)×3, (93,2067)×2, (11,9782)×3, (11,8966)×7, (95,3077)×195, (10,8966)×21, (28,9178)×1, (29,9466)×7, (96,4035)×2, (95,4035)×1, (93,2179)×14, (93,4035)×11, (1,0)×295, (96,3077)×172, (92,3077)×1, (94,3052)×1, (90,3077)×1 … (same toolchain family). **CONFIRMED** raw values.
- `RenkAyar.dll`, `Kabus22.exe`: linker 6.0 (MSVC 6 era). **CONFIRMED** header field; product mapping **HIGH CONFIDENCE**.

### A7 Bonus `acknex.dll`

SHA-256 `f96c7645ef41a3bc038f9ccae130c8304675c0f80eeb4a5d48558f49c329227b`, ImageBase `0x10000000`, entry `0x10146d24`, 362 named exports (script API such as `_chr`, `_str`, `_var`, `add_resource`, `add_folder`, `accelerate` … plus `engine_open/engine_frame/engine_close`). Version resource `CompanyName Conitec`, `FileVersion 7, 0, 5, 1`, `ProductName A6 Engine`. Imports:

| DLL | Count |
|---|---|
| `DSOUND.dll` | 2 |
| `WINMM.dll` | 18 |
| `DINPUT8.dll` | 1 |
| `WSOCK32.dll` | 3 |
| `d3dx9_30.dll` | 35 |
| `d3d9.dll` | 1 |
| `KERNEL32.dll` | 116 |
| `USER32.dll` | 56 |
| `GDI32.dll` | 25 |
| `ADVAPI32.dll` | 9 |
| `ole32.dll` | 6 |
| `d3dxof.dll` | 1 |

Unlike `K22.exe`, the A7 DLL imports D3DX dynamically from the shipped `d3dx9_30.dll` (FileVersion 9.12.589.0000) and `d3dxof.dll`. **CONFIRMED**.

## Community patch binaries (not part of the ISO)

Origin: `Kabus22Patch/` and the patched Lutris install `/mnt/1st4TB/Lutris Games/kabus22/drive_c/Program Files (x86)/Merscom/Kabus 22/` (also contains ReShade files and a `d3d9.dll` entry that is not a readable regular file). Tool sources: `reverse/community/iseeeva-kabus22/README.md` ("Kabus 22 Bug Fix and Widescreen Support" mod).

| File | Size | SHA-256 | Facts |
|---|---|---|---|
| `Kabus22Patch/K22.exe` | 2539520 | `1a6a0dbc84a1a7aa053f432f77200bfd4d98377de048c780569ab6c8fe4c7bed` | Same link timestamp/entry as ISO `K22.exe`. This is the binary in the shared Ghidra project (`reverse/ghidra/analysis.log` IMPORTING line). |
| `Kabus22Patch/K22_4GB.exe` | 2539520 | `f7e55d1843fc68cca13c982672d11d909a3f0219ad65e50222d8a25cbaa5f8db` | Differs from patched `K22.exe` in 2 bytes: characteristics `0x12f` (LARGE_ADDRESS_AWARE) and the PE checksum. |
| `Kabus22Patch/kabus_dll.dll` | 1247232 | `3402fec5bd6650c9f2d0ecf3eef84200b80bf699635fc8f1b6c996e80302e591` | 2025-12-23, linker 14.43 (VS 2022), PDB `…\Tools\kabus_vs\Debug\Kabus.Acknex.Plugin.pdb`; exports `A6_DLLAPI`, `DiscordRPC_*`, `INI_*`, `Parse_EscapeChar`. |
| `Kabus22Patch/kabus_rpc.dll` | 307720 | `ef7404768c7183bdce9e46666a121a89b77be40060d0aea4d67804cf0e5ce6d8` | 2018-11-27, linker 14.0; exports `Discord_Initialize/UpdatePresence/…` (Discord RPC). |
| Lutris `Kabus22.exe` | 1068703 | `eb5da3318f0911c644331d0daf769828342f935afe858274878e956c9535c5f7` | UPX launcher, link 2004-09-07; differs from ISO `Kabus22.exe` (another release). |

Byte comparison of ISO `K22.exe` vs patched `K22.exe` (computed here; agrees with `CONTRACT.txt:2147-2154`) — **CONFIRMED**:

| Section | Changed bytes | Locations (VA) | Interpretation |
|---|---|---|---|
| `.text` | 6 | `0x4139bb` (`jl rel32` → `jmp +4`), `0x413a7e` and `0x42027e` (`call` → `jmp +3`) | start-up / archive-path branch bypasses (`CONTRACT.txt:2148-2152`). Per-site reading from the community-patch analysis: `0x4139bb` skips the executable self-checksum failure branch, `0x413a7e` the start-up archive check, `0x42027e` the "script modified" WDL check — **HIGH CONFIDENCE** (`reverse/community/reports/pe-compare.json`, `docs/PATCH_BINARY_DIFF.md`) |
| `.rdata` | 0 | — | identical |
| `.data` | 19 | `0x621190` (embedded start-up block fields), `0x621243` main name `K22` → `K22Widescreen`, `0x627ab8` resolution cap `1920×1200` → `3840×2400` | cap: **CONFIRMED** by `CONTRACT.txt:2150-2151`; the `0x621190` fields **UNKNOWN** |
| `.rsrc` | grown 4 KiB → 192 KiB | — | 6 `RT_ICON` + `RT_GROUP_ICON` `KABUS22` only; no manifest or version resource — **CONFIRMED** (pefile, `reverse/community/reports/pe-compare.json`) |

Because `.text` differs only in those six bytes and `.rdata` is identical, function and string addresses recovered from the patched binary apply to the ISO `K22.exe`. **CONFIRMED** by the comparison above.

## File inventory (ISO edition, top-level `app/` files)

Scope: every regular file directly under `app/` and `app/Kabus 22 Mode/` of the ISO installer (subdirectories `Help`, `Gallery`, `Menu_sound`, `Menu_ses` omitted). Archives/media were hashed from the read-only native install `~/.local/share/kabus22/game/`; PE/WDF/`K22.wrs`/Bonus-mode files from the private innoextract copy. Every row's size+MD5 matched `reverse/media/iso/list-exact.txt`. Kind is from magic bytes (`RIFF` = AVI video, Indeo 5 `IV50` per `reverse/media/iso/vtto.k22.ffprobe.json`; text `*.k22` are plain-text language files). **CONFIRMED** (computed).

| Path (under `app/`) | Size | Kind | SHA-256 |
|---|---|---|---|
| `K22.exe` | 2347008 | PE32 | `cc193c6b60419118e97f614d310fdf4d2ebb847819cedb6e4b4f93f2ffcc4ebd` |
| `K22.wrs` | 862026 | WRS2 archive | `c9e3200869ded9eadaf43ff15d71b7c24eb11c05d452632613429c0756c885c5` |
| `Kabus 22 Mode/K22_0.dll` | 320512 | PE32 | `c95681742cc9139a62405d395a4751a17e6b207f4dfafc6790dce0b684d98e5a` |
| `Kabus 22 Mode/K22_1.dll` | 49152 | PE32 | `9c9e9ea62e88632eb69e72e931152c6e3d6ae4f542cb5aec7b7b90d967a09a3e` |
| `Kabus 22 Mode/K22_mode.exe` | 23552 | PE32 | `5e87d5a8204a1604efcec3dfe2d414c5107ccbd5be61be4bb96c852f55196749` |
| `Kabus 22 Mode/K22_mode.wrs` | 95253694 | WRS3 archive | `d76d1a0169175dde0c16ab18faf5ceeebd14d37cd9d7e6702a33f7794fb11eed` |
| `Kabus 22 Mode/Kabus22_mode.exe` | 1549860 | PE32 | `3442c4af0e93e537676274c7b6ea5a43ccd3de50e9487fc3f6b54418031c44a2` |
| `Kabus 22 Mode/acknex.dll` | 2007040 | PE32 | `f96c7645ef41a3bc038f9ccae130c8304675c0f80eeb4a5d48558f49c329227b` |
| `Kabus 22 Mode/acknex.wdf` | 107301 | WDF | `1ec47ea2c795a935cfe189d7526f3f44117510a77a7ea64d43b07ce8ec420b64` |
| `Kabus 22 Mode/d3dx9_30.dll` | 2388176 | PE32 | `5edeed79f2359527a55b8189cfa8b9b121cd608d44eead905a0f3436938ad532` |
| `Kabus 22 Mode/mode_ayarlar.k22` | 14 | data/text | `90ca0d10b9c09f758c9d51526983b6ab4e2a8d4f36872e4ee2793a237c733619` |
| `Kabus 22 Mode/mode_de.k22` | 10271025 | WRS3 archive | `2ab968bb5fa7be4eccb46d40519fac8744068706b9859214c05d51446d2d06a1` |
| `Kabus 22 Mode/mode_en.k22` | 10236043 | WRS3 archive | `4edafe366808a7793e8fa883d59e019a2343dd3fb5c36af932705929c8c6ebc0` |
| `Kabus 22 Mode/mode_tr.k22` | 10514110 | WRS3 archive | `1b1af9e280ce3f661a75a3799d32cf2f7b4083439233c519cdcfa77a9fa7811c` |
| `Kabus 22 Mode/ygm.k22` | 3 | data/text | `f6e0a1e2ac41945a9aa7ff8a8aaa0cebc12a3bcc981a929ad5cf810a090e11ae` |
| `Kabus22.exe` | 1244972 | PE32 | `487d8943e3b80d2162907896b83d954d328e65224c53dfb0d5fb3d6a592ceeaa` |
| `Kaynak.k22` | 976276272 | WRS2 archive | `09ec9abc056cd46213da4cd6d6752b3c62aed8f74b2f6e50327dc1963a7cbd6a` |
| `OggDS0995.exe` | 475844 | PE32 | `0482805e84f3b0ef9a64ba78ccbdb0a997c5510e10f9cc43b6db9a5effcaaf19` |
| `RenkAyar.dll` | 49152 | PE32 | `9c9e9ea62e88632eb69e72e931152c6e3d6ae4f542cb5aec7b7b90d967a09a3e` |
| `acknex.wdf` | 107292 | WDF | `e99629b67f3fed7a468a66af431871033bdd395c65254dcc7b8c4f285ed0ca80` |
| `bdi1.k22` | 6056 | data/text | `57fa7990be853591843dbd1f6a73fd066572f0fd49520dde4cafcc4d713273fd` |
| `bdi2.k22` | 3788 | data/text | `071311db6012a35c4494bf6c9df8e2f0bf8f8811fe797b2bfc5c82ba5fcdd1ab` |
| `bei1.k22` | 5302 | data/text | `d5475efefee47154e800d81f5b51b844c099d846a68e26ff7aaa326054f2a333` |
| `bei2.k22` | 3320 | data/text | `69f5a4dc8aa8c687c7eab8445028319cd65b2bdc0c8f147e44999b61206bcb83` |
| `bti1.k22` | 4897 | data/text | `64d13a9cbd47fe97e7f2aa649f76a4d9b36de055977befb6af0987f24ef7161c` |
| `bti2.k22` | 3032 | data/text | `82dbe28598a1dc15fac5f94bb0861d4f3b45575579cb84fc905efbe6894b547d` |
| `kn_arg.k22` | 16357784 | RIFF | `1d30a17d24431376a2ea2a877a661d921ad8373f0e67ac4ea614d2f24b9c1229` |
| `mdc.k22` | 3 | data/text | `2ac9a6746aca543af8dff39894cfe8173afba21eb01c6fae33d52947222855ef` |
| `msc.k22` | 3 | data/text | `2ac9a6746aca543af8dff39894cfe8173afba21eb01c6fae33d52947222855ef` |
| `ndi.k22` | 1200 | data/text | `0c608648c7e941ef5c472b513ae1066df3dc87c7b6d9fd13644f4f0093b721bc` |
| `nei.k22` | 1048 | data/text | `2e1663be99bd86c49fc89d917094823b07b029bf99b69eb662c0693f6c72b99e` |
| `nti.k22` | 774 | data/text | `cac96133f01ef0095808ce47290c52c0872506e7a56938aab80e0eba8af11d5b` |
| `options.k22` | 31 | data/text | `4a24f5060ae8428fdec519ffb8baf6bd8bf28761acaab409fb0109e6dc86b435` |
| `ses_en.k22` | 2691488 | WRS2 archive | `fb57ed61c60480e97c465de4784232d260299f4c119eec07ae67ee50f9b2cc57` |
| `ses_tr.k22` | 3597603 | WRS2 archive | `701f3379cc6bf6b7676c66861022e705859e36b4ae73583747c279dfe2341b0b` |
| `vebi.k22` | 51949772 | RIFF | `34fe4d13d370ac83561ee288a2debcedab72d37b595a97c01c5a2260a6bd4c12` |
| `vebk.k22` | 39401662 | RIFF | `454b1b162a5d5ccc4ab2c3c3e46b59d6055f05c749c48fbe7761f9e196ed9f73` |
| `veby.k22` | 19987086 | RIFF | `0d73d6f27015e32c99993a79fad22d15a280d71e102f35f18fcc5394fae9ab51` |
| `veca.k22` | 16770806 | RIFF | `9c0e23c1cd3748220b21f6ef79fccf60520c47b1983e456b3409bbc271c42948` |
| `vedh.k22` | 19515454 | RIFF | `da53929db2281a4dc7fd4b631e8d611814bf0265d788ad504f211eec7557c230` |
| `vedo.k22` | 28085832 | RIFF | `0b2f99bc06241416fd0946551b38ad7237a0805a7d526ef87cd8eee7ac4bc7cb` |
| `vedt.k22` | 62087402 | RIFF | `030eb849aaaacdc48ba6ff5b32092fec02e19e56704eee956d7432785d39d221` |
| `veil.k22` | 72649326 | RIFF | `cdb5c6b65937c80d57ddde357ecdc37275599ab2e44d98c496f79b9a52dde31e` |
| `veis.k22` | 25613054 | RIFF | `108f1fd4fc9a5b764736cc8b0fbba7856cd67ad189970b6d365f2b70e7520516` |
| `vekc.k22` | 16417416 | RIFF | `11dbf3ac77d7457a21bbe95101d278334655441a0938312a45ea298a0726df67` |
| `veoe.k22` | 32207672 | RIFF | `63b7cd6e5d0b2a6a6a9d698ee8d9a1fdc9432de40de1ebe3bae5c94e856a49e3` |
| `vese.k22` | 22315620 | RIFF | `f63dc8cf55f7e4621b57f29c22ac98589621f559cc1c82b1bdca2ea6dfbc4758` |
| `vest.k22` | 60966310 | RIFF | `9bc49c2357ea1b5c50382940e62665013d48b08006e84b72138dd0504d8360e4` |
| `veta.k22` | 16326566 | RIFF | `20963fe5228b4983ed89f78b35d1340c1d919180cc89bfbdd044c7ca02c5d1c6` |
| `veto.k22` | 7753770 | RIFF | `0a3083b362b8d19fdb5930303fd36dec5edbf4010f71aa7028955284f33ae2cf` |
| `veue.k22` | 24218458 | RIFF | `24a03ec43c287ce69b84d8f5f67f45bf521c73ec0b742dd56d03e2fd290872ed` |
| `veyo.k22` | 49046426 | RIFF | `f3b6b5b63eba852f1b1249282e5705e844a07a03af047919d8b00401706916ec` |
| `vkt.k22` | 4499100 | RIFF | `cc44b4feadcd72eef1cd3c596d0be294016eaf91d346d593a4a9b0c64ab6ba99` |
| `vtbi.k22` | 52450166 | RIFF | `6d42f8fd39aa6fafe8edbc39d84e334652f866c810be68b33b84efaf6cb2fa8a` |
| `vtbk.k22` | 39479090 | RIFF | `dafe54925a98a9d5cdd1a023a9b8b6726e8fcc92cd2e7d221be8c8269a9ebc6d` |
| `vtbs.k22` | 12243886 | RIFF | `64b6c1fc8c5e3a20176c28f6bb9b7f7615f8bdb22609b1f77595cf01d8d76856` |
| `vtby.k22` | 20056858 | RIFF | `22aaab3979a16976d807d3b8d48fa4a3db71e5d7ba7790ec4199cd3080cbf57f` |
| `vtca.k22` | 16850618 | RIFF | `a8ca7cb0a908592062ff883bc86f67628048cd55dc168abdf79ba63cf00f1146` |
| `vtdh.k22` | 19595110 | RIFF | `74c13c33ea6bfd4f56db9070f0fb7fd38f3d0c3cb20a320958029dac7e8fd273` |
| `vtdo.k22` | 28192868 | RIFF | `a242b91b572e818b9828d9cc0755fe3c3bdb9e013fa8ac212c2e40ad4d72210c` |
| `vtds.k22` | 2487938 | RIFF | `ee7eb7331db0b14c6d939c7c465df34a07fe2fdfb2ff0a1f8d86d2288c257dce` |
| `vtdt.k22` | 62217242 | RIFF | `eea29acd2fd3f8b776273f58413206607b6df45cee2f96cddb7ebf63f82f65e0` |
| `vtil.k22` | 72886250 | RIFF | `e339517b4dd06f609d83814db1bace8e0e410b9f6219fa4d202ec5817a0f0e17` |
| `vtis.k22` | 25692506 | RIFF | `f8ac7b304ffa7e1202a5605105d6f744863ca53e0717bf15c075b0471983c8ae` |
| `vtkc.k22` | 16407456 | RIFF | `0f443f990c93756b2cf323cf14b84df6ee6fb1cfba27318ef44f18e050e03a15` |
| `vtlu.k22` | 8988276 | RIFF | `04707f6b7660d6af7a1a331118e6d400f886d2a592236f7f269d6bd5a9c82622` |
| `vtoe.k22` | 32274184 | RIFF | `04ed5c6ab46a0fdbec57432dc76e50a61fcac0f8d1b521aa9ef787b68b3566b5` |
| `vtse.k22` | 22382856 | RIFF | `44ed4ebb445b0ba7677b5a29f79956d868d2c9072b894272837872ae4899b6fa` |
| `vtst.k22` | 60966310 | RIFF | `527fc45e053ef2b604e77b8b6e83aed637bc9d199b5f8ff2835e440f055b86ef` |
| `vtta.k22` | 16326566 | RIFF | `b255db05c0947914886a75b59c52a68c853cda885d4ce7a0947a66e9b82073c8` |
| `vttd.k22` | 2546046 | RIFF | `523bdbe47ce06833e6323adddd3144234192f70aa7d5d0372eda0b567684f6ad` |
| `vttg.k22` | 11554004 | RIFF | `0a01ca2ccc89811fca4170542acddea1f8d1979895de88903ff1df28d267c8c0` |
| `vtto.k22` | 7753770 | RIFF | `3f32feedee4f4fe06754b25a54a8edf3fae32dd65c3cc3b5ea8d6107e0f873eb` |
| `vtue.k22` | 24329064 | RIFF | `66f0d64590b750ae5009a258cc9b1fd02c7ba30d7899627f2bfaae934e53fb82` |
| `vtyo.k22` | 49046426 | RIFF | `9ad8b788ce34bf5db60151794a478a26574f2aa05149f45f7dbb79f30a9d28ae` |
| `yazi_de.k22` | 7854935 | WRS2 archive | `0fbc8ae9c640d91c16bad21bdae5864f53330bc4dc7283e131def4708919a9b0` |
| `yazi_en.k22` | 7763218 | WRS2 archive | `83df96a04f4082c02f6a4f2edcbc285fc80d3d729c8d683b081513041faf425c` |
| `yazi_tr.k22` | 7920867 | WRS2 archive | `6f94d0bfc1a2b5f9c4ce7202fab8dc48d50b02552ca564b07c184c3c90c48f4d` |
| `yoldata.DAT` | 117960 | data/text | `8af4e835b566aa86f4e58328fd12f6a54a2e67bc215a4c6dc08d0d176c5c2818` |
