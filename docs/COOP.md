# Local two-player coop

Purpose: two players on one screen, each playing Demir, Ebru or Inzar (any combination, duplicates too). This is a native addition; the original scripts are not modified.

Related: [Character lock](CHARACTER_LOCK.md) · [Gameplay testing](GAMEPLAY_TESTING.md) · [Gameplay systems](GAMEPLAY_SYSTEMS.md)

Status: playable. The menu and character selection are `native/coop_menu.cpp`; the gameplay (per-player global banks, P2's body, input, shared camera, respawn at the partner, P2's HUD, saves) is `native/coop.cpp` on top of the VM banks (`Runtime::enable_banks`, `native/wdl.cpp`).

## Menu and character selection

- **Main menu:** a `2 PLAYER` item sits one button above NEW GAME (`AnaMenu.wdl` stacks NEW GAME, LOAD GAME, OPTIONS, CREDITS, EXIT bottom-up). Up from NEW GAME or Down from EXIT reaches it; Down/Up leave it to NEW GAME/EXIT. Every original Down sequence from NEW GAME is unchanged. The mouse highlights it on hover and opens it on a click; Enter, pad A or Start opens it too. The menu frame (`DisMenuCerceve.wdl` `CER_Hedef`) grows by one button to cover it.
  - The label is spelled with glyphs cut from the original menu atlas (`menu_dildenbagimsiz.tga`, the key-binding labels of `TusYonetim.wdl`; `1` and `2` are the cells at (0,0) and (0,32)), drawn 1.15× to match the button lettering. Highlighted, it uses the buttons' selected style: `Menu_*_A.tga` is `Menu_*_N.tga` with the fill raised to grey (G = B ≈ 1.85·(R − 62)), so the native copy of the atlas is recoloured the same way.
  - While the item is highlighted, the original cursor (`AM_Cursor`, 0 or 4) stays where it was with its button drawn unselected; presses the item consumes are hidden from the scripts (`DM_TusKontrol`, `KEY_*`) until released, and the native menu plays the original sounds (`DMS_Yontus`, `DMS_Enter`, `DMS_Esc`).
- **Character select:** opening the item closes the main menu the way `AM_Kernel` does (`AnaMenuKapat`, its timer entity removed) and draws over the menu background (movie, `DMP_Arka`), with the menu frame moved around the screen.
  - Two columns, PLAYER 1 and PLAYER 2. Each shows the highlighted character's own model (`demir_2007.mdl`, `ebru_2006.mdl`, `koruyucu_2006.mdl`) as a VIEW entity (the `silah.wdl` `SilahPanel` precedent), cycling its idle frames (Demir `byBekle`, his arms-down stance; Ebru and Inzar `bekle`) and turning slowly, at true relative size with the feet on one line. Below it `< NAME >` and READY.
  - **P1:** keyboard, mouse and pad 0. Left/Right (or a click on the arrows) choose, Enter / A / Start (or a click on READY) is READY, Escape / B unreadies, and when not ready returns to the main menu with 2 PLAYER highlighted.
  - **P2:** pad 1: A or Start joins, Left/Right choose, A is READY, B unreadies or leaves. Until P2 joins its column shows PRESS START and no model. A keyboard fallback drives P2 too: A/D choose, Space joins and is READY, Backspace unreadies or leaves.
  - When both are READY (and every key is released), the original New Game difficulty menu opens (`YeniMenuAc`) with the coop choice pending; the character lock's `< ORIGINAL >` selector is hidden meanwhile. Escape there returns to the main menu and drops the choice.
- **Start:** when `YeniOyunBaslat` runs with the choice pending, `Engine::coop` becomes `{active=true, characters={P1, P2}}` (0 Demir, 1 Ebru, 2 Inzar) and P1's character becomes the [character lock](CHARACTER_LOCK.md). A New Game from NEW GAME, or the load of any save slot, sets `coop.active=false`.

### Controllers

`Engine::coop_pad(player)` assigns the game controller slots (`Input::controllerConnected`, stable slots, `native/render.cpp`):

| Pads connected | P1 | P2 |
|---|---|---|
| two | slot 0 (and keyboard/mouse) | slot 1 |
| one | keyboard and mouse only | that pad |
| none | keyboard and mouse | the select screen's keyboard fallback only; in play P2 needs a pad |

The select screen reads P1's and P2's pads through `coop_pad`. In play, `Engine::legacyPad` (the pad behind the original joystick globals `joy_force`/`joy_rot`/`JOY_*` and the d-pad-as-arrows aliases) is `coop_pad(0)`, so P2's pad never drives P1. Outside coop `legacyPad` is the lowest connected slot (single-player failover: a pad left alone in slot 1 drives the player). The original menus take the legacy pad's d-pad as the arrow keys in both controller modes and A/B as Enter/Escape in modern mode (`Engine::key_state`).

SDL also opens every pad as a raw joystick (`Input::joystickButtons`, the original `JOY_1..12`/`joy_raw` that `GenelAyarlar.wdl` `Joy_Takip`/`Tuslari_Esitle` turn into `Key_*`). `Input::joystickPad[j]` names the game controller slot of raw slot j (`Renderer::poll` matches SDL instance IDs). In coop, `Engine::tick` clears every raw slot whose device is a pad other than P1's and moves P1's into raw slot 0, so P2's buttons never reach bank 0's `Key_*`. Before 0.4.6 they did in original controller mode (no `controller.ini`): P2's stick moved P2, but P2's buttons aimed and fired P1. A raw joystick that is not a game controller still belongs to P1. Regression: `tests/scenarios/coop_pad_buttons.k22s`.

## Gameplay

### Per-player global banks

The player actions (`Oyuncu_Demir`, `Oyuncu_Ebru`, `oyuncu_koruyucu`) keep their state in globals: `player`, `Key_*`, `Aktif_Silah`, `a_Silah`, `silah_kaldir`, `player_distance`, the hit channel `dmr_yaralan_tip`/`d_yaralan_source`, Inzar's key latches and spell state, and so on. Two players need two copies. `Runtime::enable_banks` gives each listed global two values; a process runs with its bank's values live and bank 0 is live between processes, so the Engine, the cameras, the HUD panels and the console see P1. Bank 1 starts as a copy of bank 0 when coop starts; P2 then gets full health (and Inzar full mana).

**Banked globals** = every global assigned in a function of `oyuncu2.wdl`, `oyuncu2_ebru.wdl`, `koruyucu.wdl`, `koru_buyuler.wdl`, `silah.wdl` and `hedef.wdl` (`Runtime::assigned_globals`), plus `player`, all ten `Key_*`, `dmr_yaralan_tip`, `d_yaralan_source`, `koyucu_yaralan_tip`, `game_over_start`, `Aktif_Silah` and `player_morph_ok`, minus the **shared deny-list** (one value for both players): the story gates `oyuncu_pasif` and `koruyucu_pasif`, `Aktif_Player`, `time_factor`, `level_yuklendi_ok`, `LY_AktifLevelID`, `LY_HedefLevel`, the inventory and its ammo pool (`Silahlar`, `SilahSayisi`, `Kursun_Sayisi`, `S_Kursun_Sayisi`, `Nesneler`, `NesneSayisi`), picked-up story objects (`unic_obje_dizi`), the final boss guard's line (`boss_ebruhedefpan`), `freeze_mode`, the engine scratch and camera globals (`move_mode`, `move_friction`, `trace_mode`, `shadow_offset`, `camera`, `fog_color`, `result`), the kill counter `aktif_oldurme_sayisi`, the menu gates (`EnvaterAcilabilirmi`, `HaritaAcilabilirmi`, `SmartAcilabilirmi`), story/world flags (`ucak_olum_ok`, `oyun_finish_start`, `cadde_yara_adet`, `v_dmryakalan_ok`) and Inzar's effect-trail scratch arrays (`buyuparca_dizi`, `solsword_alt_dizi`, `solsword_ust_dizi`, `sagsword_alt_dizi`, `sagsword_ust_dizi`, `effectenerji1_alt_dizi`, `effectenerji1_ust_dizi`, `effectenerji2_alt_dizi`, `effectenerji2_ust_dizi`: 47,000 elements that every bank switch would copy, about 1 ms a frame; two Inzars swinging at once share the trail buffers). Names that do not exist in the loaded scripts or cannot be banked stay shared (stderr `kabus22: coop: …; shared`). The final list is printed once when coop starts (`kabus22: coop banks (N): …`). With the original ISO scripts it is these 117 globals:

`a2_basildimi a3_basildimi a_basildimi a_silah a_telsiz adim_korudon adim_korukosma adim_nyuru adim_sesleri Aktif_Silah animenerji_ent bicak_carp_dizi bicak_carp_handle bicak_carp_sira bomba_aktif bombacam_basla_m bombacam_basla_t bombacam_basla_z bombacam_salla_s buyu_yildirim buyucibicibi_dizi buyuparca_adet buyuparca_pan buyuparca_sira cibicibi_basladi d2_basildimi d_basildimi d_sl_fazla d_yaralan_source demir_oto_dist demirah_handle demirtekme_handle devilyaralan_tip dmr_yaralan_tip ebru_yaralanma ebruyaralan_dizi effectenerji1_ent effectenerji2_ent effectenerji_basla elbomba_sil elmayin_sil ent_elbomy ent_mayin game_over_start gard_al geri2_basildimi geri_basildimi hava_buyusu jarjor_sesleri kan_possible kanloc_tem1 kanloc_tem2 Key_Ates Key_Buyu Key_Geri Key_Harita Key_Ileri Key_Kosma Key_Menu Key_Sag Key_SilahKaldir Key_Sol kind_of_bomb kombo_hedef_no koru_adu_izin koru_efektbas_ok koru_ses_data koru_yaralanma koruaduket_aci koruaduket_aktif koruenrji_dizi korutemas_dizi koruyaralan_dizi koruyucu_distance koruyucu_mana koyucu_yaralan_tip kursun_hizi liste liste_kacta m_izi_cam_dizi m_izi_met_dizi m_izi_string m_izi_tah_dizi m_izi_tga_dizi mayin_aktifmi mayin_koor_veri mayin_sira_no mermi_yok mermikan_dizi nrpates_et one_vurusu one_vurusu_aktif one_yavasla oyn_donme_aktifmi oyn_yaralanma player player_distance player_morph_ok real_oto_hedef s2_basildimi s_basildimi sagsword_basla sagsword_ent salla_frame ses_return seytan_logo_aktif silah_isimleri silah_kaldir silahpaneldon solsword_basla solsword_ent suzme_ses_handle tam_frame_name tz42_isin_aktif w_basildimi yara_skill4no yliste_kacta`

(The `*_dizi` tables are filled once at startup and are equal in both banks.)

### Which bank runs a function

The bank resolver (`Engine::coop_resolve`) decides per function, by the script file it is defined in:

| Functions | Bank |
|---|---|
| player files (above) | the bank of the player entity they run on (`my` is P1's or P2's body); otherwise the caller's (a gun's `Silah_Ekle`, a grenade, `Dusman_Tarama` fired by P2's aim scan); a native P2 spawn starts in bank 1 |
| enemies and their hits: every `*_yara.wdl`, `bond_bekci.wdl`, `boss.wdl`, `latex.wdl`, `seytan.wdl`, `kotu_koruyucu.wdl`, `efect2.wdl`, `klasik_generate.wdl` | dynamic: at every resume, the bank of the live player nearest to the enemy (`my`); bank 0 without `my` or a live player. The enemy then chases, hits and hurts that player through its bank's `player` and hit globals |
| `PEM_Ac`, `PEM_Kapat`, `KVM_Ac`, `KVM_Kapat`, `BEM_*` (HUD), `EnvanterAcKapat`, `KoruyucuAcKapat`, `EM_Kernel` (menus) | P1's: called from bank 1 they are skipped (P2's HUD is native; P2 has no menu key) |
| everything else: story, cinematics, `LevelYukle` and the `LV_*` Giris placements, cameras (`cam.wdl`), menus, starters (`Tuslari_Esitle`, `Joy_Takip`) | bank 0 |

So a door P2 triggers loads the level for P1, cameras frame P1, and scripted scenes act on P1.

### Characters

P1's character is the [character lock](CHARACTER_LOCK.md). The body reroute `k22_body_player` answers from `coop.characters[active bank]`, so each action's guard, Ebru's hit kinds, auto-aim and kill rewards see their own player's character. Both players obey the story character's control gate (`oyuncu_pasif`/`koruyucu_pasif`), so both stand still in cutscenes. The story-switch prompt never opens in coop: both characters are fixed. P1's weapon rules (`character_weapons`) are the character lock's; P2 Demir holds weapons 1-7 (the pistol replaces anything else at his spawn), P2 Ebru selects her magnum at every spawn as her action does.

### Player 2's body

After every level entry, once P1 is placed (bank 0 `player_morph_ok` 1, `LevelYukle` with its Giris placement finished, no pending load or quickload position, 10 frames settled), `coop_spawn` creates P2's character's own player action at a free spot beside P1: P1's hull swept (USE_BOX|IGNORE_PASSABLE) to 40 units right, left, 45 behind, then closer (28-30, for tunnels) and wider spots, on a floor within 32 units of P1's, facing P1's PAN. The action runs in bank 1 and claims bank 1's `player`. Inzar first gets her `koru_efekt_bas` helper (sword and energy effect entities), as the character lock does. P2 is never a WMB-placed action (`apply_character_lock` keeps P1 only); a level load removes P2's body like every entity.

While P1 is invisible (cutscenes) P2 is hidden and passable too.

**Pushing:** the players push each other instead of blocking. When one's `ent_move` (`move_a6_legacy`) runs into the other, the rest of that step moves the other (`coop_shove`, swept with USE_BOX|IGNORE_PASSABLE against the level and other entities) and the step is retried, up to twice per move; a player that cannot move (against a wall or an enemy) still blocks, so the bodies never overlap. Walking into each other head-on, the pushes cancel. Off while either is down or hidden.

### Player 2's input

Every frame before the scripts run, bank 1's `Key_*` are written from P2's pad with `Tuslari_Esitle`'s semantics: `Key_Ileri/Geri/Sag/Sol` from `Joy_Takip`'s `JOY_*` (half of `joy_force`), the buttons through `GA_JTuslar`. `joy_force` and the buttons come from the modern controller mapping (`apply_modern_controller`: digital forward/back past half deflection, analog turn while walking, radial deadzone, `controller.ini` `[Buttons]`), for P2 in both controller modes. With the default buttons: left stick move, LT aim/Inzar's slash (`Key_SilahKaldir`), RT fire (`Key_Ates`), LB run (`Key_Kosma`), Y spell (`Key_Buyu`); `Key_Menu` and `Key_Harita` are always 0 for P2. Demir's and Ebru's reload key (`key_enter`, `oyuncu2.wdl:2529`) reads P2's A in bank 1 (`k22_coop_enter`).

**Weapons:** P2 has no inventory menu; the d-pad right/left cycles the shared inventory's firearms the P2 body can hold (Demir 1-7, Ebru 1-5 and 8), equipping as `EM_KuBiGoSMenu` does (`Aktif_Silah`, Demir's gun morphed to `Silah_isimleri[Aktif_Silah]`), not while aiming. P2 Inzar's d-pad cycles her spells 1-6 (`buyu_no`, as `KoruyucuMenu.wdl` sets it). The ammo pool is shared.

**Rumble:** `rumbleEntity[coop_pad(1)]` is P2's body, so P2's pad rumbles for P2's damage.

### Camera and leash

The original fixed zone cameras follow bank 0's `player`: P1 drives the shot, P2 is pulled along. When P2 is more than 350 units from P1, or more than 250 below P1 or below the level, for 2 s, P2 returns to a free spot beside P1.

### Death and respawn

Each player's death loop sets its own bank's `game_over_start`. `game_over_strtr`'s read of it is rerouted (`k22_coop_game_over`): the original game over starts only when both players are down, when one is down and the other has no living body, or for a scripted death while P1 lives. Otherwise, about 5 s after the death, the dead body (with its gun and radio) is removed and that player respawns beside the living partner with full health; its `game_over_start` is cleared. Demir's death loop also clears the story's `level_yuklendi_ok` (`oyuncu2.wdl:2961`): it is restored when the partner lives. When both are down the original game over runs to the main menu and coop ends.

### HUD

P1's HUD is the original scripts' (`PEM_Ac` or `KVM_Ac`, bank 0, top-left). P2's is native (`coop_hud`), top-right, mirrored from the same BMAPs: Demir/Ebru the energy ring (`PEML(1024)_Enerji`, `20·Enerji/100` rounded as `PEM_Ac`) and the beating heart (`PEML(1024)_Kalp`); Inzar the spell icon, health and mana bars (`KVIL(640)_Buyu/Enerji/Mana`) and the dimmed spell when mana is short (`KVI*_delik`). It uses P1's HUD panel scale and is shown while P1's HUD is.

### Saves

Every slot written in coop records `coop=P1,P2` (for example `coop=demir,ebru`) in `kayitN.sav.meta` next to `character=` (P1's lock). Loading such a slot restores coop: banks, P1's lock and P2 beside P1 after the level entry. Loading a slot without it ends coop. P2's own values (health, weapon choice) are not in the original 2030-byte save: P2 rejoins with full health.

### Console

`coop` prints the coop state, `coop P1 P2` (demir, ebru or inzar each) starts coop in a running game (P1's lock applies from the next level entry; P2 joins beside P1), `coop off` ends it (P2's body is removed). `kill 2` kills P2.

### Limitations

- P2 needs a pad in play; the keyboard is P1's.
- One shared screen: P1's fixed cameras may leave P2 out of the shot until the leash returns P2.
- Enemies pick the nearest player at each resume; an enemy can switch targets mid-attack, and a hit landing on the farther player is ignored by the original `you==player` check.
- The ammo pool, inventory, story flags and picked items are shared; only P1 opens the inventory, map and spell menus. P2's grenades do not shake the camera (the camera reads bank 0).
- Scripted scenes, doors and pickups act on P1 even when P2 triggers them.
- Seamless doors ([SEAMLESS_DOORS.md](SEAMLESS_DOORS.md)) open when either player walks up to them and cross when either walks in; they close once both are more than 140 units away.
- P2's health and weapon are not saved.

### Menu limitations

- The original main menu shows no mouse pointer (`mouse_mode` 0); hover and click work on the native item and the select screen without a visible pointer, as on the character lock's selector.

## Testing

`tests/scenarios/coop_menu_select.k22s` (headless, part of `gameplay_scenarios`): main menu navigation to and from 2 PLAYER, the original Down sequence, select screen open/Escape back, P1 and P2 (keyboard fallback) choices, READY/unready, the pending New Game menu and its Escape, then NORMAL starts Demir's opening as P1 Ebru (`Oyuncu_Demir` runs `Oyuncu_Ebru`) with `$coop` 1, `$coop1` 1, `$coop2` 2. Scenario terms: `$coop`, `$coop1`, `$coop2`, `$coopmenu` (0, 1 the item highlighted, 2 select screen), `$pick1`, `$pick2` (the select highlight, −1 not joined), `$p2`, `$p2x`, `$p2y`, `$p2z`, `$p2health`, `$bank1(GLOBAL)`; P2 is driven with `pad 1 …` (the only pad is P2's) and `pad 1 press-until BUTTON COND`.

Gameplay scenarios (headless, `gameplay_scenarios`):

- `coop_new_game`: menu → 2 PLAYER → P1 Demir (keyboard), P2 Ebru (pad 1) → NORMAL → Start4: two player entities, each its bank's `player`; P2 walks while P1 and the camera stay, P1 walks while P2 stays; P2 takes the pistol with the d-pad, aims and fires (shared ammo falls), P1 neither aims, fires nor moves.
- `coop_same_character`: Demir+Demir (kayit14.sav): P1 takes the machine gun in the original inventory, P2 the pistol with the d-pad; each aims and fires his own weapon while the other neither aims, fires nor moves.
- `coop_inzar`: Demir+Inzar: P2 Inzar with her helper swings her sword and casts spell 4 (her own mana falls by `buyu_gereksinimi[4]`); P1's original HUD stays (`PEM_Acik` 1, `KVM_Acik` 0) and P1's mana is unchanged.
- `coop_combat_respawn`: Demir+Ebru in the duma fight (Tnel9 → Tnel10): leash, P2 recreated beside P1 in the next level, the duma hurts P2, P2 dies (`kill 2`) and respawns beside P1 ~5 s later with full health and no game over, P1 dies and respawns beside P2, both down → the original game over to the main menu and coop off.
- `coop_door_save`: Demir+Ebru through the Tnel9 → Tnel10 door (P2 recreated), quicksave (`coop=demir,ebru` in the sidecar), `coop off`, quickload restores coop with both characters; loading the original slot 14 ends coop.
- `coop_push`: Demir+Ebru (kayit14.sav): P1 walks into the standing P2 and shoves P2 aside instead of stopping against P2; they never overlap.
