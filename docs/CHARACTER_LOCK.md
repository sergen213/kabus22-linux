# Character lock

Purpose: play the whole campaign as one chosen character (Demir, Ebru or Inzar) without breaking the original story logic. This is a native addition (`native/character.cpp`); the original scripts are not modified.

Related: [Gameplay systems §1](GAMEPLAY_SYSTEMS.md#1-playable-characters) · [Console](CONSOLE.md) · [Gameplay testing](GAMEPLAY_TESTING.md)

## Using it

- **New Game:** the difficulty menu (NORMAL/HARD) shows a selector one button above it: `< ORIGINAL >`. Left/Right, the d-pad or a click on the arrows cycles ORIGINAL, DEMIR, EBRU and INZAR. Enter starts the game as before. The labels are glyphs cut from the original menu atlas (`menu_dildenbagimsiz.tga`), in the same font, colour and scale as the menu buttons.
  - **ORIGINAL** (the default) changes nothing. The playable character follows the story, exactly as in the original game.
  - **DEMIR, EBRU, INZAR** lock the playable character.
- **Story switches:** when a locked game reaches a point where the story changes the character (for example Demir to Ebru at the Kriz dock, or to Inzar in the tunnels), the game pauses and asks *"The story switches to EBRU. Continue as:"*. The two choices are the locked character (keep playing as them) and the story's new character (switch, like the original). The answer becomes the new lock. Up/Down, the d-pad or the mouse select; Enter, a click, pad A or Start confirm. If the story returns to the locked character, there is no question.
- **Saves:** every slot written while locked (save points, autosaves, quicksave) records the lock in its sidecar, `kayitN.sav.meta` (`character=demir|ebru|inzar`). Loading such a slot restores the lock. Original saves and saves written unlocked have no `character=` line and load unlocked.
- **Console:** `character [original|demir|ebru|inzar]` prints or sets the lock. It applies from the next level entry (a door, `level ID`, or a load).
- **Command line:** `kabus22 --character demir|ebru|inzar` preselects that character in the New Game selector and locks loaded saves that carry no lock (original saves). There is no launcher option: the choice belongs to a game, not to the installation.

## How it works

`Aktif_Player` has two readers in the original scripts:

- **Story reads** decide what happens: cinematics, level entry points, item, door and puzzle gates, music, saves.
- **Body reads** describe the character being played: which player action takes the `player` pointer, its model height and footstep vertices, the weapon morph, auto-aim, the HUD and inventory menu, kill rewards.

The lock splits them. `Aktif_Player` stays the story's value. The scripts still write it at every switch, and the saves store it. Only the body reads are rerouted. When the scripts are loaded, `Engine::configure_characters` calls `Runtime::reroute_reads`, which replaces each body read of `Aktif_Player` (the functions and the array index listed under [Body reads](#body-reads-rerouted)) with a native call:

| Native call | Unlocked | Locked |
|---|---|---|
| `k22_body_player()` | `Aktif_Player` | the locked character |
| `k22_menu_player()` (`EM_Kernel`) | `Aktif_Player` | a gun body: the item inventory everywhere. Inzar's body: her spell menu in every story; outside her own story M cycles spell menu → item inventory → closed (below) |
| `k22_weapon_player()` (`EM_KuBiGoSMenu`) | `Aktif_Player` | the locked character; -1 (no gun morph) when Demir picks a weapon outside his tables |

Unlocked, every rerouted read returns `Aktif_Player`, so the default is the original behaviour.

Native rules when locked (`native/character.cpp`):

1. **Player placement** (`Engine::apply_character_lock`, in `load_level` before the placed actions start). The WMB entity whose player action belongs to the story character runs the locked character's action instead. The action's guard, a body read, then claims `player`, and the action morphs to its own model. A placed action of the locked character that the story does not use gets the story character's action, whose guard removes it, as the original does for every unused player action. So there is never more than one player.
2. **Inzar's effects:** `oyuncu_koruyucu` waits for `koru_efektbas_ok`, which only the WMB-placed `koru_efekt_bas` sets (`koruyucu.wdl:539-567`). Outside the tunnel levels, a `koru_efekt_bas` entity (`fish.mdl`) is created at the player's spot.
3. **Weapons** (`Engine::character_weapons`, every frame while locked):
   - Demir's tables stop at weapon 7 (`Silah_isimleri`, `t_Silah`). In Ebru's inventory he holds his pistol, and her magnum rounds move to it if the pistol is empty.
   - Ebru's model has one pose set, the magnum's. Outside her story she keeps the story's firearm across her spawn (her action selects the magnum at every spawn) and fires weapons 1-5 or 8 through `Ates_Et`, which is generic. Grenades and mines (6, 7) are thrown by Demir's action only, so she keeps her firearm instead.
   - Inzar uses her sword and spells and never reads `Aktif_Silah`.
4. **Switch prompt** (`Engine::character_tick`, top of `Engine::tick`). A change of the story's `Aktif_Player` during play opens the prompt and pauses the scripts (no script tick, the frame and sound continue). New Game (`YeniOyunBaslat` running) and slot loads adopt the story value silently until the next level entry.
5. **Console health and god mode** use the body's health variable (`oyn_yaralanma`, `ebru_yaralanma`, `koru_yaralanma`).
6. **Control gate.** Demir's and Ebru's actions (`oyuncu2.wdl`, `oyuncu2_ebru.wdl`) stop on `oyuncu_pasif`, Inzar's and her spells (`koruyucu.wdl`, `koru_buyuler.wdl`) on `koruyucu_pasif`; no body action writes its gate. The scripted fights release only the story character's gate (Lain `kotu_koruyucu.wdl:6486` and Seytan's intro `cinematic.wdl:9050` only `koruyucu_pasif`, the final boss `boss.wdl:896` only `oyuncu_pasif`). Locked, every gate read in those files becomes `k22_body_*_pasif()`, the story character's gate (unlocked: the variable itself). A body therefore also stands still in the other story's cutscenes.
7. **Boss fights for a gun body** (Lain and Seytan are Inzar's fights in the story):
   - Bullet hits (`silah.wdl Ates_Et:651-658`) read `kan_mertip[3*(you.skill2-1)+n]` by enemy type 1-6. Lain (`kotukoru_ai`) and Seytan (`devil_ai`) set no type and would index -2, so they get type 1 at placement (all six rows are equal; `skill2` is otherwise only compared with 4).
   - Lain's hit reaction (`Kotukoru_Yaralan`) branches on the attack kind in `skill3`: Inzar's kinds and firearm 2. Any other gun fell through, ended Lain's AI below 0 Enerji and stopped the story. Locked, `Ates_Et:680` (`you.skill3=aktif_silah`) records kind 2 for Lain (`k22_bullet_hit_kind`, `Runtime::reroute_assigned_read`).
   - Seytan's attacks on the player use globals that only Inzar's action answers (`koruyucu.wdl:1619-1887`): the floor logo (`seytan_logo_aktif`, `one_vurusu_aktif` 4) pulls her into a dash strike, a swing or the black blast (5) knocks her back for 12/24, the fire breath (3) knocks her back. While the logo is set Seytan cannot die (`seytan.wdl:958`). For a gun body both are cleared every frame (`Engine::character_tick`).
   - Demir's story poses the player through `oy_Oynat`/`Hareket_Hizi` (Demir's 8-entry weapon tables; Buyukes after Lain, `cinematic.wdl`, `BUL_Saat.wdl`). An Ebru body holding her magnum (8) indexed past them; those reads see the pistol (1) (`k22_pose_weapon`).
8. **Inzar's menus.** `EM_Kernel` (`Envantermenu.wdl:333-335`) reads `Aktif_Player` three times per M press: Demir or Ebru toggle the item inventory (`EnvanterAcKapat`), Inzar her spell menu (`KoruyucuAcKapat`). Inzar's body always gets her spell menu. Outside her own story the items are needed too (energy items, story items such as 38 at the end), so there M cycles: the first M opens the spell menu; M in the spell menu closes it, and `Engine::character_tick` starts the original `EnvanterAcKapat` on the next frame; M in the item inventory closes it. The three reads of one press see one answer (latched for the frame); the level-entry gate at `:315` runs without M held and sees the story. In her own story M is her spell menu only, as in the original.
9. **Hit kinds for Ebru's body.** Demir-story attackers set `dmr_yaralan_tip` 2 (burning: the final boss's fire `efect2.wdl:5887`, the cino creature) or 3 (Atilgan's swing). Demir's action reacts to 0-3 (`oyuncu2.wdl:1812-2022`); Ebru's only to 0 and 1 (`oyuncu2_ebru.wdl:1239/1303`). Hurt by kind 2 or 3 she matched neither branch, and with the aim key held `d_ates_go` (`:1397`) jumped back to `e_yaralan_go` with no `wait(1)`: the frame never ended (the hang at the final boss). Locked, her action reads kind 2 as 0 (hurt) and 3 as 1 (knock-back) (`k22_ebru_hit_kind`).
10. **Final boss** (`boss.wmb`, Demir's fight). The invisible solid `boss_engel.wmb` (box ±168 around (316,131), with the `boss_kalkan` shield inside) keeps the player at least ~100 from the boss, so it is fought from range. The story needs about 1000 damage after the creatures: 1500 → 1000 (`boss_ebru_start`), then 990 → below 500 while the possessed Ebru (`boss_ebru_ai`) guards it (`ebruboss_ol` → `gaile_kullan_ok`; below 400 the boss resets to 490).
    - Every shot aimed at the boss in the second phase turns the guard into its line (`silah.wdl Ates_Et:546-558`, `boss_ebruhedefpan`); Demir's way through is a grenade or mine, whose blast hurts the boss by distance (`hedef.wdl Dusman_Tarama:59-77`). Ebru's body throws neither (rule 3), so for a locked Ebru `boss_ebruhedefpan` stays 180 (the guard keeps her place west of the boss) and shots from off her line reach the boss.
    - Inzar has no firearm and her sword cannot reach past `boss_engel`; spell 4 (`koru_adu_buyu`) hits for 10 at 23 of her 230 mana, about 100 bolts for the fight. Locked to Inzar, each frame's loss of the boss's health while the player has control (`oyuncu_pasif` 0) is multiplied by 15 (`Engine::bossDamageScale`). The story's own writes (1500 floor at `boss_oyun_mode` 1, 990 at the guard's entrance under `oyuncu_pasif`, the 490 reset) are not scaled. Numbers in [Verification](#verification).

## Inventory of character dependences (ISO scripts)

Source: every non-comment read or write of `Aktif_Player` or `NS_EbruAlsinmi` in `reverse/media/iso/scripts` (355 lines in 188 functions).

Classes:

- **a**: progression gate: an item, door, puzzle or trigger that only one character may use.
- **b**: cosmetic or flavour: a sound, voice line, camera or info prompt.
- **c**: forced switch or placement: the script sets `Aktif_Player`, or a level places a character.
- **d**: character state: health, mana, weapons, HUD, model metrics.

**Handling** says which view the read gets when locked.

### Body reads (rerouted)

| Script: function (lines) | Class | Handling |
|---|---|---|
| `oyuncu2.wdl: Oyuncu_Demir` (690), `oyuncu2_ebru.wdl: Oyuncu_Ebru` (148), `koruyucu.wdl: oyuncu_koruyucu` (565) | c | Body. Plus the placement swap (rule 1) |
| `koruyucu.wdl: koru_efekt_bas` (543) | d | Body: Inzar's sword and energy effects only for an Inzar body. Plus rule 2 |
| `oyuncu2.wdl: adimsesi_turu` (363-372, `adimvertex_dizi[4*Aktif_Player+n]`) | d | Body: footstep vertices of the body's model |
| `player_yuksekli[Aktif_Player]` in 30 `LV_*_Giris` (Tnel05-28/66, hapisane*, Krizkuyu, saraygir, Seytan_E) | d | Body: spawn height for the body's model (array-index reroute) |
| `K22.wdl: game_over_strtr` (860, 866) | b | Body: the dying character's scream |
| `KuBiGoMenu.wdl: EM_KuBiGoSMenu` (47) | d | Weapon view: only Demir morphs `a_silah` to the chosen gun (rule 3) |
| `Envantermenu.wdl: EM_Kernel` (333-335; 315) | d (a at 315) | Menu view: a gun body gets the item inventory; Inzar's body her spell menu, and outside her own story also the item inventory (rule 8). Line 315 (no inventory on entering kotukoru_d as Inzar) sees the gun body there, so it gets the inventory, and the story for Inzar's body |
| `hedef.wdl: Dusman_Tarama` (87-104) | d | Body: gun auto-aim or Inzar's spell targeting |
| `duma_yara.wdl: duma_Ol` (3068), `zipleyen_yara.wdl: Zipleyen_Ol` (3355), `babayaro_yara.wdl: babayaro_Ol` (3278) | d | Body: mana reward for Inzar's kills |
| `zipleyen_yara.wdl: Zipleyen_Yaralan` (881) | d | Body: hit-reaction chance scaled by the attacker |
| `efect2.wdl: tz42_isin_strtr` (4439), `laser_parti_ctrl` (4591) | d | Body: the Akrep laser sight on Demir's gun |
| `YukleMenu.wdl: SLG_LevelKaydet` (111) | b | Body: the load-list label names the character played |
| `oyuncu2_ebru.wdl` `dmr_yaralan_tip` reads (1239, 1289, 1294, 1303) | d | Hit kind: 2 → 0, 3 → 1 for a locked body (rule 9) |

### Story reads (unchanged)

| Script: function (lines) | Class | Handling |
|---|---|---|
| `cinematic.wdl: video_atilgan_de` (3028), `video_ebru_ev` (3453), `video_ebrudusme_ebr` (3633), `video_demirkurtul_f` (10224), `video_kagityak_s` (11095), `video_tasgir_t` (13565); `seytan.wdl: Seytan_Ol` (1016); `kotu_koruyucu.wdl: Kotukoru_Ol` (6327) | c | Story writes kept. With them the story swaps inventories (`DemirYedekle`/`EbruYukle`, Ornek_Yasin.wdl:3957-4100) and maps. While locked, rule 4 asks whether to keep the body |
| WMB player placements: Demir-only, Ebru-only (Tnel8/9, Tttg2, Ttn3, Ebrudus, Yereson, hapisane_dark), Inzar (Seytan_k, Tnel*) and mixed levels ([levels.tsv](../tests/levels/levels.tsv)) | c | Rule 1 |
| `LV_*_Giris` spawn branches: Ev, Ev2, Start3/4/5, Sd, Kk, Yuy, Binaici, Arasokak, Aralevel, Finish, kotukoru_d (`Aktif_Player==Player_Demir/Ebru`) | c | Story: the story character's spawn point (the body walks in where the story's character would) |
| `Nesneler.wdl: NS_AlTarama` (427 `NS_EbruAlsinmi`, 446-448), `NS_Nesne` (500 not Inzar, 505), `NS_Bulmaca` (607 not Inzar) | a | Story: an item or puzzle is available exactly as in the story's section, so any body takes it (for example the crowbar, item 16, at Start4's burning car, and Ebru's own items in her sections) |
| `Ornek_Yasin.wdl` item indicators `kasakey_sil`, `tacigazete_sil`, `askerjarjor_sil1/2` (212/270 Demir only), `kanca_sil`, `cakmak_sil`, `halat_sil`, `asmakirici_sil`, `kolyekiz_sil` (`NS_EbruAlsinmi`) | a | Story |
| `Ornek_Yasin.wdl` scans: `v_elektcarp_Tarama`, `v_testerekes_Tarama`, `v_kutuyakma_Tarama`, `v_tnel10cark_Tarama`, `v_taskoy_Tarama` (Demir), `v_taskapi_Tarama` (Inzar), `K_tnl11kapi_Tarama` (Inzar opens it, the others need the key), `k_lahamkapi_Tarama` (Ebru), `v_dmryakalan_*`, `hapisaneload_Tarama`, `lainoutload_Tarama`, `v_demirkurtul_*`, `v_kalpsapla_Tarama`, `v_evbul_Tarama`, `k_tnel15_Tarama` (2749) | a | Story: each gate passes for the story's character, so any body passes it |
| `Ornek_Yasin.wdl`/`LevelYonetim.wdl`: `demir_kilitli_tekrar>2 && aktif_player==0` in 14 door scans and `LY_KilitTarama` (1152) | b | Story: Demir's "it's locked" voice line still plays in Demir's story |
| `silah.wdl: Kursun_Al` (149), `KursunKutusu` (186-190), `Silah_Al` (257), `ss_Silah` (310-314): ammunition and weapon pickups, Demir only | a | Story: they go into the story's (Demir's) inventory |
| `Dokumanlar.wdl: DO_Dokuman` (314), `DO_GazeteDokuman` (408): no documents for Inzar | a | Story |
| `LevelYonetim.wdl: LevelYukle` (832 movie, 1035 control gate), `LY_Tarama` (1083-1101: Seytan_k/Seytan_E, Inzar-only doors, Demir/Inzar blocked doors) | a | Story |
| `cinematic.wdl` cutscene actors: `video_ebrudusme_*`, `video_kanirt_ebru`, `video_zorla_ebru`, `video_elektpano_e`, `video_elektcarp_e`, `video_mazot_dmr`, `video_mazotdok_d`, `video_taskoy_d`, `video_saraykapi_de`, `video_sraysalter_eb`, `video_evbul_d/e`, `video_korubilgi_k`, `video_tasgir_t`, `kafa_redglow`, `kapikafaci(_b)`, `seyheykel_demir`, `k_seytanout_Tarama` | a | Story: the cutscene plays with the story character's model while the player is hidden. Pre-rendered movies are fixed. See [Limitations](#limitations) |
| `kotu_koruyucu.wdl: kotukoru_ai` (6374), `lain_efekt_bas` (632) | a | Story: Lain's behaviour and effects in Inzar's fight |
| `cino_yara.wdl: cino_yaratik` (147): not in Ebru's story | a | Story |
| `YukleMenu.wdl: otosave_fish` (433) | a | Story |
| `Yeni_Oyun_Deger.wdl: Yeni_Oyun_Kolay` (9), `YD_OyunuYukle` (901), `YD_OyunuKaydet` (1589); `K22.wdl:150` | c/d | Story: the slot stores the story character; the lock lives in the sidecar |
| Door sounds `aktif_player<2`/`==2` (`kapiacma_inzar`) in 39 `LV_*_Giris/_Cikis`; `LV_Seytan_E` music (12); `LV_Finish` (42-43); `LV_Ebrudus` (18) | b | Story |
| `cam.wdl: kanodasi_camera` (2478), `hapisaned4_cam` (4085; also `klasik_generate.wdl:403`) | b | Story: camera zones in Demir's story |
| `klasik_yara.wdl: tamtam_kontrol_fx` (10), `music_script.wdl: tamtam_music_fx` (75) | b | Story: music |
| `efect2.wdl: mesale_alev` (3221-3260), `nesne_etkilesim.wdl: nesnebilgi_mesaj` (139) | b | Story |
| `yeni_harita.wdl: harita_starter` (2380) | none | Inside a comment block; not compiled |

Character state that needs no reroute: health and mana live in per-character variables (`oyn_yaralanma`, `ebru_yaralanma`, `koru_yaralanma`, `koruyucu_mana`). Each player action reads and writes only its own, and the story's resets (for example `cinematic.wdl:11060`) and the slot keep all of them. Enemy damage, healing pickups (`ENERJI` = `SKILL1`) and bullets (`Ates_Et`: any entity with `SKILL11`) are character independent. The HUD follows the body, because each player action opens its own HUD (`PEM_Ac`/`KVM_Ac`).

## Verification

All headless unless noted (`docs/GAMEPLAY_TESTING.md`):

- `level_sweep_demir`, `level_sweep_ebru`, `level_sweep_inzar` (CTest; `tests/levels/sweep.cmake -DCHARACTER=`): every one of the 101 levels, entered from an original save of the level's own character, places exactly one player action, the locked character's, and the player stays above the level geometry for 600 frames.
- `character_ebru_new_game`: selector (Right twice), Demir's opening as Ebru, Demir-only magazine pickups, the crowbar refused as in the story, Demir's pistol chosen in the original inventory and fired, grenade refused, quicksave/quickload keeps the lock (`character=ebru`).
- `character_inzar_crowbar`: Ebru's crowbar chain played by Inzar (Sd, Start4, item 16, Kk door pried, `v_ebrukanirt_ok`).
- `character_switch_prompt`: prompt opens on a story write, scripts paused, keep and switch both work, the next level places the chosen body.
- `world_door_character`: the prompt during an open seamless doorway pauses the door; the seamless swaps place the locked body ([SEAMLESS_DOORS.md](SEAMLESS_DOORS.md)).
- With `run.cmake -DCHARACTER=ebru|inzar`: `world_item_combine`, `quicksave1_roundtrip`, `quicksave2_restart`, `demir_level5_camera` and `combat_kriz_game_over` pass for both, `combat_kriz_aa_gun` for Inzar. The other runs stop at assertions written for Demir's body (`oyn_yaralanma`, `koruyucu_pasif`) or at harness steering (`walk-to`/`route` timeouts with Ebru's and Inzar's movement), not at script errors. They were not adapted yet (`$health` now gives the body's health variable).
- Boss fights (`# character:` scenarios in `gameplay_scenarios`), headless, fixed seed:
  - `character_boss_lain_demir`, `character_boss_lain_ebru`: Lain killed with the story's firearm, `lain_sona_erdi`, the switch prompt kept Ebru, Buyukes with the locked body, Buyukes autosave.
  - `character_boss_seytan_demir`, `character_boss_seytan_ebru`: Tnel13 → Seytan_k, shotgun from the inventory, energy items between rounds (10 and 7 used), Seytan killed, story switch to Ebru (prompt kept Demir), wake-up movie, Ebrudus with the locked body.
  - `character_boss_final_ebru`, `character_boss_final_inzar` (seed 0, console `god` keeps the body alive: the harness aims and fires but does not dodge): `kayit26.sav` → bosskoli → boss, creatures until `boss_oyun_mode` 1 and none left, the boss to 1000 → `boss_ebru_start`, the guard's entrance (990), the boss below 500 → `ebruboss_ol` → `gaile_kullan_ok`, item 38 used at `v_oyunsonu_ctrl` (118,51) → `v_oyunsonu_basla`, the ending movie and `Menu_Acilis`. Ebru (shotgun from (118,51), hit kinds of rule 9, guard rule 10): creatures done at frame 19118, 990 at 21292, `gaile_kullan_ok` 23308, end 24243. Inzar (M → spell menu → M → item inventory → M closed; spell 4 bolts, scale 20): mana 230 after the creatures (sword contact), 4 bolts 1800 → 1000 (frames 10357-10812), 3 bolts 990 → below 500, 69 mana left; end at frame 17016. The pre-fix Ebru run hung at frame 18828 (rule 9). For comparison Demir with the shotgun takes about 2800 frames from 1800 to 1000, Ebru about 2200; Inzar's 7 bolts take about 660 frames of casting. 20 is the smallest round scale that leaves spare bolts: at 15 the two phases need 6 + 4 = 10 bolts, her whole 230 mana.
  - Demir (original, no lock) in the same harness: phase 1 with the shotgun reaches `boss_ebru_start` (frame ~10600); in phase 2 grenades thrown from (100,60) take the boss 990 → 509 (40-90 per grenade, they bounce off `boss_engel`), then the player was lost ("no player", frame 14615, standing at (157,68)) before the last grenade. Demir's full fight to the ending is **not established** by a scenario; the generator and logs are in the character-lock scratch tree.
- On the desktop (`DISPLAY=:0`, OpenGL 1280x720): the three `character_*` scenarios pass with captures of the selector, Ebru at Start4's burning car, the switch prompt, and Inzar at Start4 and the Kk cutscene.

## Limitations

- Cutscenes show the story's character. In-engine scenes swap the hidden player for a `v_*` actor of the story character, and the pre-rendered movies are fixed. Where the locked character is also an NPC of the scene (for example Ebru's body as an NPC in Saraygir `video_ebrumefta`, or `boss_ebru_ai` in the final fight with an Ebru body), both appear. The story is told as written.
- Demir's "it's locked" voice line and other story voice lines are not replaced for another body.
- Ebru fires the story's firearms with her magnum pose. Weapon models are not attached to her hand.
- The question at a story switch appears when the story writes the new character, usually just before the transition's fade.
- Seytan's swings and black blast hurt Inzar only through her own reaction, so a gun body takes no damage from them (its fire breath and fireballs still hurt).
- After an energy item, Ebru cannot reload until she is hit again (`oyuncu2_ebru.wdl:1684` reloads only while `ebru_yaralanma==my.enerji`; `KuBiGoMenu.wdl:144` raises `player.skill1` only). This is the original script; it applies in her own story too.
- An energy item caps health at 100 (`KuBiGoMenu.wdl:144`), below Inzar's 200.
