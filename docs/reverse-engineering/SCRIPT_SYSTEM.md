# WDL Script System

Purpose: how Kabus 22's WDL/C-Script is compiled and executed by the original engines and how `native/wdl.cpp` reimplements it.

Related: [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) (index) · [ENGINE_ARCHITECTURE.md](../ENGINE_ARCHITECTURE.md) · [FUNCTION_MAP.md](FUNCTION_MAP.md) · [DATA_STRUCTURES.md](DATA_STRUCTURES.md) · [RESOURCE_FORMATS.md](RESOURCE_FORMATS.md) · [ORIGINAL_BEHAVIOR.md](ORIGINAL_BEHAVIOR.md) · [../GAMEPLAY_SYSTEMS.md](../GAMEPLAY_SYSTEMS.md)

## Scope and conventions

- **Campaign engine.** The evidence comes from `Kabus22Patch/K22.exe` (SHA256 `1a6a0dbc…7bed`), which identifies itself as "A6 engine" "6.31.4" (`0x0061dda8`/`0x0061ddb4`). See [`CONTRACT.txt:1-10`](../../reverse/decompiled/runtime/CONTRACT.txt) and [`RECOVERY.txt:31-35`](../../reverse/decompiled/RECOVERY.txt). This is the *patched* executable. The ISO `K22.exe` (`cc193c6b…`) is not packed and differs from it in only six `.text` bytes (start-up/archive branches) with identical `.rdata`, so the recovered addresses and code apply to the ISO build (**CONFIRMED** byte comparison, [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md#community-patch-binaries-not-part-of-the-iso), `CONTRACT.txt:2147-2154`). Runtime parity with the original on Windows was never observed (**UNKNOWN**).
- **Bonus mode.** Bonus mode ("Kabus 22 Mode", `K22_mode.wrs`) runs on a separate A7 `acknex.dll` (SHA256 `f96c7645…227b`). See [`bonus-core/CORE_CONTRACT.txt:1-4`](../../reverse/decompiled/runtime/bonus-core/CORE_CONTRACT.txt). Native code selects it with `SemanticProfile::BonusA7` ([`native/value.hpp`](../../native/value.hpp) `SemanticProfile`).
- **Citations.** `CONTRACT §N` means [`reverse/decompiled/runtime/CONTRACT.txt`](../../reverse/decompiled/runtime/CONTRACT.txt) section N. Bare addresses are K22.exe VAs. Decompiled paths are relative to `reverse/decompiled/runtime/` unless they start with `../`. The CONTRACT writes "bitN" for a mask value N; for example, `006446e8` "bit1" is `|1` in [`trace/K22.exe.00404b60.c:16`](../../reverse/decompiled/runtime/trace/K22.exe.00404b60.c).
- **Extracted scripts.** Script identifiers come from `reverse/extracted/original/*.wdl` (179 files). [`recovery.json`](../../reverse/extracted/recovery.json) `archives[0]` records that these were extracted from the Lutris install's `K22.wrs` (SHA256 `5bae0b5e…c14f`). The ISO copies are in `reverse/media/iso/scripts/` (182 entries). [`script-comparison.json`](../../reverse/media/iso/script-comparison.json) marks 173/182 as identical. The 9 that differ are `K22.wdl`, `dil_ceviri_{tr,en,de}.wdl`, `sub_titles_en.wdl`, `kotu_koruyucu.wdl`, `cinematic.wdl`, `YeniMenu.wdl` and `Yeni_Oyun_Deger.wdl`. `GenelAyarlar.wdl`, `YukleMenu.wdl`, `CikisMenu.wdl`, `Ses_Sistemi.wdl`, `oyuncu2.wdl` and `LevelYonetim.wdl` are identical (CONFIRMED). Line numbers quoted here for the differing files (for example `cinematic.wdl`) refer to the extracted Lutris copy. Patch scripts are under `reverse/extracted/patch/` (188 files). Bonus scripts are under `reverse/media/iso/mode/scripts/K22_mode.wrs/`.
- **Confidence labels.** **CONFIRMED** means decompiled code, assembly, bytes or an actual run. **HIGH CONFIDENCE** means strong indirect evidence. **HYPOTHESIS** and **UNKNOWN** carry their usual meanings.

## 1. Pipeline overview

| Stage | Original (A6 K22.exe) | Native | Confidence |
|---|---|---|---|
| Archive mount | `RESOURCE` directive → `FUN_0041ff20` opens the archive and indexes it via `FUN_0041fd20` (WRS2, XOR-0xA5 names). See `RECOVERY.txt:37-57` and [`../loaders/K22.exe.0041ff20.c`](../../reverse/decompiled/loaders/K22.exe.0041ff20.c). | `Runtime::load(entry, Source, Resource)` forwards `resource "x"` to the `Resource` callback. `native/main.cpp` binds that callback to `Assets::mount` ([`native/assets.cpp`](../../native/assets.cpp)). | CONFIRMED |
| Text fetch | `FUN_00420120` is the shared file/archive loader. It detects `.WDL` by extension, checks the memory cache `0x00695eb0`, then the filesystem or archive (`RECOVERY.txt:59-66`). `FUN_00419cb0` handles INCLUDE (`RECOVERY.txt:67`). | `Source` callback; `Runtime::Impl::preprocess` re-lexes every include. | CONFIRMED |
| Engine-internal script | `FUN_0041b5d0` parses the embedded C-Script text at `0x005d3348` (`RECOVERY.txt:68`). The extracted copy is [`reverse/extracted/engine_internal.wdl`](../../reverse/extracted/engine_internal.wdl). | Native has no embedded text; defaults come from `Engine::initialize_globals` ([`native/engine.cpp`](../../native/engine.cpp)). | CONFIRMED (original); native equivalence HIGH CONFIDENCE |
| Parse/compile | Driver `FUN_0041aef0` handles BIND/LEVEL/SAVEDIR/PLUGINDIR/INCLUDE/PRINT/HALT/RESOURCE (`RECOVERY.txt:39`). Statement compiler `00402fe0`. Expressions are emitted as **x86 machine code** (`0040aba0`, §1 `:27-36`). | Recursive-descent parser `Runtime::Impl::parse` / `statement` / `expression` builds an AST (`Expr`, `Instruction`) that is interpreted. Original codegen is not reproduced. | CONFIRMED |
| Plugin enumeration | `004133c0` runs after compilation and before MAIN (§13 `:475-479`, §17 `:630-637`). | `Engine::initialize_plugins` runs after `Runtime::load`, before `configure_language` and `bootstrap` (§13 `:480-486`, `native/main.cpp`). | CONFIRMED |
| Bootstrap | `00414650` queues MAIN via `004104f0`, then each STARTER (§13 `:490-499`). | `Runtime::bootstrap` | CONFIRMED |
| Per-frame | `00414650` frame → `004125e0` (input/events/scheduler) → level command dispatch `0041de10` (§13 `:504-508`). | `Engine::tick` → `Engine::update_input`/`update_ui` → `Runtime::tick` → deferred `load_level` | CONFIRMED |

## 2. Compiler directives and preprocessing

| Feature | Original evidence | Native implementation | Confidence / gap |
|---|---|---|---|
| INCLUDE | `0041aef0:163-189` → `00419cb0`. No include-once set; filename must be under 32 bytes; suppressed when `006633c0 & 2` (§9 `:333-340`). | `Runtime::Impl::preprocess` calls `preprocess(file, source(file))` unconditionally. Native adds a recursion bound of 256 and no 32-byte filename limit. | CONFIRMED; the 32-byte limit is not enforced natively. |
| RESOURCE | `0041aef0:211-215` → `FUN_0041ff20`. Duplicate names are suppressed case-insensitively; at most 128 archives (`RECOVERY.txt:40-46`). | Same `preprocess` branch → `Resource` callback. `Assets::mount` dedups by resolved path (`mounted_`). | CONFIRMED |
| IFDEF/IFNDEF/IFELSE/ENDIF, DEFINE | Macro expansion `0041a0e0`; text macros registered by `00419d20` (§6 `:273-281`, §9 `:356-362`). | `preprocess` keeps a condition stack. `append_expanded` expands macros up to depth 64. Protected compiler macros come from `State::compilerMacros`. | CONFIRMED for the shape; native depth limit is its own choice. |
| Predefined macros | 79 numeric startup macros, as integer text, in `00413990` ([`numeric-constants.json`](../../reverse/decompiled/runtime/numeric-constants.json), §6 `:266-278`). Presence macros RELEASE, DIRECT3D, CAPS_*, SERVER/CLIENT/WMB (§9 `:356-362`). | `nativeConstants[]` (81 rows, 2 Bonus-only) in `native/engine.cpp`. | CONFIRMED (table); native presence-macro set not audited here (UNKNOWN). |
| BIND / LEVEL | Consume one parameter with no side effect (§17 `:622-625`). | `Runtime::Impl::parse` stores them in `CompilerSettings::bindings`/`levels` only. | CONFIRMED |
| PLUGINDIR | Must be ≤126 bytes; copied to `0x006c7488` when `006633c0` bit 2 is clear (§17 `:626-628`). | `parse` rejects >126 bytes and stores it in `CompilerSettings::plugin_directory`. `Engine::initialize_plugins` supports only the empty or `.` directory. | CONFIRMED |
| SAVEDIR, PRINT, HALT | Listed in the `0041aef0` dispatch (`RECOVERY.txt:39`). | No dedicated handling in `parse` (no match in `native/wdl.cpp`). | Original CONFIRMED. Their semantics are **UNKNOWN** (not recovered). No top-level use was found in the extracted original scripts. |
| WINDOW block | `00419c40` sets a handler that discards every token (`00419c20`: `*DAT_006633a8=0; return 1`). No object is built (§15 `:570-581`; [`window-properties/K22.exe.00419c20.c`](../../reverse/decompiled/runtime/window-properties/K22.exe.00419c20.c)). Example source: `K22.wdl:WINSTART`. | `parse` parses it and emits the diagnostic "obsolete WINDOW … parsed without object construction"; `initialize` skips `type=="window"`. | CONFIRMED |
| Identifiers spanning whitespace | In expression context, `0041a250` concatenates tokens without separators. `0040c090:17` strips SP/LF/TAB/FF/BS/CR/BEL. Declaration names stay single-token (§16 `:603-618`). Example: `materials.wdl` (mode) `Mtl_ arkakum`. | `Runtime::Impl::expression_token` concatenates adjacent Word/Number tokens while `compactExpressions` is true. That flag is set only while compiling function bodies (`parse`, function branch). | CONFIRMED (original). Native coverage of initializers and properties outside function bodies is **UNKNOWN**. |
| Repeated ACTION/FUNCTION bodies | The later body replaces the earlier one and descriptor identity is kept (`00403a00`, `00403b10`; §9 `:342-354`). Example: `cinematic.wdl:video_ebrudusme_maz`. | `parse` clears the code of an already-defined function and records the diagnostic "replacing function …". `FunctionId` is unchanged. | CONFIRMED |
| Scalar VAR initializer | `00420990` with `0041a1b0(0)` applies `atof` to the token, so an unresolved word becomes 0 (§10 `:366-387`). | `parse` declaration branch + `initialize`. | Original CONFIRMED; native token-to-0 parity not audited (UNKNOWN). |
| BMAP `name,<file>` | Same path as `name="file"` (`0040ef40`/`0040ee60`, §17 `:639-650`). | `parse` accepts `,` as an initializer for font/bmap/sound/string. | CONFIRMED |

## 3. Values and fixed-point arithmetic (§5 `:172-224`)

| Rule | Original | Native (`native/wdl.cpp` `Runtime::Impl::evaluate`, `Expr::Binary` branch; `native/value.hpp` `Value`) | Confidence |
|---|---|---|---|
| Representation | Signed 32-bit raw/1024 (22.10). Pointers share the word unscaled. | `Value{kind,bits}` with `Kind::Number/Object/Function/NativeText`. Object handles are `ObjectId`s, not machine pointers. | CONFIRMED |
| `+ - unary-` | Wrap mod 2³² | `raw(x.bits+y.bits)` | CONFIRMED |
| `*` | `floor((P+C)/1024)`, with C=1023 only when −2³² ≤ P < 0. The high word is compared to −1. | `if(n<0&&n>=-4294967296LL)n+=1023; n>>10` | CONFIRMED |
| `/` | `trunc((L·1024)/R)`. Division by zero or overflow raises an x86 #DE, and the engine's handling of that is **UNKNOWN**. | Throws a WDL diagnostic ("fixed-point division by zero/overflow"). | Native diverges at the fault boundary: it reports an error and does not reproduce a crash. |
| `%` | `0040bfb0` negative-L adjustment using `0xf424000000/R` | Same formula, plus diagnostics on overflow. | CONFIRMED for the formula. The `0040bfb0` export is not present under `numeric/`; the source is §5 `:198-202`. |
| Comparisons | Signed raw compare; result is 1024 or 0. | `Value::from_int(x.raw()<y.raw())` etc. | CONFIRMED |
| `&&`, `\|\|` | Both operands are evaluated eagerly, left then right. | `arg(0)` then `arg(1)` are both evaluated before the test. | CONFIRMED |
| `INT` | `00403d60` truncates toward zero and returns a scaled value. | `Engine::service_builtin` `int`: `Value::from_int(integer(0))`. | CONFIRMED |
| Literals | `atof×1024` → `005b72cc` truncation to int64, low word kept (`abi/K22.exe.00402770.asm.txt:46-54`). | `Value::from_double` truncates toward zero; it throws outside the int64 range. | CONFIRMED in range; out-of-range behaviour differs (original policy UNKNOWN). |
| DLL conversion | `FLOAT 00406fc0`, `FLOATR 00406fe0`, `FLOATD 00407010` pack float bits. Plugins decode arguments with `SAR 10`. | `Engine::service_builtin` `float` etc. | CONFIRMED (original); native per-builtin parity not audited. |

## 4. Coroutines: creation, WAIT/WAITT, scheduling

### 4.1 WAIT/WAITT binding (§1 `:16-38`)
- **CONFIRMED.** WAIT row `0061d538` → handler `00404b60`; WAITT row `0061d548` → `00404be0`; SLEEP → `00404ca0`; proc_kill → `00404cd0`. These rows are in the 317-row API descriptor table `0061c408..0061d7d8` ([`api-descriptors.json`](../../reverse/decompiled/runtime/api-descriptors.json)). Calls compile to `MOV EBX,handler; CALL EBX` (`numeric/K22.exe.0040aba0.c:181-192`). A handler yields by setting `006446e8 |= 1`. The interpreter `00410a80` checks that flag after the expression (`interpreter/K22.exe.00410a80.c:849-850`). There is no OS-level wait.
- **Native.** `Runtime::Impl::evaluate`, `Expr::Call` branch: `name=="wait"||"waitt"||"sleep"` sets `Process::countdown`, calls `capture(*p)` and sets `p->yielded=true`.

### 4.2 Timing (§3 `:71-142`)
| Item | Original | Native | Confidence |
|---|---|---|---|
| WAIT(n) | Clamp the raw value to [1024, 1048575] and store `n-1024` (`trace/K22.exe.00404b60.c:8-17`). | `std::clamp(n,1024,1048575)`; `countdown=clamped-1024` | CONFIRMED |
| WAITT(n) | `t=TIME` (`*00665cf4`); `chosen=max(t, min(n, 1048575))`; store `t-chosen` (`00404be0.c:11-19`). | Identical expression. | CONFIRMED. The CONTRACT branch for TIME > cap is "preserve exact branch". Native uses `n<t?t:min(n,cap)`; the edge case where TIME itself exceeds the cap is not separately verified (HYPOTHESIS that they agree). |
| SLEEP | Handler `00404ca0` (`clock-instructions/K22.exe.00404ca0.c`) | Native shifts the argument `<<4` (seconds→ticks) and reuses the WAITT path. | Native formula: HIGH CONFIDENCE. Original body not summarised in CONTRACT, so parity is UNKNOWN. |
| Wake rule | c ≥ 1024: subtract 1024 and keep. Otherwise resume iff `c > -min(TIME,1024)`; if not, `c += TIME`, capped at 0 (`trace/K22.exe.00412080.c:35-65`). | `Runtime::Impl::tick`: the same three branches. | CONFIRMED |
| freeze_mode | <1024: all records run. 1024–2047: only records with `my==NULL` run. ≥2048: the scheduler is skipped (`00412080`, `004125e0:16-18`). | `tick` re-reads `freeze_mode` for each record (comment cites A6 `004120ec`/A7 `10016710`). | CONFIRMED |
| Pass order | Double-buffered queues; pass 1 runs records whose function has `+0x1c & 2` clear, pass 2 the rest (PROC_LATE, legacy opcode 38). | Two passes on `functions[].schedulerFlags & 2`. A7 uses three passes ("bit1 first, neither second, bit2 third"; the code comment cites [`bonus-core/load-bonus/order/acknex.dll.10016710.c`](../../reverse/decompiled/runtime/bonus-core/load-bonus/order/acknex.dll.10016710.c)). | A6 CONFIRMED. A7 ordering is HIGH CONFIDENCE (native comment; not re-derived here). |
| TIME computation | `00414650`: QPC ticks/64000·1024; FPS_LOCK/FPS_MIN clamp to ≥51; `TIME_FACTOR` and then `TIME_SMOOTH` (default 0.5) with rounded multiply `floor((P+512)/1024)` (§3 `:103-123`). | `Engine::tick`: `fps_lock`/`fps_max`/`fps_min`, clamp to 51, rounded `mul`, `time_smooth`. | CONFIRMED |

### 4.3 Continuation record, first yield, locals, MY/YOU (§2 `:40-69`, §10 `:389-394`)
- **CONFIRMED.** `00410390` appends a record of `44+12·locals` bytes to the destination queue (layout in [DATA_STRUCTURES.md §3](DATA_STRUCTURES.md#3-scheduler-and-continuation-records)). `00410350` snapshots three words per local, and `00410310` restores them (`trace/K22.exe.00410350.c`, `interpreter/K22.exe.00410310.c`).
- **CONFIRMED.** A call into a script function is synchronous until that function's first yield. The caller then continues, and the callee resumes later on its own (`abi/K22.exe.00412220.c:12-37`). The caller receives the value of the yielding expression (`00410a80:848-853` stores it in `006445b4`). For example, `wait(3)` returns raw 3072, and `wait(0)` returns 1024.
- **Native.** `Runtime::Impl::start` runs `prepare` and then `run` on a stack `Process`. On yield, `capture` copies the `Process`, saving `savedLocals` per owned local object, and pushes it onto `queued`. `run` restores the snapshots before resuming. The call returns `p.result`, which holds the WAIT return value.
- **CONFIRMED.** MY/YOU are stored in the record and restored on resume (`00411fe0`). DLL interface `+0xa34`/`+0xa38` point at `my` (`006441d8`) and `you` (`006441dc`) (`registry/K22.exe.0040c290.c:19-23`). Native: `ProcessContext{processId,my,you}`, plus `value_slot("my")`/`("you")` locals and `_arg1.._arg4` in `Runtime::Impl::prepare`.

### 4.4 proc_kill / proc_status / proc_late (§4 `:144-170`, §10 `:396-401`)
| Mode bit | Original (`trace/K22.exe.00410660.c:8-20`) | Native (`evaluate`, `proc_kill`) | Confidence |
|---|---|---|---|
| 1 | Cancel suspended records whose `my` equals the current `my` | same | CONFIRMED |
| 2 | Cancel records whose **my** equals the current `you`. This is not kill-all. | same | CONFIRMED |
| 4 | Cancel records of the current function | same | CONFIRMED |
| 8 | Set `006446e8 \|= 2`. This blocks all continuation insertion until the next frame start (`004125e0:14` clears it). | `suppressContinuations=true`; reset at `tick` entry | CONFIRMED |
| 16 / proc_status / proc_kill2 | Absent from the 317-row table. The resolver `0040c200` returns NULL. | Not implemented, by design | CONFIRMED absent. No extracted WDL calls proc_kill/proc_status. |
| proc_late | Sets function descriptor `+0x1c \|= 2` (`00410a80:819-821`) | `Instruction::Late` and builtin `proc_late` set `schedulerFlags \|= 2` | CONFIRMED |

Entity removal: `00431920` calls `00410550(entity,1)`, but only when entity `+0x60` (event_flags) is nonzero. That call cancels records whose MY is the entity and clears matching YOU (§13 `:510-516`). Native `Runtime::kill_for_entity` always cancels and clears YOU; it applies the event-mask gate only to registered-pointer cleanup (§27 `:1149-1156`). That divergence is deliberate and documented (CONFIRMED).

## 5. Locals: three words per declaration (§11 `:403-423`)
- **CONFIRMED.** `00420b30` preallocates the shared locals `_loc255`…`_loc0` (head `006bb1c4`). Each is a 28-byte object with values at `+0x10/+0x14/+0x18`. The compiler (`00402fe0:22-74`) maps each `VAR` declaration to one `_locN`. Scalars, VECTORs and small arrays all get exactly three words, and `x/pan`, `y/tilt`, `z/roll` map to `+0x10/+0x14/+0x18`. For example, the two `Ses_Sistemi.wdl` angle locals cannot alias each other.
- **Native.** `Runtime::Impl::local_storage` allocates a per-process `Object::Kind::Vector` with `array.resize(size)`. `size` is 3 for scalars and VECTORs and the declared count for arrays (`statement`/`local` → `localDefinitions`). The divergence is that native local arrays larger than 3 get their declared length, while the original gives every declaration three words. The original's behaviour for out-of-bounds indices is **UNKNOWN**.

## 6. Global registry and object member tables (§6 `:226-281`)
- **CONFIRMED.** 330 numeric registry rows at `0061ec80..00620b70`, stride 24, are listed in [`global-registry.json`](../../reverse/decompiled/runtime/global-registry.json). Each is registered as WDL type 9 with three words of storage, and flags are ORed into object `+0x14` (`registry/K22.exe.0041e060.c`, `0041e180.c`). Example defaults: VERSION 0x1941, TIME 1024, TIME_SMOOTH 512, FREEZE_MODE 0, MAX_LOOPS 5120000.
- **Native.** `nativeDefaults[]` has 334 rows: 330 A6 rows plus 4 `bonusOnly`. For example, `{"VERSION",{6465,0,0}}`. `Engine::initialize_globals` adds `delimit_str`, the callback globals, and masks such as VISIBLE/CENTER_X. Native-only normalisations such as Bonus `FILTER` are labelled in-code as not original (§28 `:1179-1186`).
- **CONFIRMED.** Member tables for 8 kinds (entity 141, text 28, vector 10, numeric_array 14, string 1, view 46, panel 16, material 36) are in [`member-descriptors.json`](../../reverse/decompiled/runtime/member-descriptors.json). Native uses the named field maps `Object::fields` and `nativeMasks[]` (108 rows) and does not use byte offsets. Details are in [DATA_STRUCTURES.md §5](DATA_STRUCTURES.md#5-script-visible-object-members).

## 7. Input query, named slots and edges (§12 `:425-471`)
| Item | Original | Native | Confidence |
|---|---|---|---|
| Name table | 89 names → 80 distinct slots at `0061b108`, stride 8 ([`input-contract.json`](../../reverse/decompiled/runtime/input-contract.json)) | `nativeKeys[]` has 89 rows. Aliases share a slot through `keyboard_`/`keyboardCallbacks_`, plus compiler alias macros. | CONFIRMED |
| `key_pressed(code)` | `00404d30`: 0..126 → keyboard; 256..267 JOY_1..12; 268..279 JOY2_1..12; 280/281/282 mouse L/R/M; anything else → 0. Edges are not consumed. | `Engine::service_builtin` `key_pressed`: same ranges; 0..126 → `Engine::key_state` | CONFIRMED |
| Edge capture | `0042ddd0` scan = `(lParam>>16)&0x7f`; a down edge only from held=0. `00422000` publishes held OR fresh-down, so a tap lasts one frame. | SDL scancode → `legacy_scancode`. `Input.keys` / `Input.pressed` are ORed in `key_state`. | CONFIRMED (original). Native edge parity was observed for held keys in actual runs (memory/CONTRACT §42 `:2312`). |
| Event dispatch | `004125e0.asm:54-276`: per edge, the key handler runs and then ON_ANYKEY, with MY/YOU=0 and `_arg1` set/restored. `ON_MOUSE_STOP` fires each still frame. | `Engine::update_input` lambda `edge`: saves `_arg1`, then `runtime.start(fn,args,0,0)`, then `anykey`. | CONFIRMED |
| Defaults | `engine_internal.wdl` binds F2_save, F3_load, F5_toggle_video, F6_sshot, F10/ESC/CLOSE_exit, F12_tog_sounds, ENTER_toggle_screen. | Not embedded; game scripts override. | Original CONFIRMED; native default binding set **UNKNOWN** (not audited). |

The game's own mapping layer is `GenelAyarlar.wdl:Tuslari_Esitle`, a STARTER that polls once per frame. It maps `GA_Tuslar[0..9]` (indices `GA_Ileri`…`GA_Buyu`) through `key_pressed`, ORs in joystick buttons `GA_JTuslar[0..5]`, and writes globals `Key_Ileri`… (CONFIRMED by script source).

## 8. Bootstrap order and deferred level transitions (§13 `:473-520`)
1. Compile → plugin enumeration `004133c0` → `00414650` queues MAIN, then STARTERs (`+0x1c & 4`, `TEST [ESI+0x1c],BL` with `BL=4` at `004146c0`). The descriptor list `0063b9b4` is built by prepending, so starters run in reverse order of first creation. **CONFIRMED.** Native `Runtime::bootstrap`: `enqueue(main)` and then `for id-->1` over `starter&&defined`.
2. `004104f0` only appends a countdown-0 record; nothing runs immediately. MAIN runs first in the FIFO pass. For example, `K22.wdl:main` calls `GA_AyarlariYukle` and yields `sleep(1)`/`wait(1)` before `level_load`. **CONFIRMED.**
3. `level_load` (`00404f00`) stores the filename at `65580` and pending command mask 8 at `6557c`, then returns 1024 without yielding. `0041de10` executes the load after the frame's script phase, when INKEY_ACTIVE==0. **CONFIRMED.** Native: `Engine::world_builtin` `level_load` sets `pendingLevel`/`pendingLevelLoad`. `Engine::tick` calls `load_level` after `runtime.tick` and `worldFrameEvents`.
4. Level teardown `00434670` → `004341a0` → `00431920` for each entity, which cancels by MY and clears YOU. New entity actions are queued with `00410520` and first run in the *next* scheduler phase. STARTERs are not re-run. **CONFIRMED.**

## 9. Strings, files and media handoff (§14 `:522-568`)
- **CONFIRMED.** TXT_LOAD (`00408890`) and FILE_STR_READ (`00408470`) share the reader `00405e20`. The reader skips leading bytes ≤0x1F, stops at a byte ≤0x1F or at `delimit_str[0]` (default `,`), and keeps spaces. TXT_LOAD has no comment handling and returns `count<<10`.
- **CONFIRMED.** MEDIA_PLAY (`00405d10`→`00405ca0`→`00425730`) passes the filename through `MultiByteToWideChar(CP_ACP)` to DirectShow with no engine path rewriting.
- **CONFIRMED.** STR_CLIP (`00404780`) removes a prefix. A negative count is undefined memory behaviour. Native `str_clip` throws on a negative count.
- **Native.** `Engine::service_builtin` handles `txt_load` (reads `delimit_str`), `file_str_read` (skips `<32`), `file_asc_read/write` (one byte, `raw>>10`), `file_var_read/write` (text `"%d "` / `"%.3f "`) and `media_play`. File paths go through `svc_private(saveRoot,…)`, which writes to private XDG data, and fall back to read-only archive assets.

## 10. VECTOR temporaries (§18 `:661-721`)
- **CONFIRMED.** VECTOR (`00403eb0`) and `_vec` (`00407040`) share a 64-slot ring `[0063ce30,0063d130)` with cursor `0061d7e8`. The cursor is pre-incremented, so the first slot returned is `0063ce3c`. `_vec` converts float bits to scaled raw. Returned pointers are borrowed, and the 64th later producer overwrites the same slot. WAIT does not capture the ring contents.
- **CONFIRMED.** Native-call arguments are evaluated right-to-left (`0040aba0.c:202-223`).
- **Native.** `Engine` member `vectorTemps_` (`std::array<Object*,64>`) and `vectorTempCursor_` (pre-increment) in `native/engine.hpp`; the `vector`/`_vec` builtin is in `Engine::service_builtin`. The `Expr::Call` evaluation loop runs `for(i=e.args.size(); i-->1;)`, which is right-to-left. The two gaps §18 recorded at `:705-710` (a fresh allocation per call, and left-to-right evaluation) are therefore closed in current source (CONFIRMED by code reading; no run performed for this document).

## 11. Raw pointer words in VAR arrays (§29–31 `:1201-1311`)
- **CONFIRMED.** The original stores bitmap/object addresses as unscaled words in VAR arrays. For example, `CikisMenu.wdl` computes `bmap_width(DMIL_CikisSoru[DD_Dil]+Cer_Kenar)` with `Cer_Kenar=0`. The A6 emitter does `ADD EAX,EBX` on the raw words (`0040aba0`, §30 `:1246-1247`).
- **Native.** Arithmetic returns `Value::Kind::Number`, so the object tag is lost. `svc_bitmap(Engine&, Value)` (`native/engine.cpp`) therefore accepts either Object or Number. It then runs `State::require` and checks for a live `Object::Kind::Bmap`, rejecting NULL, unknown, dead and wrong-kind handles (§30 `:1241-1245`). The actual Main EXIT→NO→Main and EXIT→YES path was verified (§31 `:1273-1283`). Native does **not** emulate arbitrary machine-pointer arithmetic (by design).

## 12. Native callable/data namespaces (§41 `:1630-1667`)
- **CONFIRMED.** The A6 resolver `0040a300` (C 74-153) searches the native table `0061c408` *before* ordinary symbols. A7 `1000bf40` does the same with `101a8200`. A call to `str_len(...)` is therefore a native call even when a numeric global `STR_LEN` exists.
- **Native.** [`native/native_calls.inc`](../../native/native_calls.inc) defines `commonNativeCalls` (312 names), `campaignNativeCalls` (5) and `bonusNativeCalls` (80), generated from `api-descriptors.json` and `bonus-physics/api-descriptors.json`. `native_call_name(SemanticProfile, name)` in `native/wdl.cpp` binds plain-name calls to `Expr::Native` when the AST is built. Bare data reads still resolve through the ordinary symbol path.

## 13. Plugin DLL calls
| Plugin | Origin | Exports used by WDL | Native provider | Confidence |
|---|---|---|---|---|
| `RenkAyar.dll` | Shipped with the original game | `dl_PaKaGa`, `dl_Reset` (`RECOVERY.txt:110-112`, [`../RenkAyar.behavior.txt`](../../reverse/decompiled/RenkAyar.behavior.txt)) | `svc_plugin_open` sets `pluginGamma=true` | CONFIRMED |
| `kabus_dll.dll` | Kabus22Patch, a community patch artifact that is not on the ISO | `INI_*`, `Parse_EscapeChar`, `DiscordRPC_*`; `A6_DLLAPI` at `0x100871d0` returns 630 (`RECOVERY.txt:94-108`, [`../final/kabus_dll.dll.100871d0.c`](../../reverse/decompiled/final/kabus_dll.dll.100871d0.c)) | `Engine::service_builtin` implements `ini_getvariable` (returns `"undefined"` for a missing key), `ini_setvariable`, `parse_escapechar` (9 pairs) and `discordrpc_*` over a Unix socket | CONFIRMED |
| Handshake | `0040c330`: `A6_DLLAPI(interface 006438b0)` result must be <631, else `A5_DLLAPI` result <512. At most 64 modules (`00644730..0064482c`) (§17 `:630-659`). | `Engine::initialize_plugins` chooses a compiled provider from `dllfunction` imports (`Runtime::external_import`). No PE loading. | CONFIRMED |
| proc_status lookup | The patch DLL asks for it, and the resolver returns NULL (§4 `:164-170`). | Not provided | CONFIRMED |

## 14. Worked example: a STARTER polling once per frame

Identifiers only: `GenelAyarlar.wdl:Tuslari_Esitle`, a STARTER whose body is `while(1){ Key_* = key_pressed(GA_Tuslar[...]) | ...; wait(1); }`.

1. **Load.** `parse` sees `starter`, so `ensure_function` creates a descriptor and sets `f.starter=true`. In the original, `00403b10` passes `1` to `00403a00` and the descriptor carries `+0x1c & 4`.
2. **Bootstrap.** `Runtime::bootstrap` enqueues MAIN and then this STARTER. In the original, `004104f0` appends a record with countdown 0.
3. **Frame N.** `Runtime::tick` runs the FIFO pass. The countdown is 0, which is greater than `-min(TIME,1024)`, so the process runs. `run` evaluates the assignments, which call native `key_pressed` (right-to-left argument evaluation). It then reaches `wait(1)`: `n=1024` → clamp → `countdown=0`, `capture` copies the locals and context, and `yielded=true` makes `run` return.
4. **Frame N+1.** The pending queue is swapped. The countdown is 0, so the process resumes at the instruction after `wait` (`p.pc`). The while loop repeats, so each `Key_*` global is refreshed exactly once per frame.
5. **Freeze.** With `freeze_mode` ≥ 1024 the STARTER still runs, because `my==NULL`. Records whose MY is an entity are copied forward unchanged. With `freeze_mode` ≥ 2048 nothing runs.

## 15. Known gaps (§8 `:312-329` plus current source)
| Gap | Status |
|---|---|
| ISO `K22.exe` vs analysed patched `K22.exe` | Code identical except six start-up/archive-path `.text` bytes (CONFIRMED, [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md)); Windows runtime parity UNKNOWN |
| Special input/pointer waits (`00412080:38-46`, statement opcode `0x19`) | UNKNOWN semantics; not implemented natively |
| Queue capacity/allocation (`006446e4`) | UNKNOWN; native queues are unbounded `std::deque` |
| x87 exception policy for `/`, `%`, `_vec`, NaN | UNKNOWN; native raises diagnostics |
| SAVEDIR/PRINT/HALT directive semantics | UNKNOWN; native has no dedicated handling |
| Complete dynamic object/event/key defaults; all 317 API side effects | UNKNOWN (§8) |
| Local arrays wider than 3 words | Native diverges (§5 above) |
| Native universal MY cancellation on entity removal (not gated by event_flags) | Intentional divergence (§27) |
| Extracted `original/` scripts come from the Lutris `K22.wrs`; 9 of 182 differ from the ISO copies | Provenance caveat. The identifiers cited here were re-checked in the ISO copies (`K22.wdl:main`, `WINSTART`, `save_versiyon`). |
