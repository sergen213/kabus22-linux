# Recovered Data Structures

Purpose: in-memory and on-disk layouts recovered from K22.exe (A6 6.31.4), the A7 Bonus engine and the game scripts, with their native counterparts.

Related: [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) (index) · [SCRIPT_SYSTEM.md](SCRIPT_SYSTEM.md) · [FUNCTION_MAP.md](FUNCTION_MAP.md) · [RESOURCE_FORMATS.md](RESOURCE_FORMATS.md) · [../ENGINE_ARCHITECTURE.md](../ENGINE_ARCHITECTURE.md) · [../ASSET_FORMATS.md](../ASSET_FORMATS.md)

Conventions:
- Addresses are VAs in the analysed patched `K22.exe` (image base `0x00400000`, SHA256 `1a6a0dbc…7bed`). See [`CONTRACT.txt:1-10`](../../reverse/decompiled/runtime/CONTRACT.txt).
- Decompiled paths are relative to `reverse/decompiled/runtime/` unless they start with `../`.
- Offsets are hexadecimal bytes.
- **Native does not reproduce these byte layouts.** `native/value.hpp` stores objects as named field maps (`Object::fields`, `CaseLess` keys). Native columns therefore name the equivalent symbol, not an offset.

## 1. Archive (WRS) tables

Source: [`RECOVERY.txt:37-67`](../../reverse/decompiled/RECOVERY.txt); [`../loaders/K22.exe.0041fd20.c`](../../reverse/decompiled/loaders/K22.exe.0041fd20.c), [`../loaders/K22.exe.0041ff20.c`](../../reverse/decompiled/loaders/K22.exe.0041ff20.c).

### 1.1 Entry table `0x00665ea8` (stride 0x30, limit 0x1000 entries)
| Offset | Size | Type | Meaning | Evidence | Confidence |
|---|---|---|---|---|---|
| +0x00 | 0x20 | char[32] | XOR-decoded member name, NUL-terminated | `0041fd20.c` copy loop into `&DAT_00665ea8 + count*0x30` | CONFIRMED |
| +0x20 | 4 | int | Archive slot (index into the handle table) | `(&DAT_00665ec8)[i*0xc] = param_1` | CONFIRMED |
| +0x24 | 4 | long | Payload file offset (`ftell` after the header) | `(&DAT_00665ecc)[i*0xc] = ftell` | CONFIRMED |
| +0x28 | 4 | u32 | Stored (packed) length, read big-endian; also the seek distance | `(&DAT_00665ed0)[i*0xc]` | CONFIRMED |
| +0x2c | 4 | u32 | Decoded length, read big-endian; `0xffffffff` ends the index | `(&DAT_00665ed4)[i*0xc]`; loop `while(uVar7!=0xffffffff)` | CONFIRMED |

### 1.2 Related globals
| Address | Size | Type | Meaning | Evidence | Confidence |
|---|---|---|---|---|---|
| `0x006bb1b8` | 4 | int | Total entries across all archives; error `0x3ee` when >0xfff | `0041fd20.c` | CONFIRMED |
| `0x006bafb8` | 4×128 | FILE*[] | Resource handle table, one entry per opened archive | `0041ff20.c:28`; 0x200 bytes end exactly at `0x006bb1b8` | CONFIRMED (address); the size bound is derived |
| `0x006bb1bc` | 4 | int | Opened-archive count; error `0x3ed` after >0x7f | `0041ff20.c:34-36` | CONFIRMED |
| `0x006b9fb8` | 0x80 stride | char[] | Archive-name records compared case-insensitively for duplicates | `0041ff20.c:16-25` | CONFIRMED (reader). The writer was not found in this export, and 128×0x80 would overlap `0x006bafb8`, so the real capacity is **UNKNOWN**. |
| `0x006bb5f0` | 1 | u8 | Current name XOR key: 0xA5 for `WRS2`, 0 for legacy | `0041fd20.c` | CONFIRMED |
| `0x00695eb0` | 0x24 stride | struct | Memory cache consulted by `FUN_00420120` | `RECOVERY.txt:61` | CONFIRMED (address and stride); fields UNKNOWN |
| `0x00695ea8` | 4 | flag | "Returned cached memory" flag that tells `00419cb0` not to free | `RECOVERY.txt:67` | CONFIRMED |

Native: `Assets::Entry { archive, offset, packed, size }` in `native/assets.hpp`, kept in an `std::unordered_map<std::string,Entry>` by `Assets::mount` (`native/assets.cpp`). Native also accepts a `WRS3`/0xB5 variant and loops to EOF instead of stopping at the 0xffffffff marker (CONFIRMED by code reading). On-disk format: [RESOURCE_FORMATS.md](RESOURCE_FORMATS.md).

## 2. Compiler and descriptor tables

| Structure | Address | Layout | Evidence | Confidence |
|---|---|---|---|---|
| API (native function) descriptors | `0x0061c408..0x0061d7d8` (sentinel), 317 rows | 16 bytes: name ptr, handler ptr, argc, encoded arg types. Example WAIT `0061d538 = [005d101c,00404b60,1,1]` | CONTRACT §1 `:18-23`; [`api-descriptors.json`](../../reverse/decompiled/runtime/api-descriptors.json) | CONFIRMED |
| Legacy command tables | `0x0061b3d8`, `0x0061c298` | 9-word rows used by the statement compiler `00402fe0` | `RECOVERY.txt:72`; §1 `:24-25` | CONFIRMED (stride); fields UNKNOWN |
| Operator descriptors | `0x0061d7f0..0x0061da0c` | operator → node id (`+`→1 … `\|\|`→0x2a) | §5 `:177-178` | CONFIRMED |
| Function descriptor | List head `0x0063b9b4`, prepended on creation (`00401bd0`) | +0x0c body, +0x10 next statement (both cleared on redefinition), +0x1c flags (`&2` PROC_LATE, `&4` STARTER), +0x20 local count | §2 `:47-55`, §3 `:138-141`, §9 `:342-346`, §13 `:490-493`; STARTER test `TEST [ESI+0x1c],BL` with `BL=4` at `004146c0` ([`clock-instructions/K22.exe.00414650.asm.txt`](../../reverse/decompiled/runtime/clock-instructions/K22.exe.00414650.asm.txt)) | CONFIRMED |
| Compile-state bits | `0x006633c0` | `&2` suppresses INCLUDE, `&4` (as written "bit2") suppresses PLUGINDIR, `&0x200` suppresses body compilation | §9 `:333-346`, §17 `:626-628` | CONFIRMED |
| WINDOW parse state | `0x0066133c` (=2), handler `0x00661330` (=`00419c20`), `0x006633a8` | Discard handler | §15 `:572-575` | CONFIRMED |
| PLUGINDIR buffer | `0x006c7488` | ≤127 bytes including NUL | §17 `:626-628` | CONFIRMED |
| Current script name | `0x00651ce0` | Saved and restored around INCLUDE | `RECOVERY.txt:67` | CONFIRMED |

Native: `Function { name, params, localDefinitions, code, labels, gotos, schedulerFlags, starter, defined, external }`, `Expr`, `Instruction` and `Declaration` in `native/wdl.cpp` (anonymous namespace). `schedulerFlags & 2` mirrors PROC_LATE. STARTER is the `bool starter` member, not a mask.

## 3. Scheduler and continuation records

### 3.1 Continuation record (created by `00410390`; size `44 + 12·locals`)
Evidence: [`trace/K22.exe.00410390.c`](../../reverse/decompiled/runtime/trace/K22.exe.00410390.c) (word stores `[1]..[10]`) and CONTRACT §2 `:43-48`.

| Offset | Size | Type | Meaning | Evidence | Confidence |
|---|---|---|---|---|---|
| +0x00 | 4 | ptr | Next record; the appender zero-terminates the chain | `*DAT_00644598 = next` | CONFIRMED |
| +0x04 | 4 | ptr | Function descriptor. Cleared on cancel/resume. | `[1]=param_1`; §4 `:152-153` | CONFIRMED |
| +0x08 | 4 | int | Countdown (22.10; ≥1024 means whole frames; negative means WAITT relative) | `[2]=param_4`; `00412080.c` | CONFIRMED |
| +0x0c | 4 | ptr | Yield statement. Opcode at `+4 == 0x19` marks a special input/pointer wait. | `[3]=param_2`; `00412080.c:47` | CONFIRMED (field); special-wait semantics UNKNOWN |
| +0x10 | 4 | ptr | Next statement to resume | `[4]=param_3`; `00411fe0` | CONFIRMED |
| +0x14 | 4 | ptr | MY (checked by freeze mode 1) | `[5]`; `00412080.c:37` `DAT_006445a4[5]` | CONFIRMED |
| +0x18 | 4 | ptr | YOU | `[6]` | CONFIRMED |
| +0x1c..+0x28 | 4×4 | var | `_arg1`..`_arg4` | `[7]..[10]` | CONFIRMED |
| +0x2c… | 12·n | var[3]·n | Snapshot of each local's three words (`00410350`) | [`trace/K22.exe.00410350.c`](../../reverse/decompiled/runtime/trace/K22.exe.00410350.c) | CONFIRMED |

### 3.2 Scheduler globals
| Address | Meaning | Evidence | Confidence |
|---|---|---|---|
| `0x006446dc` / `0x006446e0` | The two queue buffers that alternate each frame | `00412080.c:12-20` | CONFIRMED |
| `0x006445ac` | Current destination queue base | `00412080.c`, `00410390.c` | CONFIRMED |
| `0x00644598` | Append cursor into the destination queue | `00410390.c` | CONFIRMED |
| `0x006445a0` / `0x006445a4` | Source queue / record being processed | `00412080.c` | CONFIRMED |
| `0x006446e4` | Queue byte capacity used in the bounds check | `00410390.c` `DAT_006446e4 + DAT_006445ac` | CONFIRMED (use); value UNKNOWN |
| `0x006446e8` | `&1` yield requested, `&2` continuation insertion suppressed (proc_kill 8) | `00404b60.c:16`, `00410660.c:20`, `004125e0.c:14`, `00410a80.c:849-850` | CONFIRMED |
| `0x006445b4` | Return value of the last compiled expression (first-yield return) | §10 `:389-391` | CONFIRMED |
| `0x006441d8` / `0x006441dc` | Current MY / YOU (also exposed to DLLs) | `registry/K22.exe.0040c290.c:19-23`; zeroed at `00412080.c` exit | CONFIRMED |

Native: `Process { context{processId,my,you}, retainedContext, function, schedulerFlags, pc, locals, bindings, ownedLocals, snapshots, savedLocals, countdown, yielded, cancelled, noContinuation, running, result }` and `LocalSnapshot { object, offset, count }` in `native/wdl.cpp`. `Runtime::Impl::queued`/`pendingQueue` replace the two buffers, and `suppressContinuations` replaces `006446e8 & 2`. Capacity is unbounded (divergence; original capacity UNKNOWN).

## 4. Local variable storage

| Offset | Size | Type | Meaning | Evidence | Confidence |
|---|---|---|---|---|---|
| +0x04 | 4 | ptr | Next local object (list head `0x006bb1c4`, `_loc255`→`_loc0`) | `local-pool/K22.exe.00420b30.c:14-23`; `00410350.c` walk | CONFIRMED |
| +0x10 | 4 | var | Word 0 (`x`/`pan`/`blue`, or the scalar value) | §11 `:405-416` | CONFIRMED |
| +0x14 | 4 | var | Word 1 (`y`/`tilt`/`green`) | same | CONFIRMED |
| +0x18 | 4 | var | Word 2 (`z`/`roll`/`red`) | same | CONFIRMED |
| total | 0x1c | | 28-byte array object (`16+4·3`); type flags `0x13000000` at allocation | `004207e0.asm:154-196` | CONFIRMED; other header words UNKNOWN |

Native: `Runtime::Impl::local_storage` creates `Object::Kind::Vector` objects with `array.resize(size)`, leased per process (`localLeases`). Size is 3 for scalars and vectors and the declared count for arrays, which diverges from the original's fixed 3 (see [SCRIPT_SYSTEM.md §5](SCRIPT_SYSTEM.md#5-locals-three-words-per-declaration-11-403-423)).

## 5. Script-visible object members

### 5.1 Global numeric registry
| Item | Offset | Size | Type | Meaning | Evidence | Confidence |
|---|---|---|---|---|---|---|
| Descriptor row | — | 24 | struct | name ptr, pointer-slot VA, 3 initial words, raw flags; 330 rows `0061ec80..00620b70` | §6 `:228-235`; [`global-registry.json`](../../reverse/decompiled/runtime/global-registry.json) | CONFIRMED |
| Value object | +0x04/+0x08/+0x0c | 3×4 | var | Three value words; the pointer slot receives `object+4` | `registry/K22.exe.0041e180.c:17-34` | CONFIRMED |
| Value object | +0x14 | 4 | flags | Descriptor flags ORed in. Flag 8 → expression readonly `0x140`; flag 0x10 → `0x200` | §6 `:233,238-240` | CONFIRMED; flags 4/0x20 meaning UNKNOWN |
| WDL type | — | — | 9 | Registered type for registry variables | `0041e060.c:9-32` | CONFIRMED |

Examples: `TIME` slot `0x00665cf4`, `FREEZE_MODE` `0x00665874`, `TIME_FRAME` `0x0066578c`, `TIME_FACTOR` `0x00665830`, `TIME_SMOOTH` `0x00665d94` (§3 `:103-105`, §3 `:125`). Native: `nativeDefaults[]` (`native/engine.cpp`, 334 rows including 4 Bonus-only) feeds `State::globals` (`Slot{Value,callback}`).

### 5.2 Member descriptor tables
`member-descriptors.json` (consumer `004015c0`, stride 20; constructors `005ce400` …) covers 8 kinds. Each row holds a name, byte offset, value type code, flags, and mask or index bound. Type codes seen: 16 numeric member and 17 bit property (mask in last word), both CONFIRMED (§6 `:255-256`). The pointer-typed codes are inferred from member names (HIGH CONFIDENCE): 1 entity, 5 string, 6 function, 7 font, 8 bmap, 11 panel, 18 view, 19 material. Separately, type 3 means local/array values resolving to `+0x10` (§11 `:414`). Full rows are in [`member-descriptors.json`](../../reverse/decompiled/runtime/member-descriptors.json). The JSON preserves offsets as recovery evidence; it is **not** a native ABI (§6 `:257`).

#### ENTITY (table `0x0061b780`, 141 rows) — selected
| Offset | Size | Type | Meaning | Evidence | Confidence |
|---|---|---|---|---|---|
| +0x0c | 4 | var | LIFESPAN | member-descriptors.json | CONFIRMED |
| +0x10/+0x14/+0x18 | 12 | var[3] | X, Y, Z (POS) | same | CONFIRMED |
| +0x1c/+0x20/+0x24 | 12 | var[3] | PAN, TILT, ROLL. VEL_X/Y/Z and TEX_SCALE alias these offsets. | same | CONFIRMED (aliasing); the particle interpretation of the aliases is a HYPOTHESIS |
| +0x28/+0x2c/+0x30 | 12 | var[3] | SCALE_X/Y/Z. SIZE, BMAP (t8) and FUNCTION (t6) alias these. | same | CONFIRMED |
| +0x34 | 4 | bits | FLAGS: FLAG1..8 `0x1..0x80`, INVISIBLE `0x100`, PASSABLE `0x200`, TRANSPARENT `0x400`, MOVE/UVSPEED `0x800`, ORIENTED/OVERLAY `0x1000`, FACING `0x2000`, NEAR `0x4000`, FLARE `0x8000`, NOFILTER `0x10000`, UNLIT `0x20000`, SHADOW `0x40000`, LIGHT `0x80000`, NOFOG `0x100000`, BRIGHT/BLEND `0x200000`, METAL/DECAL `0x400000`, CAST `0x800000`, POLYGON `0x4000000` | same | CONFIRMED |
| +0x38/+0x3c/+0x40 | 12 | var | FRAME/CYCLE, NEXT_FRAME, SKIN (SKILL_X/Y/Z alias) | same | CONFIRMED |
| +0x44/+0x48/+0x4c | 12 | var | AMBIENT (GRAVITY alias), ALBEDO, ALPHA | same | CONFIRMED |
| +0x50…+0x5c | 16 | var | LIGHTRANGE, RADIANCE/LIGHTBLUE/BLUE `+0x54`, LIGHTGREEN/GREEN `+0x58`, LIGHTRED/RED `+0x5c` | same | CONFIRMED |
| +0x60 | 4 | bits | EVENT_FLAGS: DYNAMIC `0x1`, ENABLE_BLOCK `0x2`, ENTITY `0x4`, STUCK `0x8`, PUSH `0x10`, IMPACT `0x20`, FRICTION `0x40`, DETECT `0x80`, SCAN `0x100`, SHOOT `0x200`, TRIGGER `0x400`, SONAR `0x800`, FRAME `0x1000`, RECEIVE `0x2000`, TOUCH `0x10000`, RELEASE `0x20000`, CLICK `0x40000`, RIGHTCLICK `0x80000`, DISCONNECT `0x100000` (SKILL_A aliases the word). If the word is nonzero, removal cancels processes and clears pointers. | same; §13 `:513`, §27 `:1141-1145` | CONFIRMED |
| +0x64 | 4 | bits | FLAGS2: NARROW `0x1`, FAT `0x2`, LOCAL `0x4`, NATIVE `0x8`, CLIENT `0x10` | same | CONFIRMED |
| +0x68…+0x7c | 24 | var | MIN_X/Y/Z, MAX_X/Y/Z | same | CONFIRMED |
| +0x80/+0x84/+0x88 | 12 | var | TRIGGER_RANGE, PUSH, FLOOR_DIST | same | CONFIRMED. Level transition "clears entity+80" (§13 `:510`); whether that is this field is UNKNOWN. |
| +0x8c | 4 | bits | NOSEND_* (`0x1`…`0x1000`, NOSEND `0x8000`) | same | CONFIRMED |
| +0x94 | 4×100 | var[] | SKILL1..100 (bound `0x64`) | same | CONFIRMED |
| +0x224/+0x228 | 8 | var, ptr | POSE; MATERIAL (t19) | same | CONFIRMED |
| +0x22c/+0x230 | 8 | var | U/V (SPEED_U/V) | same | CONFIRMED |
| +0x238 | 4 | bits | UNTOUCHABLE `0x1`, SPOTLIGHT `0x2`, VISIBLE `0x10000` | same | CONFIRMED |
| +0x240 | 4 | func | EVENT | same | CONFIRMED |
| +0x248…+0x264 | 32 | ptr | ENTITY1/2, STRING1/2, PANEL1/2, ACTION1/2 | same | CONFIRMED |
| +0x274 | 4 | ptr | PARENT | same | CONFIRMED |

Native: `Object::Kind::Entity` with `Object::fields`, plus `EntityData` (model, frames, light cache, physics flags) in `native/engine.hpp`. Bit properties resolve through `nativeMasks[]` and `core_mask` (`native/engine.cpp`), for example `{Entity,"BRIGHT","flags",0x00200000}`.

#### TEXT (`0x0061db60`, 28 rows) and FONT
| Offset | Size | Type | Meaning | Evidence | Confidence |
|---|---|---|---|---|---|
| TEXT+0x0c | 4 | int | UI object type; 4 = TEXT (dispatch `0040e180`) | [`FONT_LAYOUT.txt:24-27`](../../reverse/decompiled/runtime/FONT_LAYOUT.txt) | CONFIRMED |
| TEXT+0x14/+0x18 | 8 | var | POS_X, POS_Y | member table; FONT_LAYOUT | CONFIRMED |
| TEXT+0x1c | 4 | bits | flags: D3D `0x1`, CONDENSED `0x2`, CENTER_X `0x100`, CENTER_Y `0x200`, TRANSPARENT `0x400`, NARROW `0x2000`, VISIBLE `0x4000`, SHADOW `0x40000`, OUTLINE `0x80000` | same | CONFIRMED |
| TEXT+0x20…+0x2c | 16 | var/ptr | ALPHA, OFFSET_Y, LINES, FONT | same | CONFIRMED |
| TEXT+0x30/+0x34 | 8 | int/ptr | Internal STRING count / array | FONT_LAYOUT `:30-31` | CONFIRMED |
| TEXT+0x38…+0x64 | — | var | SCALE_X/Y, BLUE/GREEN/RED `+0x40/+0x44/+0x48`, STRINGS `+0x4c`, STRING `+0x50`, INDEX `+0x54`, CHAR_X/Y `+0x58/+0x5c` (read-only), SIZE_Y `+0x64` | same | CONFIRMED |
| FONT+0x0c | — | bitmap | Embedded bitmap when cell W ≥ 4; image width/height at `+0x18/+0x1c` | FONT_LAYOUT `:44-47` | CONFIRMED |
| FONT+0x6c/+0x70 | 8 | int | Cell W, H in pixels (not 22.10) | same | CONFIRMED |
| FONT+0x74/+0x78 | 8 | int | Glyph count (≤256); columns, or −1 for a system font (W<4) | same | CONFIRMED |
| Lists | — | — | FONT list head `0x006444f8`; UI list head `0x0065f1e8`; active palette `0x007192bc` | FONT_LAYOUT `:27`; CONTRACT (second) §40 `:2194-2199` | CONFIRMED |

Native: `Object::Kind::Text`/`Font`; `Engine::BitmapFont { FontAtlas atlas; indices }`, `fontPalette_` (768 B) and `pe_font.hpp`.

#### VIEW (`0x00620dd8`, 46 rows) — selected
LAYER `+0x10`; POS_X/Y `+0x14/+0x18`; flags `+0x1c` (REMEMBER `0x2`, CUBE `0x4`, TRANSPARENT `0x400`, VISIBLE `0x4000`, AUDIBLE `0x8000`, NOCULL `0x40000`, LINES `0x80000`, SKYVISIBLE `0x100000`, PORTALCLIP `0x200000`, NOSHADOW `0x400000`, NOPARTICLE `0x800000`, PORTALVISIBLE `0x2000000`); SIZE_X/Y `+0x20/+0x24`; X/Y/Z `+0x28..+0x30`; PAN/TILT/ROLL `+0x34..+0x3c`; OFFSET_X/Y `+0x40/+0x44`; ARC `+0x48`; ASPECT `+0x4c`; AMBIENT `+0x50`; FOG `+0x54`; ALPHA `+0x58`; PORTAL_X/Y/Z `+0x5c..+0x64`; PNORMAL `+0x68..+0x70`; FOG_START/END `+0x74/+0x78`; DIAMETER `+0x7c`; GENIUS `+0x80` (t1); PORTAL `+0x84` (t18); BMAP `+0x88`; CLIP_NEAR/FAR `+0x8c/+0x90`. Source: member-descriptors.json. **CONFIRMED.** Native: `Object::Kind::View`; `State::create` sets `aspect=1` (native comment cites VIEW constructor `004210d0` storing raw 0x400 at `+0x4c`). Camera details are in [../CAMERA_SYSTEM.md](../CAMERA_SYSTEM.md).

#### PANEL (`0x0061e700`, 16 rows)
POS_X/Y `+0x14/+0x18`; flags `+0x1c` (REFRESH/D3D `0x1`, TRANSPARENT `0x400`, OVERLAY `0x1000`, VISIBLE `0x4000`); ALPHA `+0x20`; BMAP `+0x24`; SCALE_X/Y `+0x28/+0x2c`; BLUE/GREEN/RED `+0x30/+0x34/+0x38`; ON_CLICK `+0x48` (t6). **CONFIRMED** (member-descriptors.json). Native: `Object::Kind::Panel`; `Engine::Button` and `DisplayObject` (`native/engine.hpp`).

#### MATERIAL (`0x0061e418`, 36 rows) — selected
AMBIENT B/G/R `+0x0c..+0x14`; DIFFUSE `+0x18..+0x20`; SPECULAR `+0x24..+0x2c`; EMISSIVE `+0x30..+0x38`; MAP `+0x3c..+0x44`; ALPHA `+0x48`; POWER `+0x4c`; ALBEDO `+0x50`; SCALE1/2 `+0x54/+0x58`; CYCLE `+0x5c`; SKILL `+0x60` (bound 0x14); MATRIX `+0xb0..+0xef`; flags `+0xf0` (TANGENT `0x10000`, ENABLE_RENDER `0x20000`, ENABLE_VIEW `0x40000`); EFFECT `+0xf4`; SKIN1..4 `+0xfc..+0x108`; EVENT `+0x10c`. **CONFIRMED.** Native: `State::create` births a MATERIAL with ambient/diffuse/map 255, alpha 100, albedo raw `0xc8df`, cycle 480 (comment cites `004159d0`→`00415390`). See also `ScriptMaterialState` in `native/render.hpp`.

#### VECTOR, numeric array, STRING
- VECTOR (`0x00620b88`): X/PAN/BLUE `+0x4`, Y/TILT/GREEN `+0x8`, Z/ROLL/RED `+0xc`.
- Numeric array (`0x00620c80`): header word `+0x0` (info `0x01000000`, nsave `0x10000000`), length `+0xc`, elements from `+0x10` (X..W/A up to `+0x1c`). Its element offsets match the local objects in §4.
- STRING (`0x00620dac`): INVISIBLE is mask `0x2` at `+0x14`.
- **CONFIRMED.** Native maps these to `Object::Kind::Vector` `array[0..2]` via `core_alias` (`native/engine.cpp`).

#### BMAP / image records
| Record | Fields | Evidence | Confidence |
|---|---|---|---|
| Standalone/script image | width `+0x0c`, height `+0x10`, bytes-per-pixel `+0x14` | [`bitmap-cpu-storage/contract.txt:37-39`](../../reverse/decompiled/runtime/bitmap-cpu-storage/contract.txt) | CONFIRMED |
| WMB CPU bitmap | width `+0x10`, height `+0x14`, bpp `+0x18` (TURB9 gate `==2`), pixels `+0x60` | same `:7-21` | CONFIRMED |
| Script BMAP wrapper | Other fields UNKNOWN in this pass | — | UNKNOWN |

Native: `Object::Kind::Bmap`, plus `Engine::bitmapImages` (`ObjectId`→`Image`); `svc_bitmap` admits Object or Number raw handles (§30).

### 5.3 VECTOR temporary ring
`[0x0063ce30, 0x0063d130)`, 64 slots × 12 bytes; cursor `0x0061d7e8` advances +12 before use (first slot returned `0x0063ce3c`). **CONFIRMED.** Sources: §18 `:663-672` and [`vector-temporary/layout.json`](../../reverse/decompiled/runtime/vector-temporary/layout.json). Native: `Engine::vectorTemps_`, `vectorTempCursor_`.

## 6. DLL interface and plugin handles
| Address / offset | Meaning | Evidence | Confidence |
|---|---|---|---|
| `0x006438b0` | A6 engine interface passed to `A6_DLLAPI` | §2 `:65-67`, §17 `:652-654` | CONFIRMED |
| interface+0xa30 | Name resolver `0040c200` (scans the 317 API rows, case-insensitive) | `registry/K22.exe.0040c290.c:19-23`; `lookup/K22.exe.0040c200.c` | CONFIRMED |
| interface+0xa34 / +0xa38 | Pointers to MY `0x006441d8` / YOU `0x006441dc` | same | CONFIRMED |
| `0x00644380` | A5 interface (`A5_DLLAPI`, accepted when the result is <512) | §17 `:655` | CONFIRMED |
| `0x00644730..0x0064482c` | Up to 64 retained module handles | §17 `:635` | CONFIRMED |
| kabus_dll `0x101a1b4c…0x101a1804` | Resolved engine API pointers stored by the patch DLL (preferred base) | `RECOVERY.txt:77-89` | CONFIRMED |

Native: no PE ABI. `Engine::initialize_plugins` and `svc_plugin_open` set `pluginNative`/`pluginGamma`, and `iniValues` holds the K22.ini map.

## 7. Input slot tables
| Address | Content | Evidence | Confidence |
|---|---|---|---|
| `0x0061b108` (stride 8, sentinel `0x0061b3d0`) | 89 key names → 80 slots (name, index) | §12 `:427-430`; [`input-contract.json`](../../reverse/decompiled/runtime/input-contract.json) | CONFIRMED |
| `0x006659a8 + 4·code` | Keyboard state, codes 0..126 (raw 1024 when held or freshly down) | §12 `:434`, `:448-449` | CONFIRMED |
| `0x00665930..0x0066595c` / `0x00665c70..0x00665c9c` | JOY_1..12 / JOY2_1..12 | §12 `:435-436` | CONFIRMED |
| `0x0066597c/80/84` | MOUSE_LEFT/RIGHT/MIDDLE | §12 `:437` | CONFIRMED |
| `0x0063ba04` | ON_ANYKEY callback cell | §12 `:456` | CONFIRMED |
| `0x0066573c` | `_arg1` pointer slot, set/restored per event | §12 `:455` | CONFIRMED |
| PE `.data` tail from `0x0063c000` | Loader-zeroed state cells | §12 `:464-467` | CONFIRMED |

Native: `Engine::keyboard_`/`keyboardCallbacks_` (`std::array<Slot*,128>`), `joystick_`/`joystickCallbacks_` (2×12), `mouse_`/`mouseCallbacks_` (3), filled from `nativeKeys[]` (89 rows) in `Engine::initialize_globals`. The level command mask `6557c` and filename `65580` are in §13 `:501-503` (CONFIRMED; the CONTRACT gives these short forms, and the full VA prefix is not restated there).

## 8. Persistence formats (as consumed by scripts)

All game files below are written through WDL `file_*` calls, not engine save APIs. No `game_save`/`game_load` call appears in the extracted original scripts (grep, CONFIRMED). `file_asc_write` emits one byte per value. Native (`Engine::service_builtin`) writes the low byte of `raw>>10`, and `file_asc_read` returns 0..255 or −1 at EOF. Native files live under `$XDG_DATA_HOME/kabus22` (default `~/.local/share/kabus22`; `Engine` `saveRoot`) via `svc_private`. Reads fall back to read-only archive/game assets.

### 8.1 `options.k22` (campaign settings; 31 bytes original, 32 with the patch)
Reader `GenelAyarlar.wdl:GA_AyarlariYukle`, writer `GA_AyarlariKaydet` (identical in the ISO and extracted copies). Validator: `installer/launcher_settings.cpp` `validateCampaign`.

| Offset | Size | Type | Meaning (script variable) | Stored transform / range | Evidence | Confidence |
|---|---|---|---|---|---|---|
| 0 | 1 | u8 | `GA_Cozunurluk` resolution index | ≤3 | script order; validator maxima | CONFIRMED |
| 1 | 1 | u8 | `GA_RenkKalitesi` colour quality | ≤1 | same | CONFIRMED |
| 2 | 1 | u8 | `GA_IsikKalitesi` light quality | ≤1 | same | CONFIRMED |
| 3 | 1 | u8 | `GA_Tamponlama` buffering | ≤1 | same | CONFIRMED |
| 4 | 1 | u8 | `GA_Mipmapping` | ≤1 | same | CONFIRMED |
| 5 | 1 | u8 | `GA_Anisotropy` | ≤1 | same | CONFIRMED |
| 6 | 1 | u8 | `GA_Yansima` reflections | ≤1 | same | CONFIRMED |
| 7 | 1 | u8 | `GA_KanRengi` blood colour | ≤1 | same | CONFIRMED |
| 8 | 1 | u8 | `GA_KanMiktari` blood amount | ≤100 | same | CONFIRMED |
| 9 | 1 | u8 | `GA_Parlaklik` brightness | stored +100, ≤200 | `GA_Parlaklik-=100` on load | CONFIRMED |
| 10 | 1 | u8 | `GA_Karsitlik` contrast | stored +80, ≤160 | `-=80` on load | CONFIRMED |
| 11 | 1 | u8 | `GA_Gamma` | stored +100, ≤200 | `-=100` on load | CONFIRMED |
| 12 | 1 | u8 | `GA_Efekt` effects volume | ≤100 | same | CONFIRMED |
| 13 | 1 | u8 | `GA_Muzik` music volume | ≤100 | same | CONFIRMED |
| 14 | 1 | u8 | `GA_Dialog` dialogue volume | ≤100 | same | CONFIRMED |
| 15–24 | 10 | u8[10] | `GA_Tuslar[0..9]` keyboard scancodes for `GA_Ileri`(0), `GA_Geri`, `GA_Sag`, `GA_Sol`, `GA_SilahKaldir`, `GA_Ates`, `GA_Kosma`, `GA_Menu`, `GA_Harita`, `GA_Buyu`(9). Default (72,80,77,75,31,32,30,50,35,16). | Unique scancodes from `keys()` | `GenelAyarlar.wdl` DEFINEs + `Tuslari_Esitle` | CONFIRMED |
| 25–30 | 6 | u8[6] | `GA_JTuslar[0..5]` joystick buttons (raise weapon, fire, run, menu, map, spell). Default 1..6. | 1..12 | same | CONFIRMED |
| 31 | 1 | u8 | `DD_Dil` language (patch only). Native/launcher: 0 tr, 1 en, 2 de. | ≤5 | `reverse/extracted/patch/GenelAyarlar.wdl:290`; `Engine::configure_language` appends this byte to 31-byte files | CONFIRMED |

Observed bytes: the ISO-installed `~/.local/share/kabus22/game/options.k22` is 31 B, beginning `02 01 00 00 01 00 01 00 32 64 50 82 41 5a 64` and followed by keys `48 50 4d 4b 1f 20 1e 32 23 10` and joystick bytes `01..06`. The Lutris (patched) copy is 32 B with trailing byte `01` (xxd, CONFIRMED).

### 8.2 Save slots
| File | Format | Writer / reader | Evidence | Confidence |
|---|---|---|---|---|
| `kysd.sav` | Text: `file_var_write(Aktif_SaveNo)`, `file_var_write(SLG_KayitSayisi)`, then each slot label via `file_str_write` + `"\n"`; at most `SLG_MaxKayit`=99 | `YukleMenu.wdl:SLG_Kaydet` / `SLG_Yukle`; also read by `GA_AyarlariYukle` | Scripts. The installed sample starts `27 27 01)Demir - Ofis` per [`script_semantics.json`](../../reverse/extracted/script_semantics.json) `save_layout_correction_evidence`. | CONFIRMED |
| `kayit<N>.sav` | Positional stream of 2030 one-byte `file_asc_write` values; no XYZ position | `Yeni_Oyun_Deger.wdl:YD_OyunuKaydet` / `YD_OyunuYukle`. The filename is `"kayit"+str_for_num(Aktif_SaveNo)+".sav"`. | Same JSON. Installed `kayit0.sav`/`kayit27.sav` are both 2030 B. CONTRACT §46 `:1880-1881` notes the writer does not save XYZ. | CONFIRMED |
| `ftz.k22` | 3 bytes: days, hours, minutes (total play time = d·1440 + h·60 + m) | `GenelAyarlar.wdl:GA_AyarlariYukle` | Script; JSON reports 3 B installed | CONFIRMED |

`kayit<N>.sav` layout (zero-based offsets; full table and line numbers in `script_semantics.json`):

| Offset | Bytes | Content | Confidence |
|---|---|---|---|
| 0 | 12 | `save_versiyon` (100), `LY_AktifLevelID`, `Aktif_Player`, `Aktif_Silah`, `akt_music_idno`, `koruyucu_mana`, `oyn_yaralanma`, `koru_yaralanma`, `buyu_no`, `lain_sona_erdi`, `ebru_yaralanma`, `OyunZorluk` | CONFIRMED (script) |
| 12 | 48 | `ari_aktiflevel`; `ari_ani_dizi[10]`, `ari_gel_dizi[6]`, `ari_adet_dizi[30]`; `atilgan_sayisi` | CONFIRMED |
| 60 | 306 | Ordered story/progression scalars and arrays | CONFIRMED (count) |
| 366 | 150 | Demir door states | CONFIRMED |
| 516 | 67 | Demir map groups | CONFIRMED |
| 583 | 208 | Demir inventory, weapons, ammo, magazines, documents | CONFIRMED |
| 791 | 213 | Ebru inventory block | CONFIRMED |
| 1004 | 212 | Active inventory block | CONFIRMED |
| 1216 | 250 | `unic_obje_dizi` | CONFIRMED |
| 1466 | 100 | `varil_dizi` | CONFIRMED |
| 1566 | 3 | `v_zipilk` story state | CONFIRMED |
| 1569 | 3 | Elapsed days/hours/minutes | CONFIRMED |
| 1572 | 11 | Counters, flags, six puzzle-solved flags | CONFIRMED |
| 1583 | 100 | `latex_gel_dizi` | CONFIRMED |
| 1683 | 80 | Scalars, `v_yaziyaz_ok[4]`, `autosave[32]` | CONFIRMED |
| 1763 | 108 | Map groups 0..12 | CONFIRMED |
| 1871 | 150 | Active door states | CONFIRMED |
| 2021 | 9 | `taskoy_esle_dz` | CONFIRMED |

The sample `kayit0.sav` starts `64 16 00 03 0c …`: version 100, level 22, player 0, weapon 3, music 12 (CONFIRMED by bytes in the JSON). These Lutris-install samples are not asserted to be pristine (`script_semantics.json`). The ISO `Yeni_Oyun_Deger.wdl` differs from the extracted copy only in a commented new-game defaults block, not in `YD_OyunuKaydet` (diff, CONFIRMED). Values above 255 or negative values truncate to one byte: the native behaviour is CONFIRMED, the original byte semantics are a HYPOTHESIS consistent with the samples.

### 8.3 Bonus `mode_ayarlar.k22` (5 text lines)
Reader `K22_mode.wdl` (ISO, `reverse/media/iso/mode/scripts/K22_mode.wrs/K22_mode.wdl:428-442`) uses `txt_load` + `str_to_num` per line. Row 0 is `GA_Cozunurluk`. Row 1 is `GA_Shader_Seviyesi`. Row 2 is anisotropy (1→2 → `d3d_anisotropy`). Row 3 is `muzik_cal_ok`. Row 4 is brightness, used as `cbt_ekran_parlaklik = value-50`. **CONFIRMED** (script). The shipped `game/Kabus 22 Mode/mode_ayarlar.k22` bytes are `2\r\n2\r\n0\r\n1\r\n50` (xxd, CONFIRMED). Native validator: `installer/launcher_settings.cpp` `decodeBonus` (ranges 0..3/0..2/0..1/0..1). Observed native consumption: second-series CONTRACT §42 `:2383-2385` (rows `2/2/0/1/55`).

### 8.4 `K22.ini` (patch plugin)
An INI map owned by `kabus_dll.dll`. Native: `svc_ini_load`/`svc_ini_parse`/`svc_ini_save` (`native/engine.cpp`) store `section:key` in `Engine::iniValues` and normalise `Screen:Mode` (≤2), `Screen:X` (≤3840) and `Screen:Y` (≤2160). The native normalisation is CONFIRMED by code. Original plugin semantics are in `RECOVERY.txt:102-104` (CONFIRMED); the bounds come from the native code only (HIGH CONFIDENCE they mirror patch expectations, not independently recovered).

## 9. Native counterpart summary
| Original concept | Native type (file) |
|---|---|
| 22.10 var / pointer word | `Value{Kind,bits,managed}` (`native/value.hpp`) |
| Registry/member storage cell | `Slot{Value,callback}`, `Ref{slot,owner,field,index,bitMask}` (`value.hpp`) |
| Engine objects (ENTITY/PANEL/TEXT/VIEW/BMAP/FONT/MATERIAL/STRING) | `Object{Kind,id,name,alive,fields,array,text,properties}` in the `State` handle table (`value.hpp`) |
| Continuation record | `Process`, `LocalSnapshot` (`native/wdl.cpp`) |
| Function descriptor | `Function` (`native/wdl.cpp`) |
| MY/YOU context | `ProcessContext{processId,my,you}` (`value.hpp`) |
| WRS entry table | `Assets::Entry` (`native/assets.hpp`) |
| Entity runtime extras | `EntityData`, `EntityLightCache` (`native/engine.hpp`) |
| File handles | `FileData{bytes,cursor,output,writable}` (`native/engine.hpp`) |
