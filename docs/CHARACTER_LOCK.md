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
| `k22_menu_player()` (`EM_Kernel`) | `Aktif_Player` | the locked character, except that Inzar's body follows the story (her spell menu only in her own story, the item inventory elsewhere) |
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
| `Envantermenu.wdl: EM_Kernel` (333-335; 315) | d (a at 315) | Menu view: M opens the item inventory, or Inzar's spell menu in her own story. Line 315 (no inventory on entering kotukoru_d as Inzar) sees the same value: a gun body there gets the inventory |
| `hedef.wdl: Dusman_Tarama` (87-104) | d | Body: gun auto-aim or Inzar's spell targeting |
| `duma_yara.wdl: duma_Ol` (3068), `zipleyen_yara.wdl: Zipleyen_Ol` (3355), `babayaro_yara.wdl: babayaro_Ol` (3278) | d | Body: mana reward for Inzar's kills |
| `zipleyen_yara.wdl: Zipleyen_Yaralan` (881) | d | Body: hit-reaction chance scaled by the attacker |
| `efect2.wdl: tz42_isin_strtr` (4439), `laser_parti_ctrl` (4591) | d | Body: the Akrep laser sight on Demir's gun |
| `YukleMenu.wdl: SLG_LevelKaydet` (111) | b | Body: the load-list label names the character played |

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
- With `run.cmake -DCHARACTER=ebru|inzar`: `world_item_combine`, `quicksave1_roundtrip`, `quicksave2_restart`, `demir_level5_camera` and `combat_kriz_game_over` pass for both, `combat_kriz_aa_gun` for Inzar. The other runs stop at assertions written for Demir's body (`oyn_yaralanma`, `koruyucu_pasif`) or at harness steering (`walk-to`/`route` timeouts with Ebru's and Inzar's movement), not at script errors.
- On the desktop (`DISPLAY=:0`, OpenGL 1280x720): the three `character_*` scenarios pass with captures of the selector, Ebru at Start4's burning car, the switch prompt, and Inzar at Start4 and the Kk cutscene.

## Limitations

- Cutscenes show the story's character. In-engine scenes swap the hidden player for a `v_*` actor of the story character, and the pre-rendered movies are fixed. Where the locked character is also an NPC of the scene (for example Ebru's body as an NPC in Saraygir `video_ebrumefta`, or `boss_ebru_ai` in the final fight with an Ebru body), both appear. The story is told as written.
- Demir's "it's locked" voice line and other story voice lines are not replaced for another body.
- Ebru fires the story's firearms with her magnum pose. Weapon models are not attached to her hand.
- The question at a story switch appears when the story writes the new character, usually just before the transition's fade.
