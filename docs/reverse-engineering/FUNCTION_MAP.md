# Function Map — recovered original functions

Purpose: address-level index of every recovered function and data address cited by the reverse-engineering evidence, with its original role and (where verified) its native reimplementation.

Related: [REVERSE_ENGINEERING index](../REVERSE_ENGINEERING.md) · [ENGINE_ARCHITECTURE](../ENGINE_ARCHITECTURE.md) · [SCRIPT_SYSTEM](SCRIPT_SYSTEM.md) · [DATA_STRUCTURES](DATA_STRUCTURES.md) · [RESOURCE_FORMATS](RESOURCE_FORMATS.md) · [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md) · [ORIGINAL_BEHAVIOR](ORIGINAL_BEHAVIOR.md)

## Conventions

| Binary | Analyzed file | SHA256 | Image base | Role |
|---|---|---|---|---|
| K22.exe | `Kabus22Patch/K22.exe` (unpacked patch-distributed executable; self-identifies "A6 engine" "6.31.4") | `1a6a0dbc…7bed` | 0x00400000 | Campaign engine (Acknex A6 6.31.4) |
| acknex.dll | `Kabus 22 Mode/acknex.dll` (copy under `reverse/decompiled/runtime/bonus-physics/pe/`) | `f96c7645…227b` | 0x10000000 | Bonus mode engine (A7) used by the Bonus launcher [HIGH CONFIDENCE: engine_* thunks in K22_mode.exe] |
| kabus_dll.dll | `Kabus22Patch/kabus_dll.dll` (community patch plugin, not on the ISO) | `3402fec5…e591` | 0x10000000 | INI / Discord RPC / escape-string plugin |
| RenkAyar.dll | shipped game directory | `9c9e9ea6…a09a3e` | 0x10000000 | Gamma/brightness plugin |
| K22_mode.exe | `runtime/bonus-physics/pe/app/Kabus 22 Mode/K22_mode.exe` | see `runtime/bonus-core/entry/survey/` manifest | 0x00400000 | Bonus launcher stub; exports include `engine_open`/`engine_frame`/`engine_close` thunks (00401030/0040102a/00401024) |

- K22.exe PE sections (parsed from the analyzed file): `.text` 0x00401000–0x005cf7bd, `.rdata` 0x005d0000–0x0061a976, `.data` 0x0061b000–0x00733938, `.rsrc` 0x00734000–. Addresses in `.text` are code; `.rdata`/`.data` addresses are listed under [Globals/data addresses](#3-globals--data-addresses). **CONFIRMED** (PE header read; hashes from `K22.exe.recovery.txt:5`, `kabus_dll.dll.recovery.txt:5`, `RenkAyar.dll.recovery.txt:5`, `runtime/bonus-physics/api-descriptors.json`).
- All 179 K22.exe manifest/report header blocks and all 293 acknex.dll header blocks under `reverse/decompiled/` carry the single hashes above. The ISO `K22.exe` (`cc193c6b…`) is not packed and differs from the analysed patched build only in six `.text` bytes at `0x4139bb`, `0x413a7e`, `0x42027e` (start-up/archive branches) plus `.data`/resources; `.rdata` is identical, so addresses below apply to the ISO build (**CONFIRMED**, [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md#community-patch-binaries-not-part-of-the-iso), `CONTRACT.txt:2147-2154`). Only the UPX-packed launchers `Kabus22.exe`/`Kabus22_mode.exe` are unanalysed (**UNKNOWN**).
- All DLLs share preferred base 0x10000000, so a `10xxxxxx` address is ambiguous without the Binary column.
- Evidence paths are relative to `reverse/decompiled/` (e.g. `runtime/trace/K22.exe.00404b60.c:8-17`). `CONTRACT §N (Lx)` = [`runtime/CONTRACT.txt`](../../reverse/decompiled/runtime/CONTRACT.txt) section N, line x; `§39b`–`§42b` are the second numbering starting at line 2139. `RECOVERY.txt` = [`reverse/decompiled/RECOVERY.txt`](../../reverse/decompiled/RECOVERY.txt). Native paths are relative to `native/` unless prefixed.
- **Parameters** = Ghidra signature (`u`=undefined, `pN`=param_N). Ghidra omits register arguments (`CONTRACT.txt:9`), so arity is approximate.
- **Callers/Callees** are computed only from `FUN_xxxxxxxx` references inside the exported `.c` files (2278 exports), so they are partial: absence means "not seen in exports", not "none". `—` = no export for that address.
- **Native** names a symbol only when verified by reading current source (comment citing the address, or matching behavior noted in the cell). Line numbers are omitted because `native/` is being edited concurrently. "behavioral substitute" = same observable contract, different mechanism.
- Confidence: **CONFIRMED** (decompiled C/ASM, file bytes, or descriptor table), **HIGH CONFIDENCE** (strong indirect), **HYPOTHESIS**, **UNKNOWN**.

Coverage: 253 distinct addresses cited in CONTRACT.txt (all appear below: 188 function rows incl. RECOVERY.txt/DLL exports, 24 instruction sites, 105 data rows); 2278 distinct decompiled export addresses (K22.exe 823, acknex.dll 1314, kabus_dll.dll 129, RenkAyar.dll 7, K22_mode.exe 5); those not annotated in §1 are listed in §5.

## 1. Annotated functions by subsystem

### Bootstrap, logging, CRT

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `005bb38b` | K22.exe | PE entry point (MSVC CRT `entry`); exporter selected it as external entry | void | — | 0042ece0, 005c7104, 005c73d9 | — | **CONFIRMED** | — (native `main` in main.cpp is unrelated code) | K22.exe.recovery.txt CANDIDATE entry@005bb38b; K22.exe.005bb38b.c |
| `00413740` | K22.exe | Owner of one "A6 engine"/"6.31.4" string xref (0x00413821); role otherwise unrecovered | `void` | 0042dbc0, 0042ddd0 | 00412a80, 004135d0, 00415130, 00423070, 00423210 +8 | — | **UNKNOWN** | — | RECOVERY.txt:34; K22.exe.00413740.c |
| `00413880` | K22.exe | Writes engine log header "Log of %s %s run at %s" using 0061dda8/0061ddb4 | `void` | 0042ece0 | 00412a80, 0041ce20, 005b8acc | 0061dda8, 0061ddb4 | **CONFIRMED** | — (no acknex.log equivalent) | RECOVERY.txt:34; final/K22.exe.00413880.c:32 |
| `00413990` | K22.exe | Startup driver: compiles scripts, registers 79 numeric macros (00413fc5..00414437) and presence macros RELEASE/DIRECT3D/CAPS_*/SERVER/CLIENT/WMB, then calls plugin enumeration 004133c0 (C:375-397) | `void` | 0042ddd0 | 0040c4d0, 0040c6c0, 00410230, 004128b0, 00412a80 +33 | 00644850, 006c6dec, 005d2808 | **CONFIRMED** | engine.cpp: `nativeConstants[]` (macro values), `Engine::initialize_plugins` (cites 00413990) | runtime/constants/K22.exe.00413990.asm.txt; CONTRACT §6, §9, §13; K22.exe.00413990.c |
| `004133c0` | K22.exe | Automatic plugin enumeration: PLUGINDIR prefix, `*.dll` scan skipping compiler.dll/ackport.dll, up to 64 handles via 0040c330 | `void` | 00413990 | 0040c330, 0042daa0, 005b962c | 006c7488, 00644730..0064482c | **CONFIRMED** | engine.cpp: `Engine::initialize_plugins` (cites 004133c0; compiled providers, no PE loading) | runtime/mode-bitmap-plugin/K22.exe.004133c0.c:41-125 |
| `0040c330` | K22.exe | Plugin handshake: LoadLibraryA; A6_DLLAPI(interface 006438b0) kept if result<631, else A5_DLLAPI(00644380) kept if result<512 | `LPCSTR p1` | 004133c0 | — | 006438b0, 00644380 | **CONFIRMED** | engine.cpp: `svc_plugin_open` (provider selection by name; no handshake) [behavioral substitute] | CONTRACT §17 (L652-659); runtime/mode-bitmap-plugin/K22.exe.0040c330.c |
| `0040c290` | K22.exe | Engine-interface setup: base 006438b0, +a30=resolver 0040c200, +a34/+a38=my/you storage | `void` | 0042ece0 | 0042b770 | 006438b0, 006441d8, 006441dc | **CONFIRMED** | — | runtime/registry/K22.exe.0040c290.c:19-23 |
| `0040c200` | K22.exe | API name resolver: case-insensitive scan of the 317 descriptors 0061c408..0061d7d8; NULL when absent (proc_status absent) | `char *p1` | — | — | 0061c408..0061d7d8 | **CONFIRMED** | native_calls.inc (317 campaign names) + wdl.cpp `native_call_name` | runtime/lookup/K22.exe.0040c200.c:11-24; runtime/api-descriptors.json |
| `00414650` | K22.exe | Per-frame main loop: clock (QPC via 00460030, raw ticks = ms*1024/64000), TIME clamp/factor/smooth, FPS_MAX sleep, queues MAIN/STARTERs (asm:20-85), runs scheduler/input phase 004125e0 then level request 0041de10 | `void` | — | 004104f0, 004125e0, 00412a80, 004136b0, 0041de10 +11 | 00665cf4, 0066578c, 00665830, 00665d94, 00665874 | **CONFIRMED** | engine.cpp `Engine::tick` (TIME clamp 51..16/FPS_MIN, factor, freeze); main.cpp `main` frame pacing (cites 00414650) | runtime/clock/K22.exe.00414650.c:96-166; runtime/clock-instructions/K22.exe.00414650.asm.txt:213-316; CONTRACT §3, §13, §38 |
| `00460030` | K22.exe | High-resolution timer: QueryPerformanceCounter/frequency, timeGetTime fallback; returns elapsed seconds | `int p1` | 00413990, 00414650, 00434220, 00434670, 0045dd50 | — | — | **CONFIRMED** | main.cpp `main` (std::chrono::steady_clock) [behavioral substitute] | runtime/clock-instructions/K22.exe.00460030.c:20-56 |
| `0044b770` | K22.exe | Frame subsystem skipped when FREEZE_MODE>=1 (clock C:142-144); function purpose not recovered | `void` | 00414650 | 0040f7b0, 004121b0 | 00665874 | **UNKNOWN** | — | CONTRACT §3 (L131); runtime/clock/K22.exe.0044b770.c |
| `005b72cc` | K22.exe | x87→signed64 truncation helper (FISTP qword + sign correction); callers keep EAX low word | `void` | 00402770, 00406720, 004067e0, 00407040, 004083c0 +64 | — | — | **CONFIRMED** | value.hpp `Value::from_double` (truncation; throws outside int64 range instead of integer-indefinite) | runtime/vector-temporary-extra/K22.exe.005b72cc.asm.txt; CONTRACT §5, §18 (L712-721); runtime/abi/K22.exe.005b72cc.c |
| `0055c152` | K22.exe | Large Windows/DirectShow system-error-name mapping function; Ghidra decompile timed out in every run; NOT a loader/scheduler | UNKNOWN (no decompilation) | — | — | — | **HIGH CONFIDENCE** | — | RECOVERY.txt:29; K22.exe.recovery.txt / final/K22.exe.recovery.txt status FAILED timed_out=true |
| `005ce400` | K22.exe | Static constructor populating member-descriptor name pointers (one of seven: 005ce400/005ce9c0/005ceb80/005ced30/005cedf0/005cee10/005cee90) | `void` | — | — | — | **CONFIRMED** | engine.cpp `nativeMasks[]` (member/flag names) [data reimplementation] | CONTRACT §6 (L258-262); runtime/member-descriptors.json; runtime/member-init/K22.exe.005ce400.c |
| `005ce9c0` | K22.exe | Static member-name constructor (see 005ce400) | `void` | — | — | — | **CONFIRMED** | engine.cpp `nativeMasks[]` | CONTRACT §6 (L259); runtime/member-descriptors.json; runtime/member-init/K22.exe.005ce9c0.c |
| `005ceb80` | K22.exe | Static member-name constructor (see 005ce400) | `void` | — | — | — | **CONFIRMED** | engine.cpp `nativeMasks[]` | CONTRACT §6 (L259); runtime/member-descriptors.json; runtime/member-init/K22.exe.005ceb80.c |
| `005ced30` | K22.exe | Static member-name constructor (see 005ce400) | `void` | — | — | — | **CONFIRMED** | engine.cpp `nativeMasks[]` | CONTRACT §6 (L259); runtime/member-descriptors.json; runtime/member-init/K22.exe.005ced30.c |
| `005cedf0` | K22.exe | Static member-name constructor (see 005ce400) | `void` | — | — | — | **CONFIRMED** | engine.cpp `nativeMasks[]` | CONTRACT §6 (L259); runtime/member-descriptors.json; runtime/member-init-more/K22.exe.005cedf0.c |
| `005cee10` | K22.exe | Static member-name constructor (see 005ce400) | `void` | — | — | — | **CONFIRMED** | engine.cpp `nativeMasks[]` | CONTRACT §6 (L259); runtime/member-descriptors.json; runtime/member-init-more/K22.exe.005cee10.c |
| `005cee90` | K22.exe | Static member-name constructor (see 005ce400) | `void` | — | — | — | **CONFIRMED** | engine.cpp `nativeMasks[]` | CONTRACT §6 (L259); runtime/member-descriptors.json; runtime/member-init/K22.exe.005cee90.c |

### WRS archive / resource loading

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `0041ff20` | K22.exe | RESOURCE archive registration: rejects empty, case-insensitive duplicate suppression, opens via 005b8acc, stores FILE* in 006bafb8, calls 0041fd20; max 128 archives | `char *p1` | 00413990, 0041aef0 | 0041fd20, 0042eca0, 0045ccf0, 005b8acc | 006bafb8, 006bb1bc | **CONFIRMED** | assets.cpp `Assets::mount` (mount-key dedup) | RECOVERY.txt:42-46; K22.exe.0041ff20.c |
| `0041fd20` | K22.exe | WRS index loader: "WRS2" magic → name XOR 0xa5, 32-byte name field; else legacy XOR 0/13 bytes; big-endian stored/decoded lengths; 0x30-byte entries; max 4096 | `int p1` | 0041ff20 | 0042eca0 | 00665ea8, 006bb1b8 | **CONFIRMED** | assets.cpp `Assets::mount` (WRS2 0xa5 and WRS3 0xb5; legacy format rejected) | RECOVERY.txt:48-57; loaders/K22.exe.0041fd20.c |
| `00420120` | K22.exe | Shared resource/file-to-buffer loader: .WDL detection, memory cache 00695eb0, folder path, archive scan of 00665ea8, then decoder 00414bc0 | `char *p1,uint *p2,void *p3,int p4` | 00415ee0, 00420490, 00455240, 0045b630 | 00414bc0, 00415320, 00420090, 0042a720, 005b8acc | 00695eb0, 00695ea8, 00665ea8 | **CONFIRMED** | assets.cpp `Assets::read` / `Assets::read_entry` | RECOVERY.txt:59-64; K22.exe.00420120.c |
| `00420490` | K22.exe | Thin wrapper: 00420120(filename,size,0,1) | `u4 p1,u4 p2` | 0040e2b0, 00419cb0, 0041e330 | 00420120 | — | **CONFIRMED** | assets.cpp `Assets::read` | RECOVERY.txt:66; loaders/K22.exe.00420490.c |
| `00414bc0` | K22.exe | WRS member decoder: LZSS, 4096-byte window filled with 0x20, initial cursor 0xfee, (file,buffer,decoded_len,stored_len) | `FILE *p1,byte *p2,uint p3,uint p4` | 00420120 | — | 00651cb8, 0064c878 | **CONFIRMED** | assets.cpp `Assets::decompress` (same window/cursor/token layout) | decoder/K22.exe.00414bc0.c:23-79; RECOVERY.txt:62-64 |
| `005b8acc` | K22.exe | CRT fopen-style open used by archive registration | `char *p1,char *p2` | 00408890, 00413880, 0041fc00, 0041ff20, 00420120 | — | — | **HIGH CONFIDENCE** | — | RECOVERY.txt:44; runtime/text-load/K22.exe.005b8acc.c |
| `0041ce20` | K22.exe | File search helper; explicitly NOT on the MEDIA_PLAY filename path | `int p1,int p2,int p3,int p4` | 00408890, 00410a80, 00413880, 0041d6e0, 0041da70 | 0045cdd0, 005b9e55, 005cd3e0 | — | **HIGH CONFIDENCE** | — | CONTRACT §14 (L557); runtime/interpreter/K22.exe.0041ce20.c |

### WDL compiler / parser

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `0041aef0` | K22.exe | Script parser/compiler driver: token loop, BIND/LEVEL/SAVEDIR/PLUGINDIR/INCLUDE/PRINT/HALT/RESOURCE directives; INCLUDE has no include-once set; BIND/LEVEL consume one parameter with no side effect | `int p1,uint p2` | 00419cb0, 0041b5d0 | 00415320, 00419750, 00419850, 00419b10, 00419cb0 +8 | 006633c0, 006c7488 | **CONFIRMED** | wdl.cpp `Runtime::Impl::parse` + `Runtime::Impl::preprocess` | RECOVERY.txt:39-40; loaders/K22.exe.0041aef0.c:111-215; CONTRACT §9, §17 |
| `00419cb0` | K22.exe | INCLUDE loader: 00420490(name), switches current script name 00651ce0, re-enters 0041aef0, frees non-cached text | `u4 p1` | 00413990, 0041aef0 | 0041aef0, 00420490, 0042a7a0 | 00651ce0, 00695ea8 | **CONFIRMED** | wdl.cpp `Runtime::Impl::preprocess` (include handling) | RECOVERY.txt:67; loaders/K22.exe.00419cb0.c:15-26 |
| `0041b5d0` | K22.exe | Copies embedded engine C-Script text (005d3348) and parses it with 0041aef0 (engine_internal.wdl defaults) | `void` | 00413990 | 0041aef0, 0042a720, 0042a7a0 | 005d3348 | **CONFIRMED** | — | RECOVERY.txt:68; CONTRACT §12 (L468-471); K22.exe.0041b5d0.c |
| `00419b10` | K22.exe | Loader follow-up target in RECOVERY export list; purpose not described | `char *p1` | 0041aef0 | — | — | **UNKNOWN** | — | RECOVERY.txt:15; K22.exe.00419b10.c |
| `00402fe0` | K22.exe | Statement compiler/dispatcher: consults nine-word command tables 0061b3d8/0061c298, emits opcodes via 004012d0 (opcode4 = expression), maps locals to `_locN` with opcode39 | `void` | 00403a00 | 004010c0, 004012d0, 004013a0, 00402a10, 00402ae0 +15 | 0061b3d8, 0061c298 | **CONFIRMED** | wdl.cpp `Runtime::Impl::statement` / `Runtime::Impl::local` | RECOVERY.txt:72; loaders/K22.exe.00402fe0.c:206-219; runtime/local-storage/K22.exe.00402fe0.c:22-74 |
| `00402d60` | K22.exe | Legacy command-descriptor compiler (receives matching 9-word descriptor from 00402fe0); "Old version keyword" xref | `u4 *p1` | 00402fe0 | 004012d0, 00402a10, 00402ae0, 00402ba0, 00402c90 +3 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::statement` (cites 0061b468/00402d60 for legacy REMOVE operand) | RECOVERY.txt:72; runtime/K22.exe.recovery.txt CANDIDATE 00402d60; K22.exe.00402d60.c |
| `004012d0` | K22.exe | Opcode emitter used by statement compiler | `u4 p1,int p2` | 00401410, 00402c90, 00402d60, 00402fe0 | 0042a750 | — | **HIGH CONFIDENCE** | wdl.cpp `Runtime::Impl::emit` [structural analogue] | RECOVERY.txt:72; runtime/trace/K22.exe.004012d0.c |
| `00401bd0` | K22.exe | Find-or-create function descriptor; prepends to list 0063b9b4 on first creation | `char *p1` | 004023a0, 00402580, 00403a00, 00418820, 0041f090 +1 | 0040f210, 0042a750 | 0063b9b4 | **CONFIRMED** | wdl.cpp `Runtime::Impl::ensure_function` | CONTRACT §9 (L342), §13 (L491-493); runtime/lookup/K22.exe.00401bd0.c |
| `00401cc0` | K22.exe | Frees an old compiled function body before redefinition | — (no export) | — | — | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::parse` (body replacement diagnostic) | runtime/definitions/K22.exe.00403a00.c:23-40 (call site) |
| `00403a00` | K22.exe | Definition body (re)compiler: resets local count +20, frees old body (+0c/+10), keeps descriptor identity; later body wins | `int p1` | 00403b10 | 00401bd0, 00401cc0, 00402fe0, 004152f0, 0041aa60 | 006633c0 | **CONFIRMED** | wdl.cpp `Runtime::Impl::parse` (cites 00403a00) | runtime/definitions/K22.exe.00403a00.c:23-40 |
| `00403b10` | K22.exe | Definition dispatch: ACTION/FUNCTION → 00403a00(0); STARTER → 00403a00(1) | `int p1` | 00403cc0, 00403cd0 | 00403a00, 00419d20, 0041a170 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::parse` (function/action/starter branch) | runtime/definition-dispatch/K22.exe.00403b10.c:16-83 |
| `0040a300` | K22.exe | Expression atom parser: searches native API table 0061c408 before ordinary symbols; emits node type 10 with handler address | `void` | 0040bd20 | 00402580, 00402770, 0040a250, 0040bd20, 00419ef0 +1 | 0061c408 | **CONFIRMED** | wdl.cpp `Runtime::Impl::expression` (cites 0040a300 / A7 1000bf40) | runtime/numeric/K22.exe.0040a300.c:74-153; CONTRACT §1, §16, §41 |
| `0040aba0` | K22.exe | Expression code generator: emits x86 for operators (ADD/SUB/IMUL+SHRD/IDIV), comparisons returning 1024/0, eager && \|\|, right-to-left native-call argument pushes | `int p1` | 0040aae0, 0040bf40 | 0040aae0, 0040ab20, 00412380, 00412390, 004123c0 +2 | 0063f9f8, 00643898, 00641948 | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (interpreted, same arithmetic results) | runtime/numeric/K22.exe.0040aba0.c:179-292,515; runtime/numeric/K22.exe.0040aba0.asm.txt; CONTRACT §5, §18, §30 |
| `0040aae0` | K22.exe | Operand helper: evaluates left then right (EAX=right, EBX=left) | `int p1` | 0040aba0 | 0040aba0 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (binary operands; `%` evaluates right first) | CONTRACT §5 (L180); runtime/numeric/K22.exe.0040aae0.c |
| `0040ab20` | K22.exe | Pointer-typed API argument: embeds object address (objectD+10) rather than first word | `int *p1` | 0040aba0 | — | — | **CONFIRMED** | — | CONTRACT §11 (L414-415); runtime/numeric/K22.exe.0040ab20.c |
| `0040bd20` | K22.exe | Builds comma/argument nodes (0xc) for native calls | `int p1,int p2` | 0040a300, 0040bff0 | 0040a250, 0040a300, 00415320 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (call args evaluated right-to-left) | runtime/interpreter/K22.exe.0040bd20.c; CONTRACT §18 (L694-698) |
| `0040bf40` | K22.exe | Emits expression prologue/epilogue around compiled code | `void` | 0040bff0 | 0040aba0 | — | **CONFIRMED** | — (no x86 emission) | runtime/interpreter/K22.exe.0040bf40.c:6-17 |
| `0040bfb0` | K22.exe | Remainder helper for `%`; negative left adjusted by (((trunc(0xf424000000/R)>>10)+1)*R) | `int p1,int p2` | — | — | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` `%` branch (same 0xF424000000 constant; native throws on R=0) | CONTRACT §5 (L198-202); runtime/abi/K22.exe.0040bfb0.c |
| `0040bff0` | K22.exe | Expression compile entry called by statement-expression wrapper | `char *p1` | 00402fe0, 0040c090 | 0040bd20, 0040bf40 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::expression` | runtime/interpreter/K22.exe.0040bff0.c; CONTRACT §16 (L613) |
| `0040c090` | K22.exe | Statement-expression wrapper: deletes SPACE/LF/TAB/FF/BS/CR/BEL (not VT) before 0040bff0 | `char *p1` | 00402fe0 | 0040bff0, 00419ef0 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::expression_spelling` (compactExpressions) [HIGH CONFIDENCE match] | runtime/interpreter/K22.exe.0040c090.c:17; CONTRACT §16 |
| `00402580` | K22.exe | Symbol/type resolution in expressions: flag8 → readonly bits 140; bare type3 resolves to objectD+10 | `char *p1,int *p2,int p3` | 00402770, 0040a300 | 00401bd0, 00401ff0, 00402140, 0040f180, 00415320 | — | **CONFIRMED** | — | runtime/abi/K22.exe.00402580.c:62-77; CONTRACT §6, §11 |
| `00402770` | K22.exe | Numeric literal parse: atof × 1024.0 (005d08c8) → 005b72cc truncation | `char *p1,int *p2,int p3` | 00402a10, 00402ae0, 00402ba0, 0040a300, 004174f0 | 004016f0, 00402580, 00415290, 00415320, 00419ba0 +2 | 005d08c8 | **CONFIRMED** | wdl.cpp `lex` + value.hpp `Value::from_double` | runtime/abi/K22.exe.00402770.asm.txt:46-54; CONTRACT §5, §10; runtime/numeric/K22.exe.00402770.c |
| `00402530` | K22.exe | Resolves an entity action by name (called from level construction 004315d0) | UNKNOWN (no export) | — | — | — | **HIGH CONFIDENCE** | wdl.cpp `Runtime::function` (name lookup) | CONTRACT §13 (L517) |
| `00402140` | K22.exe | Resolves ON_* function references | `char *p1` | 004023a0, 00402580 | 00401e10 | — | **CONFIRMED** | engine.cpp `Engine::initialize_globals` (ON_* callback globals) | runtime/registry/K22.exe.00402140.c; CONTRACT §6 (L247) |
| `00401ff0` | K22.exe | Resolves KEY state and joystick globals by name | `char *p1` | 00402580 | 00401e10 | — | **CONFIRMED** | engine.cpp `nativeKeys[]` | runtime/lookup/K22.exe.00401ff0.c; CONTRACT §6 (L248) |
| `00419ba0` | K22.exe | Numeric token validator for array initializer mode 2 (signed decimal/dot only) | `char *p1,char p2` | 00402770, 00418820, 0041a1b0, 0041aaf0, 0041abb0 | — | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` | CONTRACT §10 (L380-383); runtime/abi/K22.exe.00419ba0.c |
| `00419c40` | K22.exe | WINDOW keyword recognizer: sets parse state 0066133c=2 and handler 00661330=00419c20 | `void` | — | — | 0066133c, 00661330 | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` (WINDOW diagnostic cites 00419c20/00419c40) | runtime/window-parser/K22.exe.00419c40.c; CONTRACT §15 |
| `00419c20` | K22.exe | WINDOW property handler: `*006633a8=0; return 1` — discards the block | `void` | — | — | 006633a8 | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` (no WINDOW object constructed) | runtime/window-properties/K22.exe.00419c20.c; CONTRACT §15 |
| `00419d20` | K22.exe | Registers a text macro (name,text,3) | `char *p1,char *p2,int p3` | 00402fe0, 00403b10, 00412b90, 00419e90, 00419eb0 +1 | 00414e50, 00415320, 0042a750, 0042a7a0 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::preprocess` (macro table) | runtime/constant-helper/K22.exe.00419eb0.c:12-13; CONTRACT §6, §9; runtime/constant-helper/K22.exe.00419d20.c |
| `00419e90` | K22.exe | Registers presence macro whose expansion is its own name (RELEASE, DIRECT3D, CAPS_*, SERVER, CLIENT, WMB) | `u4 p1` | 00413990 | 00419d20 | — | **CONFIRMED** | — | CONTRACT §6 (L279-281), §9 (L359); runtime/constant-helper/K22.exe.00419e90.c |
| `00419eb0` | K22.exe | Numeric macro registration: decimal itoa of integer then 00419d20 | `u4 p1,int p2` | 00413990 | 00419d20 | — | **CONFIRMED** | engine.cpp `nativeConstants[]` | runtime/constant-helper/K22.exe.00419eb0.c:12-13 |
| `0041a0e0` | K22.exe | Macro-expanding token reader; skips class-7 punctuation | `void` | 0041a170, 0041a1b0, 0041aa60, 0041aaf0, 0041abb0 +2 | 00419ef0 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::parse` (cites 0041a0e0 for declaration params) | CONTRACT §10, §15, §16, §17; runtime/initializer-token/K22.exe.0041a0e0.c |
| `0041a170` | K22.exe | Declaration-name retrieval (one token via 0041a0e0) | `int p1` | 00402fe0, 00403b10, 004184d0, 0041f090, 00420990 +2 | 00415320, 0041a0e0 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::identifier` | CONTRACT §16 (L617); runtime/initializer-token/K22.exe.0041a170.c |
| `0041a1b0` | K22.exe | Initializer token: mode0 skips validation then atof (non-numeric → 0); mode2 validates via 00419ba0 | `int p1` | 00402fe0, 004174f0, 00418820, 00420990 | 00415320, 00419ba0, 0041a0e0, 005b72cc | 00663398 | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` (cites 0041a1b0/00420990) | runtime/initializer-token/K22.exe.0041a1b0.c:11-36 |
| `0041a250` | K22.exe | Concatenates remaining expression tokens without separators (identifiers may span whitespace) | `void` | 00402fe0 | 00415320 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::expression_identifier` [HIGH CONFIDENCE] | CONTRACT §16 (L608-611); runtime/initializer-token/K22.exe.0041a250.c |
| `0041a370` | K22.exe | Source lexer separating table-classified tokens | `void` | 0041a520 | — | — | **CONFIRMED** | wdl.cpp `lex` | CONTRACT §16 (L608); runtime/mode-lexer/K22.exe.0041a370.c |
| `0041a520` | K22.exe | Lexer: ignores comma outside expression mode; = < > are punctuation | `void` | 0041aef0 | 00401410, 00415320, 00419a40, 0041a000, 0041a370 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::statement` (cites 0041a520) | CONTRACT §17 (L642); K22.exe.0041a520.c |
| `0041aa60` | K22.exe | Reads one declaration parameter (via 0041a0e0) | `void` | 004023a0, 00402d60, 00402fe0, 00403a00, 0040ee60 +16 | 00415320, 0041a0e0 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::parse` | CONTRACT §17 (L622, L639-641); runtime/trace/K22.exe.0041aa60.c |
| `00420990` | K22.exe | Global VAR declaration: scalar takes first initializer; array length then elements via 0041a1b0(2) | `void` | — | 00415320, 0041a170, 0041a1b0, 004207e0 | 00663398 | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` (cites 00420990) | runtime/initializer/K22.exe.00420990.c:32-59 |
| `004207e0` | K22.exe | Numeric array allocate/redeclare (16+4n bytes); same-size redeclare overwrites, mismatch → Twice_defined | `u4 p1,int p2,uint p3,int p4,u4 *p5` | 00420990, 00420b30 | 0040f180, 0040f210, 00414e50, 0042a750 | 006bb1c4 | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` | runtime/initializer-token/K22.exe.004207e0.c:54-85; runtime/initializer-token/K22.exe.004207e0.asm.txt:154-196 |
| `00420b30` | K22.exe | Preallocates local pool `_loc255`..`_loc0` (28-byte objects) | `void` | 00413990 | 004207e0 | 006bb1c4 | **CONFIRMED** | wdl.cpp `Runtime::Impl::local_storage` / `LocalLease` [HIGH CONFIDENCE] | runtime/local-pool/K22.exe.00420b30.c:14-23 |
| `0040ef40` | K22.exe | BMAP declaration: NAME then FILENAME via 0041aa60 | `void` | — | 0040ee60, 0041aa60 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::parse` + `Runtime::Impl::initialize` | CONTRACT §17 (L639-647); runtime/entity-cancel/pointer-cleanup/generic/K22.exe.0040ef40.c |
| `0040ee60` | K22.exe | BMAP parameters: optional four numeric crop values | `u4 p1` | 0040ef40 | 0040e870, 0041aa60, 0041aaf0 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` | CONTRACT §17 (L648-650); runtime/entity-cancel/pointer-cleanup/variable-storage/K22.exe.0040ee60.c |
| `0040e870` | K22.exe | Bitmap constructor (filename,name,...) | `int p1,int p2,u4 p3,u4 p4,int p5, int p6,int p7` | 00404fc0, 0040c4d0, 0040eb20, 0040ee60 | 0040e270, 0040e5c0, 0040f210, 00415290, 00415320 +2 | — | **CONFIRMED** | assets.cpp `Assets::image` (decode) [HIGH CONFIDENCE] | CONTRACT §17 (L640-641); runtime/bitmap-storage-api/K22.exe.0040e870.c |

### Interpreter / scheduler / WAIT

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `00410a80` | K22.exe | Statement interpreter: runs compiled statement+0x10, returns when 006446e8 bit1 (yield); opcode38 PROC_LATE sets function+1c bit2; stores expression return in 006445b4 | `int p1` | 00411fe0, 004121b0, 00412220 | 0040c490, 0040c6a0, 00410550, 004105f0, 0041ce20 +13 | 006446e8, 006445b4 | **CONFIRMED** | wdl.cpp `Runtime::Impl::run` | runtime/interpreter/K22.exe.00410a80.c:819-853; CONTRACT §1, §2, §10, §11 |
| `00410390` | K22.exe | Appends continuation record (44+12*locals bytes: next, function, countdown, yield/next stmt, my, you, _arg1..4, locals) | `int p1,u4 p2,u4 p3,u4 p4,u4 p5 ,u4 p6,u4 p7,u4 p8,u4 p9, …` | 00404b60, 00404be0, 00404ca0, 004104f0, 00410520 | 00410350 | 006446e8 | **CONFIRMED** | wdl.cpp `Runtime::Impl::capture` | runtime/trace/K22.exe.00410390.c:9-25 |
| `00410350` | K22.exe | Captures three raw words per local (+10/+14/+18) into the record | `u4 *p1,int p2` | 00410390, 00412220 | — | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::capture` (savedLocals) | runtime/trace/K22.exe.00410350.c:8-17 |
| `00410310` | K22.exe | Restores local words from a record | `u4 *p1,int p2` | 00411fe0, 00412220 | — | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::run` (snapshot restore) | runtime/interpreter/K22.exe.00410310.c:9-19 |
| `00411fe0` | K22.exe | Resumes a record: restores next statement, my/you, args, locals, calls 00410a80, clears old function field | `void` | 00412080 | 00410310, 00410a80 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::tick` → `run` | runtime/trace/K22.exe.00411fe0.c:8-19 |
| `00412080` | K22.exe | Wakeup/carry-forward: countdown>=1024 → -1024 and retain; else resume iff c > -min(TIME,1024); freeze thresholds 1024/2048 | `void` | 004125e0 | 00410480, 00410750, 00411fe0, 00420730 | 00665874, 00665cf4 | **CONFIRMED** | wdl.cpp `Runtime::Impl::tick` (same thresholds; cites 004120ec) | runtime/trace/K22.exe.00412080.c:35-65 |
| `004125e0` | K22.exe | Frame script phase: clears 006446e8 bit2, double-buffered queue passes (bit2-clear then bit2-set), input dispatch (ENABLE_KEY, key handler then ON_ANYKEY), TOTAL_SECS | `void` | 00414650 | 00412080, 004121b0, 00412220 | 006446e8, 00665874, 0066573c, 0063ba04 | **CONFIRMED** | wdl.cpp `Runtime::Impl::tick` + engine.cpp `Engine::update_input` | runtime/trace/K22.exe.004125e0.c:14-108; runtime/trace/K22.exe.004125e0.asm.txt:54-276 |
| `00412220` | K22.exe | Immediate script invoke (ABI adapter): preserves my/you/locals across nested calls; returns child first-yield value; NULL → raw 0 | `code *p1` | 00412380, 00412390, 004123c0, 00412540, 004125e0 | 00410310, 00410350, 00410a80, 00411f80 | 006445b4 | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (indirect-call adapter; cites 00412220) and `Runtime::Impl::start` | runtime/abi/K22.exe.00412220.c:12-49 |
| `00404b60` | K22.exe | WAIT handler: clamp raw arg to [1024,1048575], countdown=n-1024, calls 00410390, sets yield bit | `int p1` | — | 00410390 | 006446e8 | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (`wait` branch) | runtime/trace/K22.exe.00404b60.c:8-17; CONTRACT §1-3 |
| `00404be0` | K22.exe | WAITT handler: n=max(TIME,arg) with cap 1048575, countdown=TIME-n | `int p1` | — | 00410390 | 00665cf4, 006446e8 | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (`waitt` branch) | runtime/trace/K22.exe.00404be0.c:11-19 |
| `00404ca0` | K22.exe | SLEEP handler (descriptor 0061d0e8, argc1) | `int p1` | — | 00410390 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (`sleep` branch: shifts arg <<4 then WAITT path) [native semantics, body not recovered in CONTRACT] | CONTRACT §1 (L23); runtime/api-descriptors.json; runtime/clock-instructions/K22.exe.00404ca0.c |
| `00404cd0` | K22.exe | proc_kill handler: SAR10 mode then 00410660 | `int p1` | — | — | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (`proc_kill` branch) | runtime/trace/K22.exe.00404cd0.c; CONTRACT §4 |
| `00410660` | K22.exe | proc_kill modes: 1 my==my, 2 my==you, 4 same function, 8 set 006446e8 bit2 (no more continuations until frame start); 16 unhandled | `byte p1` | — | 00410550, 004105f0 | 006446e8 | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (`proc_kill` cancel lambda) | runtime/trace/K22.exe.00410660.c:8-20 |
| `00410550` | K22.exe | Scans both queues; cancels records whose MY matches entity and clears matching YOU | `int p1,int p2` | 00410660, 00410a80, 00431920 | — | — | **CONFIRMED** | wdl.cpp `Runtime::kill_for_entity` | CONTRACT §4 (L152), §13 (L513-515); runtime/trace/K22.exe.00410550.c |
| `004105f0` | K22.exe | Companion queue scan to 00410550 (function/countdown clearing) | `int p1` | 00410660, 00410a80 | — | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` / `Runtime::kill_for_entity` | runtime/interpreter/K22.exe.004105f0.c; CONTRACT §4 (L152) |
| `004104f0` | K22.exe | Bootstrap enqueue: appends zero-countdown continuation via 00410390 with my/you/args 0 | `int p1` | 00414650 | 00410390 | 0063b9b4 | **CONFIRMED** | wdl.cpp `Runtime::bootstrap` (MAIN first, then STARTERs in reverse id order) [HIGH CONFIDENCE ordering match] | CONTRACT §13 (L490-496); runtime/bootstrap/K22.exe.004104f0.c |
| `00410520` | K22.exe | Queues an entity action continuation (action,entity,0) for the next scheduler phase | `int p1,u4 p2,u4 p3` | 004315d0, 00432530, 00432f40 | 00410390 | — | **CONFIRMED** | wdl.cpp `Runtime::enqueue` | CONTRACT §13 (L517-519); runtime/level-dispatch/K22.exe.00410520.c |

### Fixed-point numeric & conversion builtins

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `00403d60` | K22.exe | INT/integer: truncate toward zero, returns scaled var | `uint p1` | — | — | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`int`: `Value::from_int(raw/1024)`) | CONTRACT §5 (L204-207); runtime/api-descriptors.json; runtime/abi/K22.exe.00403d60.c |
| `00406fc0` | K22.exe | FLOAT: float32(raw)/1024, returns float bits in var word | `int p1` | — | — | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`float`) | runtime/abi/K22.exe.00406fc0.asm.txt; CONTRACT §5 (L214); runtime/abi/K22.exe.00406fc0.c |
| `00406fe0` | K22.exe | FLOATR: packs reciprocal float bits | `int p1` | — | — | — | **CONFIRMED** | NOT IMPLEMENTED (name listed in native_calls.inc only) | CONTRACT §5 (L215); runtime/abi/K22.exe.00406fe0.c |
| `00407010` | K22.exe | FLOATD: packs float ratio of two scaled args | `int p1,int p2` | — | — | — | **CONFIRMED** | NOT IMPLEMENTED (name listed in native_calls.inc only) | CONTRACT §5 (L216); runtime/abi/K22.exe.00407010.c |
| `00403eb0` | K22.exe | VECTOR: advances shared 64-slot ring cursor 0061d7e8 by 12, copies three raw words, returns slot in EAX | `u4 p1,u4 p2,u4 p3` | — | — | 0061d7e8, 0063ce30..0063d130 | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`vector`, 64-entry `vectorTemps_` ring) | runtime/vector-temporary/K22.exe.00403eb0.asm.txt; CONTRACT §18; runtime/vector-temporary/K22.exe.00403eb0.c |
| `00407040` | K22.exe | _vec: same ring; converts float32 args ×1024 via 005b72cc | `void` | — | 005b72cc | 0061d7e8, 005d1d2c | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`_vec`) | runtime/vector-temporary-extra/K22.exe.00407040.asm.txt; CONTRACT §18; runtime/vector-temporary-extra/K22.exe.00407040.c |

### Global registry & definitions

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `0041e060` | K22.exe | Registers each of 330 numeric globals as WDL type9 with 24-byte value object | `u4 p1` | 0041e180 | 0040f210, 0042a750 | 0061ec80..00620b70 | **CONFIRMED** | engine.cpp `nativeDefaults[]` / `Engine::initialize_globals` | runtime/registry/K22.exe.0041e060.c:9-32 |
| `0041e180` | K22.exe | Copies registry defaults to object+4/+8/+c, writes pointer slot, ORs flags into object+14; fills interface pointer array until first `_` name | `void` | 00413990 | 00401e80, 00401f00, 0041e060 | 0061ec80..00620b70 | **CONFIRMED** | engine.cpp `nativeDefaults[]` (e.g. VERSION 6465 = 0x1941) | runtime/registry/K22.exe.0041e180.c:17-45; runtime/global-registry.json |
| `004015c0` | K22.exe | Member-descriptor consumer (stride 20; type 0x11 bit properties with mask, type 0x10 numeric members) | `char *p1,int *p2,int *p3` | 004016f0 | 004014d0, 00414e50 | — | **CONFIRMED** | engine.cpp `nativeMasks[]` / `core_mask` | CONTRACT §6 (L252-257); runtime/member-descriptors.json; K22.exe.004015c0.c |
| `0041efb0` | K22.exe | Registers scalar star-pointer declarations as persistent registered cells | `u4 p1` | 00417b20, 0041f090, 0041f720, 0041fb80 | 0040f210, 0042a750 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` (pointerCell; cites 0041efb0/0041f090) | CONTRACT §27 (L1144-1145); runtime/font-text-init/K22.exe.0041efb0.c |
| `0041f090` | K22.exe | Companion registration for registered pointer cells | `void` | — | 00401bd0, 0040f380, 0041a170, 0041efb0 | — | **CONFIRMED** | wdl.cpp `Runtime::Impl::initialize` | CONTRACT §27 (L1145); runtime/entity-cancel/pointer-cleanup/parser/K22.exe.0041f090.c |
| `0041f050` | K22.exe | Clears registered pointer cells equal to a removed entity (cache and backing slot) before actor teardown | `int p1` | 00431920 | — | — | **CONFIRMED** | wdl.cpp `Runtime::kill_for_entity` (registered pointer NULL step) | CONTRACT §26-27 (L1134-1145); runtime/entity-cancel/pointer-cleanup/primary/K22.exe.0041f050.c |

### Input

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `00401e10` | K22.exe | Case-insensitive key-name lookup over 89 names (0061b108, stride 8); unknown → -1 | `char *p1` | 00401ff0, 00402140 | — | 0061b108, 0061b3d0 | **CONFIRMED** | engine.cpp `nativeKeys[]` (89 rows) | CONTRACT §12 (L427-431); runtime/input-contract.json; runtime/lookup/K22.exe.00401e10.c |
| `00404d30` | K22.exe | key_pressed: SAR10; 0..126 keyboard 006659a8, 256..279 joystick, 280..282 mouse; else 0 | `int p1` | — | — | 006659a8, 00665930..0066595c, 00665c70..00665c9c, 0066597c..00665984 | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`key_pressed`) | CONTRACT §12 (L433-441); runtime/input/K22.exe.00404d30.c |
| `0042ddd0` | K22.exe | Window procedure: WM_KEYDOWN/SYSKEYDOWN scan=(lParam>>16)&0x7f, edge only from released state; KEYUP clears held | `HWND p1,uint p2,uint p3,int p4` | 0042ece0 | 00412a80, 00413740, 00413990, 00422f40, 00424e30 +18 | — | **CONFIRMED** | engine.cpp `legacy_scancode` + SDL input in main.cpp [behavioral substitute] | runtime/input-window/K22.exe.0042ddd0.c:27-112 |
| `00422970` | K22.exe | Copies queued held/edge keys into frame arrays when INKEY_ACTIVE==0; always clears pending edges | `void` | 00413990, 00414650 | 00422000, 004228e0 | — | **CONFIRMED** | engine.cpp `Engine::update_input` [HIGH CONFIDENCE] | runtime/input-keyboard/K22.exe.00422970.c:10-30 |
| `00422000` | K22.exe | Publishes keys 1..127 as (held OR fresh-down)?1024:0 | `void` | 00422970 | 00421c80, 00421e50, 005b72cc | — | **CONFIRMED** | engine.cpp `Engine::key_state` / `core_key_bits` [HIGH CONFIDENCE] | runtime/input-scan/K22.exe.00422000.c:21-35 |
| `00421c80` | K22.exe | Mouse down-edge producer | `void` | 00422000 | 00421c10, 00428620, 00428880, 00428970 | — | **CONFIRMED** | engine.cpp `Engine::update_input` | CONTRACT §12 (L453-454); runtime/clock/K22.exe.00421c80.c |
| `00421e50` | K22.exe | Joystick down-edge producer | `void` | 00422000, 00422520 | 00428f40 | — | **CONFIRMED** | engine.cpp `Engine::update_input` | CONTRACT §12 (L453-454); runtime/input-edges/K22.exe.00421e50.c |
| `0040e0e0` | K22.exe | ON_CLICK hit-test/UI path (separate from ON_MOUSE_LEFT) | `void` | 0040e180 | 0040d9a0, 004121b0, 00412390, 00412540 | — | **CONFIRMED** | engine.cpp `Engine::update_ui` (panel on_click buttons) [HIGH CONFIDENCE] | CONTRACT §12 (L460-462); runtime/font-xrefs/K22.exe.0040e0e0.c |

### Text, strings & media handoff

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `00405e20` | K22.exe | Shared token reader (FILE in EBX, dest EAX, limit on stack): skip bytes<=0x1f, stop at <=0x1f or delimit_str[0] | `int p1` | — | — | 00620c78 | **CONFIRMED** | engine.cpp `Engine::builtin` (`txt_load`) and `Engine::service_builtin` (`file_str_read`) | runtime/string-paths/K22.exe.00405e20.c; CONTRACT §14 (L524-541) |
| `00408890` | K22.exe | TXT_LOAD: inserts every positive reader result as new string; shared buffer 0063bc30, limit 4095; returns count<<10 | `int p1,uint *p2` | — | 0041ce20, 004205e0, 0042eca0, 005b8acc | 0063bc30 | **CONFIRMED** | engine.cpp `Engine::builtin` (`txt_load`) | runtime/string-paths/K22.exe.00408890.asm.txt:64-90; runtime/text-load/K22.exe.00408890.c |
| `00408470` | K22.exe | FILE_STR_READ handler (shares reader 00405e20) | `int p1,int p2` | — | 00420520, 0042a7a0, 0042eca0 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`file_str_read`) | runtime/api-descriptors.json; CONTRACT §14 (L524); runtime/text-load/K22.exe.00408470.c |
| `00404780` | K22.exe | STR_CLIP: removes a PREFIX of SAR10(count) chars; no negative clamp (0x004047a5..bf) | `int p1,int p2` | — | — | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`str_clip`; throws on negative count) | runtime/string-paths/K22.exe.00404780.c; CONTRACT §14 (L562-568) |
| `00405d10` | K22.exe | MEDIA_PLAY handler → 00405ca0 | `u4 p1,u4 p2,u4 p3` | — | — | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`media_play`) → media.cpp `Media::movie_play` | CONTRACT §14 (L553); runtime/string-paths/K22.exe.00405d10.c |
| `00405ca0` | K22.exe | Unwraps script STRING, passes filename unchanged to 00425730 | `uint *p1,int p2,u4 p3,u4 p4` | — | 00425730, 00428460, 0043bb10 | — | **CONFIRMED** | media.cpp `Media::movie_play` | CONTRACT §14 (L553-554); runtime/string-paths/K22.exe.00405ca0.c |
| `00425730` | K22.exe | DirectShow handoff: MultiByteToWideChar(CP_ACP) then graph vtable+34 | `int p1,LPCSTR p2,int p3,int p4` | 00405ca0 | 0040f5c0, 0040f600, 00424aa0, 00424d90, 00425680 +5 | — | **CONFIRMED** | media.cpp `Media::movie_play` (FFmpeg) [behavioral substitute] | runtime/media-paths/K22.exe.00425730.c:68-69 |
| `00420f00` | K22.exe | Registers engine STRING delimit_str default "," (two bytes) | `void` | 00413990 | 004205e0 | 00620c78 | **CONFIRMED** | engine.cpp `Engine::initialize_globals` (`delimit_str`) | runtime/media-paths/K22.exe.00420f00.c:24 |
| `004205e0` | K22.exe | String construction (calls 00420520); bytes copied unchanged | `int p1,char *p2,int p3,u4 p4` | 00402fe0, 00408890, 0040a300, 0040fa80, 00420c00 +2 | 0040f210, 00415320, 00420520, 0042a750, 0042a7a0 | — | **CONFIRMED** | value.hpp `State::make_string` [HIGH CONFIDENCE] | CONTRACT §14 (L547); runtime/numeric/K22.exe.004205e0.c |
| `00420520` | K22.exe | String storage copy helper | `int p1,char *p2,int p3,uint p4` | 00404500, 00408470, 004205e0, 00420c00, 00420c2f +1 | 0042a750 | — | **CONFIRMED** | — | CONTRACT §14 (L547); runtime/text-load/K22.exe.00420520.c |

### Level lifecycle & transitions

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `00404f00` | K22.exe | level_load: sets filename 65580 and pending command bit 6557c=8, returns raw1024 without loading | `uint *p1` | — | — | — | **CONFIRMED** | engine.cpp `Engine::world_builtin` (`level_load` sets pendingLevel, returns 1) | CONTRACT §13 (L501-503); runtime/bootstrap/K22.exe.00404f00.c |
| `0041de10` | K22.exe | Level dispatch: processes command bit8 via 00434670 only when INKEY_ACTIVE==0, then clears commands | `void` | 00410a80, 00414650 | 00412a80, 0041d6e0, 0041da70, 0041dd50, 00434670 | — | **CONFIRMED** | engine.cpp `Engine::tick` (pendingLevelLoad → `Engine::load_level` after script phase) | runtime/level-dispatch/K22.exe.0041de10.c:11-43 |
| `00434670` | K22.exe | Level transition: stop level sound slots 0042f880(0xc00), clear entity+80, teardown 004341a0, release 0043ba30, construct 00434220 | `void` | 0041d6e0, 0041de10 | 0042f880, 004341a0, 00434220, 0043ba30, 00460030 | — | **CONFIRMED** | engine.cpp `Engine::load_level` | runtime/level-transition/K22.exe.00434670.c; CONTRACT §13 (L510-511) |
| `0042f880` | K22.exe | SND_STOPALL handler (also called with 0xc00 by level transition) | `uint p1` | 00434670 | 0041edd0, 0042b130 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`snd_stopall`, mode 4 only) | runtime/api-descriptors.json; CONTRACT §13 (L510); runtime/level-transition/K22.exe.0042f880.c |
| `004341a0` | K22.exe | Level clear: walks old entities calling 00431920(entity,1) | `void` | 00413740, 00434670 | 0041e2b0, 00431920, 004374b0, 00450750 | — | **CONFIRMED** | engine.cpp `Engine::load_level` / `Engine::remove_entity` | runtime/level-clear/K22.exe.004341a0.c |
| `0043ba30` | K22.exe | Level resource release step | `void` | 0040e270, 0040ed60, 00434670 | 00426890, 004268f0 | — | **HIGH CONFIDENCE** | assets.cpp `Assets::release_level` [HYPOTHESIS mapping] | CONTRACT §13 (L511); runtime/font-entries/K22.exe.0043ba30.c |
| `00434220` | K22.exe | Level construction (also optional WMB-entry bootstrap path) | `u4 p1` | 00414650, 00434670 | 0040f720, 00412a80, 0042eca0, 0042f7b0, 004315d0 +19 | — | **CONFIRMED** | engine.cpp `Engine::load_level` | CONTRACT §13 (L498, L511); runtime/bootstrap/K22.exe.00434220.c |
| `004315d0` | K22.exe | Level entity construction: resolves declared actions with 00402530, queues via 00410520 for next phase; no STARTER rerun | `int *p1,u4 p2,u4 p3` | 00434220 | 00402530, 00410520, 0042eca0, 0042fd30, 00430800 +6 | — | **CONFIRMED** | engine.cpp `Engine::load_level` | runtime/level-lifecycle/K22.exe.004315d0.c:108-119 |
| `00431920` | K22.exe | Entity cancel/teardown: if event_flags nonzero, 00410550(entity,1) and registered-pointer cleanup (sites 00431976/00431986) | `uint *p1,int p2` | 00431a10, 00431d20, 00432f40, 004341a0 | 0040f7e0, 00410550, 0041edd0, 0041f050, 00434cc0 +3 | — | **CONFIRMED** | wdl.cpp `Runtime::kill_for_entity` (cites 00431920, 00431976/00431986) | runtime/entity-cancel/K22.exe.00431920.c:15-17; CONTRACT §13, §27 |
| `004347b0` | K22.exe | Entity declaration defaults: ENTITY red/green/blue = 128 | `int p1` | 00417b20, 00434850 | 00434730 | — | **CONFIRMED** | engine.cpp `Engine::register_entity` (cites 004347b0 / A7 1003ac80) | CONTRACT §26 (L1118); runtime/entity-ambient-source/K22.exe.004347b0.c |

### World, model, collision, render state

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `00459eb0` | K22.exe | WMB object record parser: type3 entity names (name40/20, file60/13, action73/20; byte72 → 32-char file via 0045ccf0), type5 sun angle, type7 372-byte copy | `int *p1` | 0045b1e0 | 004076c0, 0041e7c0, 0042eca0, 0043a700, 0045ccf0 +1 | — | **CONFIRMED** | assets.cpp `Assets::decode_world` (type3 byte72 branch, padded 136/372) | runtime/entity-file-layout/contract.txt; CONTRACT §19, §24; runtime/sun-header-xrefs/K22.exe.00459eb0.c |
| `0045ccf0` | K22.exe | Bounded string copy (capacity 33, decrements before write) for long WMB6 entity filenames | `char *p1,char *p2,int p3` | 0040d460, 0040eb60, 00412b90, 0041ff20, 004315d0 +5 | — | — | **CONFIRMED** | assets.cpp `Assets::decode_world` (`c.str(32)`) | CONTRACT §24 (L816-819); runtime/font-layout/K22.exe.0045ccf0.c |
| `00456530` | K22.exe | WMB post-parser: bitmap-chain initialization (not sun RGB) | `void` | 0045b1e0 | — | — | **CONFIRMED** | assets.cpp `Assets::decode_world` | runtime/sun-header-asm/contract.txt; CONTRACT §19 (L734-736); runtime/sun-level-closure/K22.exe.00456530.c |
| `00457980` | K22.exe | WMB post-parser: geometry-lookup initialization | `void` | 0045b1e0 | 0043a700, 00457910 | — | **CONFIRMED** | assets.cpp `Assets::decode_world` (cites 00457980) | CONTRACT §19 (L734); runtime/sun-level-closure/K22.exe.00457980.c |
| `00458500` | K22.exe | MDL skin/UV preparation: legacy pixel UVs normalized by 00426aa0 padded dims; MDL7 float UV × raw/padded | `int p1,ushort *p2,uint p3` | 0045b630 | 00426aa0, 0042eca0, 0043a700, 0043a7a0, 0043a990 +25 | — | **CONFIRMED** | assets.cpp `Assets::decode_model` | runtime/model-uv-contract/contract.txt; CONTRACT §25-26; runtime/sky-normalize/K22.exe.00458500.c |
| `00426aa0` | K22.exe | Rounds skin dimension up to power of two, minimum 8 (table initialized at 00413eaf) | `int p1` | 00426ad0, 00458500 | — | — | **CONFIRMED** | assets.cpp `mdl_texture_extent` (cites 00426aa0 / A7 1002c580) | CONTRACT §25 (L832-834); runtime/model-uv-contract/K22.exe.00426aa0.c |
| `005b6a80` | K22.exe | MDL face reader: outputs vertex/UV indices only | `int p1,uint *p2,uint *p3,uint *p4,uint *p5,uint *p6, uint…` | 00458500 | 005b6550 | — | **CONFIRMED** | assets.cpp `Assets::decode_model` (cites 005b6a80) | CONTRACT §26 (L858); runtime/entity-skin-mdl7/K22.exe.005b6a80.c |
| `0045b630` | K22.exe | HMP terrain loader; reaches 00458500 | `char *p1,char *p2,uint p3` | 0045b810 | 00420120, 0042a7a0, 0042eca0, 00458500, 00459a90 +2 | 006441f0 | **CONFIRMED** | assets.cpp `Assets::decode_model` (HMP path) [HIGH CONFIDENCE] | CONTRACT §26 (L860); runtime/sky-normalize/K22.exe.0045b630.c |
| `004349f0` | K22.exe | bmap_for_entity: truncates selector, clamps against model-wide bank, does not mutate ENTITY.SKIN | `int p1,int p2` | 00405410 | 00434940 | — | **CONFIRMED** | engine.cpp `worldModelBuiltin` (`bmap_for_entity`) | runtime/entity-skin-contract/contract.txt; CONTRACT §26 (L854-856); runtime/entity-skin-selection/K22.exe.004349f0.c |
| `0044a590` | K22.exe | Draw preparation: clamps and writes ENTITY+0x40 skin selector | `void` | 0044ad00 | 004583c0, 00459970 | — | **CONFIRMED** | engine.cpp `Engine::make_frame` [HYPOTHESIS mapping] | CONTRACT §26 (L856-857); runtime/entity-skin-render/K22.exe.0044a590.c |
| `00437190` | K22.exe | Adds scalar ambient to cached RGB; normalizes by float32(255/max) when >255; packs via 00437100 | `int p1,u4 p2` | 00437ef0, 0043ee40, 00441210, 00443350, 00444eb0 | 00437100, 005b72cc | — | **CONFIRMED** | engine.cpp `Engine::prepare_entity_light` [HYPOTHESIS mapping] | runtime/entity-ambient-source/contract.txt; CONTRACT §21 (L762-764); runtime/entity-ambient-consumers/K22.exe.00437190.c |
| `00438b10` | K22.exe | Model draw path dispatched from 0044ad00; stage0 COLOROP choice not recovered | `void` | 0044ad00 | 00437e90, 00438030, 004390e0, 00439230 | — | **HIGH CONFIDENCE** | — | CONTRACT §21 (L766-769); runtime/lighting-writers/K22.exe.00438b10.c |
| `0044ad00` | K22.exe | Caller dispatching to 00438b10 (model path) | `void` | 0044f2e0, 0044f420, 0044f970, 0044fde0 | 00438b10, 0043a990, 0043d4f0, 00442770, 004465c0 +5 | — | **HIGH CONFIDENCE** | — | CONTRACT §21 (L766); runtime/model-stage-candidates/K22.exe.0044ad00.c |
| `0043dc70` | K22.exe | Sampler setup: MIP filter, LOD bias, MAXANISOTROPY sent as float bits of raw/1024 (decompiler shows 0 wrongly) | `int p1` | 0043ee40, 0043f460, 00440be0, 00441210, 00442310 +3 | — | — | **CONFIRMED** | engine.cpp `Engine::make_frame` (frame_.anisotropy) + render_gl.cpp sampler setup (enable/disable only) | runtime/sampler-config/contract.txt; CONTRACT §23; runtime/portal-composition/K22.exe.0043dc70.c |
| `0043dfc0` | K22.exe | Sampler mip/LOD control companion | `int p1,int p2` | 0043f280, 0043f370, 00441130, 00441210, 00444eb0 | — | — | **CONFIRMED** | render_gl.cpp sampler creation [HIGH CONFIDENCE] | runtime/sampler-config/K22.exe.0043dfc0.c; CONTRACT §23 |
| `004550e0` | K22.exe | FONT palette normalization: entry0 black, dark entries →8, active 255 white | — (no export) | — | — | 007192bc | **CONFIRMED** | engine.cpp `Engine::set_font_palette` (cites 004550e0/00425f20) | CONTRACT §40b (L2198-2202) |
| `00455240` | K22.exe | Active palette selection: keep existing, else <input>.raw, palette.raw, RGB332 fallback | `void` | — | 00420120, 0042a720, 00455180, 005b72cc | 007192bc | **CONFIRMED** | engine.cpp `Engine::ensure_font_palette` (cites 00455240); assets.cpp `Assets::initialize_native_lightmap_tables` | CONTRACT §40b (L2195-2197); runtime/tex-lut-lifecycle/K22.exe.00455240.c |
| `00472030` | K22.exe | c_trace input preparation (USE_BOX+MY hull, MOVE_BUFFER, zero-length 51/1024) | `u4 *p1` | 00406720, 0046f3b0, 00472a60 | 00467530, 00469780, 0046a370, 0046f010 | — | **CONFIRMED** | engine.cpp `Engine::collide` | CONTRACT §48 (L1988); runtime/entity-ambient-source/K22.exe.00472030.c |
| `00470a50` | K22.exe | Modern trace hull builder (MIN+span/2 rotated; SCALE not read) | UNKNOWN (no export) | — | — | — | **CONFIRMED** | engine.cpp `Engine::collide` (cites 00470a50) | CONTRACT §48 (L1988); native/check_engine.cpp comment |

### A7 Bonus engine (acknex.dll, Kabus 22 Mode)

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `1000bf40` | acknex.dll | Native-call descriptor search over 394 rows at 101a8200 before symbol resolution 1001f410 | `void` | — | 10002960, 10002b30, 1000be90, 1000dc20, 1001f410 +1 | 101a8200..101a9aa0 | **CONFIRMED** | wdl.cpp `Runtime::Impl::expression` (cites A7 1000bf40); native_calls.inc Bonus list | runtime/bonus-core/predefined-surface/initial/acknex.dll.1000bf40.c:175-240; CONTRACT §41 (L1640) |
| `10008190` | acknex.dll | `effect` API handler → 10036e70 | `u4 p1,uint p2,u4 p3,u4 p4` | — | 10036e70 | — | **CONFIRMED** | engine.cpp `Engine::world_builtin` (`effect`) | runtime/bonus-core/effect-null-direction/acknex.dll.10008190.c; CONTRACT §32 (L1319) |
| `10036e70` | acknex.dll | Effect emitter: NULL 4th (direction) pointer → three zero DWORDs (10036e9a..ae) | `u4 p1,u4 p2,int p3,u4 *p4,int p5` | 10008190, 100081c0, 100155d0 | 10035550, 10035590, 100355e0, 10035620, 100359f0 +2 | — | **CONFIRMED** | engine.cpp `Engine::world_builtin` (cites 10036e9a..10036eae) | runtime/bonus-physics/contact-defaults/acknex.dll.10036e70.c; CONTRACT §32 (L1319-1321) |
| `1007bc40` | acknex.dll | Physics partner gate: rejects static ENTITY partner with flag 512 (sites 1007bcc8/1007bce5) | `int p1,int p2` | 1007bd00 | 1006df90, 1007af60, 1007b470 | — | **CONFIRMED** | physics.cpp `Physics::collide` (cites 1007bcc8/1007bce5) | runtime/bonus-physics/edges/acknex.dll.1007bc40.c; CONTRACT §32 (L1334) |
| `100c9930` | acknex.dll | GenerateContact: float32 contact comparison with x87 intermediates; capacity check before duplicate scan | `u4 p1,uint *p2,int p3,float p4,float p5,float *p6 ,float …` | 100caac0 | — | — | **CONFIRMED** | physics.cpp `Physics::collect_contact` [HYPOTHESIS mapping; native cites 1007ab80 there] | runtime/bonus-physics/world-contact-boundary/precision-normal/acknex.dll.100c9930.c; CONTRACT §36 (L1458) |
| `101589eb` | acknex.dll | MDL7 face reader: widens only UV ushort 0xffff to 0xffffffff; absent-UV float 10187124 = 0.0 | `int p1,uint *p2,uint *p3,uint *p4,uint *p5,uint *p6, uint…` | — | 10157771 | 10187124 | **CONFIRMED** | assets.cpp `Assets::decode_model` (cites 101589eb) | runtime/bonus-asset-census/mdl7-reader/face/acknex.dll.101589eb.c; CONTRACT §30 (L1253-1254) |
| `10025cc0` | acknex.dll | Constructor registering PLAYER_NAME as live STRING borrowing 32-byte buffer 101f64b0 (flags 0x24) | `void` | 100181f0 | 10025520 | 101f64b0 | **CONFIRMED** | engine.cpp `Engine::initialize_globals` (cites 10025d72) | runtime/bonus-core/entry/mount-exports/acknex.dll.10025cc0.c; CONTRACT §28 (L1172) |
| `1001ff00` | acknex.dll | FLAGS token grammar: skips + and \|, matches literal flag names (PANEL/TEXT FILTER mask 0x100000) | `void` | 10013c03, 1001c640, 1001ddf0, 1001de3d, 10025fc0 | 1001fea0 | — | **CONFIRMED** | engine.cpp `core_mask` (FILTER 0x100000, cites 100015e0) | runtime/bonus-core/predefined-surface/token-lifetime/acknex.dll.1001ff00.c; CONTRACT §28 (L1180) |

### Patch plugin kabus_dll.dll (community patch, not ISO original)

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `1007aa1e` | kabus_dll.dll | A6_DLLAPI export thunk → 100871d0 | `int p1` | — | — | — | **CONFIRMED** | engine.cpp `Engine::initialize_plugins` / `svc_plugin_open` (compiled provider) | RECOVERY.txt:75,97; kabus_dll.dll.1007aa1e.c |
| `100871d0` | kabus_dll.dll | A6_DLLAPI implementation: resolves engine API names via interface+0xa30 (incl. proc_status → NULL on 6.31.4), stores bindings, installs wrappers, returns 0x276 | `int p1` | — | — | 101a1b4c, 101a1888, 101a1b9c, 101a1b20, 101a16f0, 101a1704, 101a172c, 101a1788, 101a1a08, 101a17d4, 101a1804 | **CONFIRMED** | — (no engine API binding needed natively) | RECOVERY.txt:75-90,97; kabus_dll.dll.100871d0.c; CONTRACT §4 (L166) |
| `1007c67f` | kabus_dll.dll | DiscordRPC_IsConnected thunk → 100c32d0 | `void` | — | 100ab5c0 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`discordrpc_isconnected`) | RECOVERY.txt:98; kabus_dll.dll.1007c67f.c |
| `100c32d0` | kabus_dll.dll | IsConnected impl: false unless instance 101a1694 exists and status true | `void` | — | 100ab5c0 | 101a1694 | **CONFIRMED** | engine.cpp `Engine::service_builtin` + `svc_receive` | RECOVERY.txt:98; final/kabus_dll.dll.100c32d0.c |
| `100803b5` | kabus_dll.dll | DiscordRPC_SetStateDetails thunk → 100c3360 | `int p1,int p2` | — | 100a2010, 100a5460, 100a6420, 100adfb0, 100aed90 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`discordrpc_setstatedetails`) | RECOVERY.txt:99; kabus_dll.dll.100803b5.c |
| `100c3360` | kabus_dll.dll | SetStateDetails impl: submits two strings only when connected | `int p1,int p2` | — | 100a2010, 100a5460, 100a6420, 100adfb0, 100aed90 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` | RECOVERY.txt:99; final/kabus_dll.dll.100c3360.c |
| `1007b68a` | kabus_dll.dll | DiscordRPC_TryConnect thunk → 100c3470 | `int p1` | — | 100ae300, 100aed90 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`discordrpc_tryconnect`) | RECOVERY.txt:100; kabus_dll.dll.1007b68a.c |
| `100c3470` | kabus_dll.dll | TryConnect impl: returns whether instance exists, not connection success | `int p1` | — | 100ae300, 100aed90 | — | **CONFIRMED** | engine.cpp `svc_connect` | RECOVERY.txt:100; final/kabus_dll.dll.100c3470.c |
| `1007aedd` | kabus_dll.dll | DiscordRPC_TryDisconnect thunk → 100c3530 | `void` | — | 100ae3f0 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`discordrpc_trydisconnect`) | RECOVERY.txt:101; kabus_dll.dll.1007aedd.c |
| `100c3530` | kabus_dll.dll | TryDisconnect impl: disconnect only if IsConnected | `void` | — | 100ae3f0 | — | **CONFIRMED** | engine.cpp `svc_disconnect` | RECOVERY.txt:101; final/kabus_dll.dll.100c3530.c |
| `1007bdfb` | kabus_dll.dll | INI_GetVariable thunk → 100c3600 | `int p1` | — | 100a0770, 100a4b80, 100aa9b0, 100aed90, 100b6b70 +1 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`ini_getvariable`) | RECOVERY.txt:102; kabus_dll.dll.1007bdfb.c |
| `100c3600` | kabus_dll.dll | INI_GetVariable impl: map lookup, allocated C-string copy; missing key → literal "undefined" | `int p1` | — | 100a0770, 100a4b80, 100aa9b0, 100aed90, 100b6b70 +1 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (cites 100c3600; returns "undefined") | RECOVERY.txt:102; final/kabus_dll.dll.100c3600.c |
| `100aa9b0` | kabus_dll.dll | INI lookup helper; uses literal "undefined" on missing key | `u4 p1,int p2` | 1007bdfb, 1007c940, 100c3600 | 10090830, 100a2360, 100a5560, 100a9d60, 100aa690 +5 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`ini_getvariable`) | RECOVERY.txt:102; final/kabus_dll.dll.100aa9b0.c |
| `1007b5d6` | kabus_dll.dll | INI_IsInitialized thunk → 100c3730 | `void` | — | — | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`ini_isinitialized`) | RECOVERY.txt:103; kabus_dll.dll.1007b5d6.c |
| `100c3730` | kabus_dll.dll | IsInitialized impl: pointer 101a1698 exists; logs "Ini not initialized." | `void` | — | — | 101a1698 | **CONFIRMED** | engine.cpp `Engine::service_builtin` | RECOVERY.txt:103; final/kabus_dll.dll.100c3730.c |
| `1007d250` | kabus_dll.dll | INI_SetVariable thunk → 100c37c0 | `int p1,int p2` | — | 100a0770, 100a4b80, 100adb00, 100aed90 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`ini_setvariable`) | RECOVERY.txt:104; kabus_dll.dll.1007d250.c |
| `100c37c0` | kabus_dll.dll | SetVariable impl: no-op if uninitialized; calls setter 100adb00 | `int p1,int p2` | — | 100a0770, 100a4b80, 100adb00, 100aed90 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (native throws if uninitialized) | RECOVERY.txt:104; final/kabus_dll.dll.100c37c0.c |
| `100adb00` | kabus_dll.dll | INI setter helper | `int p1,int p2` | 1007cc6f, 1007d250, 100c37c0 | 1008f8d0, 10090830, 100a0bf0, 100a4b80, 100a5560 +6 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` | RECOVERY.txt:104; final/kabus_dll.dll.100adb00.c |
| `1007d7eb` | kabus_dll.dll | Parse_EscapeChar export thunk → 100c38b0 | `int p1` | — | 100a0770, 100a4b80, 100aa280, 100aed90, 100b6b70 +1 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`parse_escapechar`) | RECOVERY.txt:105; kabus_dll.dll.1007d7eb.c; CONTRACT §15 (L596) |
| `100c38b0` | kabus_dll.dll | Parse_EscapeChar impl: converts engine string, runs 100aa280, returns allocated copy | `int p1` | — | 100a0770, 100a4b80, 100aa280, 100aed90, 100b6b70 +1 | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`parse_escapechar`) | RECOVERY.txt:105; final/kabus_dll.dll.100c38b0.c |
| `100aa280` | kabus_dll.dll | Escape substitution over nine pairs (\' \" \\ \n \r \t \b \f \v); does not touch forward slashes | `u4 p1,int p2` | 1007d7eb, 1007eaa1, 100c38b0 | 1008e240, 1009ca20, 1009cac0, 100a0260, 100a0340 +7 | 10184014..1018405b | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`parse_escapechar`) | RECOVERY.txt:108; final/kabus_dll.dll.100aa280.c |
| `1008e240` | kabus_dll.dll | Escape-pair constructor (escaped bytes, raw char) | `u4 p1,u4 p2` | 1007eaa1, 100aa280 | 1009c0f0, 1009c140, 100a0770 | — | **CONFIRMED** | — | RECOVERY.txt:108; final/kabus_dll.dll.1008e240.c |
| `1009ca20` | kabus_dll.dll | Escape-pair accessor (pair+0x18) | `int p1` | 1007eaa1, 100aa280 | — | — | **CONFIRMED** | — | RECOVERY.txt:108; final/kabus_dll.dll.1009ca20.c |
| `1009cac0` | kabus_dll.dll | Escape-pair accessor (pair+0) | `u4 p1` | 1007eaa1, 100aa280 | — | — | **CONFIRMED** | — | RECOVERY.txt:108; final/kabus_dll.dll.1009cac0.c |
| `100aed90` | kabus_dll.dll | Engine-string accessor: character buffer at object+0xc | `int p1` | 1007ae74, 1007b68a, 1007bdfb, 1007d250, 1007d7eb +6 | — | — | **CONFIRMED** | — | RECOVERY.txt:106; final/kabus_dll.dll.100aed90.c |
| `1011d2e1` | kabus_dll.dll | Return allocator: strlen+1 allocate and copy | `char *p1` | 1007bdfb, 1007d7eb, 1007ebf5, 100c3600, 100c38b0 | 1014a569, 101520ff | — | **CONFIRMED** | — | RECOVERY.txt:106; final/kabus_dll.dll.1011d2e1.c |

### Gamma plugin RenkAyar.dll (shipped original)

| Address | Binary | Purpose | Parameters | Callers | Callees | Related globals | Confidence | Native reimplementation (file:symbol) | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| `100016a0` | RenkAyar.dll | A6_DLLAPI: binds "draw_begin" via interface+0xa30 into 10009f30; returns 0x276 | `int p1` | — | — | 10009f30 | **CONFIRMED** | engine.cpp `Engine::initialize_plugins` → `svc_plugin_open("renkayar.dll")` | RenkAyar.behavior.txt:13-19; RenkAyar.dll.100016a0.c:54 |
| `10001080` | RenkAyar.dll | dl_PaKaGa(b,c,g,mode): SAR10 args, linear+parabolic 256-entry ramp, modes 1 invert / 2 R / 3 G / 4 B, SetGammaRamp via device vtable+0x54 | `int p1,u4 p2,u4 p3,int p4` | — | — | 100070c0..100070e0 | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`dl_pakaga`, same formula into nativeAdjustment.gammaRamp) | RenkAyar.behavior.txt:26-57; RenkAyar.dll.10001080.c |
| `10001010` | RenkAyar.dll | dl_Reset: identity ramp 257*i, returns 0 | `void` | — | — | — | **CONFIRMED** | engine.cpp `Engine::service_builtin` (`dl_reset`) | RenkAyar.behavior.txt:21-24; RenkAyar.dll.10001010.c |
| `100023e8` | RenkAyar.dll | DLL CRT entry (external entry/export) | `u4 p1,int p2,u4 p3` | — | 10001000, 1000230f | — | **CONFIRMED** | — | final/RenkAyar.dll.recovery.txt CANDIDATE 100023e8; RenkAyar.dll.100023e8.c |
| `10001000` | RenkAyar.dll | Direct callee of DLL entry 100023e8; purpose not described | `void` | 100023e8 | 10002290 | — | **UNKNOWN** | — | final/RenkAyar.dll.recovery.txt; RenkAyar.dll.10001000.c |
| `1000230f` | RenkAyar.dll | Direct callee of DLL entry 100023e8; purpose not described | `u4 p1,int p2` | 100023e8 | 10002957, 10002995, 10002a75, 10002ac9, 10002afa +7 | — | **UNKNOWN** | — | final/RenkAyar.dll.recovery.txt; RenkAyar.dll.1000230f.c |
| `100022e8` | RenkAyar.dll | MSVC `__ftol` (float→long), called by dl_PaKaGa | `void` | — | — | — | **CONFIRMED** | — (C++ conversions) | RenkAyar.behavior.txt:50-53; RenkAyar.dll.100022e8.c |

## 2. Instruction-site addresses

Addresses inside a function (not entry points) that CONTRACT.txt or native comments cite as exact proof sites.

| Address | Binary | Containing function | Meaning | Confidence | Native | Evidence |
|---|---|---|---|---|---|---|
| `004047a5` | K22.exe | 00404780 STR_CLIP | start of memmove sequence; no nonnegative clamp | **CONFIRMED** | — | CONTRACT §14 (L564) |
| `004047bf` | K22.exe | 00404780 STR_CLIP | end of the same sequence; no negative/suffix branch | **CONFIRMED** | — | CONTRACT §14 (L564) |
| `00413b94` | K22.exe | 00413990 startup | EBX=2 established for macro registration | **CONFIRMED** | — | CONTRACT §6 (L270) |
| `00413b99` | K22.exe | 00413990 startup | ESI=512 established | **CONFIRMED** | — | CONTRACT §6 (L271) |
| `00413ec1` | K22.exe | 00413990 startup | joining path re-establishes EBX=2 | **CONFIRMED** | — | CONTRACT §6 (L271) |
| `00413ec6` | K22.exe | 00413990 startup | joining path re-establishes ESI=512 | **CONFIRMED** | — | CONTRACT §6 (L271) |
| `00413fc5` | K22.exe | 00413990 startup | first of 79 numeric macro registrations | **CONFIRMED** | engine.cpp `nativeConstants[]` | CONTRACT §6 (L267); runtime/numeric-constants.json |
| `00414437` | K22.exe | 00413990 startup | last of 79 numeric macro registrations | **CONFIRMED** | engine.cpp `nativeConstants[]` | CONTRACT §6 (L267) |
| `00413eaf` | K22.exe | 00413990 startup | initializes power-of-two skin-size helper data used by 00426aa0 | **CONFIRMED** | assets.cpp `mdl_texture_extent` | CONTRACT §25 (L834) |
| `00419c30` | K22.exe | 00419c20 WINDOW handler | end of the two-instruction discard body | **CONFIRMED** | — | CONTRACT §15 (L575) |
| `004120ec` | K22.exe | 00412080 wakeup | rereads FREEZE_MODE slot 00665874 per record | **CONFIRMED** | wdl.cpp `Runtime::Impl::tick` (cites 004120ec) | runtime/K22.exe.recovery.txt CANDIDATE 00412080 (REASON data 00665874 via 004120ec) |
| `00431546` | K22.exe | dynamic entity birth (containing function not exported) | sets event_flags bit1 before optional action (..00431550) | **CONFIRMED** | engine.cpp `Engine::create_entity` (cites 00431546–00431550) | CONTRACT §27 (L1144) |
| `004315f6` | K22.exe | entity creation (containing function not exported) | consumes internal filename at +73 | **CONFIRMED** | assets.cpp `Assets::decode_world` / engine.cpp `Engine::load_level` | CONTRACT §24 (L824) |
| `0043186e` | K22.exe | entity creation (containing function not exported) | checks internal action +106; copies <=19 chars + NUL | **CONFIRMED** | engine.cpp `Engine::load_level` | CONTRACT §24 (L824-825) |
| `0043a77a` | K22.exe | allocator | REP STOSD zero-fill proves untouched action slot is zero | **CONFIRMED** | — | CONTRACT §24 (L820) |
| `00458b3b` | K22.exe | model/bitmap storage | explicit CPU-format write: MDL7 formats 6/7 → CPU4 | **CONFIRMED** | media.cpp `image_depth` [HYPOTHESIS mapping] | CONTRACT §20 (L750); runtime/bitmap-cpu-storage/contract.txt |
| `0045a15a` | K22.exe | 00459eb0 WMB object parser | type3 entity branch | **CONFIRMED** | assets.cpp `Assets::decode_world` | CONTRACT §24 (L813) |
| `0045a17d` | K22.exe | 00459eb0 | tests byte72 (long-filename flag) | **CONFIRMED** | assets.cpp `Assets::decode_world` (`record[72]`) | CONTRACT §24 (L815) |
| `0045a1cc` | K22.exe | 00459eb0 | calls 0045ccf0(src+60, capacity 33) | **CONFIRMED** | assets.cpp `Assets::decode_world` | CONTRACT §24 (L816) |
| `0045a358` | K22.exe | 00459eb0 | type7 verbatim 372-byte copy | **CONFIRMED** | assets.cpp `Assets::decode_world` | CONTRACT §24 (L823) |
| `0045a696` | K22.exe | 00459eb0 | type5: start of SUN_ANGLE pan/tilt assignment (float ×1024) under 007192e0 | **CONFIRMED** | engine.cpp `Engine::load_level` (sun_angle from world sunAzimuth/sunElevation) | CONTRACT §19 (L728-729) |
| `0045a6f2` | K22.exe | 00459eb0 | end of SUN_ANGLE assignment; third word unchanged | **CONFIRMED** | engine.cpp `Engine::load_level` | CONTRACT §19 (L728) |
| `10036e9a` | acknex.dll | 10036e70 effect emitter | NULL direction test (..10036eae) | **CONFIRMED** | engine.cpp `Engine::world_builtin` (cites 10036e9a..10036eae) | CONTRACT §32 (L1320) |
| `1004dd52` | acknex.dll | particle birth (containing function not exported) | copies supplied non-NULL XYZ to velocity (..1004dd61) | **CONFIRMED** | native/check_engine.cpp regression comment | CONTRACT §32 (L1321) |

## 3. Globals / data addresses

| Address | Binary | Meaning | Confidence | Native counterpart | Evidence |
|---|---|---|---|---|---|
| `00400000` | K22.exe | Image base | **CONFIRMED** | — | K22.exe.recovery.txt:8 (IMAGE_BASE) |
| `005d060c` | K22.exe | String "EXECUTE" (statement compiler xref 00403807) | **CONFIRMED** | — | RECOVERY.txt:72 |
| `005d08c8` | K22.exe | double 1024.0 (literal scaling) | **CONFIRMED** | value.hpp `Value::from_double` | CONTRACT §5 (L209) |
| `005d1014` | K22.exe | Name string pointer of WAITT descriptor | **CONFIRMED** | — | CONTRACT §1 (L21) |
| `005d101c` | K22.exe | Name string pointer of WAIT descriptor | **CONFIRMED** | — | CONTRACT §1 (L20) |
| `005d143c` | K22.exe | Name string pointer of proc_kill descriptor | **CONFIRMED** | — | CONTRACT §1 (L22) |
| `005d1d2c` | K22.exe | float32 1024.0 (clock, _vec) | **CONFIRMED** | — | CONTRACT §3 (L108), §18 (L677) |
| `005d2808` | K22.exe | Name pointer for presence macro WMB | **CONFIRMED** | — | CONTRACT §9 (L359) |
| `005d2910` | K22.exe | double 1000.0 (clock seconds→ms) | **CONFIRMED** | — | CONTRACT §3 (L108) |
| `005d3348` | K22.exe | Embedded engine C-Script text (engine_internal.wdl) | **CONFIRMED** | — | RECOVERY.txt:68 |
| `005d4728` | K22.exe | String "RESOURCE" | **CONFIRMED** | — | RECOVERY.txt:40 |
| `0061b000` | K22.exe | .data section VA (raw 0x21000, virtual 0x118938) | **CONFIRMED** | — | CONTRACT §12 (L465-466); PE header |
| `0061b108` | K22.exe | Input name/index table (89 rows, stride 8) | **CONFIRMED** | engine.cpp `nativeKeys[]` | CONTRACT §12 (L427); runtime/input-contract.json |
| `0061b3d0` | K22.exe | Input table sentinel (two zero words) | **CONFIRMED** | — | CONTRACT §12 (L428) |
| `0061b3d8` | K22.exe | Nine-word legacy command table A | **CONFIRMED** | — | CONTRACT §1 (L24); RECOVERY.txt:72 |
| `0061b468` | K22.exe | Legacy REMOVE command descriptor (one operand) | **HIGH CONFIDENCE** | wdl.cpp `Runtime::Impl::statement` comment | native/wdl.cpp comment only |
| `0061c298` | K22.exe | Nine-word legacy command table B | **CONFIRMED** | — | CONTRACT §1 (L24) |
| `0061c408` | K22.exe | Start of 317-row native API descriptor table (16 B: name, handler, argc, arg types); row0 `_str` | **CONFIRMED** | native_calls.inc | CONTRACT §1 (L18); runtime/api-descriptors.json |
| `0061c418` | K22.exe | Descriptor `_vec` → 00407040 | **CONFIRMED** | — | runtime/api-descriptors.json |
| `0061cc78` | K22.exe | Descriptor `integer` → 00403d60 | **CONFIRMED** | — | runtime/api-descriptors.json |
| `0061cc88` | K22.exe | Descriptor `INT` → 00403d60 | **CONFIRMED** | — | runtime/api-descriptors.json |
| `0061cf98` | K22.exe | Descriptor `proc_kill` → 00404cd0 | **CONFIRMED** | — | CONTRACT §1 (L22) |
| `0061d0e8` | K22.exe | Descriptor `SLEEP` → 00404ca0 | **CONFIRMED** | — | CONTRACT §1 (L23) |
| `0061d4e8` | K22.exe | Descriptor `vector` → 00403eb0 (argc3, enc 0x15) | **CONFIRMED** | — | CONTRACT §18 (L663) |
| `0061d538` | K22.exe | Descriptor `WAIT` → 00404b60 | **CONFIRMED** | — | CONTRACT §1 (L20) |
| `0061d548` | K22.exe | Descriptor `WAITT` → 00404be0 | **CONFIRMED** | — | CONTRACT §1 (L21) |
| `0061d7d8` | K22.exe | API descriptor table sentinel | **CONFIRMED** | native_calls.inc | CONTRACT §1 (L18) |
| `0061d7e8` | K22.exe | VECTOR/_vec ring cursor | **CONFIRMED** | engine.hpp `vectorTempCursor_` | CONTRACT §18 (L666) |
| `0061d7f0` | K22.exe | Operator descriptor table start (+,-,/,*,<,>,<=,>=,==,!=,&&,\|\|) | **CONFIRMED** | wdl.cpp `precedence` | CONTRACT §5 (L177) |
| `0061da0c` | K22.exe | Operator descriptor table end | **CONFIRMED** | — | CONTRACT §5 (L177) |
| `0061dda8` | K22.exe | String "A6 engine" | **CONFIRMED** | — | RECOVERY.txt:33 |
| `0061ddb4` | K22.exe | String "6.31.4" (engine version) | **CONFIRMED** | — | RECOVERY.txt:33; CONTRACT header (L5) |
| `0061ec80` | K22.exe | Global registry start (330 rows, stride 24); row0 VERSION | **CONFIRMED** | engine.cpp `nativeDefaults[]` | CONTRACT §6 (L228); runtime/global-registry.json |
| `0061ed40` | K22.exe | FREEZE_MODE descriptor | **CONFIRMED** | engine.cpp `nativeDefaults[]` | CONTRACT §3 (L125) |
| `0061ee60` | K22.exe | TIME descriptor | **CONFIRMED** | — | CONTRACT §3 (L103) |
| `0061ee78` | K22.exe | TIME_FACTOR descriptor | **CONFIRMED** | — | CONTRACT §3 (L104) |
| `0061ee90` | K22.exe | TIME_SMOOTH descriptor | **CONFIRMED** | — | CONTRACT §3 (L105) |
| `0061eea8` | K22.exe | TIME_FRAME descriptor | **CONFIRMED** | — | CONTRACT §3 (L104) |
| `00620b70` | K22.exe | Global registry end | **CONFIRMED** | — | CONTRACT §6 (L229) |
| `00620c78` | K22.exe | delimit_str engine STRING | **CONFIRMED** | engine.cpp `Engine::initialize_globals` | CONTRACT §14 (L531) |
| `0063b9b4` | K22.exe | Function descriptor list head | **CONFIRMED** | wdl.cpp `Runtime::Impl::functions` | CONTRACT §13 (L491) |
| `0063ba04` | K22.exe | ON_ANYKEY callback cell | **CONFIRMED** | engine.cpp `Engine::update_input` | CONTRACT §12 (L456) |
| `0063bc30` | K22.exe | TXT_LOAD shared buffer (limit 4095) | **CONFIRMED** | — | CONTRACT §14 (L527) |
| `0063c000` | K22.exe | Start of loader-zero-filled .data tail | **CONFIRMED** | — | CONTRACT §12 (L466) |
| `0063ce30` | K22.exe | VECTOR ring storage start (64×12 B to 0063d130); initial cursor | **CONFIRMED** | engine.hpp `vectorTemps_` | CONTRACT §18 (L667-672) |
| `0063ce3c` | K22.exe | First returned VECTOR slot | **CONFIRMED** | — | CONTRACT §18 (L671) |
| `0063d124` | K22.exe | Last VECTOR slot (wraps) | **CONFIRMED** | — | CONTRACT §18 (L672) |
| `0063d130` | K22.exe | VECTOR ring end (exclusive) | **CONFIRMED** | — | CONTRACT §18 (L666) |
| `006438b0` | K22.exe | A6 engine-interface base passed to plugins | **CONFIRMED** | — | CONTRACT §2 (L66), §17 (L653) |
| `006441d8` | K22.exe | `my` storage (interface+a34) | **CONFIRMED** | — | CONTRACT §2 (L67) |
| `006441dc` | K22.exe | `you` storage (interface+a38) | **CONFIRMED** | — | CONTRACT §2 (L67) |
| `006441f0` | K22.exe | Terrain skin state consumed by terrain draw | **HIGH CONFIDENCE** | — | CONTRACT §26 (L861) |
| `00644380` | K22.exe | A5 interface passed to A5_DLLAPI | **CONFIRMED** | — | CONTRACT §17 (L655) |
| `006445b4` | K22.exe | Compiled-expression return value (first-yield result) | **CONFIRMED** | wdl.cpp `Process::result` | CONTRACT §10 (L390) |
| `006446e8` | K22.exe | Interpreter control: bit1 yield, bit2 suppress continuation insertion | **CONFIRMED** | wdl.cpp `suppressContinuations`, `Process::yielded` | CONTRACT §1, §2, §4, §10 |
| `00644730` | K22.exe | Plugin module handle array start (64 entries to 0064482c) | **CONFIRMED** | — | CONTRACT §17 (L635) |
| `0064482c` | K22.exe | Plugin module handle array end | **CONFIRMED** | — | CONTRACT §17 (L635) |
| `00644850` | K22.exe | SERVER (&1) / CLIENT (&2) presence bits | **CONFIRMED** | — | CONTRACT §9 (L358) |
| `00651ce0` | K22.exe | Current script name | **CONFIRMED** | — | RECOVERY.txt:67 |
| `0065f1e4` | K22.exe | Private zero DWORD shared by numeric header binder 004174f0 | **HIGH CONFIDENCE** | wdl.cpp `numeric_binding` (comment) | native/value.hpp, native/wdl.cpp comments only |
| `00661330` | K22.exe | Specialized declaration handler slot | **CONFIRMED** | — | CONTRACT §15 (L573) |
| `0066133c` | K22.exe | Compiler parse state (WINDOW sets 2) | **CONFIRMED** | — | CONTRACT §15 (L573) |
| `00663398` | K22.exe | Raw parsed initializer value | **CONFIRMED** | — | CONTRACT §10 (L368) |
| `006633a8` | K22.exe | Pointer target cleared by WINDOW handler | **CONFIRMED** | — | CONTRACT §15 (L575) |
| `006633c0` | K22.exe | Conditional-suppression bits (&2 include/PLUGINDIR, &0x200 body) | **CONFIRMED** | — | CONTRACT §9, §17 |
| `0066573c` | K22.exe | _arg1 pointer slot | **CONFIRMED** | engine.cpp `Engine::update_input` (`_arg1`) | CONTRACT §12 (L455) |
| `0066578c` | K22.exe | TIME_FRAME pointer slot | **CONFIRMED** | — | CONTRACT §3 (L104) |
| `00665830` | K22.exe | TIME_FACTOR pointer slot | **CONFIRMED** | — | CONTRACT §3 (L104) |
| `00665874` | K22.exe | FREEZE_MODE pointer slot | **CONFIRMED** | wdl.cpp `Runtime::Impl::tick` (freeze_mode) | CONTRACT §3 (L125) |
| `00665930` | K22.exe | JOY_1 state (..0066595c JOY_12) | **CONFIRMED** | engine.cpp `Engine::service_builtin` key_pressed joy names | CONTRACT §12 (L435) |
| `0066595c` | K22.exe | JOY_12 state | **CONFIRMED** | — | CONTRACT §12 (L435) |
| `0066597c` | K22.exe | mouse LEFT state | **CONFIRMED** | — | CONTRACT §12 (L437) |
| `00665980` | K22.exe | mouse RIGHT state | **CONFIRMED** | — | CONTRACT §12 (L437) |
| `00665984` | K22.exe | mouse MIDDLE state | **CONFIRMED** | — | CONTRACT §12 (L437) |
| `006659a8` | K22.exe | Keyboard state array (+4*code) | **CONFIRMED** | engine.cpp `Engine::key_state` | CONTRACT §12 (L434) |
| `00665c1c` | K22.exe | SUN_LIGHT pointer slot | **CONFIRMED** | — | CONTRACT §19 (L733) |
| `00665c50` | K22.exe | SUN_COLOR pointer slot (zero xrefs) | **CONFIRMED** | — | CONTRACT §19 (L732) |
| `00665c70` | K22.exe | JOY2_1 state (..00665c9c) | **CONFIRMED** | — | CONTRACT §12 (L436) |
| `00665c9c` | K22.exe | JOY2_12 state | **CONFIRMED** | — | CONTRACT §12 (L436) |
| `00665cf4` | K22.exe | TIME pointer slot | **CONFIRMED** | wdl.cpp `Runtime::Impl::evaluate` (time) | CONTRACT §3 (L84, L103) |
| `00665d94` | K22.exe | TIME_SMOOTH pointer slot | **CONFIRMED** | — | CONTRACT §3 (L105) |
| `00665ea8` | K22.exe | WRS entry table (0x30-byte entries) | **CONFIRMED** | assets.cpp `Assets::entries_` | RECOVERY.txt:54 |
| `00695ea8` | K22.exe | Cached-memory flag from shared loader | **CONFIRMED** | — | RECOVERY.txt:67 |
| `00695eb0` | K22.exe | File memory cache (stride 0x24) | **CONFIRMED** | — | RECOVERY.txt:61 |
| `006bafb8` | K22.exe | Archive FILE* table | **CONFIRMED** | — | RECOVERY.txt:44 |
| `006bb1b8` | K22.exe | WRS entry count (limit 0xfff) | **CONFIRMED** | — | RECOVERY.txt:56 |
| `006bb1bc` | K22.exe | Archive count (limit 0x7f) | **CONFIRMED** | — | RECOVERY.txt:45 |
| `006bb1c4` | K22.exe | Local pool list head | **CONFIRMED** | — | CONTRACT §11 (L406) |
| `006c4fb8` | K22.exe | Ordinary D3D palette | **CONFIRMED** | — | CONTRACT §40b (L2202) |
| `006c53b8` | K22.exe | Mono palette | **CONFIRMED** | — | CONTRACT §40b (L2202) |
| `006c6dec` | K22.exe | WMB presence-macro bit (&4) | **CONFIRMED** | — | CONTRACT §9 (L359) |
| `006c7488` | K22.exe | PLUGINDIR buffer (<=126 chars + NUL) | **CONFIRMED** | wdl.hpp `Runtime::CompilerSettings` (plugin_directory) | CONTRACT §17 (L627) |
| `007192bc` | K22.exe | Active RGB palette used by FONT upload | **CONFIRMED** | engine.cpp `Engine::ensure_font_palette` | CONTRACT §40b (L2195) |
| `007192e0` | K22.exe | WMB load-mode predicate for SUN_ANGLE assignment | **CONFIRMED** | — | CONTRACT §19 (L729) |
| `10184014` | kabus_dll.dll | Escape pair table (..1018405b) | **CONFIRMED** | — | RECOVERY.txt:108 |
| `101a1694` | kabus_dll.dll | Discord RPC instance pointer | **CONFIRMED** | engine.cpp `discordSocket` | RECOVERY.txt:98 |
| `101a1698` | kabus_dll.dll | INI singleton pointer | **CONFIRMED** | engine.cpp `iniInitialized` | RECOVERY.txt:103 |
| `101a16f0` | kabus_dll.dll | Bound proc_kill pointer | **CONFIRMED** | — | RECOVERY.txt:82 |
| `101a1704` | kabus_dll.dll | Bound proc_status pointer (NULL on 6.31.4) | **CONFIRMED** | — | RECOVERY.txt:83; CONTRACT §4 (L164-170) |
| `100070c0` | RenkAyar.dll | double constants 0.0/255.0/128.0/1/25600/0.01 (..100070e0) | **CONFIRMED** | engine.cpp `Engine::service_builtin` dl_pakaga | RenkAyar.behavior.txt:59-64 |
| `10009f30` | RenkAyar.dll | Bound draw_begin pointer | **CONFIRMED** | — | RenkAyar.behavior.txt:15 |
| `10187124` | acknex.dll | float 0.0 (absent MDL7 U/V) | **CONFIRMED** | assets.cpp `Assets::decode_model` | CONTRACT §30 (L1254) |
| `101a8200` | acknex.dll | A7 native descriptor table start (394 rows, 392 names) | **CONFIRMED** | native_calls.inc (Bonus list) | CONTRACT §41 (L1641); runtime/bonus-physics/api-descriptors.json |
| `101a9aa0` | acknex.dll | A7 descriptor table sentinel | **CONFIRMED** | native_calls.inc | runtime/bonus-physics/api-descriptors.json |
| `101f64b0` | acknex.dll | PLAYER_NAME 32-byte loader-zero buffer | **CONFIRMED** | engine.cpp `Engine::initialize_globals` | CONTRACT §28 (L1172) |

Full per-row tables: `runtime/api-descriptors.json` (317 A6 natives), `runtime/global-registry.json` (330 globals), `runtime/member-descriptors.json`, `runtime/input-contract.json`, `runtime/numeric-constants.json`, `runtime/bonus-physics/api-descriptors.json` (394 A7 rows). See [DATA_STRUCTURES](DATA_STRUCTURES.md).

## 4. Addresses cited only by native source comments

These addresses appear in `native/` comments but neither in CONTRACT.txt nor as a decompiled export filename. The claim is the port author's; where an evidence file under `reverse/decompiled/` mentions the address it is listed, otherwise the label is **HYPOTHESIS** (comment-only). False positives excluded: `10240000` (MAX_PARTICLES value, engine.cpp `nativeDefaults[]`); `00400000` as a flag mask in engine.cpp `nativeMasks[]` (also the image base, §3).

| Address | Binary (inferred) | Native location | Native comment (excerpt) | Confidence | Other evidence |
|---|---|---|---|---|---|
| `00425f20` | K22.exe | engine.cpp `Engine::set_font_palette` | Original004550e0/00425f20: normalized RGB triples, not index-as-alpha. | **HYPOTHESIS** | — (native comment only) |
| `00431550` | K22.exe | engine.cpp `Engine::create_entity` | 00431546–00431550: every dynamic birth, before optional action. | **HYPOTHESIS** | — (native comment only) |
| `00431976` | K22.exe | wdl.cpp `Runtime::kill_for_entity` | A6 00431976/00431986: registered pointer NULL follows cancellation, before retirement. | **HYPOTHESIS** | — (native comment only) |
| `00431986` | K22.exe | wdl.cpp `Runtime::kill_for_entity` | A6 00431976/00431986: registered pointer NULL follows cancellation, before retirement. | **HIGH CONFIDENCE** | runtime/entity-cancel/pointer-cleanup/primary/K22.exe.0041f050.refs.txt |
| `00439150` | K22.exe | check_engine.cpp `push_nested` | candidate clears the other, so 00439150/00439240 keeps the UNNORMALIZED crease. | **HYPOTHESIS** | — (native comment only) |
| `00447918` | K22.exe | engine.cpp `ui_order` | A6 00447918/0044a869 and A7 10049da0 wrap next against the selected resource. | **HYPOTHESIS** | — (native comment only) |
| `0044a869` | K22.exe | engine.cpp `ui_order` | A6 00447918/0044a869 and A7 10049da0 wrap next against the selected resource. | **HYPOTHESIS** | — (native comment only) |
| `004514b0` | K22.exe | check_engine.cpp `cast` (+1) | 004514b0 epsilon=1/32, not an ellipsoid touching the visual triangles. | **HIGH CONFIDENCE** | runtime/material-renderer/a6-albedo/K22.exe.005d1d8c.refs.txt |
| `00452a45` | K22.exe | check_engine.cpp `cast` | actor() leaves NARROW/FAT clear: USE_BOX8192 still selects POINT (00452a45..58). | **HYPOTHESIS** | — (native comment only) |
| `004537a0` | K22.exe | engine.cpp `Engine::move_a6_legacy` | 004537a0: unclamped MOVE_FRICTION, 1.01 overclip, strict .2*(1-friction) deadband. | **HIGH CONFIDENCE** | runtime/graphics-settings-source/a6-presentation/K22.exe.005d1d2c.refs.txt |
| `00453b9b` | K22.exe | engine.cpp `Engine::move_a6_legacy` | 00453b9b retries without consuming one of the six bumps. | **HYPOTHESIS** | — (native comment only) |
| `00453bbe` | K22.exe | check_engine.cpp `cast` | Its PASSABLE contact sets MOVE.IN_PASSABLE from status0x40 (00453bbe), unlike TRACE. | **HYPOTHESIS** | — (native comment only) |
| `00453eb9` | K22.exe | engine.cpp `Engine::move_a6_legacy` | 00453eb9 VIEW tag0x63, not USE_BOX. | **HYPOTHESIS** | — (native comment only) |
| `004544a1` | K22.exe | check_engine.cpp `physicsRuntime` | 004544a1–004544aa: a non-PASSABLE zero move only dispatches triggers. | **HYPOTHESIS** | — (native comment only) |
| `004544aa` | K22.exe | check_engine.cpp `physicsRuntime` | 004544a1–004544aa: a non-PASSABLE zero move only dispatches triggers. | **HYPOTHESIS** | — (native comment only) |
| `0045491c` | K22.exe | engine.cpp `Engine::publish_hit` | 0045491c returns before NORMAL/status/owner publication. | **HIGH CONFIDENCE** | runtime/bonus-core/entity-light-cache/a6/query-detail/zero-distance-contract.txt |
| `00454e8e` | K22.exe | check_engine.cpp `cast` | MDL public normal is the normalized start-to-owner XY vector (00454e8e), | **HYPOTHESIS** | — (native comment only) |
| `00456d10` | K22.exe | assets.cpp `Assets::decode_world` | if(!bonusProfile_ && !sections[14].empty()) { | **HIGH CONFIDENCE** | runtime/sky-normalize/runtime-manifest.txt |
| `0045af50` | K22.exe | assets.cpp `Assets::decode_world` | malformed("invalid WMB6 BSP node");result.bspNodes.push_back(node); | **HIGH CONFIDENCE** | runtime/entity-cancel/pointer-cleanup/bulk-pool/K22.exe.0043a700.refs.txt |
| `0045b470` | K22.exe | check_engine.cpp `cast` | 0045b470..49d: non-level resource loads do not publish HULL globals. | **HYPOTHESIS** | — (native comment only) |
| `00467530` | K22.exe | engine.cpp `wPublicHullRay` | A6 00467530 / A7 1006d370: float length, normalized components and strict snapping. | **HIGH CONFIDENCE** | runtime/entity-ambient-source/runtime-manifest.txt |
| `0046f45a` | K22.exe | engine.cpp `Engine::move_a6_modern` (+1) | 0046f45a: snapping must not shorten distance-based travel. | **HYPOTHESIS** | — (native comment only) |
| `00472072` | K22.exe | check_engine.cpp `measure` | A6 00472072..0047209d: USE_BOX + MY enables the hull query. | **HYPOTHESIS** | — (native comment only) |
| `0047209d` | K22.exe | check_engine.cpp `measure` | A6 00472072..0047209d: USE_BOX + MY enables the hull query. | **HYPOTHESIS** | — (native comment only) |
| `004728d4` | K22.exe | engine.cpp `Engine::collide` | A6 modern 004728d4 filters global WMB; A7 10067cc0 bypasses entity type masks for it. | **HIGH CONFIDENCE** | runtime/bonus-core/entity-light-cache/a6/query-detail/contract.txt |
| `00585f60` | K22.exe | modern_box.hpp `k22::modern_box` (header comment) | A6 00585f60 / A7 100b2680's USE_BOX mesh primitive is an oriented, | **HYPOTHESIS** | — (native comment only) |
| `0059f5b0` | K22.exe | modern_box.hpp `worldNormal` | 0059f5b0/100d01e0 uses the cofactor products, not R*n or SCALE. | **HIGH CONFIDENCE** | runtime/material-renderer/a6-albedo/K22.exe.005d1d8c.refs.txt |
| `0059f7b0` | K22.exe | modern_box.hpp `inside` | 0059f7b0/100d03e0 tests float sign bits; the opposite edge is strict. | **HYPOTHESIS** | — (native comment only) |
| `0059f8d0` | K22.exe | modern_box.hpp `root` | 0059f8d0 / 100d0500. Zero A and strict -.01/limit comparisons are | **HYPOTHESIS** | — (native comment only) |
| `0059f9a0` | K22.exe | modern_box.hpp `k22::modern_box` (header comment) | 0059f9a0 and 100d0890 moving feature rules, not the point query. | **HIGH CONFIDENCE** | runtime/material-renderer/a6-albedo/K22.exe.005d1d8c.refs.txt |
| `005a0b00` | K22.exe | modern_box.hpp `Query` | 005a0b00/100d1690: inverse rigid orientation, then reciprocal half | **HIGH CONFIDENCE** | runtime/material-renderer/a6-albedo/K22.exe.005d1d8c.refs.txt |
| `10019ca0` | acknex.dll | value.hpp `State::create` | A6 MATERIAL declarations 004159d0 call 00415390; Bonus 10019d20/10019ca0 | **HYPOTHESIS** | — (native comment only) |
| `10019d20` | acknex.dll | value.hpp `State::create` | A6 MATERIAL declarations 004159d0 call 00415390; Bonus 10019d20/10019ca0 | **HYPOTHESIS** | — (native comment only) |
| `10025d72` | acknex.dll | engine.cpp `Engine::initialize_globals` | A7 10025d72 constructs a live empty offline STRING borrowing static bytes (flags0x24). | **HYPOTHESIS** | — (native comment only) |
| `1002ca3c` | acknex.dll | media.cpp `std::memcmp` | decoded-texture policy follows 1002ca3c: all other stored DDS formats | **HYPOTHESIS** | — (native comment only) |
| `1002cd4d` | acknex.dll | assets.cpp `Assets::decode_world` | A7 10059e90: +24 is the format, +40 starts packed pixels. | **HYPOTHESIS** | — (native comment only) |
| `10036eae` | acknex.dll | engine.cpp `Engine::world_builtin` (+1) | A7 10036e9a..10036eae replaces only a NULL direction pointer with three zero words. | **HYPOTHESIS** | — (native comment only) |
| `10039598` | acknex.dll | engine.cpp `Engine::create_entity` | 10039598 EMASK60\|1, before initial mode3/construction. | **HYPOTHESIS** | — (native comment only) |
| `100399fa` | acknex.dll | engine.cpp `std::nth_element` | WDL startup sets101f59f0\|1; 100399fa admits this serialized action. | **HYPOTHESIS** | — (native comment only) |
| `1003c481` | acknex.dll | engine.cpp `worldModelBuiltin` | A7 1003c481–52f stores the percent fraction and absolute position as float32. | **HYPOTHESIS** | — (native comment only) |
| `1003c533` | acknex.dll | engine.cpp `worldModelBuiltin` | 1003c533–566 truncates ANM_SKIP only after deciding the cycle's next_frame. | **HYPOTHESIS** | — (native comment only) |
| `10049da0` | acknex.dll | engine.cpp `ui_order` | A6 00447918/0044a869 and A7 10049da0 wrap next against the selected resource. | **HYPOTHESIS** | — (native comment only) |
| `1004dd61` | acknex.dll | check_engine.cpp `reload_bonus_null_effect` | 1004dd52..1004dd61 copies a supplied vector unchanged, including a zero first component. | **HYPOTHESIS** | — (native comment only) |
| `100597e7` | acknex.dll | assets.cpp `mdl_packed` | return v; | **HIGH CONFIDENCE** | runtime/bonus-asset-census/mdl7-reader/padding/survey.txt |
| `1005af0a` | acknex.dll | engine.hpp `frame` | 1005af0a latches global-WMB info bit4; not a network/CONNECTION flag. | **HYPOTHESIS** | — (native comment only) |
| `1005b47c` | acknex.dll | assets.cpp `Assets::decode_world` (+1) | mapping.baseFlags=materialCursor.u16();uint32_t userFlags=materialCursor.u32(); | **HIGH CONFIDENCE** | runtime/bonus-physics/world-contact-boundary/material-flags/acknex.dll.manifest.txt |
| `10067000` | acknex.dll | physics.cpp `Physics::step` | 100665c0 divides by 1000 before 10067000's near-zero test, then multiplies back. | **HIGH CONFIDENCE** | runtime/bonus-physics/contact-reduction/exact/acknex.dll.10066f60.refs.txt |
| `1006df5e` | acknex.dll | engine.cpp `Engine::construct_query_actor` | if(!data.model\|\|!data.type)throw std::runtime_error("A7 1006e1e0 rebuild to an empty resource has no concrete fronten… | **HYPOTHESIS** | — (native comment only) |
| `1006fef8` | acknex.dll | engine.cpp `Engine::collide` | 100704a0 admission uses route0 count; 1006fef8 binds AUX payload onto PRIMARY. | **HYPOTHESIS** | — (native comment only) |
| `100757e0` | acknex.dll | engine.cpp `Engine::move_a7_modern` (+1) | 10075e80/10075bb0/100757e0, separately qualified from both A6 movers. | **HIGH CONFIDENCE** | runtime/bonus-core/animation/pose/numeric-evidence.txt |
| `1007584e` | acknex.dll | engine.cpp `Engine::move_a7_modern` | Original 1007584e/10075863 skips all three local stores here. Keep | **HYPOTHESIS** | — (native comment only) |
| `10075863` | acknex.dll | engine.cpp `Engine::move_a7_modern` | Original 1007584e/10075863 skips all three local stores here. Keep | **HYPOTHESIS** | — (native comment only) |
| `1007631b` | acknex.dll | engine.cpp `Engine::world_builtin` | if(bonusModern){if(!ownPass)set_position(id,storedDestination);auto actor=queryActors_.find(id);if(actor==queryActors… | **HYPOTHESIS** | — (native comment only) |
| `1007bcc8` | acknex.dll | physics.cpp `Physics::collide` | 1007bcc8/1007bce5 tests geometry flag9 only for the no-body ENTITY partner. | **HYPOTHESIS** | — (native comment only) |
| `1007bce5` | acknex.dll | physics.cpp `Physics::collide` | 1007bcc8/1007bce5 tests geometry flag9 only for the no-body ENTITY partner. | **HYPOTHESIS** | — (native comment only) |
| `1007fbf0` | acknex.dll | engine.cpp `wHullSupportPoint` | 1007fbf0's original dispatch is shifted; IDs9/10 intentionally share A0/B2. | **HYPOTHESIS** | — (native comment only) |
| `100d01e0` | acknex.dll | modern_box.hpp `worldNormal` | 0059f5b0/100d01e0 uses the cofactor products, not R*n or SCALE. | **HIGH CONFIDENCE** | runtime/bonus-core/animation/pose/numeric-evidence.txt |
| `100d03e0` | acknex.dll | modern_box.hpp `inside` | 0059f7b0/100d03e0 tests float sign bits; the opposite edge is strict. | **HYPOTHESIS** | — (native comment only) |
| `100d0500` | acknex.dll | modern_box.hpp `root` | 0059f8d0 / 100d0500. Zero A and strict -.01/limit comparisons are | **HIGH CONFIDENCE** | runtime/bonus-physics/contact-reduction/exact/acknex.dll.101672ec.refs.txt |
| `100d0890` | acknex.dll | modern_box.hpp `k22::modern_box` (header comment) | 0059f9a0 and 100d0890 moving feature rules, not the point query. | **HIGH CONFIDENCE** | runtime/bonus-core/animation/pose/numeric-evidence.txt |
| `100d1160` | acknex.dll | modern_box.hpp `triangle` | 100d1160 keeps the pre-transform vertex pointers and at | **HYPOTHESIS** | — (native comment only) |
| `100d12a1` | acknex.dll | modern_box.hpp `triangle` | 100d12a1..12c8 publishes their face normal directly for negative | **HYPOTHESIS** | — (native comment only) |
| `100d1690` | acknex.dll | modern_box.hpp `Query` | 005a0b00/100d1690: inverse rigid orientation, then reciprocal half | **HIGH CONFIDENCE** | runtime/bonus-core/animation/pose/numeric-evidence.txt |
| `100e4170` | acknex.dll | modern_box.hpp `triangle` | 100e4170 keeps the reciprocal in x87 until the three final | **HIGH CONFIDENCE** | runtime/bonus-core/animation/pose/numeric-evidence.txt |
| `10158a70` | acknex.dll | assets.cpp `Assets::decode_model` | mdl_resize(group.faces,triangles); | **HYPOTHESIS** | — (native comment only) |
| `10158aa3` | acknex.dll | assets.cpp `Assets::decode_model` | mdl_resize(group.faces,triangles); | **HYPOTHESIS** | — (native comment only) |
| `10158ad7` | acknex.dll | assets.cpp `Assets::decode_model` | mdl_resize(group.faces,triangles); | **HYPOTHESIS** | — (native comment only) |
| `1016e258` | acknex.dll | engine.cpp `Engine::collide` | if(actor.type==5\|\|(f&0x04000000u)\|\|(mode&0x80000u)){if(actor.type==1)throw std::runtime_error("A7 Sprite POLYGON priv… | **HYPOTHESIS** | — (native comment only) |
| `101abb80` | acknex.dll | engine.cpp `Engine::initialize_globals` | Bonus row101abb80/public101e7390 defaults100; fixed-device capacity is a separate clamp. | **HIGH CONFIDENCE** | runtime/graphics-settings-source/a7-worldlight-source/lifecycle/slot-registry-identity.txt |
| `101e7390` | acknex.dll | engine.cpp `Engine::initialize_globals` | Bonus row101abb80/public101e7390 defaults100; fixed-device capacity is a separate clamp. | **HIGH CONFIDENCE** | runtime/graphics-settings-source/a7-worldlight-source/lifecycle/slot-registry-identity.txt |
| `101f59f0` | acknex.dll | engine.cpp `std::nth_element` (+1) | WDL startup sets101f59f0\|1; 100399fa admits this serialized action. | **HYPOTHESIS** | — (native comment only) |
| `106bfe64` | acknex.dll | engine.cpp `Engine::world_builtin` | if((!bonusModern\|\|ownPass)&&(mode&4096)&&field(id,"trigger_range")>0&&requestedMovement)throw std::runtime_error("pos… | **HYPOTHESIS** | — (native comment only) |

## 5. Index of remaining decompiled exports

Every decompiled `.c` export not annotated in §1, grouped by binary and by its most specific export directory (directory name = the recovery topic that selected it; the file in parentheses is that directory's contract/manifest). Tags: `API x` = handler of native descriptor `x` (from `runtime/api-descriptors.json` or `runtime/bonus-physics/api-descriptors.json`, **CONFIRMED**); a bare name = Ghidra library/import symbol; `sel:` = selection reason from the `.recovery.txt` CANDIDATE block; `native …` = address cited in that native location; `[+N dir]` = also exported under N other directories. Untagged entries carry only their directory's scope: role **UNKNOWN** beyond that topic.

### K22.exe

- `runtime/abi/` (runtime-manifest.txt): `004016f0`; `00412380`; `00415290` [+4 dir]; `00415320` [+16 dir]; `00419ef0` [+3 dir]; `005b7140` (_strchr) [+3 dir]; `005b71fe` (_atof) [+2 dir]; `005b72bd` (__security_check_cookie) [+12 dir]
- `runtime/bitmap-cpu-storage/` (contract.txt): `00415e20`; `0043a700` [+7 dir]; `00456260`; `00459d50`; `0045bee0`; `0045c290`; `005b86da`; `005b9b90` (_strrchr) [+1 dir]; `005ba257` (_malloc) [+1 dir]
- `runtime/bitmap-model-storage/` (runtime-manifest.txt): `00429520` [+6 dir]; `00429660` [+4 dir]; `0042d940` [+6 dir]; `0042db20` [+3 dir]; `0042dbc0` [+6 dir]; `0042eca0` [+10 dir]; `00458480` [+2 dir]
- `runtime/bitmap-standalone-storage/` (contract.txt): `0045b940`; `0045b980`; `0045ba70`; `0045bf20`; `0045c040`; `0045c520`; `0045cb20`; `0045cc10`
- `runtime/bitmap-storage-api/` (runtime-manifest.txt): `00404fc0` (API bmap_create); `004054b0` (API bmap_load); `0040e270` [+1 dir]; `0040f210` [+7 dir]; `00427220`; `0042a750` [+11 dir]; `0043bb10` [+6 dir]
- `runtime/bonus-core/entity-light-cache/a6/initial/` (manifest.txt): `0042fba0`; `00430680`; `004312b0` [+3 dir]; `004354e0` [+5 dir]; `00435580` (native engine.cpp Engine::update_entity_light); `00454fc0` (native check_engine.cpp masked_trace) [+3 dir]; `004706d0` (sel: data 0066599c, data 00665908; native engine.cpp Engine::move_a6_modern) [+3 dir]; `00472800` (native engine.cpp Engine::collide) [+2 dir]
- `runtime/bonus-core/entity-light-cache/a6/legacy-move/` (contract.txt): `00405800` (API ent_move); `004346c0` [+4 dir]; `00451e80`; `00454310` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a6/legacy-move/normal/` (manifest.txt): `00451cc0`
- `runtime/bonus-core/entity-light-cache/a6/legacy-move/publication/` (manifest.txt): `00451d70` [+1 dir]; `00453680`; `00453940` (native engine.cpp Engine::move_a6_legacy)
- `runtime/bonus-core/entity-light-cache/a6/morph/` (contract.txt): `00434cc0` [+2 dir]; `00435f60`; `00436040` [+4 dir]; `00469940`
- `runtime/bonus-core/entity-light-cache/a6/public-create/` (contract.txt): `00405670` (API ent_create); `004056b0` (API ent_createlocal)
- `runtime/bonus-core/entity-light-cache/a6/public-create/birth-chain/` (manifest.txt): `00430760` [+3 dir]; `00430800` [+1 dir]; `00430890` [+3 dir]; `00430920` [+3 dir]; `00434850` [+2 dir]
- `runtime/bonus-core/entity-light-cache/a6/public-create/dynamic/` (manifest.txt): `00431b50` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a6/public-create/initialization/` (manifest.txt): `00431510`
- `runtime/bonus-core/entity-light-cache/a6/public-query/` (contract.txt): `004057e0` (API trace) [+2 dir]; `004061d0` (API c_move); `00406720` (API c_trace/PH_TRACE; thunk_FUN_00472a60) [+1 dir]; `00406750`; `0046fa80` (native check_engine.cpp physicsRuntime)
- `runtime/bonus-core/entity-light-cache/a6/public-query/move/` (manifest.txt): `00460ef0`; `0046f010` [+1 dir]; `0046f730` (native engine.cpp Engine::move_a6_legacy); `004706c0`
- `runtime/bonus-core/entity-light-cache/a6/public-query/move-bridge/` (manifest.txt): `0046f030`; `0046f3b0` (native engine.cpp Engine::move_a6_modern)
- `runtime/bonus-core/entity-light-cache/a6/query-detail/` (contract.txt): `00437080` [+1 dir]; `00437b90` (sel: data 0066599c, data 00665908; native engine.cpp Engine::prepare_entity_light) [+5 dir]; `004546f0` (sel: data 0066599c, data 00665908; native check_engine.cpp cast) [+2 dir]; `00462df0`; `004630c0`; `004650c0`; `00465130`; `004651c0` [+1 dir]; `00465240`; `00465840`; `00469de0`; `00469fd0`; `0046a0c0`; `0046ab70`; `0046dcd0`; `0046dde0`; `0046e630`; `00470d30`; `00470e90`; `00470f40`; `004713b0` [+1 dir]; `004717b0`; `00471960` [+2 dir]; `00471bc0`; `00471c50`; `00471d40`; `00471f10`; `00471f60`; `00471f80`; `00472230`; `00472330`; `00472530` [+1 dir]; `00472a60` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a6/query-detail/broadphase/` (manifest.txt): `00460d10`; `00462e20`; `004691e0`; `00469780`; `0046a370`; `00476160`; `004765c0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a6/query-detail/legacy/` (manifest.txt): `004523f0` (native engine.cpp wWorldContent); `00452f90` (native check_engine.cpp cast); `00453500` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a6/query-detail/legacy-scratch/` (contract.txt): `00452180` (native engine.cpp Engine::sample_texture_hit) [+1 dir]
- `runtime/bonus-core/entity-light-cache/a6/query-detail/modern-null-texture/` (contract.txt): `00471510` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a6/query-detail/visitor/` (manifest.txt): `00475ef0`
- `runtime/bonus-core/entity-light-cache/a6/scheduling/` (manifest.txt): `00432cb0` [+3 dir]; `00432f40` [+1 dir]; `0044dca0`; `0044de90`; `0044df20` [+2 dir]; `0044e4f0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a6/spatial/` (manifest.txt): `0042fc50` (native engine.cpp worldModelBuiltin); `004306e0` [+1 dir]; `00433c30` [+2 dir]; `00434730` (native engine.hpp comment) [+1 dir]; `004348e0` (native engine.cpp Engine::update_entity_light) [+2 dir]; `00437aa0` (sel: data 006c8d90, data 006cf4e0) [+4 dir]; `00437e90` (sel: data 006cf4e0) [+5 dir]; `00439010` [+2 dir]; `0044d5e0` [+2 dir]; `00459a90` [+1 dir]; `0045b1e0` (native assets.cpp Assets::decode_world) [+2 dir]
- `runtime/bonus-core/entity-light-cache/a6/trigger-range/` (contract.txt): `0042fd30` (native engine.cpp load_entity_asset)
- `runtime/bonus-core/entity-light-cache/a6/view-insertion/` (manifest.txt): `004210d0` (native value.hpp State::create) [+1 dir]; `00421a30`; `00421ad0`
- `runtime/bonus-core/entity-light-cache/a6/view-order/` (manifest.txt): `0044ed80` (sel: data 006cf4e0) [+5 dir]; `00450050` (sel: data 00627ac4) [+2 dir]
- `runtime/bonus-core/entity-light-cache/a6/view-origin/` (contract.txt): `0040e180` (sel: data 0065f1e8) [+2 dir]; `00417b20` [+3 dir]; `00417bc0`; `004184b0` (native check_engine.cpp cast); `004184d0` [+2 dir]; `0044f970` (sel: data 006441e0, data 006441e4; native engine.cpp Engine::prepare_entity_light) [+2 dir]
- `runtime/bonus-core/entity-light-cache/a6/view-origin/common-prep/` (manifest.txt): `0043b5f0`; `0043fc10`; `0044bb10`; `0044e260` (native engine.cpp Engine::display_aspect) [+1 dir]; `0044e9f0` (sel: data 006cf568, data 006cf550) [+1 dir]; `0044f530` (sel: data 006cf400, data 006cf340) [+2 dir]
- `runtime/bonus-core/entity-light-cache/a6/view-origin/dirty-lifetime/` (manifest.txt): `00446500`; `004465c0` [+5 dir]; `00446800` [+2 dir]; `00447830` [+2 dir]; `0044a790` [+2 dir]; `0044cd00` (native engine.cpp Engine::display_aspect); `0044d300`
- `runtime/bonus-core/entity-light-cache/a6/view-origin/lighting/` (manifest.txt): `0044a6f0` [+2 dir]; `0044add0` [+4 dir]
- `runtime/bonus-core/entity-light-cache/a6/view-origin/material/` (contract.txt): `004370c0` [+3 dir]; `00437100` (sel: data 00627ac4) [+2 dir]; `00437630` [+1 dir]; `00438030` [+3 dir]; `0043ee40` [+2 dir]
- `runtime/bonus-core/entity-light-cache/a6/view-origin/registration/` (manifest.txt): `00417800`
- `runtime/bonus-core/load-bonus/initial/` (runtime-manifest.txt): `10004120`; `10006dc0` (level_load)
- `runtime/bonus-core/load-campaign/` (runtime-manifest.txt): `0040f600` [+1 dir]; `00412a80` [+8 dir]; `00416980`; `00416a00`; `0041c450`; `00426760` [+2 dir]; `00426890` [+1 dir]; `004268f0` [+1 dir]; `00429a90` [+2 dir]; `0042a1a0` [+2 dir]; `0042a2e0` [+1 dir]; `0042a7a0` [+9 dir]; `0042daa0` [+5 dir]; `005b8a18` (_sprintf) [+7 dir]; `005ba13f` (_free) [+2 dir]; `005ba192`; `005ba4d4`; `005bace0` (__alldiv); `005be8d6` (__lock); `005be908` (__SEH_prolog; sel: callee of 005bb38b) [+4 dir]; `005c4e8b` (___sbh_find_block); `005c4eb6` (___sbh_free_block)
- `runtime/bootstrap/` (runtime-manifest.txt): `0043a7a0`; `0043a7b0` [+1 dir]; `0044ccc0`; `00456150` (sel: data 006441e0, data 006441e4) [+1 dir]
- `runtime/clock/` (K22.exe.recovery.txt): `00432530` (sel: data 00665874, data 0066578c) [+1 dir]; `0043e390` (sel: data 00665cf4, data 006cf400) [+2 dir]; `00461a20` (sel: data 00665cf4)
- `runtime/cloud-alpha/` (contract.txt): `00426380`; `00426e60` [+3 dir]; `00427ef0` [+3 dir]; `004813d1`; `00481a91`; `005b7350` (_strncpy) [+5 dir]
- `runtime/constant-helper/` (runtime-manifest.txt): `005bcc8e` (__itoa) [+2 dir]
- `runtime/constants/` (runtime-manifest.txt): `0041fc00` [+1 dir]; `005b8540` (_strstr) [+2 dir]
- `runtime/entity-ambient-consumers/` (runtime-manifest.txt): `00426330`; `00441130` (sel: data 00627ac4); `005b7f54` [+3 dir]
- `runtime/entity-ambient-source/` (contract.txt): `00434700` [+1 dir]; `005b7481` (_rand)
- `runtime/entity-cancel/` (runtime-manifest.txt): `00469400` [+1 dir]
- `runtime/entity-cancel/pointer-cleanup/action-locals/` (K22.exe.manifest.txt): `00403cd0` [+1 dir]
- `runtime/entity-cancel/pointer-cleanup/bulk-pool/` (K22.exe.manifest.txt): `0040f720`
- `runtime/entity-cancel/pointer-cleanup/deferred/` (K22.exe.manifest.txt): `0040f7f0` [+2 dir]
- `runtime/entity-cancel/pointer-cleanup/generic/` (K22.exe.manifest.txt): `0040ef90` [+1 dir]
- `runtime/entity-cancel/pointer-cleanup/locals/` (K22.exe.manifest.txt): `0040f920`; `0040fa80`; `00410150` [+1 dir]
- `runtime/entity-cancel/pointer-cleanup/primary/` (K22.exe.manifest.txt): `0040f7e0`; `004374b0`; `00452140`
- `runtime/entity-cancel/pointer-cleanup/producers/` (K22.exe.manifest.txt): `00405b40` (API ent_remove) [+1 dir]; `0041bd10`; `0041da70` (sel: data 0065f1e8) [+2 dir]; `0041f5d0` [+1 dir]; `00431d20` [+1 dir]
- `runtime/entity-cancel/pointer-cleanup/reclamation/` (K22.exe.manifest.txt): `0041f670` [+1 dir]; `004300a0`; `00431a10`; `00450750`
- `runtime/entity-cancel/pointer-cleanup/synonyms/` (K22.exe.manifest.txt): `0041f3b0`; `0041f5f0`; `0041fb80` [+1 dir]
- `runtime/entity-cancel/pointer-cleanup/types/` (K22.exe.manifest.txt): `0041f720` (sel: data 006441e0, data 006441e4) [+2 dir]
- `runtime/entity-cancel/pointer-cleanup/variable-storage/` (K22.exe.manifest.txt): `004023a0` [+1 dir]
- `runtime/entity-skin-contract/` (contract.txt): `00405410` (API bmap_for_entity; thunk_FUN_004349f0); `00405720` (API ent_morphskin); `00405870` (API ent_skins); `004360b0` [+2 dir]; `00446610`; `004467a0`; `00447660` [+3 dir]; `00449cd0`; `0044a1a0`
- `runtime/entity-skin-mdl7/` (runtime-manifest.txt): `005b6920` [+1 dir]
- `runtime/entity-skin-render/` (runtime-manifest.txt): `00437ef0` (sel: data 006c8d50) [+1 dir]; `0043a9e0` [+1 dir]; `0043d530` [+2 dir]; `00442310`; `00442a30` [+1 dir]; `00446d40`; `0044a350`; `0044f110` [+1 dir]; `005ba56f` (_fabs)
- `runtime/entity-skin-selection/` (runtime-manifest.txt): `00434940`; `00439030`; `00439470`; `00439660` [+2 dir]; `0044e810` [+3 dir]
- `runtime/entity-skin-subsets/` (runtime-manifest.txt): `0043c2a0` [+1 dir]; `0043c5d0` [+1 dir]; `0043f280`; `00450ed0` (sel: data 00627ac4)
- `runtime/entity-skin-surfaces/` (runtime-manifest.txt): `0043d4f0`; `00442770`
- `runtime/font-builtin/` (K22.exe.recovery.txt): `00408310` (API file_var_write; sel: str "Script") [+12 dir]; `004083c0` (API file_var_read; sel: str "Script") [+12 dir]; `0040c4d0` (sel: str "_a4font") [+6 dir]; `00415130` (sel: str ",script") [+10 dir]
- `runtime/font-digits/` (runtime-manifest.txt): `0040c8e0` [+2 dir]; `0040d460` [+1 dir]; `00427a30` [+1 dir]; `00427ad0` [+1 dir]; `005c0055` (__fload_withFB) [+3 dir]; `005c11f9`; `005c21e7` (__flsbuf); `005c238e`
- `runtime/font-entries/` (runtime-manifest.txt): `00408970` (API draw_textmode); `004089d0` (API draw_text); `0040ed60` (sel: data 006444f8) [+1 dir]; `004152f0` [+1 dir]; `00417670`; `004178d0` (sel: data 0065f1e8) [+1 dir]; `00425ca0`; `0048355d`; `004d5e1f`; `005b6d45` (operator_new) [+1 dir]
- `runtime/font-layout/` (runtime-manifest.txt): `00405220` (thunk_FUN_00405070) [+1 dir]; `0040ccb0`; `0040ce80` [+1 dir]; `0040d7a0`; `0040e2b0` [+1 dir]; `0040e5c0`; `0040eb60` (sel: data 006444f8) [+2 dir]; `00419960` [+1 dir]; `00420500`; `0042a720` [+1 dir]; `005b7030` (__stricmp) [+14 dir]; `005bbb22` (___updatetlocinfo) [+5 dir]; `005bf372` (__getptd) [+6 dir]; `005bf650` (___ascii_stricmp) [+4 dir]
- `runtime/font-text-init/` (runtime-manifest.txt): `00418660`; `005bb5a7` (___tolower_mt) [+2 dir]
- `runtime/font-xrefs/` (K22.exe.recovery.txt): `0040ce30` (sel: data 0065f1e8); `0040ed20` (sel: data 006444f8); `0040ee00` (sel: data 006444f8); `0040ee30` (sel: data 006444f8); `00417850` (sel: data 0065f1e8); `00417890` (sel: data 0065f1e8); `00417940` (sel: data 0065f1e8) [+1 dir]; `0041bae0` (sel: data 0065f1e8); `00429810` [+3 dir]; `00429890` (sel: str "acknex_mainwin") [+7 dir]; `0042ece0` (sel: callee of 005bb38b, str "acknex.wdf") [+7 dir]; `004905d1` (sel: str "version token") [+7 dir]; `004915e7` (sel: str "include interface required to support #) [+7 dir]; `004939b5` (sel: str "D3DX_VERSION", str "DIRECT3D_VERSION") [+6 dir]; `00494d40` (sel: str "ID3DXEffectCompiler: Unexpected compone, str "ID3DXEffectCompiler: Unexpected templat) [+6 dir]; `004954ab` (sel: str "ID3DXEffectCompiler: Arrays must be eit, str "ID3DXEffectCompiler: Only numeric types) [+6 dir]; `004959d2` (sel: str "ID3DXEffectCompiler: Error initializing, str "ID3DXEffectCompiler: Internal error ini) [+6 dir]; `0049651d` (sel: str "ID3DXEffectCompiler: State '%s' was ass, str "ID3DXEffectCompiler: Initializers must ) [+6 dir]; `004966eb` (sel: str "ID3DXEffectCompiler: Unexpected value t, str "ID3DXEffectCompiler: State '%s' was ass) [+5 dir]; `00496ce4` (sel: str "ID3DXEffectCompiler::CompileEffect: The, str "ID3DXEffectCompiler::CompileEffect: Unr) [+5 dir]; `00496f54` (sel: str "ID3DXEffectCompiler::CompileEffect: Cou, str "ID3DXEffectCompiler::CompileEffect: The) [+5 dir]; `00498d67` (sel: str "ID3DXEffectCompiler: Unsupported sample, str "ID3DXEffectCompiler: DMAPOFFSET sampler) [+5 dir]; `00499412` (sel: str "ID3DXEffectCompiler: Error initializing, str "ID3DXEffectCompiler: Only numeric types) [+5 dir]; `0049b451` (sel: str "ID3DXEffectCompiler: There was a proble, str "ID3DXEffectCompiler: Error initializing) [+4 dir]; `0049bb26` (sel: str "ID3DXEffectCompiler: There was a proble) [+4 dir]; `0049befe` (sel: str "ID3DXEffectCompiler: There was a proble, str "ID3DXEffectCompiler: Only pass allowed ) [+4 dir]; `0049c189` (sel: str "ID3DXEffectCompiler: There was an error, str "ID3DXEffectCompiler: There was an unexp) [+4 dir]; `0049c6cf` (sel: str "version token") [+4 dir]; `0049f3f0` (sel: str "invalid subscript '%s'") [+4 dir]; `004a2a95` (sel: str "compile_fragment", str "compile") [+3 dir]; `004a52c9` (sel: str "unsupported compiler target '%s'") [+3 dir]; `004a9f1b` (sel: str "D3DX Effect Compiler") [+3 dir]; `004d4e1c` (sel: str "ScriptString_pSize", str "ScriptStringCPtoX") [+3 dir]; `004ec6bd` (sel: str "'%s' is not a valid instruction in this) [+3 dir]; `004ed386` (sel: str "relative addressing of predicates is no, str "predicates are not supported in this sh) [+3 dir]; `004eed1d` (sel: str "complement is not supported in this sha, str "instruction predication is not supporte) [+3 dir]; `004f37c8` (sel: str "Incompatible libpng version in applicat) [+3 dir]; `004f3ae2` (sel: str "Incompatible libpng version in applicat, str "zlib version error") [+3 dir]; `00521eae` (sel: str "program is order of magnitude too large) [+3 dir]; `00527e8c` (sel: str "shader cannot compile to a ps_1_%i shad) [+3 dir]; `005284c7` (sel: str "program is order of magnitude too large, str "program too complex to compile to ps_1_) [+3 dir]; `0053e107` (sel: str "Microsoft (R) D3DX9 Shader Compiler 5.0) [+3 dir]; `0053ea71` (sel: str "Microsoft (R) D3DX9 Shader Compiler 5.0) [+3 dir]; `0055c14c` (Direct3DCreate9; sel: import Direct3DCreate9 indirect via 005d, import Direct3DCreate9 via 0055c14c) [+3 dir]; `005b98bf` (__cinit; sel: callee of 005bb38b) [+3 dir]; `005b99ec` (_exit; sel: callee of 005bb38b) [+3 dir]; `005b9a0e` (__cexit; sel: callee of 005bb38b) [+3 dir]; `005b9d40` (__chkstk; sel: callee of 005bb38b) [+3 dir]; `005bb342` (__amsg_exit; sel: callee of 005bb38b) [+3 dir]; `005bb367` (fast_error_exit; sel: callee of 005bb38b) [+3 dir]; `005be943` (__SEH_epilog; sel: callee of 005bb38b) [+3 dir]; `005bf559` (__mtinit; sel: callee of 005bb38b) [+3 dir]; `005c3f1e` (__ioinit; sel: callee of 005bb38b) [+3 dir]; `005c4448` (__RTC_Initialize; sel: callee of 005bb38b) [+3 dir]; `005c4df2` (__heap_init; sel: callee of 005bb38b) [+3 dir]; `005c70a7` (__wincmdln; sel: callee of 005bb38b) [+3 dir]; `005c7104` (sel: callee of 005bb38b) [+3 dir]; `005c71cb` (parse_cmdline) [+3 dir]; `005c7337` (__setargv; sel: callee of 005bb38b) [+3 dir]; `005c73d9` (sel: callee of 005bb38b) [+3 dir]
- `runtime/graphics-settings-source/a6-cadence/` (K22.exe.manifest.txt): `00456ed0` [+1 dir]; `0045d660`; `0045dbb0`; `0045dcc0`
- `runtime/graphics-settings-source/a6-initial/` (K22.exe.manifest.txt): `00425ff0`; `00437530`; `00438210`; `00438390`; `00438470` (sel: data 00665c1c) [+2 dir]; `00441210` (sel: data 006cf550, data 006c8d90) [+5 dir]; `00444eb0` (sel: data 006cf55c, data 006cf568) [+5 dir]; `0044bcc0` (sel: data 006cf550) [+2 dir]
- `runtime/graphics-settings-source/a6-presentation/` (K22.exe.manifest.txt): `00456f30` [+1 dir]; `0045ce90`; `0045cf60`; `0045d6b0`; `0045d7e0`; `0045da10`; `0045dd50`
- `runtime/graphics-settings-source/a6-producers/` (K22.exe.manifest.txt): `00425cf0`; `00426610` [+1 dir]; `00437800` (sel: data 006c8d7c, data 00665c1c) [+4 dir]; `004500b0` (sel: data 006cf55c, data 006cf568) [+2 dir]; `00457550` [+1 dir]; `0045d080`; `0045d370`; `0045db1b`
- `runtime/graphics-settings-source/a6-video-boundary/` (K22.exe.manifest.txt): `00455d40` [+1 dir]
- `runtime/graphics-settings-source/a6-window-selection/` (K22.exe.manifest.txt): `0045d180`
- `runtime/graphics-settings-source/a6-worldlight-bootstrap/` (K22.exe.manifest.txt): `0044cce0`; `00451080`
- `runtime/graphics-settings-source/a6-worldlight-postload/` (K22.exe.manifest.txt): `00452110`; `00457500`; `00459e80`
- `runtime/graphics-settings-source/a6-worldlight-source/` (K22.exe.manifest.txt): `0042f7b0`; `0042fcf0`; `00431270`; `00437250`; `00437450`; `00438710`
- `runtime/initializer-token/` (runtime-manifest.txt): `0040f180` [+3 dir]; `00414e50` [+6 dir]
- `runtime/input/` (runtime-manifest.txt): `005b702b` (_atol) [+5 dir]; `005b7099` (__strnicmp) [+2 dir]
- `runtime/input-edges/` (runtime-manifest.txt): `00421f30`; `004228e0`; `00428620`
- `runtime/input-keyboard/` (runtime-manifest.txt): `00422520`; `00428cc0`; `00428d30`; `00429130`
- `runtime/input-scan/` (runtime-manifest.txt): `00421f90`
- `runtime/interpreter/` (runtime-manifest.txt): `00404a10` (API ptr_for_name/SYN_FOR_NAME); `00404a50` (API ptr_for_handle); `0040c410` [+1 dir]; `0040c490`; `0040c6a0`; `00411f80` [+1 dir]; `0041ec20`; `00420730` [+1 dir]; `00431e80`; `00431f90`; `004323b0`; `00435ed0`; `004365a0` (sel: data 00665908) [+2 dir]; `00451f00`; `00455eb0` [+1 dir]; `005b8e00` (_strpbrk)
- `runtime/legacy-remove/` (runtime-manifest.txt): `00402a10` [+2 dir]; `00402ae0` [+2 dir]; `00402ba0` [+1 dir]; `00402c90` [+2 dir]; `0042ef50` [+2 dir]; `0042efb0` [+2 dir]; `0042f600`; `0042f650`; `00430300`; `00430350` [+3 dir]
- `runtime/level-dispatch/` (runtime-manifest.txt): `0041d6e0`
- `runtime/lighting-entity-vector/` (runtime-manifest.txt): `004390e0`
- `runtime/lighting-origin/` (K22.exe.recovery.txt): `00436f50` (sel: data 006c8d50, data 006c8d7c) [+2 dir]
- `runtime/lighting-origin-asm/` (contract.txt): `00427560` [+1 dir]; `00439190` [+1 dir]; `00439230` [+2 dir]; `00439240` (native check_engine.cpp push_nested) [+5 dir]; `004395c0` [+1 dir]; `0044ec40` [+1 dir]; `005b7834` [+1 dir]; `005b7ea4` [+2 dir]; `005b8014` [+1 dir]
- `runtime/lighting-stages/` (runtime-manifest.txt): `00436f90` (sel: data 006c8d50, data 006c8d90) [+1 dir]; `0043b1a0` [+1 dir]; `0043d2e0`
- `runtime/lighting-writers-asm/` (runtime-manifest.txt): `00439260` [+4 dir]; `00440ac0` [+1 dir]; `00440b10` [+1 dir]; `0044baf0`
- `runtime/local-digit-bindings/` (runtime-manifest.txt): `00401160`; `00401410`; `00402450`; `00403cc0` [+1 dir]; `0040aa50`; `0040d2a0`; `0040d5d0`; `0041a000`; `004447e0`; `005b6fa3` (_atol)
- `runtime/local-pool/` (runtime-manifest.txt): `005bcc50` (xtoa)
- `runtime/local-storage/` (runtime-manifest.txt): `004010c0`; `0040aab0` [+1 dir]; `0040eb20` [+1 dir]
- `runtime/lookup/` (runtime-manifest.txt): `0040f030` [+1 dir]; `0040f140` [+1 dir]
- `runtime/material-renderer/a6-gates/` (K22.exe.manifest.txt): `0043aa60` [+1 dir]
- `runtime/media-name/` (runtime-manifest.txt): `0040f5c0` [+1 dir]; `00424aa0`; `00424d90`; `00425680` [+1 dir]; `00428460`; `0043b850` [+2 dir]; `0045fe00` [+1 dir]; `0045fff0`; `00460010` [+1 dir]
- `runtime/mode-bitmap-plugin/` (runtime-manifest.txt): `005b96b0` (__findfirst) [+2 dir]
- `runtime/mode-directives/` (runtime-manifest.txt): `00419750`; `0041acc0` [+1 dir]
- `runtime/model-stage-candidates/` (runtime-manifest.txt): `0043f370`
- `runtime/model-uv-binding/` (runtime-manifest.txt): `00426410`; `00426ad0` [+1 dir]
- `runtime/model-uv-contract/` (contract.txt): `005b6860`; `005b6a30`
- `runtime/model-uv-textures/` (runtime-manifest.txt): `004272d0` [+1 dir]
- `runtime/numeric/` (runtime-manifest.txt): `0040a250`; `00412540` [+2 dir]; `0042f6a0` [+1 dir]; `00431dc0` [+1 dir]
- `runtime/ownership/consumer/` (K22.exe.manifest.txt): `00412390`; `004123c0`
- `runtime/ownership/engine/` (K22.exe.manifest.txt): `00404500` (API str_cpy); `00420720`
- `runtime/ownership/lifecycle/` (K22.exe.manifest.txt): `0041bdb0`; `0041bdf0`; `00420780`; `00420c2f`; `00420dc0` [+1 dir]
- `runtime/panel-digit-callbacks/` (runtime-manifest.txt): `004014a0`; `0040f380` [+1 dir]; `004174b0`; `004174f0` (native value.hpp field); `00418770`; `00418820` (native wdl.cpp Runtime::Impl::initialize); `0041aaf0` [+1 dir]; `0041abb0`
- `runtime/panel-digit-producer/` (runtime-manifest.txt): `0040fa20`; `00415390` (native value.hpp State::create) [+2 dir]; `004159d0` (native value.hpp State::create); `00419780`; `00419af0`; `0041ac90`; `0041e330`; `0041e820`; `00420c00`; `004219c0`; `00428db0`; `00428f40`; `0045cdd0` [+2 dir]
- `runtime/particle-material/` (runtime-manifest.txt): `00404130`; `0040f690`; `00415ad0` (native engine.cpp Engine::initialize_globals) [+1 dir]; `0042b770` [+1 dir]; `0042f860`; `00439940`; `004399b0`; `0043b040`; `0043b1f0` (sel: data 00627ac4) [+1 dir]; `0043bd20`; `0043bd50`; `0043bd80`; `0043bdb0`; `0043bf70`; `0043c070`; `0043d7c0` (sel: data 006cf554, data 006cf558) [+3 dir]; `0043ddc0`; `0043f460`; `00443940`; `00443950`; `00447760`
- `runtime/particle-material-bridge/` (runtime-manifest.txt): `0040f7b0`; `004121b0` [+3 dir]; `00415d50`; `00437780`; `004377d0`; `0043b520`; `0047d31c` (thunk_FUN_0047d309); `0047d842` (thunk_FUN_0047d82f)
- `runtime/particle-material-hardware/` (runtime-manifest.txt): `00404000` [+2 dir]; `004040f0` [+3 dir]; `004067e0` [+2 dir]; `00442ea0` [+2 dir]; `00443350` [+1 dir]; `004473c0` [+1 dir]; `0044d140` [+1 dir]; `0044d2a0` [+1 dir]; `0044f420` [+1 dir]; `00459970` [+2 dir]
- `runtime/planar-shadow-source/a6/` (K22.exe.manifest.txt): `0043d1e0`; `00449c60`; `004561c0` (native assets.cpp Assets::load_model); `0045b810` (native assets.cpp Assets::load_model) [+3 dir]
- `runtime/planar-shadow-source/a6/emitted-vertices/` (K22.exe.manifest.txt): `0043be70`
- `runtime/planar-shadow-source/a6/initializer/` (K22.exe.manifest.txt): `0043b380` [+1 dir]
- `runtime/portal-albedo/` (runtime-manifest.txt): `00415ee0`; `00416930` [+1 dir]; `00424630`; `0042fa20` [+1 dir]; `0042fa80`; `0042ff50`; `00430e10`
- `runtime/portal-composition/` (runtime-manifest.txt): `00439e60`; `0043cac0`; `0043cde0`; `0043db10`; `0044bc10`; `0044ebd0`; `00459d80`
- `runtime/portal-frame/` (runtime-manifest.txt): `004136b0`; `004246a0`; `00424720`; `00427cc0`; `0043b740`; `0044e100`; `004691d0`; `005b946a` (_fflush); `005b94c3` (_vsprintf); `005b951a` (FID_conflict__vfwprintf)
- `runtime/portal-render/` (runtime-manifest.txt): `00415a40`; `0043a550`; `0043c440`; `0043ff00`; `0044cbe0`; `0044cff0`; `0044d080`; `0044d4e0`; `0044d530`; `0044f2e0`; `0044fde0` (sel: data 006cf550, data 006441e0) [+2 dir]; `00450d70`; `005ba6b0` (_qsort)
- `runtime/portal-surface-xrefs/` (K22.exe.recovery.txt): `0043d240` (sel: data 006cf550) [+1 dir]
- `runtime/portal-xrefs/` (K22.exe.recovery.txt): `0043a230` (sel: data 006cf400, data 006cf340); `0044d3c0` (sel: data 006cf340)
- `runtime/registry/` (runtime-manifest.txt): `00401e80`; `00401f00`; `005bf6a0` (___ascii_strnicmp)
- `runtime/sky-coordinates/` (runtime-manifest.txt): `0043c760` (sel: data 00627ac4); `0043c9f0` (sel: data 006cf5c0, data 006cf5cc) [+1 dir]; `0043ce50` (sel: data 006c93d0) [+1 dir]; `0043cf20` (sel: data 006c93d0) [+1 dir]; `0044e010`
- `runtime/sky-material-source/` (contract.txt): `0040f870` [+1 dir]; `00457b60`; `00459c70` (sel: data 006441e0, data 006441e4) [+1 dir]; `0045a8a0`
- `runtime/sky-normalize/` (runtime-manifest.txt): `00420090`; `004599e0`; `0045a750`; `0045b540`; `005b883a` (_fclose) [+2 dir]; `005b8974` (_fread) [+1 dir]; `005b9e0c` (_fseek); `005bac90` (_strncmp)
- `runtime/sky-setup/` (K22.exe.recovery.txt): `0043f950` (sel: data 006cf58c, data 006c939c) [+1 dir]; `00440be0` (sel: data 006cf58c, data 006c939c) [+1 dir]
- `runtime/sky-turb/` (runtime-manifest.txt): `004395b0`
- `runtime/string-paths/` (runtime-manifest.txt): `005b8200` (_memmove); `005cd52e` (__getcwd) [+1 dir]
- `runtime/sun-header-asm/` (contract.txt): `0041e7c0`
- `runtime/sun-level-closure/` (runtime-manifest.txt): `00457910`
- `runtime/tex-face-ceil/` (runtime-manifest.txt): `005bab70`
- `runtime/tex-face-origin/` (runtime-manifest.txt): `005ba940`
- `runtime/tex-lightmap-cap-xrefs/` (K22.exe.recovery.txt): `00412b90` (sel: data 00627ac4); `00425e70` (sel: data 00627ac4); `0043ae80` (sel: data 00627ac4); `0043b690` (sel: data 00627ac4)
- `runtime/tex-lightmap-lut/` (contract.txt): `00455180` (native assets.cpp native_lightmap_ramp); `00455530` (sel: data 00627ac4); `004555e0` (sel: data 00627ac4, data 006f0da0; native assets.cpp Assets::initialize_native_lightmap_tables)
- `runtime/tex-lut-lifecycle/` (runtime-manifest.txt): `0042d990`; `00455450`
- `runtime/tex-native-publish/` (runtime-manifest.txt): `00462ec0`
- `runtime/tex-native-trace/` (runtime-manifest.txt): `004512f0`
- `runtime/tex-publish/` (contract.txt): `0040c880`
- `runtime/tex-world-layout/` (runtime-manifest.txt): `004566f0` (sel: data 006f0da0); `00457a10`; `0045aac0` (sel: data 00627ac4, data 006f0da0)
- `runtime/text-load/` (runtime-manifest.txt): `005b8a70` (__fsopen); `005b9e55`; `005cd3e0`
- `runtime/trace/` (runtime-manifest.txt): `00404cf0` (API proc_client); `00404d10` (API proc_local); `00410480`; `00410750`; `00428520`; `00428590`
- `runtime/view-birth-defaults/` (runtime-manifest.txt): `004061c0` (API view_create; thunk_FUN_00421150)
- `runtime/window-parser/` (runtime-manifest.txt): `00419f50`; `00419fa0`; `0041aad0`

### kabus_dll.dll

- `./` (K22.exe.recovery.txt): `1007b306` (__acrt_stdio_parse_mode_D) [+1 dir]; `1007b66c` (__acrt_stdio_parse_mode_plus) [+1 dir]; `1007b9af` (__acrt_stdio_parse_mode_c) [+1 dir]; `1007bfea` (__acrt_stdio_parse_mode_n) [+1 dir]; `1007c6d9` [+1 dir]; `1007c855` (___acrt_execute_uninitializers) [+1 dir]; `1007c85a` [+1 dir]; `1007c896` (FID_conflict___acrt_stdio_parse_mode_b) [+1 dir]; `1007c8d7` (__acrt_stdio_parse_mode_x) [+1 dir]; `1007ca3a` [+1 dir]; `1007d2a5` (FID_conflict___acrt_stdio_parse_mode_b) [+1 dir]; `1007d3b8` [+1 dir]; `1007dcb9` [+1 dir]; `1007e812` [+1 dir]; `1007eea7` (__acrt_stdio_parse_mode_T) [+1 dir]; `1007f78a` [+1 dir]; `1007f7df` [+1 dir]; `1007f8e8` (___acrt_execute_initializers) [+1 dir]; `1007fc58` (PE entry; sel: entry/export) [+1 dir]; `100b5d40` (sel: str "invalid vector subscript") [+1 dir]; `100e8852` (_Stringify; sel: str "regex_error(error_parse)") [+1 dir]; `1010fff8` (___GetPlatformExceptionInfo) [+1 dir]; `10110bfa` [+1 dir]; `10111ab0` (Is_bad_exception_allowed) [+1 dir]; `10116def` (sel: str " Type Descriptor'") [+1 dir]; `1011df58` [+1 dir]; `1011dff8` [+1 dir]; `1011e098` [+1 dir]; `1011e138` [+1 dir]; `1011f795` [+1 dir]; `1011f8da` [+1 dir]; `1011fa60` [+1 dir]; `1011faab` [+1 dir]; `1011fb01` [+1 dir]; `1011fb4c` [+1 dir]; `1011fba2` [+1 dir]; `1011fd69` [+1 dir]; `1011fed6` [+1 dir]; `1011ff31` [+1 dir]; `10124f59` (parse_digit) [+1 dir]; `10151fa2` (___acrt_initialize) [+1 dir]; `10151fcf` (___acrt_uninitialize) [+1 dir]; `10158d68` (try_get_proc_address_from_first_available_module) [+1 dir]; `101607de` [+1 dir]; `10160a92` [+1 dir]; `10160d97` [+1 dir]; `10160e3a` [+1 dir]; `10161059` (__acrt_stdio_parse_mode_D) [+1 dir]; `10161090` (__acrt_stdio_parse_mode_R) [+1 dir]; `101610b5` (__acrt_stdio_parse_mode_S) [+1 dir]; `101610da` (__acrt_stdio_parse_mode_T) [+1 dir]; `10161100` (FID_conflict___acrt_stdio_parse_mode_b) [+1 dir]; `10161128` (__acrt_stdio_parse_mode_c) [+1 dir]; `10161152` (__acrt_stdio_parse_mode_n) [+1 dir]; `1016117c` (__acrt_stdio_parse_mode_plus) [+1 dir]; `101611bf` (FID_conflict___acrt_stdio_parse_mode_b) [+1 dir]; `101611e7` (__acrt_stdio_parse_mode_x) [+1 dir]; `10166349` (___acrt_execute_initializers) [+1 dir]; `101663cb` (___acrt_execute_uninitializers) [+1 dir]
- `final/` (K22.exe.recovery.txt): `1007a285` (thunk_FUN_100a5460; sel: callee of 100803b5); `1007ae74` (thunk_FUN_100aed90; sel: callee of 100803b5, callee of 1007b68a); `1007b02c` (thunk_FUN_100a2010; sel: callee of 100803b5); `1007b077` (thunk_FUN_100a0770; sel: callee of 1007bdfb, callee of 1007d250); `1007c5cb` (__security_check_cookie; sel: callee of 100803b5, callee of 1007bdfb); `1007c846` (thunk_FUN_100adfb0; sel: callee of 100803b5); `1007c940` (thunk_FUN_100aa9b0; sel: callee of 1007bdfb); `1007cc0b` (thunk_FUN_100ab5c0; sel: callee of 1007c67f); `1007cc6f` (thunk_FUN_100adb00; sel: callee of 1007d250); `1007d1a1` (thunk_FUN_100a4b80; sel: callee of 1007bdfb, callee of 1007d250); `1007d430` (_RTC_CheckStackVars; sel: callee of 100803b5, callee of 1007bdfb); `1007dce1` (thunk_FUN_100a6420; sel: callee of 100803b5); `1007ddea` (__RTC_CheckEsp; sel: callee of 1007c67f, callee of 100803b5); `1007e574` (thunk_FUN_100ae300; sel: callee of 1007b68a); `1007eaa1` (thunk_FUN_100aa280; sel: callee of 1007d7eb); `1007ebf5` (thunk_FUN_1011d2e1; sel: callee of 1007bdfb, callee of 1007d7eb); `1007ed76` (thunk_FUN_100b6b70; sel: callee of 1007bdfb, callee of 1007d7eb); `1007f857` (__CheckForDebuggerJustMyCode; sel: callee of 1007c67f, callee of 100803b5); `10080464` (___security_init_cookie; sel: callee of 1007fc58); `100807c5` (thunk_FUN_100ae3f0; sel: callee of 1007aedd); `100a0770` (sel: callee of 1007b077); `100a2010` (sel: callee of 1007b02c); `100a5460` (sel: callee of 1007a285); `100a6420` (sel: callee of 1007dce1); `100ab5c0` (sel: callee of 1007cc0b); `100adfb0` (sel: callee of 1007c846); `100ae300` (sel: callee of 1007e574); `100ae3f0` (sel: callee of 100807c5); `10108ae0` (_RTC_CheckStackVars; sel: callee of 1007d430); `10108b49` (__RTC_CheckEsp; sel: callee of 1007ddea); `10108b74` (__CheckForDebuggerJustMyCode; sel: callee of 1007f857); `10108bb2` (__security_check_cookie; sel: callee of 1007c5cb); `10108f7e` (dllmain_dispatch; sel: callee of 1007fc58); `1010910b` (__DllMainCRTStartup_12; sel: entry/export); `1010aa7f` (___security_init_cookie; sel: callee of 10080464)
- `runtime/ownership/dll/` (kabus_dll.dll.manifest.txt): `100a4b80` (sel: callee of 1007d1a1) [+1 dir]; `100b6b70` (sel: callee of 1007ed76) [+2 dir]; `1014a569`
- `runtime/ownership/dll-lifecycle/` (kabus_dll.dll.manifest.txt): `1007d86d`; `100a3ce0`; `100b57d0`
- `runtime/ownership/dll-storage/` (kabus_dll.dll.manifest.txt): `100af560`; `100b3880`; `100b4070`

### RenkAyar.dll


### acknex.dll

- `runtime/bonus-asset-census/mdl7-reader/adapter/` (runtime-manifest.txt): `100ea810` [+1 dir]
- `runtime/bonus-asset-census/mdl7-reader/banks/` (runtime-manifest.txt): `100e8780` [+2 dir]; `100e9000` [+2 dir]; `100ea3e0`; `100ea5e0`; `100ea620`; `100ea970`
- `runtime/bonus-asset-census/mdl7-reader/callers/` (runtime-manifest.txt): `1004bcb0` [+2 dir]; `1004d7b0` [+2 dir]; `1005dc50` [+1 dir]
- `runtime/bonus-asset-census/mdl7-reader/engine/` (runtime-manifest.txt): `100e7410` [+1 dir]; `100eacda`
- `runtime/bonus-asset-census/mdl7-reader/face/` (runtime-manifest.txt): `10157771`
- `runtime/bonus-asset-census/mdl7-reader/final/` (runtime-manifest.txt): `1004cc80` [+2 dir]; `1004ee50` [+4 dir]; `1005c720`; `100e7360`; `100e8b80`
- `runtime/bonus-asset-census/mdl7-reader/global/` (runtime-manifest.txt): `1005ef60` (native assets.cpp Assets::embedded_image) [+1 dir]; `100e7220`; `100e8260`; `100e82c0`; `100e8480`; `100e85b0`
- `runtime/bonus-asset-census/mdl7-reader/groups/` (runtime-manifest.txt): `1015816b` [+1 dir]; `10158253`; `10158338`; `101583ed` [+1 dir]; `101584b4`; `101585e5`; `1015876e`; `101588c2`; `1015891e`; `10158989`
- `runtime/bonus-asset-census/mdl7-reader/importer/` (runtime-manifest.txt): `100e8320`; `100e9c20`; `100eaa11`; `100eac27`
- `runtime/bonus-asset-census/mdl7-reader/interface/` (runtime-manifest.txt): `101578cd`; `10157a3f`; `10157b67`; `10157c30`; `10157c92`; `10157d29`; `1015810b`; `10158442` [+1 dir]
- `runtime/bonus-asset-census/mdl7-reader/load/` (runtime-manifest.txt): `1005ee90` (native assets.cpp Assets::load_model) [+2 dir]
- `runtime/bonus-asset-census/mdl7-reader/lod/` (runtime-manifest.txt): `100ead50`; `100ead70`; `100ead90`; `100eadb0`; `100eadd0`
- `runtime/bonus-asset-census/mdl7-reader/lod-parser/` (runtime-manifest.txt): `10158f32`; `10158f72`
- `runtime/bonus-asset-census/mdl7-reader/model/` (runtime-manifest.txt): `1005f1c0` (native assets.cpp Assets::decode_model) [+2 dir]
- `runtime/bonus-asset-census/mdl7-reader/padding/` (runtime-manifest.txt): `10017a90` [+3 dir]; `1002c580` (native assets.cpp mdl_packed) [+2 dir]; `100597e0` [+2 dir]
- `runtime/bonus-asset-census/mdl7-reader/resource/` (runtime-manifest.txt): `1005ecc0` [+2 dir]
- `runtime/bonus-asset-census/mdl7-reader/skin-interface/` (runtime-manifest.txt): `100ea320`; `100ea490`; `100ea560`
- `runtime/bonus-asset-census/mdl7-reader/subset/` (runtime-manifest.txt): `1005cce0` [+1 dir]; `100e8a30`; `100e8a90`
- `runtime/bonus-asset-census/texture-reader/bitmap/` (runtime-manifest.txt): `100073d0` (API bmap_preload; bmap_preload) [+1 dir]; `10016f10` [+5 dir]; `1002b7f0`; `1002bdf0` [+1 dir]; `1002bef0`; `1002c1c0` [+1 dir]; `1002c420`; `1002c490` [+1 dir]; `1002c7b0` (native media.cpp duration) [+7 dir]; `1002d8d0` [+3 dir]; `1002de40` [+4 dir]; `10034ea0` [+3 dir]; `10059e90` (native assets.cpp Assets::decode_world) [+3 dir]; `10088550` (D3DXGetImageInfoFromFileInMemory) [+1 dir]; `1008856e` (D3DXCreateTextureFromFileInMemoryEx)
- `runtime/bonus-asset-census/texture-reader/bridge/` (runtime-manifest.txt): `1005a180`; `1005b3b0` [+2 dir]; `1005c500`; `1014264a` (thunk_FUN_101425c2) [+1 dir]
- `runtime/bonus-asset-census/texture-reader/exact/` (runtime-manifest.txt): `1002b8c0`; `1005a450` [+3 dir]; `1005e700` [+5 dir]; `1008855c` (D3DXFilterTexture); `10088562` (D3DXLoadSurfaceFromMemory); `10088568` (D3DXCreateTexture)
- `runtime/bonus-asset-census/texture-reader/section2/` (runtime-manifest.txt): `1001aad0`; `10041ae0` [+2 dir]; `1005b000`; `10143d1d`; `10143f60` (_strrchr); `10145130` (_strncmp)
- `runtime/bonus-core/animation/consumer/` (evidence.txt): `100083f0` (API ent_animate; ent_animate) [+1 dir]; `100181f0` [+12 dir]; `1001f270` [+3 dir]; `1001f3e0` [+5 dir]
- `runtime/bonus-core/animation/paired/` (runtime-manifest.txt): `1003c250`; `1003c310` [+1 dir]
- `runtime/bonus-core/animation/pose/` (evidence.txt): `10004120` [+4 dir]; `1003bfe0`
- `runtime/bonus-core/effect-null-direction/` (runtime-manifest.txt): `100081c0` (API effect_local; effect_local); `1003abd0` [+2 dir]; `1004dbe0`
- `runtime/bonus-core/entity-light-cache/a7/actor-filter/` (runtime-manifest.txt): `10054bb0` [+1 dir]; `10056a30` [+1 dir]; `10078970`; `10078cf0` (native engine.cpp Engine::collide)
- `runtime/bonus-core/entity-light-cache/a7/bank-source/` (runtime-manifest.txt): `1001e850` (native engine.cpp Engine::register_entity) [+2 dir]; `1003c1f0` [+1 dir]; `1004d320`; `1004fe50` [+1 dir]; `1004fee0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/closed/` (runtime-manifest.txt): `1000aa60` (API c_trace; c_trace) [+1 dir]; `1000abc0` (trace) [+1 dir]; `100191c0` [+9 dir]; `1001ab90` [+3 dir]; `10036140` [+1 dir]; `100361f0`; `10036250`; `10037a80` [+2 dir]; `100385e0` [+5 dir]; `10038d10` [+4 dir]; `10039640` [+4 dir]; `1003a580` [+2 dir]; `1003ab50` [+2 dir]; `1003ac80` (native engine.hpp comment) [+2 dir]; `1003ad90`; `1003add0` (native engine.cpp Engine::update_entity_light) [+1 dir]; `1003b300` (native engine.cpp Engine::prepare_query_geometry); `1003dbd0` [+1 dir]; `1003dc30` [+3 dir]; `1003def0` [+3 dir]; `1003f760` [+1 dir]; `1004d8a0` [+2 dir]; `1004d9a0` [+3 dir]; `10050120` [+2 dir]; `10050f10` [+2 dir]; `10055a10` [+1 dir]; `10058590`; `10058eb0` [+1 dir]; `1005a330` [+1 dir]; `10077110` [+3 dir]; `10077890` (native engine.cpp Engine::publish_hit) [+1 dir]; `100785c0` (native engine.cpp Engine::collide) [+3 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/actor-context-admission/` (runtime-manifest.txt): `10035e20` (native engine.cpp Engine::collide) [+2 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/attached-detach-finalizer/` (runtime-manifest.txt): `100675b0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/attached-rigid-destructor/` (runtime-manifest.txt): `10066af0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/baked-pose-initialization/` (runtime-manifest.txt): `100660b0` [+4 dir]; `10066160` (native engine.cpp Engine::refresh_query_pose) [+4 dir]; `1006dff0` (native engine.cpp Engine::refresh_query_pose) [+2 dir]; `1006e1e0` (native engine.cpp Engine::construct_query_actor) [+2 dir]; `10071390` [+1 dir]; `100b17f0` [+2 dir]; `100b1c50` [+4 dir]; `100b7b70` [+3 dir]; `100b7e30` [+3 dir]; `100bceb0` [+2 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/baked-terrain-source/` (runtime-manifest.txt): `1006da40`; `1006dae0` [+2 dir]; `10074610`; `10074640`; `100746b0`; `10074c60` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/bounds-producers/` (runtime-manifest.txt): `1006da20` [+4 dir]; `1006f550` [+3 dir]; `100711e0` [+1 dir]; `10071650` (native engine.cpp Engine::refresh_query_pose) [+1 dir]; `100736d0` [+4 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/center-setter/` (runtime-manifest.txt): `1006da80` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/chunk-basename-copy/` (runtime-manifest.txt): `10061570`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/chunk-name-gate/` (runtime-manifest.txt): `10061660`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/coarse-radius/` (runtime-manifest.txt): `10001000`; `10004030`; `100753a0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/declaration-list-finalizer/` (runtime-manifest.txt): `1001d9c0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/declaration-render-gate/` (runtime-manifest.txt): `1001e6d0`; `10036300`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/endpoint-classifier/` (runtime-manifest.txt): `100554e0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-contact-sort/` (runtime-manifest.txt): `1006a140` (native engine.cpp Engine::sort_query_contacts)
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-contact-sort-final-gates/` (runtime-manifest.txt): `10068f90`; `100694e0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-contact-sort-helpers/` (runtime-manifest.txt): `10069420`; `10069770`; `10069e80`; `1006a000` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-contact-sort-ordering/` (runtime-manifest.txt): `100690f0`; `10069220`; `10069680`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-contact-sort-record-copy/` (runtime-manifest.txt): `10069000`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-hull-contact-math/` (runtime-manifest.txt): `1003ab30`; `10067c80`; `10068b50`; `1006d570`; `1006f530`; `1007d710`; `1007d760`; `1007d770`; `10081a70`; `10086ac0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-hull-intersection/` (runtime-manifest.txt): `1007d740`; `10080150` (native engine.cpp wPublicHullRay); `10080ee0` (native engine.cpp wPublicHullRay)
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-hull-polygon-lifetime/` (runtime-manifest.txt): `10086560`; `10086580`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-hull-primitive/` (runtime-manifest.txt): `10068e10`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/final-hull-sat-outputs/` (runtime-manifest.txt): `1007d780`; `1007d940`; `1007e7e0`; `1007fc20` (native engine.cpp wPublicHullBox); `10080550`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/geometry-abi/` (runtime-manifest.txt): `1006dbb0` [+2 dir]; `100747a0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/heightmap-patches/` (runtime-manifest.txt): `10073e50`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/hmp-load-state/` (runtime-manifest.txt): `10059dc0` (native assets.cpp Assets::load_model) [+1 dir]; `1005c780`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/legacy-classifiers/` (runtime-manifest.txt): `10055570`; `10055600`; `10055c70`; `10055cc0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/modern-predicates/` (runtime-manifest.txt): `10065ed0`; `10068440` (native engine.cpp wPublicHullRay); `1006dac0`; `1006f790`; `1006ff90`; `100704a0` (native engine.cpp Engine::collide); `100724c0`; `10072ff0`; `100731f0`; `10074230` [+1 dir]; `10078bf0`; `10079420`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/owned-wmb-classifier/` (runtime-manifest.txt): `1006ffc0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/passable-move-final-gates/` (runtime-manifest.txt): `10056e80`; `100753c0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/patch-span-getter/` (runtime-manifest.txt): `10074580`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/pose-transform/` (runtime-manifest.txt): `1006e4c0` [+1 dir]; `10071020` [+1 dir]; `100732a0` (native engine.cpp Engine::refresh_query_pose) [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/prepare-entry/` (runtime-manifest.txt): `10036a70`; `10039b80` [+2 dir]; `10052750` (native engine.cpp Engine::prepare_entity_light) [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/prepare-epoch/` (runtime-manifest.txt): `10036a90` [+1 dir]; `10039550` (native engine.cpp Engine::collide) [+1 dir]; `10039ab0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/query-box-radius/` (runtime-manifest.txt): `10068e90` [+1 dir]; `10076d30`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/stages/` (runtime-manifest.txt): `100028a0` [+1 dir]; `1000fa80` [+2 dir]; `10012f00` [+1 dir]; `10055620`; `10076fb0`; `10078aa0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/terrain-admission/` (runtime-manifest.txt): `1006ff80`; `100736c0`; `10074fe0`; `10078a20`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/topology-and-ray/` (runtime-manifest.txt): `1005bc50`; `1005bf10`; `1005c360` (native physics.cpp Physics::load_world); `100e3ef0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/trigger-range-assignment/` (runtime-manifest.txt): `100014f0`; `100015e0` (native engine.cpp core_mask) [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/content-mask/unbound-mesh-consumer/` (runtime-manifest.txt): `100b18a0` [+1 dir]; `100b2680` (native modern_box.hpp comment); `100b9ea0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/unbound-mesh-payload/` (runtime-manifest.txt): `100b7f50`; `100c92b0`; `100d20a0`
- `runtime/bonus-core/entity-light-cache/a7/content-mask/wmb-partition/` (runtime-manifest.txt): `1006d370` (native engine.cpp wPublicHullRay) [+6 dir]; `1006f580` [+1 dir]; `1006f5b0` [+1 dir]; `1006fa90` [+1 dir]; `1006fbd0` [+1 dir]; `10070040` [+2 dir]; `100701d0` [+1 dir]; `100b71b0` [+2 dir]
- `runtime/bonus-core/entity-light-cache/a7/declarations/` (runtime-manifest.txt): `1001dd40` [+3 dir]; `1001ddf0`; `1001e830`; `10076c30` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/entity-properties/` (runtime-manifest.txt): `1001de3d`; `100519f0`; `10051ba0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/face-bounds/` (runtime-manifest.txt): `1005b540` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/face-projection/` (runtime-manifest.txt): `1005b4e0`; `101464c0`; `101465e0`
- `runtime/bonus-core/entity-light-cache/a7/gate-producers/` (runtime-manifest.txt): `1000e730`; `10055a50`; `1005e190` [+1 dir]; `1005e610` (native engine.cpp Engine::update_entity_light) [+1 dir]; `1006f8d0`; `10076b70` [+2 dir]; `10077410`
- `runtime/bonus-core/entity-light-cache/a7/global-contact-accept/` (runtime-manifest.txt): `10067cc0` (native engine.cpp Engine::collide)
- `runtime/bonus-core/entity-light-cache/a7/global-contact-postfilter/` (runtime-manifest.txt): `10067ca0`; `100689a0`; `1006ad50`; `1006adc0`
- `runtime/bonus-core/entity-light-cache/a7/global-world-gates/` (runtime-manifest.txt): `10067c50`; `10067e90`
- `runtime/bonus-core/entity-light-cache/a7/hmp-kind/` (runtime-manifest.txt): `1005cdc0`; `1005da80` (native engine.cpp Engine::prepare_query_geometry)
- `runtime/bonus-core/entity-light-cache/a7/initial/` (runtime-manifest.txt): `10078790` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/legacy-move-publication/` (runtime-manifest.txt): `10057210`
- `runtime/bonus-core/entity-light-cache/a7/lightmap-policy/` (runtime-manifest.txt): `1005a520` (native assets.cpp Assets::decode_world) [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/lightmap-producers/` (runtime-manifest.txt): `1001e760`; `1005b800` (native assets.cpp Assets::decode_world); `1005b9a0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/movement-publication/` (runtime-manifest.txt): `100570f0`; `100756e0`; `10075bb0` (native engine.cpp Engine::move_a7_modern)
- `runtime/bonus-core/entity-light-cache/a7/movement-routing/` (runtime-manifest.txt): `1000abd0` (API ent_move; ent_move); `10057460`; `10075e80` (native engine.cpp Engine::move_a7_modern)
- `runtime/bonus-core/entity-light-cache/a7/null-name-publication/` (runtime-manifest.txt): `10010740` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/object-mask/` (runtime-manifest.txt): `1006dfc0`; `10076bf0`; `10076c20`
- `runtime/bonus-core/entity-light-cache/a7/public-movement/` (runtime-manifest.txt): `100580c0` [+1 dir]; `10076180`
- `runtime/bonus-core/entity-light-cache/a7/query-class-types/` (runtime-manifest.txt): `1006dc70` (native engine.cpp Engine::refresh_query_pose) [+2 dir]; `1006e7c0` (native engine.hpp frame) [+2 dir]
- `runtime/bonus-core/entity-light-cache/a7/query-constructors/` (runtime-manifest.txt): `10070600` [+3 dir]; `10072af0` (native engine.cpp Engine::collide) [+3 dir]
- `runtime/bonus-core/entity-light-cache/a7/query-filters/` (runtime-manifest.txt): `10036a00` (native engine.cpp worldModelBuiltin); `1003b510`; `1003bec0` [+1 dir]; `10056010`; `10056b00`; `10077b60`; `10077da0`; `10078dd0`
- `runtime/bonus-core/entity-light-cache/a7/query-kind-getters/` (runtime-manifest.txt): `1006daa0` [+1 dir]; `1006db90` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/query-tail/` (runtime-manifest.txt): `1000ab80` (API trace; trace); `10053490`; `10055b00` (native engine.cpp Engine::sample_texture_hit); `10077320`; `10078190`
- `runtime/bonus-core/entity-light-cache/a7/resources/` (runtime-manifest.txt): `1000aae0` (API c_move; c_move); `100361b0`; `10036210`; `1003ad40`; `1003ae30`; `1003af20`; `1003c170`; `10040cd0` [+1 dir]; `10055410`; `10056f70`; `100777b0` [+1 dir]; `10078b00` (native engine.cpp Engine::collide) [+1 dir]; `100795c0` [+1 dir]
- `runtime/bonus-core/entity-light-cache/a7/shadow-selector/` (runtime-manifest.txt): `1003dad0`; `10049db0` [+1 dir]; `1004c490` [+1 dir]; `1004e1d0`
- `runtime/bonus-core/entity-light-cache/a7/view-range/` (runtime-manifest.txt): `10050720`
- `runtime/bonus-core/entry/consumer-exports/` (survey.txt): `10023b80`; `10028100`
- `runtime/bonus-core/entry/engine-survey/` (survey.txt): `10033bc0` (engine_frame) [+1 dir]; `10033c30` (engine_close) [+1 dir]; `10034fe0` (engine_open) [+6 dir]
- `runtime/bonus-core/entry/exports/` (survey.txt): `1000e260` (native engine.cpp Engine::initialize_globals) [+12 dir]; `100203c0`; `10024860`; `10024e70`; `10024ed0`; `10024f60`; `10025ac0` [+1 dir]
- `runtime/bonus-core/entry/mode-detector/` (survey.txt): `1000e820`
- `runtime/bonus-core/entry/mount-exports/` (survey.txt): `1000e010`; `10019370` [+5 dir]; `10024ac0`; `10024c30`; `10024c70`
- `runtime/bonus-core/entry/options-exports/` (survey.txt): `1000f7d0`; `10017070` (native assets.cpp Assets::load_model) [+1 dir]; `10017100` [+8 dir]; `10020ab0` [+1 dir]; `10024660`
- `runtime/bonus-core/entry/sound-bridge/` (survey.txt): `100086c0` (API snd_create; snd_create); `10025340`
- `runtime/bonus-core/entry/sound-consumer/` (survey.txt): `100232a0` [+1 dir]
- `runtime/bonus-core/entry/sound-lookup/` (survey.txt): `10023140`
- `runtime/bonus-core/load-bonus/boundary/` (manifest.txt): `10001fc0` [+1 dir]; `100155d0` [+3 dir]; `10016110` [+2 dir]; `10017df0` [+4 dir]; `10022ca0` [+3 dir]; `1003a6f0` [+2 dir]; `10053060` [+6 dir]
- `runtime/bonus-core/load-bonus/callback/` (manifest.txt): `100023f0`; `10006dc0` (API level_load; level_load) [+1 dir]; `10011e90` [+5 dir]; `10016300` [+2 dir]; `10019260` [+3 dir]
- `runtime/bonus-core/load-bonus/context/` (manifest.txt): `100149a0` [+1 dir]; `10014a00`; `10015f70` [+3 dir]; `100169a0` [+2 dir]; `1003aae0`; `10064cd0` [+1 dir]
- `runtime/bonus-core/load-bonus/order/` (manifest.txt): `100145c0` (native wdl.cpp Runtime::Impl::capture); `10016060` [+2 dir]; `10016710` (native wdl.cpp Runtime::Impl::tick) [+1 dir]; `10035fc0` [+1 dir]
- `runtime/bonus-core/load-bonus/ui-freeze/` (CONTRACT.txt): `10010bb0` [+1 dir]; `100112d0` [+2 dir]; `100115e0`; `10011620`; `10011670` [+2 dir]; `10011dd0` (native engine.cpp Engine::update_ui) [+4 dir]; `10014400` [+1 dir]; `10015fc0` [+1 dir]; `10016340` [+4 dir]; `10016530` [+5 dir]
- `runtime/bonus-core/load-bonus/ui-freeze/text-input/` (manifest.txt): `100107b0` [+2 dir]; `100120d0`
- `runtime/bonus-core/material-effect/capability/` (runtime-manifest.txt): `1002b450` [+2 dir]; `1002c1d0` [+3 dir]; `1002ddf0` [+4 dir]
- `runtime/bonus-core/material-effect/capability-values/` (runtime-manifest.txt): `10026bd0` [+1 dir]; `1002c0e0` [+3 dir]; `1002c120` [+3 dir]
- `runtime/bonus-core/material-effect/public-parser/` (runtime-manifest.txt): `10019d90`
- `runtime/bonus-core/material-effect/shader-counter/` (runtime-manifest.txt): `10022fe0` [+6 dir]
- `runtime/bonus-core/material-effect/wrapper/` (runtime-manifest.txt): `10007d30` (API effect_load; effect_load; native engine.cpp Engine::builtin); `1001a9c0`; `1001b320`; `10030020` [+1 dir]
- `runtime/bonus-core/persistence/boundaries/` (acknex.dll.manifest.txt): `10025fc0`; `1002b510`; `1002b560` [+1 dir]; `1002b600` [+2 dir]; `1002c280` [+1 dir]; `10043200`; `100627b0` [+2 dir]
- `runtime/bonus-core/persistence/consumers/` (acknex.dll.manifest.txt): `10042980`; `100431c0` [+4 dir]; `10043550` [+3 dir]; `100523e0`
- `runtime/bonus-core/persistence/device/` (acknex.dll.manifest.txt): `1002ba60` [+2 dir]; `1002bc70` [+3 dir]; `1002bdc0`; `1002c310` [+2 dir]; `1002c340` [+3 dir]; `1002dec0` [+4 dir]; `1002df00` [+3 dir]; `100423b0`; `10042570` [+1 dir]; `10042810` [+1 dir]
- `runtime/bonus-core/persistence/frame-order/` (acknex.dll.manifest.txt): `1002c0a0` [+3 dir]
- `runtime/bonus-core/persistence/graphics/` (acknex.dll.manifest.txt): `100425e0`; `10042e70` [+4 dir]; `10043460`; `10044d20`; `10045400` [+1 dir]; `10047490` [+7 dir]; `10052560`; `10052650` [+2 dir]; `10052b70`; `10052bb0` [+2 dir]; `10053790` [+1 dir]; `10053970` [+4 dir]
- `runtime/bonus-core/persistence/parameters/` (acknex.dll.manifest.txt): `1002b390` [+2 dir]; `1002b900`; `1002bc10` [+1 dir]
- `runtime/bonus-core/persistence/present/` (acknex.dll.manifest.txt): `1002bf10` [+1 dir]; `1002bfa0` [+3 dir]; `1002c350` [+1 dir]
- `runtime/bonus-core/persistence/presentation-order/` (acknex.dll.manifest.txt): `1002b380`; `1002b9d0`
- `runtime/bonus-core/persistence/retention/` (acknex.dll.manifest.txt): `1002b3e0` [+1 dir]; `1002b4d0`; `1002c2f0` [+1 dir]; `10061f40` [+1 dir]; `10062370` [+3 dir]
- `runtime/bonus-core/persistence/swapchain/` (acknex.dll.manifest.txt): `100620f0` [+1 dir]; `10062260` [+1 dir]; `10062320`; `10062470` [+1 dir]; `10062590` [+2 dir]
- `runtime/bonus-core/predefined-surface/constant-resolver/` (manifest.txt): `100016f0` [+2 dir]; `10001d10`; `10002960`; `10005d50` (API session_connect; session_connect) [+7 dir]; `10013250` [+1 dir]; `10013430` [+1 dir]; `10013720`; `10014460` [+1 dir]; `10016cb0` [+7 dir]; `10016cf0` [+7 dir]; `1001aa60`; `1001c640` [+7 dir]; `1001f3c0` [+1 dir]; `10022ec0` [+4 dir]; `10025740` [+2 dir]; `1002aa90`; `10035cc0` [+7 dir]; `10038580` [+7 dir]; `10163a90`
- `runtime/bonus-core/predefined-surface/descriptor/` (manifest.txt): `10005dd0` (API session_open; session_open); `10008800` [+3 dir]; `100088f0` (API media_playing; media_playing); `10008910` (API media_start; media_start); `10008930` (API media_pause; media_pause); `10008950` (API media_stop; media_stop); `10008970` (API media_tune; media_tune); `10019610` [+1 dir]; `10019710`; `1002ee70`; `10033e70` [+1 dir]; `10034360` [+4 dir]; `100353c0`; `10035400` [+2 dir]; `10035e50` [+2 dir]; `10036540` [+1 dir]; `10036c00` [+2 dir]; `10036cb0` [+1 dir]; `10036d70` [+1 dir]; `10036fd0` [+1 dir]; `10037080` [+1 dir]; `10037170` [+1 dir]; `10037540` [+1 dir]; `10039d60` [+3 dir]; `10059b70`
- `runtime/bonus-core/predefined-surface/initial/` (manifest.txt): `10001bb0`; `10002840`; `10003280` [+1 dir]; `10005fd0` (API str_cpy; str_cpy) [+1 dir]; `100061c0` (API str_cat; str_cat) [+1 dir]; `10008df0` (API file_str_readto; file_str_readto) [+1 dir]; `10009400` (API txt_load; txt_load) [+1 dir]; `100094d0` (API txt_for_dir; txt_for_dir) [+1 dir]; `100127e0` [+1 dir]; `100132d0` [+1 dir]; `10013770`; `1001db40`; `10021a30` [+1 dir]; `10023ca0` [+3 dir]; `100241f0` [+6 dir]; `10025430` [+2 dir]; `10025520` [+2 dir]; `10025b10` [+2 dir]; `10025b30` [+2 dir]; `10025c30` [+1 dir]
- `runtime/bonus-core/predefined-surface/predefines/` (manifest.txt): `10022e10` [+1 dir]; `1002593c`; `10025a60` [+1 dir]; `101639f0`
- `runtime/bonus-core/predefined-surface/registration/` (manifest.txt): `1000f890` [+2 dir]; `1000fb60` [+1 dir]; `1000fd70` [+1 dir]; `1000ffb0` [+1 dir]; `10010190` [+1 dir]; `100103a0` [+1 dir]; `10011000` [+1 dir]; `10011ff0` [+3 dir]; `10013c03`; `10035550` [+1 dir]; `10035ab0` [+1 dir]; `10035d70`; `100365c0`; `100377a0` [+1 dir]; `10042610` [+1 dir]; `10043940` [+2 dir]; `10044d70` [+3 dir]; `10044ee0` [+3 dir]; `100450b0` [+1 dir]; `10045d90` [+5 dir]; `10146d24` (PE entry); `10163e20`
- `runtime/bonus-core/predefined-surface/token-lifetime/` (manifest.txt): `10003d40`; `10006740` (API ptr_first; ptr_first); `100201b0`; `10021980`; `100219c0`; `10025660`; `100256c0` [+2 dir]; `10025710`; `10145306` (__cinit)
- `runtime/bonus-core/primitives/callees/` (runtime-manifest.txt): `10013620`; `1001dab0` [+9 dir]; `100616d0`; `10061710`; `1014576c` (_free) [+1 dir]
- `runtime/bonus-core/primitives/details/` (runtime-manifest.txt): `1001da70` [+2 dir]; `10025420`; `10030060`; `1006dad0` [+1 dir]; `1006db70` [+1 dir]
- `runtime/bonus-core/primitives/exports/` (runtime-manifest.txt): `100049b0` (API fraction/FRC; fraction); `10005110` (API vec_lerp; vec_lerp); `10005f80` (API str_create; str_create); `10005fb0` (API str_remove; str_remove); `10006110` (API str_to_asc; str_to_asc); `100063e0` (API str_trunc; str_trunc); `10006680` (API handle; handle); `10006690` (API ptr_for_handle; ptr_for_handle); `10007170` (API sys_exit; sys_exit) [+1 dir]; `1000aa10` (API c_setminmax; c_setminmax) [+1 dir]
- `runtime/bonus-core/primitives/fields/` (runtime-manifest.txt): `10163540`
- `runtime/bonus-cursor/boundaries/` (source-manifest.txt): `1000f1d0` [+2 dir]; `100270a0` [+5 dir]; `10027740` [+3 dir]; `10034ee0` [+2 dir]
- `runtime/bonus-cursor/device-active/` (source-manifest.txt): `1002b360` [+1 dir]; `1002db20` [+1 dir]; `1002db80` [+2 dir]; `10062300` [+1 dir]
- `runtime/bonus-cursor/gamma/` (source-manifest.txt): `10042240` [+2 dir]
- `runtime/bonus-cursor/gamma-binding/` (source-manifest.txt): `1002dbb0` [+1 dir]; `1002dbe0`; `1002dda0` [+2 dir]
- `runtime/bonus-cursor/gamma-final/` (source-manifest.txt): `10026bf0` [+1 dir]; `10026cd0` [+1 dir]; `10027f90` [+3 dir]; `1002ce00` [+1 dir]; `1002cfb0` [+1 dir]; `1002d0c0` [+1 dir]; `1003bb80`
- `runtime/bonus-cursor/gamma-ramp/` (source-manifest.txt): `1002dc10` [+1 dir]; `1002dd50` [+1 dir]; `1002e760` [+4 dir]
- `runtime/bonus-cursor/gamma-sink/` (source-manifest.txt): `10009650` (API draw_begin; draw_begin); `100460b0` [+1 dir]; `10046240` [+1 dir]
- `runtime/bonus-cursor/input-final/` (source-manifest.txt): `10026b80`; `10026ca0`; `10026cb0`; `10027520`; `1002ba10`; `1002bba0`; `1002be50`; `1002bf90`; `1002c0b0`; `1002d6f0`; `1002d790`; `1002d840`; `1002e1e0` [+2 dir]; `100628c0` [+1 dir]
- `runtime/bonus-cursor/ordering/` (source-manifest.txt): `1000e230` [+1 dir]; `1000e900`; `10029c70` [+1 dir]; `10029e10` [+1 dir]; `100425a0` [+1 dir]; `10042a00` [+1 dir]; `10043760` [+1 dir]; `100437b0` [+1 dir]; `100437f0` [+2 dir]; `100438c0` [+1 dir]; `10045920` [+2 dir]; `10045fa0`; `10045fc0` [+1 dir]; `10046040` [+2 dir]; `100488f0` [+3 dir]; `10102b40`; `10103380`; `10103470`
- `runtime/bonus-cursor/packed-format/` (source-manifest.txt): `10007c60` (API bmap_to_alpha; bmap_to_alpha); `100120f0` (native media.cpp duration) [+1 dir]; `10012470` (native assets.cpp Assets::image_ptr) [+1 dir]; `10012b30` [+1 dir]; `1002c610`; `100601c0` [+1 dir]; `100603a0` [+1 dir]; `10060ac0` [+1 dir]; `100613f0` [+1 dir]
- `runtime/bonus-cursor/presentation/` (source-manifest.txt): `100086a0` (API ent_purge; ent_purge); `100086b0` (API ent_preload; ent_preload); `10026e60` [+1 dir]; `1003b860` [+1 dir]; `1003b940`; `1003bad0`; `100429c0`; `10042a70`; `10043510`; `10043630`; `10043690`; `10043710`; `10043860`; `100449f0` [+1 dir]; `10048070` [+2 dir]; `100498b0`; `10049930`
- `runtime/bonus-cursor/source/` (source-manifest.txt): `100073e0` (API bmap_lock; bmap_lock); `10015460`; `1002e070`; `1002e0b0`; `100340d0`; `10034130`; `10034190`; `10034200`; `10034260` [+1 dir]; `100463a0` [+2 dir]
- `runtime/bonus-model-layout/layout/` (runtime-manifest.txt): `10158b7a`; `10158c22`; `10158cd1`; `10158d4f`; `10158e01`; `10158eb0`
- `runtime/bonus-physics/adapters/` (runtime-manifest.txt): `100164c0`; `10029c90`; `10035460` [+1 dir]; `100356a0`; `100357c0`; `10035800`; `10035840`; `100358a0`; `10035900`; `10035970`; `100359f0` [+1 dir]; `10037860`; `10037890`; `1007ab80` (native physics.cpp Physics::collect_contact) [+6 dir]; `100b3500` [+3 dir]; `100b5090` [+5 dir]; `100b50c0` [+5 dir]; `1014264f` (__stricmp) [+1 dir]; `10143862` (FID_conflict___time32); `10144fad` (_localtime)
- `runtime/bonus-physics/api/` (runtime-manifest.txt): `10009f40` (API phent_settype; phent_settype); `10009fc0` (API phent_setmass; phent_setmass) [+1 dir]; `1000a040` (API phent_setelasticity; phent_setelasticity); `1000a120` (API phent_setfriction; phent_setfriction); `1000a320` (API phent_setdamping; phent_setdamping); `1000a430` (API phent_enable; phent_enable); `1000a490` (API phent_setgroup; phent_setgroup); `1000a920` (API phcon_add; phcon_add); `1000ae90` (API phent_addcentralforce; phent_addcentralforce); `1000b010` (API phent_addvelglobal; phent_addvelglobal); `1000b150` (API phent_addvellocal; phent_addvellocal); `1000b600` (API ph_setgravity; ph_setgravity); `1000b680` (API phcon_setparams1; phcon_setparams1); `1000b7f0` (API phcon_setparams2; phcon_setparams2); `1000bdf0`; `1000be20`; `1000be50` [+2 dir]; `1003ac10` [+11 dir]; `10065fe0` [+2 dir]; `10065ff0` [+6 dir]; `10066000` [+10 dir]; `10066050` [+6 dir]; `10066490` [+5 dir]; `100667e0` [+7 dir]; `10066ba0` [+3 dir]; `10066ca0` [+2 dir]; `10066e70` [+2 dir]; `10068b90` [+2 dir]; `1006ae50` [+2 dir]; `1006aed0` [+3 dir]; `1006b110`; `1006b3e0` [+1 dir]; `1006b410`; `1006b5d0` [+3 dir]; `1006b8a0`; `1006bc60` [+1 dir]; `1006bc90`; `1006bcd0`; `1006bef0` [+1 dir]; `1006bfa0`; `1006c230`; `1006c250` (thunk_FUN_1006ae90); `1006c260` [+4 dir]; `1006c300`; `1006c640`; `10070440` [+7 dir]; `10078f90` [+5 dir]; `10079950` [+10 dir]; `10079bf0` [+2 dir]; `10079c40` [+4 dir]; `1007a290` [+1 dir]; `1007a340` [+2 dir]; `1007a4a0`; `1007cb40` [+5 dir]; `1007cd20` [+4 dir]; `1007d210` [+1 dir]; `10084990`; `10084a70`; `10085600` [+1 dir]; `100aff50` [+2 dir]; `100affb0` [+4 dir]; `100b0100` [+4 dir]; `100b0120` [+2 dir]; `100b0480` [+3 dir]; `100b04e0`; `100b0500` [+2 dir]; `100b0520`; `100b05b0`; `100b06f0` [+5 dir]; `100b0860` [+2 dir]; `100b0d70` [+3 dir]; `100b1320` [+4 dir]; `100b1710` [+1 dir]; `100b1d70` [+5 dir]; `100b5730`; `100bc080` [+5 dir]; `100bec10` [+2 dir]; `100bef30` [+3 dir]; `100bef60` [+1 dir]; `10106e00` [+3 dir]; `10106fd0` [+3 dir]; `10142422` (operator_new) [+5 dir]; `10142427` (operator_new) [+10 dir]; `1014250a` [+6 dir]; `101439e0` (_memmove) [+5 dir]; `10145858` (__nh_malloc) [+4 dir]; `10145884` (_malloc) [+6 dir]; `101458aa` (_free) [+9 dir]; `1014598f` (_atexit) [+3 dir]; `10146271` (__CxxThrowException_8) [+4 dir]; `101462bc` (exception) [+3 dir]; `101462f9` (exception) [+3 dir]; `10148b64` (__callnewh) [+7 dir]
- `runtime/bonus-physics/boundary/` (runtime-manifest.txt): `100051e0` (API accelerate; accelerate); `10005350` (API vec_accelerate; vec_accelerate); `10014900`; `10014e80`; `10015410`; `100165d0`; `1001b620`; `10021200`; `100256b0`; `10035d40`; `10036bb0`; `10041da0`; `10065b20`; `10066f60` [+5 dir]; `1006c9b0` (native physics.cpp Physics::reduce_contacts) [+2 dir]; `1006cbd0` [+2 dir]; `1006cc80` (native physics.cpp Physics::reduce_contacts) [+2 dir]; `1006d4c0` [+2 dir]; `1006d5c0` [+2 dir]; `1006d770` [+2 dir]; `1007a540` [+6 dir]; `1007aec0` [+6 dir]; `1007af60` [+5 dir]; `1007b470` [+4 dir]; `1007c050` [+5 dir]; `1007d6b0` [+1 dir]; `100affc0` [+3 dir]; `100b1770` [+4 dir]; `10142f60`; `101435e4` [+7 dir]
- `runtime/bonus-physics/contact-allocation/` (runtime-manifest.txt): `10065c50` [+1 dir]; `10067e80` (thunk_FUN_10067e10) [+2 dir]; `1006ab10` [+4 dir]; `1006b2b0` [+3 dir]; `1006e510` [+2 dir]; `10079990` [+6 dir]; `100799c0` [+6 dir]; `10079b90` [+3 dir]; `10079f80` [+4 dir]; `1007c1a0` [+5 dir]; `1007ce30` [+3 dir]; `10087c60` [+3 dir]; `100b0770` [+4 dir]; `100b12f0` [+3 dir]; `101457dd` (__heap_alloc) [+1 dir]; `101469b0` [+2 dir]
- `runtime/bonus-physics/contact-defaults/` (runtime-manifest.txt): `1000f790`; `1000fa50`; `10014a70` [+1 dir]; `10014b70`; `10021d70`; `10030090`; `10066010` [+6 dir]; `10066170` [+5 dir]; `10066850` [+4 dir]; `10079930` [+1 dir]; `10079cf0` [+1 dir]; `1007aac0` [+4 dir]; `1007bf20` [+3 dir]; `1007c6e0` [+5 dir]; `1007c9d0` [+4 dir]; `100856a0` [+1 dir]; `100b0110` [+1 dir]; `100b08a0` [+5 dir]; `100b08c0` [+5 dir]; `100b0930` [+3 dir]; `100b0950` [+3 dir]; `100b0970` [+3 dir]; `100b0990` [+3 dir]; `100b09a0` [+4 dir]; `100b09d0` [+1 dir]; `100b0a10` [+2 dir]; `100b1270` [+1 dir]; `100b7140` [+2 dir]; `100b71c0` [+2 dir]; `101456aa` (_free) [+4 dir]; `10146b8d` (__controlfp) [+2 dir]
- `runtime/bonus-physics/contact-lifetime/` (contract.txt): `10008680` (API ent_remove; ent_remove); `1000a510` (API ph_setcorrections; ph_setcorrections); `1000a730` (API ph_setcontacts; ph_setcontacts); `1000f2e0`; `10035590`; `100359b0`; `10065c80`; `10065cb0`; `10065cc0`; `1006bf60` [+1 dir]; `1006cc00` [+3 dir]; `1006e630` [+1 dir]; `1007ca30` [+3 dir]; `1007cf00` [+6 dir]; `10083600` [+1 dir]; `10087290` [+1 dir]; `100b07e0` [+1 dir]; `100b14b0` [+1 dir]; `100bc150` (thunk_FUN_100b1760) [+2 dir]; `100bce50` [+1 dir]; `1014a965` (__fload_withFB) [+5 dir]
- `runtime/bonus-physics/contact-reduction/batch/` (acknex.dll.manifest.txt): `10087e70` (native physics.cpp Physics::load_world)
- `runtime/bonus-physics/contact-reduction/continuations/` (acknex.dll.manifest.txt): `1006cf60` (native physics.cpp Physics::reduce_contacts) [+2 dir]; `1006d6df` [+1 dir]
- `runtime/bonus-physics/contact-reduction/crt/` (acknex.dll.manifest.txt): `101461c0` (_fabs) [+4 dir]; `10150bca` (__umatherr) [+1 dir]; `10150c68` (__handle_qnan1) [+1 dir]; `10150d1a` (__except1) [+1 dir]; `10150ec5` (__sptype) [+1 dir]; `10150ff3` (__ctrlfp) [+1 dir]; `10154d78`
- `runtime/bonus-physics/contact-reduction/exact/` (acknex.dll.manifest.txt): `1006c770` [+1 dir]; `1006c7d0` [+1 dir]; `1006cec0` [+1 dir]; `1006d2a0` (native physics.cpp Physics::reduce_contacts) [+3 dir]; `1006d300` [+2 dir]; `1006d6c0` [+1 dir]; `1006d700` [+1 dir]; `1006d950` [+1 dir]; `10078f60` [+4 dir]; `10079940` [+2 dir]; `10079960` [+2 dir]; `1007ada0` [+3 dir]; `10087740` [+1 dir]; `100b12c0` [+3 dir]
- `runtime/bonus-physics/contact-reduction/functions/` (runtime-manifest.txt): `100401d0` [+2 dir]; `10040300` [+2 dir]; `100b0540` [+1 dir]; `101454bd` [+1 dir]; `10146ec2` (__security_check_cookie); `101506ac` (__raise_exc); `10150951` (__handle_exc); `10150b75` (__set_errno); `10150b9d` (__errcode)
- `runtime/bonus-physics/contact-reduction/numeric/` (acknex.dll.manifest.txt): `10068ca0` [+1 dir]; `1006a290` [+1 dir]
- `runtime/bonus-physics/contact-reduction/ownership-complete/` (acknex.dll.manifest.txt): `1006cf0d`; `1006cf41`; `1006d283`; `1006d33a`; `1006d353`; `1006d6e5`; `1006d893`; `1006d9e3`; `1007ae73`
- `runtime/bonus-physics/contact-reduction/query/` (acknex.dll.manifest.txt): `10071920` [+1 dir]; `10073720` [+1 dir]; `1007bd00` [+2 dir]
- `runtime/bonus-physics/contact-reduction/scratch/` (acknex.dll.manifest.txt): `100690c0`; `1006a0a0`; `1006a230`
- `runtime/bonus-physics/contact-reduction/setup/` (acknex.dll.manifest.txt): `1006aa80`; `100b14e0` [+2 dir]
- `runtime/bonus-physics/contact-world-settings/` (runtime-manifest.txt): `100b09c0` [+2 dir]
- `runtime/bonus-physics/edges/` (runtime-manifest.txt): `1000f690`; `10023a10`; `1002ac30`; `10035d30`; `1004de60` [+1 dir]; `10059850` [+2 dir]; `10059a40`; `1006e520`
- `runtime/bonus-physics/frame/` (runtime-manifest.txt): `1006dc30`; `1006df90` [+3 dir]; `1006e680`; `1006e6e0`; `10076b10` [+1 dir]; `1007a6a0` (native physics.cpp Physics::step) [+3 dir]; `1007c480`; `10087000` [+4 dir]; `10146405` [+3 dir]
- `runtime/bonus-physics/geometry-contract/backend/` (runtime-manifest.txt): `10066eb0` [+1 dir]; `10067570` [+3 dir]; `1006c690`; `1006c700`; `1006df50` [+1 dir]; `1006e130` [+1 dir]; `1006e150` (native engine.cpp Engine::synchronize_query_body); `1006e390` [+2 dir]; `1006f5f0`; `1006f600`; `1006fe40`; `1006ff40`; `10070320` [+1 dir]; `10070520` [+2 dir]; `100705c0`; `10070660`; `10070c40` (native engine.cpp Engine::refresh_query_pose) [+1 dir]; `100719a0`; `10071ac0`; `10071fc0` [+1 dir]; `10072220` [+1 dir]; `10072550` [+1 dir]; `100726b0`; `100729f0`; `10072a60`; `100affd0` [+7 dir]; `100b0020` [+3 dir]; `100b1750` [+3 dir]; `100b1760` [+2 dir]; `100b1780` [+6 dir]; `100b17c0` [+4 dir]; `100b1800` [+2 dir]; `100b7090` [+1 dir]; `100b7340` [+3 dir]; `100b79e0` [+2 dir]; `100b7a30` [+2 dir]; `100b7d40` [+3 dir]; `100b7dc0`; `100b7e90`; `100b9d10`; `100b9e20` [+2 dir]; `100b9e70`; `100be440` [+1 dir]; `100be740` [+2 dir]; `100be800` [+2 dir]; `100bedf0` [+2 dir]; `100d6740` [+1 dir]; `10142ec4` [+2 dir]; `10143534` [+4 dir]
- `runtime/bonus-physics/geometry-contract/bounds/` (runtime-manifest.txt): `1002d490`; `1004d150` [+1 dir]; `1004d430` [+1 dir]; `1004d850` [+1 dir]; `100675d0` [+1 dir]; `10067640` [+2 dir]; `10067820` [+2 dir]; `1006f650` [+1 dir]; `10070140`; `10070850`; `10072130` [+1 dir]; `10072500`; `10072530` [+1 dir]; `100885e6` (D3DXCreateMeshFVF); `100b3490` [+1 dir]; `100b4ab0` [+2 dir]; `100b4bb0` [+2 dir]; `100be590`; `100d2100`; `100d2230`; `100d2250`; `100d46e0`; `100d4910`; `10142e90` [+1 dir]; `101463e4` (__copysign) [+1 dir]; `1014afcb` (__ctrandisp2) [+1 dir]
- `runtime/bonus-physics/geometry-contract/creation/` (runtime-manifest.txt): `10065ea0` [+1 dir]; `10066070`; `10083800`; `100b00d0` [+1 dir]; `100b0d40` (native physics.cpp Physics::copy_pose) [+1 dir]; `10142be4` [+1 dir]
- `runtime/bonus-physics/geometry-contract/initial/` (runtime-manifest.txt): `1006dbc0`; `1006dbe0`; `100741b0`; `10079970` [+2 dir]; `100870d0` (native physics.cpp Physics::load_world) [+1 dir]; `100872b0` [+1 dir]; `10087560` [+1 dir]; `10087f60` [+1 dir]; `100b0620`; `10147014` (_calloc); `10149282`
- `runtime/bonus-physics/geometry-contract/population/` (runtime-manifest.txt): `1003fe30`; `1003feb0`; `1003ff4e`; `100400e0`; `100402c0`; `1006da60` [+2 dir]; `1006e740` [+1 dir]; `1006eb50`; `1006ec60`; `1006f610`; `10070580`; `100705a0`; `1014244b`; `1014248b`; `101452dc`; `1014584f`; `101458af` (__onexit_lk); `10145957` (__onexit); `10145989`; `101462ab`; `101481b0`; `101482b0` (_strlen); `10148b80`; `1014a69c` (__SEH_prolog); `1014a6d7` (__SEH_epilog); `1014e432` (__lock); `1014f7b6` (___sbh_alloc_block)
- `runtime/bonus-physics/geometry-contract/pose/` (runtime-manifest.txt): `100405d0`; `10054f80`; `1006af20` [+1 dir]; `1006b790` [+1 dir]; `100703a0` [+1 dir]; `10079570`
- `runtime/bonus-physics/geometry-contract/transforms/` (runtime-manifest.txt): `1006dbf0`; `1006e450`; `100704c0`; `100705d0`; `10070a80`; `10070d80` [+1 dir]; `10072420`; `10077650`; `10143744`; `1014a757` (__trandisp2); `1014b013`; `1014b194` (__fload)
- `runtime/bonus-physics/geometry-contract/world/` (runtime-manifest.txt): `10065d20`; `10065e70` [+1 dir]; `10066710` [+1 dir]; `10066d00` [+2 dir]; `10067e10`; `100744f0`; `100b00a0` [+1 dir]; `100b0130`; `100b0a50` [+1 dir]; `100b0bd0` [+1 dir]; `100b1110`
- `runtime/bonus-physics/global-binding/core/` (address-evidence.txt): `1000f260`
- `runtime/bonus-physics/global-binding/lookup/` (address-evidence.txt): `10013100`; `10013210`
- `runtime/bonus-physics/lifecycle/` (runtime-manifest.txt): `10066500` [+1 dir]; `100665c0` (native physics.cpp Physics::step); `100667a0` [+2 dir]; `10066c30`; `1006bdf0`; `1006be30`; `1006e5a0`; `1006e5d0`; `1006e8b0`; `1007a0a0`; `1007a100`; `1007c490`; `1007ca70`; `1007d450`; `100b0140`; `100b01c0` [+1 dir]; `100b0240` [+1 dir]; `100b0880`; `100b08b0`; `100b08d0`; `100b0920`; `100b0940`; `100b0960`; `100b0980`; `100b0fb0` [+1 dir]
- `runtime/bonus-physics/mass-zero/` (runtime-manifest.txt): `10065e30` (native physics.cpp Physics::wake_query_body) [+1 dir]; `10065fb0` [+1 dir]; `100672a0` [+1 dir]; `100799f0` [+1 dir]; `100af850` [+1 dir]; `100afe30` [+1 dir]; `100b0150` [+1 dir]
- `runtime/bonus-physics/mass-zero/body/` (runtime-manifest.txt): `100b02e0`; `100bde50` [+3 dir]; `100be4d0` [+2 dir]
- `runtime/bonus-physics/mass-zero/factorization/` (runtime-manifest.txt): `100bdd30`
- `runtime/bonus-physics/mass-zero/guards/` (runtime-manifest.txt): `10065e10`; `10065fa0`; `10067270`; `100bf120`; `100bf210`
- `runtime/bonus-physics/mass-zero/linkage/` (runtime-manifest.txt): `10065b50` [+1 dir]; `10065cd0`; `10066380`; `100b0410`
- `runtime/bonus-physics/mass-zero/step/` (runtime-manifest.txt): `100bf4d0`; `100bfaf0`; `100bfdb0` [+1 dir]; `100c11a0` (thunk_FUN_100bfdb0) [+1 dir]; `100c1c00`
- `runtime/bonus-physics/mass-zero/thread-boundaries/` (runtime-manifest.txt): `100afd20`
- `runtime/bonus-physics/mass-zero/visible/` (runtime-manifest.txt): `100b0dd0`
- `runtime/bonus-physics/mass-zero/world/` (runtime-manifest.txt): `100afeb0`; `100b01a0` [+1 dir]; `100b03d0` [+1 dir]; `100b0400` [+1 dir]; `100b0420`; `100b08e0` [+1 dir]; `100b0900`
- `runtime/bonus-physics/mass-zero/writeback/` (runtime-manifest.txt): `10066840` [+1 dir]; `10066a50`
- `runtime/bonus-physics/solver/` (runtime-manifest.txt): `10065bc0`; `10065c90`; `10066680`; `10066e50`; `10066f20`; `100670c0`; `10067110`; `10067520`; `1006b7f0`; `1006e850`; `10072fb0`; `100af880`; `100af950`; `100af9d0`; `100afa30`; `100afaa0`; `100b2dc0`
- `runtime/bonus-physics/world-contact-boundary/boundary/` (acknex.dll.manifest.txt): `100cbb20`
- `runtime/bonus-physics/world-contact-boundary/contact-rows/` (acknex.dll.manifest.txt): `100b5fa0`; `100b6170`
- `runtime/bonus-physics/world-contact-boundary/kernel/` (acknex.dll.manifest.txt): `10088300`; `100b7a00`
- `runtime/bonus-physics/world-contact-boundary/narrowphase/` (acknex.dll.manifest.txt): `100b1e80`; `100c9c30`; `100caac0`; `10146b5b` (__control87)
- `runtime/bonus-physics/world-contact-boundary/precision-normal/` (acknex.dll.manifest.txt): `100c95a0`; `100c9a40`; `100c9b00`; `10146a3b` (__abstract_cw); `10146acd` (__hw_cw)
- `runtime/bonus-physics/world-contact-boundary/rows/` (acknex.dll.manifest.txt): `100b35d0`; `100b3680`
- `runtime/graphics-settings-source/a7-admission/` (acknex.dll.manifest.txt): `1003e700`; `1003f540` [+1 dir]
- `runtime/graphics-settings-source/a7-device/` (acknex.dll.manifest.txt): `10061ee0`; `10062c60`
- `runtime/graphics-settings-source/a7-light/` (acknex.dll.manifest.txt): `1003ea30`; `1004f140`
- `runtime/graphics-settings-source/a7-light-type/` (acknex.dll.manifest.txt): `1003e320` [+1 dir]; `1003e740` [+1 dir]; `10040650` [+1 dir]
- `runtime/graphics-settings-source/a7-producers/` (acknex.dll.manifest.txt): `1003dbb0`; `1003e130`; `1003e2b0`; `1003e410`; `1003e4b0`; `1003ec70` [+1 dir]; `10044230`; `10050370`; `100618e0`; `10061b60`; `100621c0`; `100653e0`
- `runtime/graphics-settings-source/a7-topology/` (acknex.dll.manifest.txt): `10049550`
- `runtime/graphics-settings-source/a7-worldlight-source/activation/` (acknex.dll.manifest.txt): `10036350` [+1 dir]; `1003e930` [+1 dir]; `1003f7f0` [+1 dir]; `10058f30`
- `runtime/graphics-settings-source/a7-worldlight-source/admission/` (acknex.dll.manifest.txt): `1003f470`; `1003f4f0`
- `runtime/graphics-settings-source/a7-worldlight-source/lifecycle/` (acknex.dll.manifest.txt): `100137f0`; `10013860`; `100138c0`; `1003e5c0`; `100501f0`; `10050290`; `10059d40`; `1005c2d0`
- `runtime/graphics-settings-source/a7-worldlight-source/mode-fields/` (acknex.dll.manifest.txt): `1005b1c0`
- `runtime/graphics-settings-source/a7-worldlight-source/slots/` (acknex.dll.manifest.txt): `1003e540` (native engine.cpp Engine::make_frame); `1003e600`; `1003e9c0`; `1003eef0`; `1003f890`
- `runtime/material-renderer/a7-ambient/` (acknex.dll.manifest.txt): `1003dfc0`
- `runtime/material-renderer/a7-draw/` (acknex.dll.manifest.txt): `1003df70`
- `runtime/material-renderer/a7-external-bitmap/` (acknex.dll.manifest.txt): `10012d60`
- `runtime/material-renderer/a7-external-bitmap/helpers/` (acknex.dll.manifest.txt): `10060c60`; `10060d60`; `100613b0`; `10061470`; `100614b0`
- `runtime/material-renderer/a7-external-bitmap/pixels/` (acknex.dll.manifest.txt): `100602c0`; `100607b0`; `100608c0`
- `runtime/material-renderer/a7-gates/` (acknex.dll.manifest.txt): `10041f10`; `10042900`
- `runtime/material-renderer/a7-lighting/` (acknex.dll.manifest.txt): `1003df40`; `1003e0d0`; `10042dc0`; `100457d0`
- `runtime/material-renderer/a7-movie-bitmap/` (acknex.dll.manifest.txt): `100088b0` (API media_play; media_play); `100088d0` (API media_loop; media_loop)
- `runtime/material-renderer/a7-particles/` (acknex.dll.manifest.txt): `1004dd90`
- `runtime/planar-shadow-source/a7/` (acknex.dll.manifest.txt): `100530e0`
- `runtime/planar-shadow-source/a7/edition-selection/` (acknex.dll.manifest.txt): `10024650`
- `runtime/planar-shadow-source/a7/edition-selector/` (acknex.dll.manifest.txt): `100245e0`
- `runtime/planar-shadow-source/a7/lod-writer/` (acknex.dll.manifest.txt): `10051a90` [+1 dir]; `10051b60` [+1 dir]
- `runtime/planar-shadow-source/a7/numeric/` (acknex.dll.manifest.txt): `1001a7a0`; `10049980`
- `runtime/planar-shadow-source/a7/planar-draw/` (acknex.dll.manifest.txt): `1004ca00`
- `runtime/planar-shadow-source/a7/resource-birth/` (acknex.dll.manifest.txt): `10049d70`
- `runtime/planar-shadow-source/a7/sun/` (acknex.dll.manifest.txt): `10040f70`
- `runtime/planar-shadow-source/a7/sun-numeric/` (acknex.dll.manifest.txt): `101436a4`
- `runtime/planar-shadow-source/a7/visible-lod/` (acknex.dll.manifest.txt): `10053e30`

### K22_mode.exe

- `runtime/bonus-core/entry/stub-exports/` (survey.txt): `00401000`; `0040105b` (PE entry)
- `runtime/bonus-core/entry/survey/` (survey.txt): `00401024` (engine_close); `0040102a` (engine_frame); `00401030` (engine_open)

## 6. Gaps and discrepancies found while building this map

- `0055c152` (system-error-name mapping) never decompiled: every run timed out (`final/K22.exe.recovery.txt` status FAILED timed_out=true). **CONFIRMED** gap.
- `00402530` (action-name resolver) and `00470a50` (modern trace hull builder) are cited in CONTRACT.txt but have no `.c` export; their parameters are **UNKNOWN**.
- `0044b770` (frame step skipped under FREEZE_MODE>=1) has an export but its purpose is not described anywhere. **UNKNOWN**.
- FLOATR `00406fe0` and FLOATD `00407010` are recovered handlers whose names are present in `native/native_calls.inc`, but no native implementation branch was found in `engine.cpp`/`wdl.cpp`. **CONFIRMED** by search of current source.
- Native fixed-point `/` and `%` throw on a zero divisor (`wdl.cpp` `Runtime::Impl::evaluate`), whereas `0040aba0` emits an unguarded IDIV (x86 divide exception; engine policy unrecovered, `CONTRACT.txt:184-185`). **CONFIRMED** difference.
- Native `str_clip` throws on negative counts; original `00404780` has no clamp (undefined memory behavior, `CONTRACT.txt:562-568`). **CONFIRMED** difference (deliberate).
- Native `Assets::mount` accepts WRS2 (0xa5) and WRS3 (0xb5) only; original `0041fd20` also accepts a legacy 12-char/XOR-0 index. WRS3 is the Bonus archive format (`Kabus 22 Mode/K22_mode.wrs` begins with bytes `WRS3`; campaign `K22.wrs` begins `WRS2`, read from the extracted install); the acknex.dll loader that uses the 0xb5 key is not mapped here. **CONFIRMED** difference; A7 loader address **UNKNOWN**.
- CONTRACT §18 (L705-709) records native gaps (VECTOR allocation, left-to-right call args) that current source has closed: `Engine::service_builtin` uses a 64-entry `vectorTemps_` ring and `Runtime::Impl::evaluate` evaluates call arguments from last to first. **CONFIRMED** by reading current source.
- acknex.dll (A7) has 1314 exports, mostly unannotated here beyond directory topic; their contracts live in the per-directory `contract.txt`/manifest files.
- Callers/callees are export-local; a full call graph would need a new read-only Ghidra pass.

