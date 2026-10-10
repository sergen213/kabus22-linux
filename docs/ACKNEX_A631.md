# Acknex A6.31 plugin interface and its native equivalents

How the Kabus 22 campaign engine (A6 6.31.4, `K22.exe`) loads and calls plugin DLLs, what the community patch's `kabus_dll.dll` and the original `RenkAyar.dll` do through that interface, and where the native engine implements the same services.

Labels: **CONFIRMED** / **HIGH CONFIDENCE** / **HYPOTHESIS** / **UNKNOWN**. Related: [COMMUNITY_PATCH_ANALYSIS](COMMUNITY_PATCH_ANALYSIS.md) · [PATCH_BINARY_DIFF](PATCH_BINARY_DIFF.md) · [reverse-engineering/SCRIPT_SYSTEM](reverse-engineering/SCRIPT_SYSTEM.md) · [ENGINE_ARCHITECTURE](ENGINE_ARCHITECTURE.md).

Sources: Conitec plugin SDK headers as shipped in the author's repository (`reverse/community/iseeeva-kabus22/kabus_vs/Kabus.Acknex.Plugin/Acknex/include/{adll,afuncs,atypes,avars}.h`, `Acknex/lib/adll.lib`, `readme_acknex.txt`); plugin source `…/Index.hpp`, `Utils/**`; recovered engine contract `reverse/decompiled/runtime/CONTRACT.txt` §5, §17; `reverse/decompiled/RenkAyar.behavior.txt`; native `native/engine.cpp`, `native/wdl.cpp`, `native/check_engine.cpp`. All private except `native/`.

## 1. Entry points and registration

| Element | Original A6 6.31 | Label |
|---|---|---|
| Plugin discovery | At start-up, after script compilation, the engine enumerates `*.dll` in `PLUGINDIR` (empty or trailing `.` = current directory), skips `compiler.dll`/`ackport.dll`, `LoadLibraryA`s each, keeps ≤ 64 modules (`004133c0`, `CONTRACT.txt:630-637`) | CONFIRMED (static) |
| Handshake | If export `A6_DLLAPI` exists it is called with the engine interface `006438b0`; module kept only if the signed result < 631; otherwise `A5_DLLAPI(00644380)` with result < 512; else unloaded (`0040c330`, `CONTRACT.txt:652-659`) | CONFIRMED (static) |
| `A6_DLLAPI` | Provided by the SDK import library (`adll.lib` contains `/EXPORT:_A6_DLLAPI` and `engine_bind`); the plugin does not write it. `RenkAyar.dll` returns `0x276` (630) | CONFIRMED |
| Function binding inside the plugin | `adll.h` with `DLL_USE`: every engine function in `afuncs.h` becomes a function pointer `F(f)=(*f)`; `ENGINE_VARS *ev` holds pointers to every engine variable listed in `avars.h` (578 entries); `engine_bind()` fills them | CONFIRMED (headers) |
| Callback resolution | Engine functions are resolved by name through the interface callback at `interface+0xa30` (`RenkAyar` binds `draw_begin` this way) | CONFIRMED (`RenkAyar.behavior.txt:13-19`) |
| Script declaration | `dllfunction Name(args);` declares an external; calls are resolved by export name among loaded plugins. Up to 4 `var`/pointer arguments, `cdecl` (`CONTRACT.txt:220-221`) | CONFIRMED (contract); multi-DLL name resolution order UNKNOWN |
| Explicit load | `dll_open("x.dll")` returns a handle; scripts assign the global `dll_handle` before calling that DLL's functions (`K22Widescreen.wdl` `main`, original `K22.wdl`) | CONFIRMED (scripts); exact effect of `dll_handle` on dispatch HYPOTHESIS |

## 2. Data access

| Topic | SDK contract | Label |
|---|---|---|
| Numeric `var` | `typedef long var`; 22.10 fixed point. `_VAR(int)=i<<10`, `_VAR(double)`, `_INT(x)=x>>10` (arithmetic shift, rounds toward −∞ for negatives), `_FLOAT(x)=x/1024` (`adll.h`) | CONFIRMED |
| Script `INT()` vs SDK `_INT` | script `INT` truncates toward zero and stays scaled; SDK `_INT` floors — different results for negative fractions (`CONTRACT.txt:204-207`) | CONFIRMED |
| Engine variables | `v(name)` = `*ev->name`; e.g. `v(screen_size)`, `ev->camera` (`VIEW*`) | CONFIRMED (headers) |
| Strings | `STRING{C_LINK link; char* chars; long length; long flags}`; `_CHR(s)=s->chars` (`atypes.h:270-275`) | CONFIRMED |
| Engine structs | `ENTITY`, `VIEW`, `PANEL`, `TEXT`, `BMAP`, `MATERIAL`… in `atypes.h`. A7/A8 SDK layouts must not be used as the A6 ABI (`CONTRACT.txt:224`) | CONFIRMED |

## 3. The two Kabus plugins

### 3.1 `kabus_dll.dll` (community patch; source `Index.hpp`)

| Export | Signature (source) | Behaviour | Label |
|---|---|---|---|
| `DiscordRPC_TryConnect` | `bool(STRING* appId)` | `Discord_Initialize(appId, handlers, 1, NULL)` then presence update; idempotent | CONFIRMED (source) |
| `DiscordRPC_IsConnected` | `bool()` | returns a local flag set by `TryConnect` (not a real connection state) | CONFIRMED |
| `DiscordRPC_TryDisconnect` | `void()` | `Discord_Shutdown` | CONFIRMED |
| `DiscordRPC_SetStateDetails` | `void(STRING* details, STRING* state)` | copies strings, start timestamp on first call, `Discord_UpdatePresence` | CONFIRMED |
| `INI_IsInitialized` | `bool()` | false only if `new Ini` failed | CONFIRMED |
| `INI_GetVariable` | `char*(STRING* key)` | `Screen:Mode` [0,2], `Screen:X` [0,3840], `Screen:Y` [0,2160]: digit-only and in range or reset to the minimum (0) **and rewritten to disk**; other keys raw or `"undefined"`; returns `_strdup` | CONFIRMED |
| `INI_SetVariable` | `void(STRING* key, STRING* value)` | validated for the three keys, saved immediately | CONFIRMED |
| `Parse_EscapeChar` | `char*(STRING* in)` | replaces `\' \" \\ \n \r \t \b \f \v` with control characters (comment: A6 `txt_load` does not), returns `_strdup` | CONFIRMED |

Lifetime and ownership:

- `DllMain` calls `engine_bind()` for every reason code (attach, detach, thread). **CONFIRMED.**
- Module globals `DiscordRpcVar = new RPC()` and `IniVar = new Ini("K22.ini")` are created during `LoadLibrary` (static initialisation). Because the engine auto-loads every plugin in the game directory at start-up (§1), `K22.ini` is read, validated and possibly rewritten **before** any script runs; it is resolved relative to the process working directory and never freed. **HIGH CONFIDENCE.**
- `INI_GetVariable`/`Parse_EscapeChar` return heap copies that the engine never frees: one leak per call (≈ 575 per subtitle load, plus each INI query). Small, bounded by language reloads. **HIGH CONFIDENCE.**
- The `bool` exports return their value in `AL` only (MSVC `bool` ABI); the engine reads the whole `EAX` as a raw `var`. `true` therefore arrives as raw 1 (0.000977), not 1.0, and the upper 24 bits are not guaranteed zero. Scripts only test `== 0`, which is correct unless stale upper bits make a `false` non-zero. **HYPOTHESIS** (not observed on Windows).
- `kabus_rpc.dll` has no `A6_DLLAPI`/`A5_DLLAPI`, so engine auto-enumeration loads and immediately unloads it; it stays resident only as an import of `kabus_dll.dll`. **HIGH CONFIDENCE.**

Patch scripts using it: `K22Widescreen.wdl` (`INI_*` for display set-up), `silah.wdl`, `cinematic.wdl` (`Screen:Mode` dependent weapon offsets), `dil_degistir.wdl` (`Parse_EscapeChar` on every subtitle line), `discord.wdl` (connect/update loop every frame). **CONFIRMED.**

### 3.2 `RenkAyar.dll` (original game)

Exports `dl_Reset`, `dl_PaKaGa(brightness, contrast, gamma, mode)`; builds 3×256 16-bit ramps and calls `IDirect3DDevice9::SetGammaRamp` via `draw_begin`'s device (vtable 0x54). Formula and modes: `reverse/decompiled/RenkAyar.behavior.txt:21-40`. **CONFIRMED** (static); D3D method identity **HIGH CONFIDENCE**.

## 4. Native equivalents

The native engine loads no Windows DLL. External script functions that the original game and patch need are host services.

| Original mechanism | Native implementation | Label |
|---|---|---|
| `PLUGINDIR` + `*.dll` auto-load | `Engine::initialize_plugins` (`native/engine.cpp:1148-1156`): only the default/current `PLUGINDIR` is accepted; if any of the 8 `kabus_dll` names is declared `dllfunction`, opens the `kabus_dll.dll` service; if `dl_PaKaGa`/`dl_Reset` is declared, opens `renkayar.dll` | CONFIRMED |
| `dllfunction` declaration | parsed as an external function (`native/wdl.cpp:169-174`, `Runtime::external_import`, `native/wdl.cpp:462`); a script-defined body with the same name wins over the service (`native/check_engine.cpp:3868-3869`) | CONFIRMED |
| `dll_open` | `svc_plugin_open` (`native/engine.cpp:1139-1146`): `renkayar.dll` → gamma service, `kabus_dll.dll` → loads `K22.ini` once ("module-global IniVar … dll_open is not a reload"), anything else → explicit error | CONFIRMED |
| `dll_handle` | ordinary global; services are dispatched by function name. Export names of the two DLLs do not overlap, so no ambiguity arises | CONFIRMED |
| `INI_*` | `svc_ini_parse`/`svc_ini_load`/`svc_ini_save`/`svc_ini_normalize` (`native/engine.cpp:1111-1138`, `:1205-1206`): same three validated keys and ranges, invalid → `"0"` and rewritten; private `K22.ini` under the save root, falling back to a read-only copy in the game data | CONFIRMED |
| `INI_GetVariable` return | new engine string per call (`native/engine.cpp:1205` comment: original export returns an independent `strdup`) — no leak | CONFIRMED |
| `Parse_EscapeChar` | same nine escape pairs (`native/engine.cpp:1207`) | CONFIRMED |
| `DiscordRPC_*` | native Discord IPC client over `$XDG_RUNTIME_DIR/discord-ipc-N` with 1 s retry throttle, handshake, duplicate-presence suppression (`native/engine.cpp:1091-1110`, `:1208-1212`); returns integer `var` 1/0 | CONFIRMED |
| `dl_PaKaGa`/`dl_Reset` | gamma ramp computed with the recovered integer/float sequence into `nativeAdjustment`, applied by the renderer post-pass (`native/engine.cpp:1235-1236`) | CONFIRMED |
| Tests | `native/check_engine.cpp:3761-3905`: INI fixture, provider admission per `PLUGINDIR`, script-override precedence, re-open without reload, escape service | CONFIRMED (source; executed by the existing native CTest suite) |

Differences that matter:

1. Native `bool` services return 1.0 (raw 1024) where the DLL returns raw 1 in `AL`; equal under the scripts' `== 0` tests. **HIGH CONFIDENCE.**
2. Native refuses unknown DLLs instead of silently loading them; original start-up would load any `*.dll` in the game directory (including, in the Lutris install, ReShade's `d3d9.dll` entry). **CONFIRMED** (native), original behaviour per §1.

## 5. SDK licence constraints

`readme_acknex.txt` (Conitec): the SDK may be used for plugins only if a machine-readable copy of the complete product source is provided (unless the author holds a Pro/SDK licence); `.cpp`/`.h` may be redistributed, **the `.lib` libraries may not**; DLLs must not act as wrappers exposing the engine interface. The author's repository redistributes `adll.lib`. The native project contains none of these SDK files. **CONFIRMED** (text); legal conclusions: [COMMUNITY_CREDITS](COMMUNITY_CREDITS.md).
