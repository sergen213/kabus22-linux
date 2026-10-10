# Camera System

Purpose: describe how the original Kabus 22 scripts drive the A6 `camera`, other VIEWs, cutscenes and mirrors; how the native engine turns those VIEWs into projections; and what has been verified, including widescreen.

Related: [Reverse-engineering index](REVERSE_ENGINEERING.md) · [Engine architecture](ENGINE_ARCHITECTURE.md) · [Gameplay systems](GAMEPLAY_SYSTEMS.md) · [Script system](reverse-engineering/SCRIPT_SYSTEM.md) · [Original behavior](reverse-engineering/ORIGINAL_BEHAVIOR.md) · [Data structures](reverse-engineering/DATA_STRUCTURES.md) · [Function map](reverse-engineering/FUNCTION_MAP.md)

Confidence labels follow [Gameplay systems](GAMEPLAY_SYSTEMS.md#conventions-and-sources). Script line numbers refer to `reverse/extracted/original/` (cam.wdl and efect2.wdl are byte-identical in the ISO edition, per `reverse/media/iso/script-comparison.json`). `CONTRACT.txt` means `reverse/decompiled/runtime/CONTRACT.txt`.

## 1. Model: one camera action per level, no camera-mode setting

- The original game has no camera-mode option and no first-person camera. **CONFIRMED** (no first-person action in the ISO `cam.wdl`; no camera option in `GenelAyarlar.wdl`). Community patch 1.8.5 adds an F5 first-person camera (`fps_camera_control`, patch `cam.wdl:77-170`). Native offers the same camera as an opt-in engine option for the original scripts, `--camera first-person` (§7.1). The default stays `original`.
- `cam.wdl` (7316 lines) holds 109 `action` blocks, roughly one per level or area (e.g. `start4_camera` :276, `level4_camera2` :422, `mekke_camera` :891, `sd_camera1` :1071, `boss_cam` :4345, `tnel*_camera` :4499-6220). **CONFIRMED** (`grep -c '^action'` = 109).
- Each action is attached to an entity placed in the level WMB. The `LV_*.wdl` level scripts never select a camera; source comment `cam.wdl:276` reads "attach it to fish.mdl". **HIGH CONFIDENCE** (no WDL assigns these actions; WMB entity action attachment is the A6 mechanism, see CONTRACT §24).
- Standard prologue of nearly every camera action: entity `invisible`/`passable`, `camera.pos_x/pos_y=0`, `camera.size_x/size_y=screen_size`, an authored `camera.arc` (60 most common; 62-80 observed, e.g. `cam.wdl:164`=80, `:211`=60, `:287`=62), `camera.visible=on`, then a `while(1) … wait(1)` loop rewriting `camera.x/y/z/pan/tilt/roll`. **CONFIRMED**
- Entity skills/flags parameterise the camera from the level editor: `Cam_No`=SKILL1, `Cam_Arc`=SKILL2, `Effect_Mesafesi`=SKILL3, `Max/Min_Yukseklik`, `Max/Min_Roll`, `Max/Min_Tilt` SKILL4-9, flags `Sapik_Cam`, `Sabit_Cam`, `Daire_Hareketi` (`cam.wdl:38-49`); path cameras `takip_uzunluk` etc. (`:51-55`). **CONFIRMED**

## 2. Camera behaviours found in scripts

| Behaviour | Examples | Mechanism | Status |
|---|---|---|---|
| Anchored fixed camera with player-driven offsets (dominant) | `wc_camera` :149, `yuy_camera` :1383 | start from the entity pose; per-zone `if(player.x<=…)` branches add linear offsets to x/y/z/pan/roll and select a per-zone ARC | CONFIRMED |
| Distance-blended look-at | `start4_camera2` :342, `level4_camera2` :422 | within `Effect_Mesafesi`, lerp height/roll/tilt between Max_/Min_; `vec_to_angle` aims at the player | CONFIRMED |
| Path-follow | `strt5_camera_init` :950 (`ent_path("path_cam")` :964), `level5_camera` :1759 (`path_oda`), `strt3_camera_init` :1125 | follows a WED path after the player spawns | CONFIRMED |
| Zone switch controllers | `Strt4_cam_kontrol` :393 sets `start4_cam_no` (:274); `LVL4_cam_kontrol1` :536 (`level4_cam_no` :419); `level5_camkontrol` :1942 (`lv5_camtip` :1756); `cami_cam_mod` :1503 | separate invisible trigger entities write the mode global read by the camera actions; these are the "trigger regions" | CONFIRMED |
| Third-person orbit | `3rd_camera` :75 with `validate_view` :110 (two `trace` rays pull the camera out of walls) | behind `player.pan` at distance `3rdMesafe` | CONFIRMED defined; **HYPOTHESIS** unused (no other WDL reference found by grep) |
| Additive shake | `bombacam_salla_s/_m` :1-2, `bombacam_sallan_fx` :4, starter `bombacam_salla_strtr` :~21; boss `seytancam_salla_fx` :6301 | every action adds `random` pan/tilt jitter when active; triggered from `efect2.wdl`, `silah.wdl`, `babayaro_yara.wdl`, `cinematic.wdl`, `koruyucu.wdl` | CONFIRMED |
| Animated ARC | `klasik_generate.wdl:393-448` (ARC oscillates 65↔70); `koruyucu.wdl:2940-2991` (spell zoom `camera.arc-=…*time`, then restored) | scripted FOV change | CONFIRMED |
| HUD tied to ARC | `silah.wdl:128` positions `SilahPanel.x` from `camera.arc` | UI depends on authored ARC | CONFIRMED |

## 3. Cutscene camera (`Video_Cam`)

- Declared `K22.wdl:102 view Video_Cam {}`. **CONFIRMED**
- Swap pattern (e.g. `cinematic.wdl:509-520`): set `Video_Cam.arc`, `size_x/y=screen_size`, `pos=0`, aim with `vec_to_angle`, `camera.visible=off`, `Video_Cam.visible=on`, hide HUD panels; reverse at `:561-562`. **CONFIRMED**
- Also used by puzzles and set pieces: `BUL_Board.wdl:507-530`, `BUL_Kordinat.wdl:312-353`, `BUL_Saat.wdl:395-445`, `Ornek_Yasin.wdl` (ARC 60-80), `boss.wdl:1375-1422`, `dinamik_modeller.wdl:831-1087` (`Video_Cam.clip_near=0` :851), `kotu_koruyucu.wdl:6214-6568`, `seytan.wdl:975-1014`, `cam.wdl:3301`. `cinematic.wdl:7642 kelkafa_camera` orbits then dollies. **CONFIRMED**
- Full-screen video cutscenes are separate: `media_play` with `media_layer`=900 in `LevelYonetim.wdl LevelYukle` (see [Gameplay systems §12](GAMEPLAY_SYSTEMS.md#12-level-management-and-story-flow)). `korku_sahneleri.wdl` uses no camera/VIEW writes. **CONFIRMED**

## 4. VIEW fields used by scripts

| Field | Writers (examples) | Notes |
|---|---|---|
| `x,y,z,pan,tilt,roll` | `cam.wdl:96-103`, `klasik_generate.wdl:408-430` | every frame |
| `arc` | `cam.wdl:164`, `cinematic.wdl:510`; mirror `efect2.wdl:1706` (`-camera.arc`) | signed; negative flips X |
| `aspect` | only the mirror: `efect2.wdl:1705` (`=camera.aspect`), `:1723` (`=-camera.aspect`) | no script writes `camera.aspect`; birth value raw 0x400 = 1.0 |
| `size_x/size_y`, `pos_x/pos_y` | `cam.wdl:87-88,155-156`, `GrafikMenu.wdl:237-238`, `cinematic.wdl:511-514` | `size = screen_size` |
| `visible` | `cam.wdl:86`, `cinematic.wdl:519/562`, `LevelYonetim.wdl:754`, `Kabusdismenu.wdl:141` | camera ↔ Video_Cam swap |
| `clip_near` | `cam.wdl:92` (1), `Kabusdismenu.wdl:142` (10), `LV_massotaltkat.wdl:12/24` (5/10) | `clip_far` never written |
| `ambient` | `K22.wdl:849/889`, `cino_yara.wdl:44-65` (flicker), `LV_Start6.wdl:16`, `LV_Startwc.wdl:32` | |
| `portal`, `genius`, `fog` (read), `noshadow`, `portalclip`, `nocull` | `efect2.wdl:1686-1739` mirror | see §6 |
| `offset_x/offset_y` | none | |

All rows **CONFIRMED** by scout grep over the reference script copy. VIEW declarations are only `Video_Cam` and `mirror`; `camera` is engine-created.

## 5. Original executable: VIEW layout and projection

| Fact | Evidence | Status |
|---|---|---|
| VIEW offsets: flags +0x1c (VISIBLE 0x4000, NOCULL 0x40000, PORTALCLIP 0x200000, NOSHADOW 0x400000, PORTALVISIBLE 0x2000000); ARC +0x48; ASPECT +0x4c; PORTAL_X/Y/Z +0x5c..; PNORMAL +0x68..; GENIUS +0x80; PORTAL +0x84; BMAP +0x88; CLIP_NEAR/FAR +0x8c/+0x90 | `reverse/decompiled/runtime/PORTAL_LAYOUT.txt:36-37` and layout section | CONFIRMED (static) |
| VIEW constructor `004210d0` stores ASPECT raw 0x400; other constructor words are not public defaults | `reverse/decompiled/runtime/view-birth-defaults/profile.json` | CONFIRMED (static) |
| `0044e260` uses absolute ARC for horizontal screen scale, clamped 1..175°, and absolute ASPECT as scale ratio; `0044f530` restores signs with copysign | `PORTAL_LAYOUT.txt:189-194` | CONFIRMED (static) |
| Exclusive fullscreen applies `Q=(H*320)/(W*240)`; windowed uses `Q=1`; original and community-patch projection code are byte-identical | `CONTRACT.txt:2156-2163` (second §39) | CONFIRMED (static Ghidra) |
| `video_set`/`video_switch` size cap: 1920×1200 original, 3840×2400 patch | `CONTRACT.txt:2150-2151` | CONFIRMED (static) |
| Runtime behaviour on Windows | not observed | UNKNOWN — no Windows run |

## 6. Mirrors and water (portal VIEW)

- Script side: `efect2.wdl:1686 view mirror {}`; `init_mirror` loop (`:1688-1739`) links `camera.portal=mirror`, copies `genius=player`, `fog`, sets `noshadow/portalclip/nocull`, and reflects the pose itself: wall mirror `mirror.arc=-camera.arc`; water `mirror.aspect=-camera.aspect` and z mirrored about `portal_z`. Clears `camera.portal` at `:1736`, `LevelYonetim.wdl:755`, `Kabusdismenu.wdl:139`. **CONFIRMED**
- Binary side (`PORTAL_LAYOUT.txt:71-128`): frame loop `004500b0` (called from `004136b0`) renders the linked target VIEW into the owner viewport (`0044ed80`→`0044ec40` load target pose/ARC/ASPECT; `0044df20` owner viewport). Mirror surfaces are material flag 0x2000 (`00444eb0`), the FIRST processed axis-classifiable surface (|n|>0.9) wins, and the discovered PORTAL_X/Y/Z and PNORMAL are published for the NEXT frame. CPU clipping `0044bcc0` (hardware hook `0042b770` is a bare RET). **CONFIRMED** (static)
- Native: `Engine::tick` publishes `portal_x/y/z` and `pnormal_*` from `Renderer::portal_surface` (`native/render.cpp`) back into the VIEW each tick; `Renderer::portal_surface` selects the first visible eligible 0x2000-material triangle. **CONFIRMED** (code). Exact parity with original surface ordering/one-frame delay: **UNKNOWN**.
- Native GL portal clip-plane fix (every camera uploads its own plane, zero for unclipped views): `CONTRACT.txt:966-971`. **CONFIRMED** (llvmpipe consumer).

## 7. Native implementation

| Symbol | File | Role |
|---|---|---|
| `struct Camera` | `native/render.hpp` | per-view packet: position/angles, `horizontalFov`, `referenceAspect`(4/3), signed `pixelAspect`, `widescreen` flag, clip planes, portal fields |
| `camera_matrix` | `native/render_policy.hpp` | ARC is the horizontal FOV; vertical scale = horizontal / (`widescreen` ? `referenceAspect` : viewport aspect) × \|`pixelAspect`\|; negative ARC flips X, negative pixelAspect flips Y; throws unless 0<\|arc\|<179 |
| `Engine::make_frame` (lambda `camera_for`) | `native/engine.cpp` | builds a `Camera` for every visible VIEW and every hidden VIEW referenced as a portal; sets `widescreen=false`; applies `display_aspect`; a VIEW with `bmap` (render-to-bitmap) throws (unsupported) |
| `Engine::display_aspect` | `native/engine.cpp` | returns `1/(ASPECT*Q)` with `Q=H*320/(W*240)` only in native exclusive fullscreen, else `Q=1`; source comment cites original `0044e260/0044cd00` "screen-mode factor, not a patch-added Hor+ projection" |
| `Engine::configure_display` / `prepare_display` | `native/engine.cpp` | admitted only for the auto-selected original `K22.wdl`; recompute menu/HUD/text geometry and set the MAIN camera `size_x/size_y` to the window; other VIEW rectangles untouched |
| `video_switch` / `video_set` builtins | `Engine::service_builtin` | native display mode re-prepares display instead of switching modes; `--display authored` keeps the original preset table (cap 16384) |
| `vec_to_screen` builtin | `Engine::service_builtin` | uses the same `Camera` construction |
| `Renderer::project`, `Renderer::portal_surface` | `native/render.cpp` | projection/portal helpers shared by all backends |
| GL / Vulkan raster / Vulkan RT | `native/render_gl.cpp`, `render_vulkan*.cpp` | all call `camera_matrix`; winding flips when sign(ARC)≠sign(ASPECT) |

All symbols verified by grep in the current tree. There is no `Hor+`/`hor_plus` symbol in `native/`; the only "Hor+" text is the negating comment in `Engine::display_aspect`. Production paths set `widescreen=false`; the struct default `true` is used only by test fixtures. **CONFIRMED**

### 7.1 Optional first-person camera (`--camera first-person`)

| Item | Native behaviour | Evidence | Status |
|---|---|---|---|
| Source | patch `cam.wdl` `fps_camera_control`, ported as `Engine::update_first_person` (`native/engine.cpp:847-883`). It runs after scripts and physics each tick, so the scripts' own camera writes are overridden only while it is active | patch script | CONFIRMED |
| Activation | original campaign only (rejected for Bonus and for patched scripts, which have their own F5 camera). Active when Demir (`Aktif_Player` 0) or Ebru (1) is the player, a level is loaded, the player can be controlled (`oyuncu_pasif==0 \|\| koruyucu_pasif==0`), `freeze_mode` is 0, `camera.visible` is on and `Video_Cam` is hidden | patch guard `cam.wdl:80`, `:109` | CONFIRMED |
| Pose | per-state `dist_total`/`yuksek` and tilt clamps for standing, walking, aiming (tilt −5) and running; ARC 70, roll 0; bomb shake added | patch `cam.wdl:111-161` | CONFIRMED |
| Mouse | `player.pan -= 8*mouse_force.x*time`, `camera.tilt += 8*mouse_force.y*time`, exactly as in the patch. `mouse_force` is now the original A6 value (see `GAMEPLAY_SYSTEMS`/`BUG_FIX_PORTING` §5: mickeys·MOUSE_SENSE·10/max(TIME_FRAME raw, 50) plus the previous frame's term, y up, clamped to ±2) | `K22.exe 00422000` ASM | CONFIRMED |
| Buttons | while active, `key_pressed` reports the aim key `GA_Tuslar[4]` when the right mouse button is held and the fire key `GA_Tuslar[5]` when the left is held (patch `GenelAyarlar.wdl:120-121`) | patch script | CONFIRMED |
| Yield | when it switches off, camera fields the scripts did not rewrite go back to their last script values, so the level camera, pause/menu camera or cutscene takes over at once | `check_engine` `patch_option_contracts` | CONFIRMED |
| `validate_view` | omitted. With \|distance\| ≤ 8 the camera stays inside the player's own hull, so the patch's wall traces (from player.z+50, threshold 3) cannot trigger | code comment | HIGH CONFIDENCE |

Live verification (2026-10-09). Current tree, OpenGL on the RX 7900 XTX, 1280×720 windowed, private XDG with the Windows saves imported via `--import-saves`, driven by `--scenario`. Each run was repeated with `--camera original` for comparison. Montages: `reverse/wave2/EngineFixes2/fp-l5-montage.png`, `fp-fire-montage.png`, `fp-chars-montage.png`.

- Demir, Level5 (`load-save 0`). The camera sat 35.2 above the player and within 2 units horizontally (original: about 194 away, 101 above). Walking through `level5_camkontrol` still switched `lv5_camtip` 2→1→0, so the script triggers fire, while the camera stayed first-person (30 above, about 1 unit behind). Player positions after each waypoint and after holding forward into the end wall for 240 frames (482.376, 138.023, −82.969) were **identical** to the original camera run, so collision and movement are unaffected. **CONFIRMED**
- Mouse turn: a 100-pixel step turned `player.pan` by 4.28° on two consecutive frames (the force is clamped to 2, and the original keeps the previous frame's term), then stopped. In original mode the mouse did nothing. **CONFIRMED**
- Firing: with the right and left mouse buttons held, `Key_SilahKaldir` and `Key_Ates` went to 1 and rifle ammo (`S_Kursun_Sayisi[3]`) went 0 → 26 (reload) → 17. The keyboard S/D path in original mode gives the same sequence. In original mode the mouse buttons do not fire. **CONFIRMED**
- Takeover: the pause menu (`freeze_mode` 1, `STM_Acik` 1) brought back the script camera (−179/+74 from the player); closing it restored first-person (−1.8/+35). Inzar (`load-save 15`, Tnel24) has the same camera offsets as the original run (FP inactive). Ebru (`load-save 6`, Ev) is first-person at height 30. Walking into the same obstacle stopped Ebru at the same point (−47, −8, 40) in both modes. **CONFIRMED**
- Not exercised live: a `Video_Cam` cutscene (none starts within 1800 frames of loading any of the 27 saves, and the triggers found need inventory items or long routes), and a level-transition door (the Level5 `LY_Kapi` is beyond the reach of the scenario's greedy steering). The `Video_Cam` yield is covered by the unit test. **UNKNOWN** (live) / **CONFIRMED** (unit).
- Known script interaction, same in the patch: a few scripts read `camera.x/y` (e.g. `cinematic.wdl` `v_asker_ucurtma` `camera.y>333`). With the first-person camera those conditions follow the player's position instead of the level camera's. **HIGH CONFIDENCE**

## 8. Widescreen handling

| Statement | Evidence | Status |
|---|---|---|
| Original scripts offer only 4:3/5:4 presets (`GenelAyarlar.wdl:3 GA_Cozunurlukler` 640×480…1600×1200) applied by `video_switch(GA_Cozunurluk+6,…)` (`K22.wdl:654`) | script | CONFIRMED |
| Community patch widescreen is script-side (resolution/menu/HUD/text geometry in `K22Widescreen.wdl`), not a new projection routine | `CONTRACT.txt:2167-2171` | CONFIRMED (static) |
| Native v0.1.1+: auto-selected original `K22.wdl` defaults to desktop-sized borderless; recovered state-dependent menu/HUD/FONT/hit geometry with original ISO art; authored signed ARC/ASPECT and exclusive-mode Q preserved; separate VIEWs (portal/mirror/Video_Cam) keep their own rectangles; Bonus unchanged | `CONTRACT.txt:2214-2223`, `README.md` "Original-campaign widescreen (v0.1.1)" | CONFIRMED (code + qualification receipts) |
| Not an inferred Hor+ correction | `CONTRACT.txt:2161-2163`, `:1787-1788`, `:2063-2064`; README | CONFIRMED |
| Consequence: in windowed/borderless (Q=1) the authored ARC stays horizontal across the wider viewport, so a 16:9 view shows less vertical extent than 4:3 at the same ARC; in exclusive fullscreen Q restores the 4:3 vertical extent with non-square pixel scaling | derived from `camera_matrix` + `display_aspect` | HYPOTHESIS (derived from code, not compared with a Windows capture) |
| Known parity gaps: original ARC clamp 1..175° vs native throw at ≥179°; the "1.005 calibration" noted in CONTRACT | `CONTRACT.txt:2161-2162`, `PORTAL_LAYOUT.txt:189-190` | CONFIRMED gap, deliberately unchanged |
| CLI: `--display native\|authored`, `--resolution WxH` (min 1280×720, max 16384), `--window-mode windowed\|fullscreen\|borderless` | `README.md` "Run the native engine"; `native/main.cpp` | CONFIRMED |

## 9. Verification evidence

| Evidence | What it shows | Status |
|---|---|---|
| `reverse/campaign-checkpoint-replay-Start4-fresh.png` (2560×1440) | high-angle anchored level camera over the Start4 street with the player in frame (patched-install run) | CONFIRMED native capture |
| `reverse/campaign-pointer-fixed-Start4-return-interactive.png`, `reverse/native-global-skin-travel-skipped.png` (2560×1440) | in-world camera after level transitions | CONFIRMED native capture |
| `reverse/native-gl-input-debug-startup.png` (3840×2160), `reverse/native-gl-input-debug-held-map.png` (3072×1728) | native world and map at 16:9 sizes | CONFIRMED native capture |
| `reverse/campaign-checkpoint-SD-acid-cinematic.png`, `reverse/campaign-checkpoint-replay-Spray7-cinematic-complete.png` | authored cinematic sequences reached under script control | CONFIRMED native capture; Video_Cam use in those exact scenes is HIGH CONFIDENCE (from `cinematic.wdl`) |
| `native/check_engine.cpp` projection/aspect tests (pixelAspect = 1/(aspect·Q) over windowed/borderless/fullscreen, signed ARC, aspect 0 rejected; ASPECT birth 0x400; mirror copy) | unit-level contract | CONFIRMED (code; pass results per CONTRACT CTest receipts) |
| Widescreen qualification receipts (2560×1377 FONT rows; 5120×1440 headless geometry; 16384×720 clipping) | `/home/sergeng/.cache/kabus22-widescreen-e67no64k/` (private, outside repo) cited at `CONTRACT.txt:2229-2233` | CONFIRMED per CONTRACT; files not in repository |
| Original camera output under Wine (ISO `K22.exe`, wined3d, windowed 1024×768; reference set `reverse/wine-reference/set/`, private) vs native `--display authored` 1024×768 | Framing matches: the level5 `level5_camkontrol` fixed cameras (loaded view and the walk zones), Start4, `ev`, `tnel24` and `cam1` show the same camera position, angle and ARC. Phase correlation of the central 750×550 region gives a global offset of 0–2 px (level5 `walk3` 4,2 px, where the walked distance differs a little); residual difference images are texture detail. kriz's following camera differs only because Demir stands elsewhere after the random `atilgan_ai` attack ([GAMEPLAY_TESTING](GAMEPLAY_TESTING.md#reference-comparison-original-under-wine)). Widescreen/Q>1 framing and real Direct3D 9 output were not compared | CONFIRMED (4:3 windowed, Wine) |

No in-repo image is labelled specifically for widescreen or mirrors; the mirror/water portal path has native consumer tests but no recorded authentic-level mirror capture: **UNKNOWN** for in-game mirror appearance.
