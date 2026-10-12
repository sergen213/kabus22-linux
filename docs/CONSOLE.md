# Save anywhere and developer console

Native additions (`native/console.cpp`); the original game has neither. Both stay inert until a key or command uses them, so
ordinary play, the original save points and all rendering paths are unchanged.

Related: [Gameplay systems §10 Save and load](GAMEPLAY_SYSTEMS.md#10-save-and-load) · [Gameplay testing](GAMEPLAY_TESTING.md)

## Keys

| Key | Action |
|---|---|
| F5 | Quicksave. With the community patch scripts (`K22Widescreen.wdl`) F5 belongs to their first-person camera (`cam.wdl` polls `key_f5`), so quicksave is **F8** there. |
| F9 | Quickload the newest save slot. |
| `` ` `` (the key left of 1, `SDL_SCANCODE_GRAVE`) or F10 | Open/close the console. F10 is for layouts where the `` ` `` key is awkward (Turkish Q prints `"` there; the key still works by position). |
| Escape | Close the console. |
| Up / Down | Command history. |
| Tab | Complete a command name. |
| Return | Run the line. |

The original scripts never read F5, F8, F9, F10 or `` ` ``: `K22.wdl:1027-1058` sets `on_f1`…`on_f12` and `on_grave` to `null`, and
no script polls those keys. Movement keys can be rebound in the original Controls menu; binding one of these keys there makes it
do both.

While the console is open the game receives no keyboard, mouse or pad input (time keeps running, so close it before a fight).
After it closes the game sees nothing until every key is released, so the closing key never reaches the scripts (Escape would
otherwise open the in-game menu).

## Save anywhere

A quicksave is an ordinary slot of the game's own save list. It runs the same three script functions as a save point
(`YukleMenu.wdl` `LV_LevelKaydeta`): `SLG_LevelKaydet` (load-menu label), `YD_OyunuKaydet` (`Yeni_Oyun_Deger.wdl`, the 2030-byte
`kayit<N>.sav` payload) and `SLG_Kaydet` (`kysd.sav` index). It also counts in `aktif_kayit_sayisi` like a save point. Slots follow
the game's own ring (`Aktif_SaveNo`, 99 slots, `SLG_MaxKayit`), so a quicksave never overwrites the newest save. The load-menu
label is `Quicksave` (`Hızlı Kayıt` with the Turkish scripts) with the character, date and difficulty like any other entry.

- **Where:** the save root (`${XDG_DATA_HOME:-~/.local/share}/kabus22`, or its `_data/saves/` with the patch scripts), with the
  usual atomic replacement and rotating `backups/`.
- **When:** only while a level is in play and the player has control: `LY_AktifLevelID` set, `level_yuklendi_ok`,
  `player_morph_ok`, no level change (`LY_HedefLevel`, `Yukleme_Yapilsin`), no game over or ending, no menu or puzzle
  (`freeze_mode`, `DM_Acik`), no full-screen video and `oyuncu_pasif`/`koruyucu_pasif` clear. Otherwise the save or load is
  refused with a message ("Cannot save now: a menu is open") and nothing is written.
- **Position:** the original format stores no position; a script save point always respawns at the level's `LV_*_G00`. A
  quicksave adds `position=LEVEL X Y Z PAN` to the slot's `kayit<N>.sav.meta` sidecar (the payload is never extended). Loading
  that slot, by F9, `load` or the original load menu (also after a restart), puts the player back at that spot once the level
  has loaded. Slots without the line (save points, autosaves, imported Windows saves) load exactly as before.
- **What is restored:** everything the original save holds (level, character, weapon, health, ammo, inventory, mana, story
  flags, difficulty) plus the position. Per-level state the original does not save (for example an enemy killed since the
  level was entered) is rebuilt by the level scripts as on any load.

**Quickload** (F9 or `load [SLOT]`) uses the original load: it performs the teardown of the in-game menu's "main menu" choice
(`Smartmenu.wdl:208-243`: stop sounds and video, hide subtitles and letterbox, close the inventory/energy panels,
`LY_AktifLevelID = 0`) and hands the slot to the original `Yukleme_Kontrol` starter exactly as the load menu does
(`YUM_Cursor`/`YUM_Kafa`, `Yukleme_Yapilsin`). The newest slot is the one before `Aktif_SaveNo`, whether it came from a quicksave
or a save point.

Messages ("Game saved (slot 27)", "Game loaded", refusals) appear at the top of the screen for three seconds and in the console.

## Console commands

Command names are case-insensitive. Output also goes to stderr (`kabus22: console] …`).

| Command | Effect |
|---|---|
| `help [COMMAND]` | List all commands, or one command's usage. |
| `save` | Quicksave (as F5). |
| `character [original\|demir\|ebru\|inzar]` | Print or set the character lock ([Character lock](CHARACTER_LOCK.md)). It applies from the next level entry (a door, `level ID` or a load); `original` lets the body follow the story again. |
| `load [SLOT]` | Quickload the newest slot, or `kayitSLOT.sav` (0-98; the load menu shows slot N as entry N+1). |
| `god` | Toggle invulnerability: after every frame the played character's `ENERJI` (`SKILL1`) and health VAR are refilled (Demir `oyn_yaralanma` 100, Ebru `ebru_yaralanma` 100, Inzar `koru_yaralanma` 200, the new-game values). A hit can still play its flinch animation, and one hit of the full health or more in a single frame (an own grenade at point blank) still kills. |
| `noclip` | Toggle walking through walls and entities: the player gets `PASSABLE`, which the A6 mover treats as "move without collision". Gravity and floor tracing still apply, so it walks, not flies. |
| `give health` | Refill health as above. |
| `give mana` | Inzar's `koruyucu_mana` to 230. |
| `give ammo` | Fill the magazine (`S_Kursun_Sayisi`) of every carried weapon and add three magazines of reserve through the original pickup function `oyunici_kursun_al` (grenades and mines only get the magazine, as in `Silah_Al`). |
| `give weapons` | Demir only: add every weapon 1-7 he does not carry, with a full magazine, as `Silah_Al` does for a new weapon. |
| `give all` | All of the above. |
| `get NAME`, `get NAME[i]`, `get NAME.FIELD` | Print a WDL global, a VAR array element or a field of an object global (`get player.skill1`, `get camera.ambient`). |
| `set NAME VALUE`, `set NAME[i] VALUE`, `set NAME.FIELD VALUE` | Write a number the same way (`set OyunZorluk 1`, `set player.skill1 50`). Strings and objects are read-only. |
| `levels` | List the campaign level IDs and WMB files (`LVSL_Leveller`). |
| `level ID\|WMB`, `map ID\|WMB` | Go to a level through the original door transition (`LY_HedefLevel`, `LevelYonetim.wdl LY_YuklemeTakip`), as a door from the current level would. The level's own entry rules apply: an entry refused by the scripts shows their message, and a level without an entry point for the current level keeps the WMB's own player placement. |
| `pos` | Print the player position, `PAN`, level ID and WMB. |
| `tp X Y Z [PAN]` | Teleport the player. |
| `kill` | Set the player's `ENERJI` to 0: the original death and game over (also turns `god` off). |
| `fps` | Toggle a frame-rate counter (top right). |
| `timescale [FACTOR]` | Print or set `TIME_FACTOR` (0.1-10, 1 = normal). |
| `clear` | Clear the console. |
| `quit` | Exit immediately (no save). |

Cheats change live game state that the next save stores (for example `give weapons`). `god` and `noclip` themselves are not saved.

The console draws with the engine's own UI text path (system monospace font through fontconfig/FreeType), so it looks the same on
OpenGL, Vulkan and Vulkan ray tracing: captures in `reverse/console/` (local evidence).

The Bonus game (`--mode bonus`) has no campaign save system or level table: `save`, `load`, F5/F9 and `levels`/`level` report
that, and `god`/`give`/`kill` (campaign health and weapon variables) are campaign only. `get`/`set`/`timescale`/`fps`/`clear`/
`quit` work there; `noclip`/`pos`/`tp` need a `player` entity.

## Levels

All 101 campaign levels from the original level table (`LevelYonetim.wdl LVSL_Olustur`). Use `level ID` or `level WMB`
(e.g. `level 22` or `level level5`). IDs 63 and 96 are empty. 33 and 34 reuse `kriz`. Character: who the level places as the player.

| ID | WMB | Character | ID | WMB | Character | ID | WMB | Character |
|---|---|---|---|---|---|---|---|---|
| 1 | `start4` | Demir | 35 | `kizic3` | Demir | 70 | `saraygir` | Demir |
| 2 | `sd` | Demir | 36 | `kizic` | Demir | 71 | `saraygiralt` | Demir |
| 3 | `start5` | Demir | 37 | `kizic2` | Demir | 72 | `masso_lg` | Demir |
| 4 | `level4` | Demir | 38 | `krizkuyu` | Demir | 73 | `massotaltkat` | Demir |
| 5 | `start3` | Demir | 39 | `tnel24` | Inzar | 74 | `kolidorsag` | Demir |
| 6 | `nstart7` | Demir | 40 | `tnel12` | Inzar | 75 | `ayinyeri` | Demir |
| 7 | `cam1` | Demir | 41 | `tnel66` | Inzar | 76 | `kutuphane` | Demir |
| 8 | `yuy` | Demir | 42 | `tnel13` | Inzar | 77 | `yemekodasi` | Demir |
| 9 | `mekke` | Demir | 43 | `tnel10` | Inzar | 78 | `kanodasi` | Demir |
| 10 | `start7` | Demir | 44 | `tnel15` | Inzar | 79 | `saatodasi` | Demir |
| 11 | `mezarlik` | Demir | 45 | `tnel19` | Inzar | 80 | `dumur` | Demir |
| 12 | `start1` | Demir | 46 | `tnel16` | Inzar | 81 | `kolidorsol` | Demir |
| 13 | `kk` | Ebru | 47 | `tnel14` | Inzar | 82 | `dumurek1` | Demir |
| 14 | `start2` | Demir | 48 | `tnel7` | Inzar | 83 | `bedkorgel` | Demir |
| 15 | `untitled44` | Demir | 49 | `tnel21` | Inzar | 84 | `buyukes` | Demir |
| 16 | `iceri1` | Demir | 50 | `tnel17` | Inzar | 85 | `massotsoloda` | Demir |
| 17 | `fiinis` | Ebru | 51 | `tnel6` | Inzar | 86 | `massotsolodad1` | Demir |
| 18 | `start6` | Demir | 52 | `tnel11` | Inzar | 87 | `massotsolodad2` | Demir |
| 19 | `startwc` | Demir | 53 | `tnel18` | Inzar | 88 | `massotsolodad3` | Demir |
| 20 | `level7` | Demir | 54 | `tnel5` | Inzar | 89 | `massotmerdiven` | Demir |
| 21 | `start10` | Demir | 55 | `tnel22` | Inzar | 90 | `massotsagoda` | Demir |
| 22 | `level5` | Demir | 56 | `tnel20` | Inzar | 91 | `solustoda` | Demir |
| 23 | `kazankol` | Demir | 57 | `tnel23` | Inzar | 92 | `solustodad1` | Demir |
| 24 | `camtuvalet` | Demir | 58 | `tnel25` | Inzar | 93 | `hapisane` | Demir |
| 25 | `iceri2` | Demir | 59 | `tnel26` | Inzar | 94 | `hapisaned1` | Demir |
| 26 | `binaici` | Ebru | 60 | `tnel27` | Ebru | 95 | `hapisaned2` | Demir |
| 27 | `ev` | Ebru | 61 | `tnel28` | Inzar | 97 | `hapisaned4` | Demir |
| 28 | `ev2` | Ebru | 62 | `seytan_k` | Inzar | 98 | `kotukoru_d` | Demir |
| 29 | `arasokak` | Ebru | 64 | `seytan_e` | Demir | 99 | `ev3` | Demir |
| 30 | `aralevel` | Ebru | 65 | `ebrudus` | Ebru | 100 | `bosskoli` | Demir |
| 31 | `kriz` | Demir | 66 | `tnel8` | Ebru | 101 | `boss` | Demir |
| 32 | `kizicyolwmp` | Demir | 67 | `tnel9` | Ebru | 102 | `yereson` | Ebru |
| 33 | `kriz` | Demir | 68 | `tttg2` | Ebru | 103 | `hapisane_dark` | Ebru |
| 34 | `kriz` | Demir | 69 | `ttn3` | Ebru |  |  |  |

## Tests

- `tests/scenarios/quicksave1_roundtrip.k22s`: F5 in kizic2, walk away, change ammo, F9: position (±1), pan and ammo restored;
  F5 refused in the inventory menu.
- `tests/scenarios/quicksave2_restart.k22s`: after a restart the quicksave loads from the original load menu at its saved spot;
  `load 10` (a save-point slot) keeps the G00 spawn.
- `tests/scenarios/console_commands.k22s`: every command typed through the real toggle keys, SDL text input and Return, with
  their effects asserted (god undoes damage, ammo/weapons, get/set, timescale, noclip walks through the west wall that stops
  the same walk without it, tp, save, `level kizic`, F9 back across levels, kill, quit).
- `check_engine --input-saves` (CTest `input_saves`): the position line is written only for a quicksave, survives
  format/parse, arms the restore on read, is cleared by a save-point slot and is applied once the level reloads.
- `tests/scenarios/quicksave3_characters.k22s`: the same for Inzar (Tnel24, `koruyucu_pasif`, god keeps `koru_yaralanma` = ENERJI
  = 200) and Ebru (Ev, reached from Inzar by `load 6`).
