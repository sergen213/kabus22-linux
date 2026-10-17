# Gameplay Systems

Purpose: catalog the gameplay systems implemented by the original Kabus 22 WDL scripts, the engine services each one needs, the native C++ code that supplies those services, and how much of each system has been verified.

Related: [Reverse-engineering index](REVERSE_ENGINEERING.md) · [Engine architecture](ENGINE_ARCHITECTURE.md) · [Camera system](CAMERA_SYSTEM.md) · [Script system](reverse-engineering/SCRIPT_SYSTEM.md) · [Original behavior](reverse-engineering/ORIGINAL_BEHAVIOR.md) · [Function map](reverse-engineering/FUNCTION_MAP.md) · [Asset formats](ASSET_FORMATS.md)

## Conventions and sources

Confidence labels:

- **CONFIRMED**: direct evidence, such as script text, decompiled code, file bytes, or a recorded native run.
- **HIGH CONFIDENCE**: strong indirect evidence.
- **HYPOTHESIS**: plausible but not checked.
- **UNKNOWN**: the evidence needed to decide is missing.

Script references use `file.wdl:line identifier`. The scripts are proprietary, so this document reproduces only identifiers and short constants.

| Script set | Location | Notes |
|---|---|---|
| Campaign (A6 6.31.4), reference copy | `reverse/extracted/original/*.wdl` (179 files) | Extracted from the Lutris-installed `K22.wrs` (804591 bytes, SHA-256 `5bae0b5e…`; `reverse/extracted/recovery.json`). All line numbers below refer to this copy. |
| Campaign, ISO edition (the edition the native port installs) | `reverse/media/iso/scripts/*.wdl` (182 files) | `reverse/media/iso/script-comparison.json`: 173 files are byte-identical to the reference copy and 3 are new. Nine differ: `K22.wdl`, `dil_ceviri_tr/en/de.wdl`, `sub_titles_en.wdl`, `kotu_koruyucu.wdl`, `cinematic.wdl`, `YeniMenu.wdl`, `Yeni_Oyun_Deger.wdl`. Line numbers in those nine files may not match. **CONFIRMED** |
| Community patch (Kabus22Patch `K22Widescreen.wrs`, 190 entries) | `reverse/extracted/patch/` | Most recorded campaign runs used ISO plus this patch (see [Verification](#verification-status-summary)). It is not shipped. |
| Bonus mode (A7, "Demolition Day") | `reverse/media/iso/mode/scripts/K22_mode.wrs/*.wdl` (23 files) | Packed inside `Kabus 22 Mode/K22_mode.wrs` of an installation. |

Native references name the C++ symbol. Line numbers in `native/engine.cpp` and `native/main.cpp` shifted while this document was written, because those files were being edited concurrently. Use the symbol name rather than the line.

## Architecture in one paragraph

All gameplay is written in WDL. The engine contributes only generic services: scheduling, entity movement and collision, tracing and scanning, animation, sound and movie playback, files, input and rendering. The campaign uses the legacy A6 API: `ent_move` driven by `move_mode`, `trace` driven by `trace_mode`, and `scan_entity` with `EVENT_SCAN`. It makes almost no use of the `c_*` API and none of ODE physics. **CONFIRMED**: `c_move` occurs once (`dinamik_modeller.wdl:1062`), and the campaign contains no `ph_*`, `phent_*`, `c_trace` or `game_save` calls (scout grep over the reference copy). The native side resolves these calls through `Engine::builtin`, which dispatches to `Engine::service_builtin` (sound, media, files, video, DLL and keys), `Engine::world_builtin` (create, remove, trace, move, scan, effect and level) and `worldModelBuiltin` (animation, paths and vertices), all in `native/engine.cpp`. The callable name set is the recovered A6/A7 descriptor table in `native/native_calls.inc`, consumed by `native_call_name` in `native/wdl.cpp`. **CONFIRMED** (grep).

## 1. Playable characters

Three characters share one `player` pointer. The global `Aktif_Player` selects which player action runs. **CONFIRMED**

| Item | Demir | Ebru | Inzar (internal name "Koruyucu") |
|---|---|---|---|
| Selector constant | `oyuncu2.wdl:14 Player_Demir`=0 | `:15 Player_Ebru`=1 | `:16 Player_Koruyucu`=2 |
| Player action | `oyuncu2.wdl:687 Oyuncu_Demir` | `oyuncu2_ebru.wdl:145 Oyuncu_Ebru` | `koruyucu.wdl:561 oyuncu_koruyucu` |
| Health | `oyuncu2.wdl:171 oyn_yaralanma`=100 | `oyuncu2_ebru.wdl:141 ebru_yaralanma`=100 | `koruyucu.wdl:14 koru_yaralanma`=150; mana `oyuncu2.wdl:12 koruyucu_mana` |
| Gravity / grounding | `dmr_yercekimi` :509, `demir_yercekimi` :531 | `yercekimi_ebru` :84, `ebru_yercekimi` :106 | UNKNOWN (not enumerated) |
| Weapon attachment | `Silah_Ekle` :403 (`ent_create` of `Silah_isimleri[Aktif_Silah]`) | `Silah_Ekle2` :53 | spells cost `buyu_gereksinimi[buyu_no]` (`koruyucu.wdl:2876`ff) |
| Animation | `Oynat` :446, `oy_Oynat` :540, `blendfx_*` | `blendfx_ebru` :2 | `ent_frame`/`ent_animate` (`koruyucu.wdl:2980`, `:1569`) |
| HUD | `PlayerEnerji_Menu.wdl PEM_Ac`/`PEM_Kapat` | same | `KoruyucuMenu.wdl KOMP_Enerji/KOMP_Mana` |

- Each player action starts with an `Aktif_Player==` guard (`oyuncu2.wdl:690`, `oyuncu2_ebru.wdl:148`, `koruyucu.wdl:565`). **CONFIRMED**
- **Switching characters is scripted, not a player choice.** Cinematics set `Aktif_Player` together with a target level `LY_HedefLevel`. Examples: `cinematic.wdl:3442` (→Ebru), `:3622` (→Demir), `:11084`/`:13554` (→Inzar), `seytan.wdl:1016`, `kotu_koruyucu.wdl:6327`. **CONFIRMED** in the reference copy. `cinematic.wdl` and `kotu_koruyucu.wdl` differ in the ISO edition, so those line numbers may differ there.
- Native addition, not in the original: the [character lock](CHARACTER_LOCK.md) (New Game selector, `--character`, console `character`) plays the whole campaign as one character. The story keeps its own `Aktif_Player`; only the reads that describe the played body are rerouted, the placed player action is swapped, and a story switch asks whether to keep the body. ORIGINAL (the default) is unchanged. **EXERCISED**: `level_sweep_demir/ebru/inzar` (all 101 levels), `character_*` scenarios and the `world_*`/`combat_kriz_*`/`quicksave*` scenarios with `--character ebru|inzar` ([Gameplay testing](GAMEPLAY_TESTING.md)).
- Native addition, not in the original: [local coop](COOP.md), phase 1. The main menu's 2 PLAYER item opens a two-player character select (the characters' own models, any combination); the choice is kept in `Engine::coop` when New Game starts, P1's character as the lock. Coop gameplay is phase 2. **EXERCISED**: `coop_menu_select`.
- Movement uses `ent_move(player_distance,nullvector)` with `move_mode` = IGNORE_PASSABLE + ACTIVATE_TRIGGER + USE_BOX + GLIDE (`oyuncu2.wdl:826-827`, `oyuncu2_ebru.wdl:273-274`). Ground probing and melee use `trace`; door search uses `scan_entity` (`PL_KapiTara` `oyuncu2.wdl:556`); footstep surface selection uses `trace` with `scan_texture`/`tex_flag1` (`adimsesi_turu` :356). **CONFIRMED**

**Native services:** `ent_move`/`c_move` and `trace`/`c_trace` in `Engine::world_builtin`, built on the shared legacy BSP/hull kernel. CONTRACT §48 records the source-qualified A6 query cutover (`reverse/decompiled/runtime/CONTRACT.txt:1979-2006`). Animation is handled by `worldModelBuiltin`, which supports vertex and sprite animation; skeletal animation throws (scout reading of `engine.cpp`, **HIGH CONFIDENCE**).

**Verification:**
- Demir **CONFIRMED** on the native port. CONTRACT second-numbered §42 (`CONTRACT.txt:2305-2324`) records a fresh New Game/Normal reaching `start4.wmb` with `PLAYER` model `demir_2007.mdl` running `oyuncu_demir`. Held A+Up movement, wall glide and two-plane corner behaviour were observed.
- Ebru and Inzar as playable characters: **CONFIRMED loading** from the user's original Windows saves through the original load menu (Ebru Ev/Sahil/Kanalizasyon/Tüneller, Inzar Tnel24/Tnel66/Tnel13/kotukoru_d). Ebru saves crashed until the A6 `vec_for_vertex` index clamp was fixed. Sustained play as Ebru or Inzar is not established. See [Gameplay testing](GAMEPLAY_TESTING.md).

## 2. Combat and weapons

| Item | Identifier | Status |
|---|---|---|
| Weapon IDs | `silah.wdl:2-10`: `Silahsiz` 0, `Ondortlu` 1, `Pompali` 2, `Makinali` 3, `Akrep` 4, `Agir` 5, `ElBomba_Y` 6, `Mayin` 7, `Magnum` 8 | CONFIRMED |
| Owned weapons and ammunition | `Silahlar[14]` :26, `Kursun_Sayisi[9]` (reserve) :29, `S_Kursun_Sayisi[9]` (loaded) :30, `Silah_Kapasite` :31, `Silah_Zarar` :32, `Silah_Menzil`=500 :33 | CONFIRMED |
| Fire | `Ates_Et` :510: hit-scan `trace` :569, muzzle from `vec_for_vertex` :499, sparks `effect` :613, blood `emit` :676 | CONFIRMED |
| Reload | `Kursun_Doldur` :701, `Oy_Kursun_Doldur` :727 | CONFIRMED |
| Thrown and placed | `elbombasi_gonder` :1043, `mayin_gonder` :1369 | CONFIRMED |
| Pickups | `KursunKutusu` :180, `ss_Silah` :304 | CONFIRMED |
| Auto-aim | `hedef.wdl:7 yakin_bul` (`scan_entity`, 120° sector), `Dusman_Tarama` :48, `yakina_don` :134, `kombo*` :400-469 | CONFIRMED |
| Melee (Demir) | `trace` between arm vertices `loc_kol1`/`loc_kol2` (`oyuncu2.wdl:2311`, `:2384`, `:2469`) | CONFIRMED |
| Controls | `Key_Ates`, `Key_SilahKaldir` (`GenelAyarlar.wdl:57-58`) | CONFIRMED |

**Native services:**
- `trace`, `scan_entity`, `effect` and `emit` are handled in `Engine::world_builtin`.
- `vec_for_vertex` is handled in `worldModelBuiltin`.
- `effect` admits a NULL fourth argument: A7 recovery, §32 (`CONTRACT.txt:1318-1326`).

**Verification:**
- **CONFIRMED** on the native port (patched campaign, OpenGL on llvmpipe):
  - Aiming and firing with muzzle smoke.
  - The loaded pistol went from 14 to 10 rounds after firing (`reverse/native-global-skin-ammo-after-fire.png`).
  - An ammunition pickup message appeared (`reverse/native-global-skin-ammo-interact.png`).
  - The weapons grid opened (`reverse/native-gl-campaign-integrated-weapons-released.png`).
  - Sources: `CONTRACT.txt:881-896`, `:988-1006`.
- Weapon selection, firing with ammunition decrement and reload for all eight weapons (Demir 1-7, Ebru Magnum 8), grenade/mine, the S+Q kick, the Kız Kulesi anti-aircraft gun, Inzar's sword and spells: **EXERCISED** by the `combat_*`/`inzar_*` scenarios ([Gameplay testing](GAMEPLAY_TESTING.md)).
- Damage dealt to enemies and enemy death: **EXERCISED**. Machine gun and sword hits on a Tnel10 `duma_yaratik` play its `Yrt_Yaralan_*` reaction, `duma_Ol` and `ent_remove` (`combat_demir_duma_gun`, `inzar_duma_fight`); AA-gun kills of `atilgan_ai` (`combat_kriz_aa_gun`); Lain (`kotukoru_ai`) defeated by the sword (`inzar_lain_boss`).

## 3. Enemies and AI

All enemy files are included from `klasik_yara.wdl:1572-1583`. That file also defines the shared skill layout (`Enerji`=SKILL1, `Dusmanmi`=SKILL11 …, lines 60-77) and the state constants `Yrt_Bekle`…`Yrt_Yaralan_U` (0-19, lines 25-44). **CONFIRMED**

| File | Main action | Control and state functions | Base health (× `OyunZorluk+1`) |
|---|---|---|---|
| `klasik_yara.wdl` | `klasik_yaratik` :1448 | `Klasik_Kontrol` :352, `Klasik_SaldiriKontrol` :390, `Klasik_Bekle` :497 | 50+50×m :1485 |
| `ari_yara.wdl` | `ari_ai` :1108 | `ari_SaldiriKontrol` :390; `ent_path("path_ari1..5")` | 35+35×m :1162 |
| `atilgan_yara.wdl` | `atilgan_ai` :220 | `ent_path`/`ent_waypoint`/`ent_nextpoint` | 90+90×m :305 |
| `babayaro_yara.wdl` | `babayaro_yaratik` :3409 | `babayaro_Kontrol` :210 | 150/200 + … :3369 |
| `cino_yara.wdl` | `cino_yaratik` :142 | `ent_path("path_cino")` | — |
| `duma_yara.wdl` | `duma_yaratik` :3199 | `duma_Kontrol` :230 | 100/125 + … |
| `zipleyen_yara.wdl` | `Zipleyen_yaratik` :3493 | `Zipleyen_Kontrol` :255 | 50/75 + … |
| `seytan.wdl` | `devil_ai` :1576 | `Seytan_Kontrol` :198, `Seytan_Tarama` :1537 | 1280×m :1593 |
| `kotu_koruyucu.wdl` | `kotukoru_ai` :6372 | `Kotukoru_*Kontrol` :806-1018 | `kotukoru_yaralanma`=400 :40 |
| `bond_bekci.wdl` | `Bond_ai` :1909, `Bond_yz` :1997 | `Bond_Kontrol` :227 | 75/100 + … |
| `boss.wdl` | `boss_ai` :809, `boss_ebru_ai` :1367 | `boss_Yaralan` :520 | `boss_yaralanma` :1 |
| `latex.wdl` | `latex_ai` :1210 | — | 300+300×m :1268 |

Every enemy uses the same perception and steering pattern (**CONFIRMED**; examples from `klasik_yara.wdl`):

- Range gating with `vec_dist(my.x,player.x)` (:294, :417).
- Facing with `vec_to_angle` (:304).
- Movement with `ent_move` and `move_mode` = IGNORE_PASSABLE + IGNORE_PUSH + ACTIVATE_TRIGGER + GLIDE (:645-646).
- Melee hits by `trace` between two arm vertices (:1085).
- Ground snap by `trace` with USE_BOX (:231).

Other AI support:

- `yoldata.wdl` starter `DatYukle` (:22) loads `yoldata.DAT` into `YapayYol[117960]`, with per-level `LevelKutu`/`Atlama` offsets used by `YapayKutu` (`klasik_yara.wdl:1562`). **HYPOTHESIS**: this is a precomputed navigation box graph.
- `iletisim.wdl` (`yara_koordinat`, `yara_mesafeler`) spaces enemies apart; it is not dialogue. **HYPOTHESIS**, based on identifiers and code bodies.
- Spawning is done by `klasik_generate.wdl:300 yaratik_generate`.

**Native services:** `vec_dist`, `vec_to_angle`, `ent_move`, `trace`, `scan_entity` and `ent_path`/`ent_waypoint`/`ent_nextpoint`, in `Engine::service_builtin`, `Engine::world_builtin` and `worldModelBuiltin`.

**Verification:**
- Enemy entities were decoded and alive in the native port: Start4 had 184 decoded entities (`CONTRACT.txt:874-879`, `:907-910`). **CONFIRMED**
- Combat AI: **EXERCISED** at three locations: Kriz `atilgan_ai` divers (approach, damage, game over), Tnel10 `duma_yaratik` (activation through `vec_to_screen`, run-in, strikes, reaction, death, removal) and the Lain boss. Handheld firearms do not hurt `atilgan_ai` (only the AA gun does), and `atilgan_ai` respawns until `atilgan_sayisi` reaches 15 (`atilgan_yara.wdl:620-633`), so its despawn is not exercised.
- Engine fixes found by these runs (all A6, with regressions): `vec_to_screen` gated on the view VISIBLE flag, read with fixed-point scaling from raw flag bits, so it always failed on `camera`, so every `duma_yaratik` stayed frozen (K22.exe `00404020`→`0044ee20` tests only depth and view rectangle); the MDL resource box was the union of all animation frames instead of frame 0 (`00457d10`/`00457d70`/`00458500 +0x38`), which made `klasik_yercekimi` float dumas 69 quants above the floor; legacy `trace` always rewrites YOU (`004546f0`); `vec_*` on `my.skillN` addresses skillN..N+2 (`0043e390`); IF/WHILE conditions are the function result accumulator (`00410a80`).

## 4. Inventory and items

| Item | Identifier | Status |
|---|---|---|
| Item list | `Nesneler.wdl:398 NS_Nesneler[100]`, `NS_NesneSayisi` :399, `NS_AktifNesneNo` :405 | CONFIRMED |
| Combining items | `NS_Birlesim[12]` :402 (triples: item 1, item 2, result), `NS_BirlesimToplam`=4 :401 | CONFIRMED |
| Pickup | `NS_NesneAl` :417, `NS_AlTarama` :423 (`EVENT_SCAN`), action `NS_Nesne` :490 | CONFIRMED |
| Use on a puzzle | `NS_KullanTarama` :559, action `NS_Bulmaca` :605, starter `NS_AktifTarama` :622 | CONFIRMED |
| Health pickups | `NS_EnerjiMiktar[3]`=(12,25,50) :186 | CONFIRMED |
| Inventory menu | `Envantermenu.wdl EM_TusKontrol` :167, `EM_Ayarla` :190; sub-menus `NesneMenu`, `KuBiGoMenu`, `SiNeDoMenu` | CONFIRMED |
| One-time objects | `Unic_Obje_Data.wdl:2 unic_obje_dizi[250]` | CONFIRMED |
| Spray (item 7) | `nesne_varmi(7)` `cinematic.wdl:365`; flags `video_sprey_*` :323-326; `video_sprey_de` :376 | CONFIRMED (reference copy) |

**Native services:** `scan_entity` events and `ent_morph` (`worldModelBuiltin`), plus UI panels and bitmaps (see [Engine architecture](ENGINE_ARCHITECTURE.md)).

**Verification:**
- **CONFIRMED** on the native port (ISO plus patch). The original controls took the player through these steps:
  1. Acquire Spray7 in Start3 (`NS_Nesneler` slots `[6,6,7]`).
  2. Use it from the M → Items → Spray7 → Use menu at the SD acid gate.
  3. Watch the authored spray and acid cinematics, then enter Level4.
- Sources: `CONTRACT.txt:1360-1362`, `reverse/campaign-checkpoint-input-ledger.json` (`items`).
- Screenshots: `reverse/campaign-checkpoint-Spray7-pickup.png`, `…-Spray7-items-released.png`, `…-replay-Spray7-use-menu.png`, `…-SD-acid-cinematic.png`.
- The note that Enter confirmation is posted after key release comes from `Envantermenu.wdl:177-180` (`CONTRACT.txt:998-1001`).
- **EXERCISED** on the ISO-only native campaign under the scenario harness (`world_cam1_padlock`): `NS_Nesne` pickups by D scan (cam1 item 5, camtuvalet item 21, `NS_NesneSayisi` +1 each) and use of item 12 through the original M → Nesneler → Kullan menu on `kilit_camiwc_ctrl`. Opening the inventory compacts it (`EM_OrganizeNesneler`), so slot indices shift when a slot is empty. Item 52 was also picked up in Kriz (route only, not a scenario). `world_kizic_kordinat` uses item 52 (already carried in `kayit10.sav`) through the same menu on the kizic `kapi_kriz_ctrl` door.
- Combining items (`NS_Birlesim`): **EXERCISED** (`world_item_combine`, `kayit4.sav`): Birlestir on 14 then 13 gives 15 and the inventory closes the gap; a non-matching pair changes nothing. The original saves also carry the pairs 1+2 (`kayit0`) and 22+23 (`kayit10`); `kayit23`→`kayit24` shows 39+40→41 combined in the original play.

## 5. Interaction, puzzles and documents

| Element | Identifiers | Status |
|---|---|---|
| Puzzle dispatch | `Bulmacalar.wdl:15 BUL_FPInit` fills `BUL_FP[]` with open functions; `Nesneler.wdl:566` sets `Bul_AcPointer` (a `FUNCTION*` declared at `K22.wdl:539`) | CONFIRMED |
| Puzzles | Board `BULBoardAc` (`BUL_Board.wdl:414`), Kordinat `:242`, Renkler `:237`, Saat `:339`, Salter `:225`, Tahta `:248`; each sets a solved flag `BUL*OK` | CONFIRMED |
| Disabled puzzles | Slots 3 (Sadirvan), 6 (Cark) and 8 (Dortlu) are commented out in `BUL_FPInit` | CONFIRMED |
| Documents | `Dokumanlar.wdl DO_DokumanAl` :264, actions `DO_Dokuman` :303 and `DO_GazeteDokuman` :401; `DO_Dokumanlar[50]` (`K22.wdl:97`) | CONFIRMED |
| Doors | `LevelYonetim.wdl LY_Kapi` :1118, `LY_KilitliKapi` :1170, `dolap_kilit_ctrl` :1211 | CONFIRMED |
| Scripted scares | `korku_sahneleri.wdl kapi_kan` :20, `lamba_bozuk` :78, `duran_saat` :226, `tv_plane` :259 (`media_loop` onto an entity texture :269) | CONFIRMED |
| Info prompts | `nesne_etkilesim.wdl *_mesaj` actions :131-409 | CONFIRMED |

All interactions are driven by `scan_entity` and handled under `event_type==event_scan` (`Nesneler.wdl:425`). **CONFIRMED**

**Verification:**
- **CONFIRMED** (nonvisual): the Start4 electrical interaction caused the Start4→SD transition without teleporting or editing flags (`CONTRACT.txt:912-918`).
- **CONFIRMED** (visual): doors driven by the original scripts through Start4, Start5, Start3, SD, Level4, NStart7, Mekke and Start2, from the `route` field in `reverse/campaign-checkpoint-replay-input-ledger.json`.
- **EXERCISED** (`world_cam1_padlock`): a permanently locked `LY_KilitliKapi` (message, `kapi_acikmi`=2; no script unlocks it), and a key-locked door (`kilit_camiwc_ctrl`): "padlocked" → item 12 → `video_asmacami_d` in-engine `Video_Cam` cutscene → story flag `v_asmacami_ok` → the door opens to camtuvalet.
- **EXERCISED** (`world_kizic_kordinat`): item 52 opens the kizic stairwell gate (`k_kizkapi_Tarama` → `video_kapikriz_d` cutscene → `v_kapikriz_ok`). Then the coordinate puzzle (`NS_Bulmaca` BulmacaNo 5 → `BUL_FP[5]` = `BULKordinatAc`) opens on a D scan. It is solved with the original keys: Enter ×3 (Sol), Right ×2, Enter (Asagi), Right, Enter (Sag), giving `KB_Durum` (−2,−9) = `KB_Hedef`. That sets `BULKordinatOK`, plays the `video_criptoal` cutscene and grants item 25. The other `BUL_*` puzzles are already solved in every original save that reaches them and were not run.

## 6. Map

- `yeni_harita.wdl`:
  - `harita_tetikleyici` (:2237) sets `freeze_mode=1` and `h_menu_aktifmi=1`.
  - Starter `harita_starter` (:2370) opens the map when `HaritaAcilabilirmi`, `player` and `LY_AktifLevelID` allow it and `Key_Harita==1`.
  - The synchronous map loop closes on Escape (:2343-2359), not on a second H press.
  - Per-area map actions are at :2396-2697.
- Tables live in `harita_tanimlama.wdl` (starter `Tanimlama` :418). The player arrow comes from `player_ok_wdl.wdl` (`ok_yon_bul`, `nokta_yenileme`).
- **CONFIRMED** `Key_Harita` is computed as `key_pressed(GA_Tuslar[GA_Harita])` OR a joystick button (`GenelAyarlar.wdl:115`). `GA_Harita`=8, and the default `GA_Tuslar` slot 8 holds scancode 35 (`:37`, `:50`).
- **HIGH CONFIDENCE** scancode 35 is the H key. The native run observed `KEY_H` set to raw 1024, followed by `Key_Harita`=1024, when H was held (`CONTRACT.txt:926-931`).

**Native services:**
- `key_pressed` (`Engine::service_builtin`). Scancode semantics follow CONTRACT §12 (`CONTRACT.txt:433-449`).
- `freeze_mode` thresholds follow §3 (`:125-133`).

**Verification:** **CONFIRMED** on the native port. H opens the original map grid, red regions, player marker and navigation arrows; Escape returns to the world.

- Images: `reverse/native-gl-resume2-map.png`, `reverse/native-gl-campaign-integrated-map.png`, `reverse/native-gl-input-debug-held-map.png` (3072×1728).
- Sources: `CONTRACT.txt:898-910`, `:926-937`.

## 7. Sound and music

- `Ses_Sistemi.wdl:1 sterio_ses3d` computes volume and pan from distance and angle in script, then calls `snd_play(sound, vol*Vol_Efekt, pan)`.
- `music_script.wdl`: starters `fonmuzikkodu_strtr` :42 and `fon_muzik_strtr` :127, using `snd_loop` with `snd_tune` fades.
- `SesMenu.wdl SM_Kernel` :130 adjusts `GA_Efekt`/`GA_Muzik` in steps of 5.
- Defaults: `GA_Efekt`=65, `GA_Muzik`=90, `GA_Dialog`=100 (`GenelAyarlar.wdl:27-29`).
- Engine calls used: `snd_play`, `snd_loop`, `snd_stop`, `snd_playing`, `snd_tune`, `snd_stopall`, `media_play`, `media_loop`, `media_playing`, `media_stop`. The scripts use no `ent_playsound`.

**CONFIRMED** for all of the above.

**Native services:**
- `Engine::service_builtin` forwards to `Media::sound_play`, `sound_playing`, `sound_stop`, `sound_stop_all`, `sound_tune`, `movie_play`, `movie_status` and `movie_stop` in `native/media.cpp` (FFmpeg decoding, SDL output).
- RIFF/WAVE files with format tag `0x674f` that contain Ogg/Vorbis are unwrapped by `media_payload` (`native/media.cpp`). CONTRACT §33-34 (`CONTRACT.txt:1358-1393`).

**Verification:**
- **CONFIRMED**: the wrapped-Vorbis Level4 door sound (`cinematic.wdl:848`, `v_tacikos_kapi`) blocked the campaign, and the replay got past it after the fix (`CONTRACT.txt:1480`).
- **CONFIRMED**: nonzero audio output from the game's own PipeWire stream (peak 32332, RMS 9587). The "no sound" user report was a persisted 4% per-application stream volume, not an engine defect (`CONTRACT.txt:2326-2331`).
- Parity with the original Windows mixer: **UNKNOWN**.

## 8. Settings

| Setting | Script identifier | Engine target | Status |
|---|---|---|---|
| Resolution and colour depth | `GenelAyarlar.wdl:11 GA_Cozunurluk`, `:12 GA_RenkKalitesi`; table `GA_Cozunurlukler` (640×480 … 1600×1200) :3 | `video_switch(GA_Cozunurluk+6, …)` (`K22.wdl:654`) | CONFIRMED |
| Light quality, buffering, mipmapping, anisotropy | `GA_IsikKalitesi`, `GA_Tamponlama`, `GA_Mipmapping`, `GA_Anisotropy` :13-16 | `d3d_lightres`, `d3d_triplebuffer`, `d3d_mipmapping=×3`, `d3d_anisotropy=×2` (`K22.wdl` main) | CONFIRMED |
| Reflections | `GA_Yansima` :17 | not wired: the script carries the comment "yansima kodu eklenecek" | CONFIRMED |
| Blood colour and amount | `GA_KanRengi`, `GA_KanMiktari` :19-20 | `yarakan_pointer` = `blood22_` or `asit22_` | CONFIRMED |
| Brightness, contrast, gamma | `GA_Parlaklik`, `GA_Karsitlik`, `GA_Gamma` :22-24 | `RenkAyar.dll` `dl_PaKaGa` (`K22.wdl:69`, opened in `main`) | CONFIRMED |
| Persistence | `GA_AyarlariYukle` :140 / `GA_AyarlariKaydet` :242 | `options.k22` | CONFIRMED |
| Key and joystick bindings | `GA_Tuslar` :37, `GA_JTuslar`; starters `Tuslari_Esitle` :103, `Joy_Takip` :71 | `key_pressed`, `JOY_*` | CONFIRMED |

**Native services:**

- `dll_open`, `dl_reset` and `dl_pakaga` are emulated by compiled providers selected through `Engine::initialize_plugins`. No PE DLL is loaded (CONTRACT §13, `CONTRACT.txt:475-486`).
- `d3d_mipmapping` and `d3d_anisotropy` follow the sampler contract in CONTRACT §23.
- The GTK launcher (`installer/`) edits the same private records. `README.md` "Separate GTK settings launcher". **CONFIRMED**: actual Apply persistence is recorded at `CONTRACT.txt:2293-2297` and `:2381-2385`.

## 9. Language and subtitles

- **Reference copy:**
  - `K22.wdl` activates only the Turkish block (`resource "turkce.k22"`, `dil_ceviri_tr.wdl`, `sub_titles_tr.wdl`). The other language blocks are commented out.
  - The runtime language index is `GA_Dil` (`GenelAyarlar.wdl:129`), used for example as `DMIL_CikisSoru[GA_Dil]` (`CikisMenu.wdl:25`).
  - **CONFIRMED**
- **ISO edition:**
  - `K22.wdl` selects text with `IFDEF defdil_tr`/`defdil_en`/`defdil_de` (resources `yazi_tr/en/de.k22`) and voice with `defses_tr`/`defses_en` (`ses_tr/en.k22`, plus the movie names `video_ismi_strNN`).
  - Source: `reverse/media/iso/scripts/K22.wdl:3-50`, `reverse/media/iso/script-differences.txt`.
  - **CONFIRMED**
- **Subtitles:**
  - Frame-indexed functions in `sub_titles_tr.wdl` (`sub_intro` :455 …, starter `altyazi_yazi_strtr` :489).
  - They are driven by the frame number returned from `media_playing(handle)` (`LevelYonetim.wdl:779-780`).
  - **CONFIRMED**
- **Native:**
  - `native/main.cpp` defines `defdil_<lang>` and `defses_<audio>` as compiler macros.
  - `Engine::configure_language` handles the patch-only `DD_Dil`/`load_subtitle` protocol and returns early when `load_subtitle` is absent.
  - `media_playing` maps to `Media::movie_status`.
  - **CONFIRMED** (grep)
- **Verification:**
  - The English-subtitle intro was observed: `reverse/campaign-pointer-fixed-english-intro.png`, `CONTRACT.txt:995`. **CONFIRMED**
  - Turkish and German runtime text: **UNKNOWN**.
  - Movie timing: **EXERCISED** on `veca.k22` (`video_camitaci_f`, 1007 frames at 25 fps, audio 40.28 s). The video is clocked from the movie's audio voice (`Media::update`); decoded frames 283 and 963 arrived at 11.285 s and 38.49 s (expected 11.28 and 38.48), the movie ended at 40.279 s, and `VST_AltText` followed the `sub_cam_fx` frame windows (`world_cam1_padlock`).

## 10. Save and load

The original scripts never call `game_save`/`game_load`. Saves are written by hand through the `file_*` API. **CONFIRMED**

| Step | Identifiers |
|---|---|
| Slot index file `kysd.sav` | `YukleMenu.wdl:19 SLG_Yukle`, `:119 SLG_Kaydet`, `SLG_MaxKayit`=99 :6, label builder `SLG_LevelKaydet` :87 |
| Slot data `kayit<N>.sav` | `Yeni_Oyun_Deger.wdl:1554 YD_OyunuKaydet` (sequence of `file_asc_write` calls starting with `save_versiyon`=100, then `LY_AktifLevelID`, `Aktif_Player`, `Aktif_Silah`, …); loaded by `:873 YD_OyunuYukle` |
| Save points | action `save_tga` (`YukleMenu.wdl:362`, manual) and action `otosave_fish` (`:417`, automatic), both going through `LV_LevelKaydeta` :345 |
| Load at startup | starter `Yukleme_Kontrol` :318 → `YD_OyunuYukle` → `LY_HedefLevel` |

**Native:**
- `file_open_read`/`file_open_write`, `file_asc_*`, `file_var_*` and `file_str_*` are implemented in `Engine::service_builtin`. Paths are confined to `saveRoot`, which is `$XDG_DATA_HOME/kabus22` or `~/.local/share/kabus22` (set in the `Engine::Engine` constructor).
- The `game_save`/`game_load` names are listed in `native_calls.inc`, but `engine.cpp` contains no implementation. The original scripts do not use them. **CONFIRMED** (grep)
- Native addition, not in the original: quicksave/quickload (F5/F9) and the developer console ([CONSOLE.md](CONSOLE.md)). A quicksave runs the original `SLG_LevelKaydet`/`YD_OyunuKaydet`/`SLG_Kaydet` at any gameplay moment and adds the player position to the slot's `.meta` sidecar; a quickload goes through `Yukleme_Kontrol`. Save-point and imported slots carry no position and still respawn at `LV_*_G00`. **EXERCISED** for all three characters (`quicksave1_roundtrip`, `quicksave2_restart`, `quicksave3_characters`, `console_commands`).

**Verification:**
- The original save menu, writer and reader ran through the SourceAPI consumer in the native port: `campaign-save-load-result.json`, exit 0 (`CONTRACT.txt:1682-1685`). **CONFIRMED**
- A real saved checkpoint through reload: **CONFIRMED** on the native port (`world_save1_point`…`world_save4_restart`, [Gameplay testing](GAMEPLAY_TESTING.md)).
  - kizic2 `save_tga`, reached by walking after a level transition, wrote `kayit27.sav`. That file is byte-identical to the original `kayit10.sav` apart from the save counter.
  - After a restart, it reloaded through `YukleMenu` with the same level, weapon, HP and ammo. `kysd.sav` and `ftz.k22` persisted across restarts.
  - Two engine fixes were needed: A6 placed-entity Z lift (`004315d0`), without which every save icon sat ~130 quants under its spot, and `SYS_*` date update (`00414650`).
  - Save points exist in Level5, cam1, fiinis, kizic2, tnel66 and masso_lg (save inventory). `otosave_fish` is **EXERCISED**: after the Lain fight, Buyukes' `otosave_fish` (skill1 14) wrote `kayit27.sav` once `buyukes_koruses` granted `OSN_OtoSave_izin`, and that slot reloads (`inzar_lain_boss`, `world_otosave_reload`). Every original save already has its own level's `OSN_OtoSave_dizi` entry set, so an autosave only fires on reaching a later autosave level.
  - The save file does not store XYZ; a load uses `LV_*_G00` (`CONTRACT.txt:1880-1882`).

## 11. Difficulty

- `OyunZorluk` (`K22.wdl:82`) is set by `YeniMenu.wdl:135 YeniOyunBaslat(_Zorluk)`: 0 = Normal ("kolay"), 1 = Hard ("Zor"). It is saved and loaded with the slot (`Yeni_Oyun_Deger.wdl:1587`/`:899`). **CONFIRMED**
- Difficulty scales by the multiplier `(OyunZorluk+1)`, so ×1 on Normal and ×2 on Hard. It applies to:
  - enemy health (table in §3 above);
  - damage to the player (for example `klasik_yara.wdl:1092`, `bond_bekci.wdl:620`, `seytan.wdl:391`);
  - boss-bar scaling (`kotu_koruyucu.wdl:348-350`).

  **CONFIRMED** in the reference copy. `YeniMenu.wdl` and `kotu_koruyucu.wdl` differ in the ISO edition, but the ISO `YeniOyunBaslat` is still at line 135 with the same 0/1 meaning (`reverse/media/iso/scripts/YeniMenu.wdl:135,561-562`).
- Verification: selecting Normal was observed (`reverse/campaign-checkpoint-normal-difficulty.png`, `reverse/native-gl-campaign-integrated-difficulty.png`). **CONFIRMED**. Hard mode in play: **UNKNOWN**.

## 12. Level management and story flow

- `LevelYonetim.wdl`:
  - `LVSL_Olustur` :147 maps level IDs 1-103 to WMB names; 63 and 96 are null, leaving 101 levels.
  - `LVPL_Olustur` :549 builds the per-level function tables `LVPL_Acikmi`/`Giris`/`Cikis`.
  - `LevelYukle` :665 plays the transition movie (`media_play`, `media_layer`=900, :772ff) and calls `Level_Load` :756.
  - Native `seamless_doors` (default on) turns eligible LY_Kapi transitions into a doorway preview plus a fadeless run of this same LevelYukle ([SEAMLESS_DOORS.md](SEAMLESS_DOORS.md)).
  - Starter `LY_YuklemeTakip` :1058 polls `LY_HedefLevel`.
  - **CONFIRMED**
- Each of the 101 `LV_*.wdl` files has the same shape: `LV_<Name>_Giris(_EskiLevel)` sets the spawn point by previous level, `_Cikis` handles exit, `_Acikmi` decides whether entry is allowed (for example `LV_Start4.wdl:14/51/61`). **CONFIRMED**
- `cinematic.wdl` holds about 200 `video_*` scene actions, each built from `Video_Cam`, `media_play`, subtitles and flags. See [Camera system](CAMERA_SYSTEM.md). **CONFIRMED**
- Game over: `K22.wdl game_over_strtr` (:767) calls `snd_stopall`/`media_stop`, then `Level_Load(analevel_wmb)` back to the menu level. **CONFIRMED**

**Native services:**
- `level_load` sets `Engine::pendingLevel`/`pendingLevelLoad`, and the request is processed after the frame's script phase. This matches the deferred A6 contract in CONTRACT §13 (`CONTRACT.txt:501-520`).
- Level teardown cancels processes whose MY is the removed entity, and NULLs registered pointer cells (§27, §29).

**Verification:**
- **CONFIRMED** visual transitions under original control: Start4 → Start5 → Start3 → Start5 → Start4 → SD → Level4 → NStart7 → Mekke → Start2 (`reverse/campaign-checkpoint-replay-input-ledger.json` `route`; `CONTRACT.txt:1360-1362`, `:1480-1482`).
- **CONFIRMED** SD return without a stale pointer (`CONTRACT.txt:1201-1222`).
- The replay ended with an unattributed external SIGKILL in Start2 (`reverse/campaign-checkpoint-stop-evidence.json`).
- Files named `campaign-checkpoint-replay-Start6-*.png` exist, but the ledger's world snapshots record only `start2.wmb` in that phase. Arrival in Start6 is therefore **UNKNOWN** from walked play. Start6 is covered only by the frozen SourceAPI packet (§46/§48).
- About 93 of the 101 levels have never been reached by walking: only 8 distinct campaign worlds (Start4, Start5, Start3, SD, Level4, NStart7, Mekke and Start2) were walked. **UNKNOWN** for walked play.
- **EXERCISED** all 101 levels through the original `LevelYukle` path under the scenario harness (`level_sweep`, `tests/levels/levels.tsv`): from an original save of the WMB's character, two door transitions (`LY_HedefLevel`), then 600 frames with the player in control except where the original entry keeps it passive (Krizkuyu, Seytan_k, boss, hapisane_dark). No engine exception, script error or fall below the level geometry after fix 14 (sprite strip `s_alev+61.tga`); OpenGL captures of every level and Vulkan RT captures of 12 ([Gameplay testing](GAMEPLAY_TESTING.md#level-sweep-every-campaign-level)). Each level's own story sequence beyond entry is not covered.

## 13. Bonus mode (A7, "Demolition Day")

- Scripts: `K22_mode.wdl`, `anamenu.wdl`, `silah_secme_menu.wdl`, `ucaksavar.wdl`, `silah_mode.wdl`, `paneller.wdl`, `bonus_yardim.wdl`, `mod_fizik.wdl` and 15 others under `reverse/media/iso/mode/scripts/K22_mode.wrs/`.
- Installed data: `Kabus 22 Mode/` holds `K22_mode.wrs`, `mode_{tr,en,de}.k22`, `mode_ayarlar.k22`, `ygm.k22`, `Menu_ses/` and `engine/_a4font.pcx`.

**CONFIRMED**

| System | Identifiers (from `reverse/native-gl-bonus-1b-gameplay-evidence.txt`) | Engine services |
|---|---|---|
| Menu and New Game | `anamenu.wdl:478-510` keyboard gate; `:514-559` loads `basla.wmb` | software cursor (`bonus-cursor/contract.txt`), PANEL/TEXT `FILTER`, `PLAYER_NAME` (§28) |
| Loadout | `silah_secme_menu.wdl:16-56` | UI panels |
| Turret movement and aim | `ucaksavar.wdl:465-512` A/D movement; `:642-648` mouse aim | `mouse_force`, entity movement |
| Paired firing | `ucaksavar.wdl:1583-1638`; `silah_mode.wdl:1725` `effect(…,null)` | `effect` with an optional NULL direction (§32) |
| Aircraft drops and timer | `paneller.wdl:694-795`, `:855-863`; five `PH_RIGID`/`PH_BOX` crates with masses 3 to 5 (`bonus_yardim.wdl:759-817`) | ODE: `Physics::builtin` (`ph_setgravity`, `phent_*`) and `Physics::step` in `native/physics.cpp`, stepped only for A7 from `Engine::finish_frame` |

**Verification:**
- **CONFIRMED** menu (`reverse/native-gl-bonus-predefined-entry.png`, `CONTRACT.txt:1190-1199`).
- **CONFIRMED** New Game, movement, aim, firing of both guns, ammunition 950 → 868, and the timer running to single digits with no LCP warning (`reverse/native-gl-bonus-1b-*.png`, `native-gl-bonus-1b-live-gameplay.mp4`, `CONTRACT.txt:1477-1479`).
- **EXERCISED** as a repeatable scenario (`bonus_new_game`, headless and OpenGL/Vulkan raster/RT): mouse New Game, any-key start, weapon menu, rail movement (A/D), mouse aim and minigun fire (950 → 814) over 1030 frames.
- Crate physics: **HYPOTHESIS / not accepted**. Visible falling motion was recorded, but the evidence file states it is "not proof that all five settled correctly".
- Physics parity with the original solver is explicitly not claimed (§34, §36).
- A full Bonus session through to its end: **UNKNOWN**.

## Verification status summary

The README states that a full campaign or Bonus playthrough has not been established. The status below is consistent with that.

| System | Status on the native port | Where |
|---|---|---|
| Startup, menus, difficulty, intro | Exercised | [Original behavior](reverse-engineering/ORIGINAL_BEHAVIOR.md) |
| Demir movement and collision | Exercised (Start4) | `CONTRACT.txt:2305-2324` |
| Firing, ammunition, pickups, weapons grid | Exercised (all weapons, reload, kick: `combat_*` scenarios) | `CONTRACT.txt:881-896`, `docs/GAMEPLAY_TESTING.md` |
| Map and inventory | Exercised | `CONTRACT.txt:898-910`, `:996-1001` |
| Spray item and the story gate | Exercised | `CONTRACT.txt:1360-1362` |
| Level transitions | 9 transitions across 8 worlds walked (patched install); all 101 levels entered via `LevelYukle` by `level_sweep` | replay ledger, `docs/GAMEPLAY_TESTING.md` |
| Enemy combat, damage, death | Exercised: enemy aggro/damage at Kriz, Tnel10 and Kotukoru_d; firearm, sword and AA-gun damage, death and removal; game over → menu → reload | `docs/GAMEPLAY_TESTING.md` |
| Ebru and Inzar play | All 27 original saves load (16 worlds, 3 characters); sustained play not established | `docs/GAMEPLAY_TESTING.md` |
| Puzzles (`BUL_*`) | Not established | — |
| Walked save and reload | Not established | `reverse/campaign-checkpoint-stop-evidence.json` |
| Bonus gameplay | Partial (menu, firing, timer) | `reverse/native-gl-bonus-1b-gameplay-evidence.txt` |

Most walked campaign evidence used an ISO plus Kabus22Patch installation, which runs the patch's `K22Widescreen.wrs` entry (`reverse/campaign-pointer-fixed-input-ledger.json`, `asset_archive_identity`). The ISO-only native campaign has a recorded startup and a fresh New Game into Start4 (`CONTRACT.txt:1773-1779`, `:2305-2310`); deeper walks have not been recorded on it.
