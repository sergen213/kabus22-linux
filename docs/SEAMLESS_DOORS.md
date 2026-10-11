# Seamless doors

Purpose: describe the native `seamless_doors` option (default `on`): an ordinary LY_Kapi door shows the next zone through the open doorway and the zones swap without the LYP_Fade black fade. `--graphics seamless_doors=off` (launcher Rendering page, applies under every preset) is the original path exactly.

Code: `native/seamless_doors.cpp` (engine), `RenderFrame::door` / `Camera::doorPortal` / `Renderer::release_scene(keep)` (`native/render.hpp`; GL `render_gl.cpp`, Vulkan `scene.frag` + `render_vulkan_raster.cpp`/`render_vulkan_rt.cpp`), `Assets::prefetch_world` (`native/assets.cpp`).

## Flow

1. **Prefetch.** Every 15 ticks the nearest LY_Kapi door group within 320 quants (not a scripted transition, below) gets its destination WMB decoded on a worker thread (`Assets::prefetch_world`: a snapshot of the archives, decode settings and decoded images; the World is adopted on the main thread). Once decoded, the frame carries the door with a closed rect so the renderer uploads the destination mesh, textures and lightmaps. One prefetched destination at a time. Measured cost of an ordinary load (kizic2 -> kizic): WMB decode 128 ms, entities 13 ms, physics 2 ms, then 30-90 ms of first-draw uploads.
2. **Use.** The original `LY_Tarama` runs unchanged (FLAG1 enemy check, character refusals, the 62/64 mapping). When it sets `LY_HedefLevel=N` from an LY_Kapi scan and the transition is eligible, the engine puts `LY_HedefLevel` back to 0 and opens the doorway: it widens from one jamb over 0.4 s (smoothstep) and the destination is drawn through it.
3. **Cross.** Walking into the doorway (within 28 quants of the door plane, inside its width) or standing in front of the fully open door for 0.6 s (the original "use = go") writes `LY_HedefLevel=N`. Walking more than 140 quants away closes the door with no other effect.
4. **Swap.** The original `LY_YuklemeTakip -> LevelYukle(N)` runs completely: Acikmi, Cikis, the resets, `Level_Load`, Giris, `LY_AktifLevelID`, map and save bookkeeping. The engine only hides the LYP_Fade panel, sets its alpha to 100 so the fade-out loop ends on its next test, keeps the destination's decoded World and GPU mesh across `load_level` (`Assets::release_level(keep)`, `release_scene(keep)`), and presents no frames from `Level_Load` until one tick after Giris placed the player (the last frame, with the open doorway, stays up; about 5-8 frames). Then it sets the alpha to 0 so the fade-in loop ends at once. Use to control: about 33 frames instead of 73, no black frame.

The camera is the destination's own authored camera action (Kabus 22 has fixed per-area cameras), so the view cuts to it; the Giris placement is authoritative, and the held frames hide the WMB spawn before it.

## Doorway geometry

Both sides come from data. A side: the scanned trigger cluster (LY_Kapi triplets within 48 quants), the nearest near-vertical collision face (40 quants), the room side (the side with a floor 24 quants away; Ev3's trigger stands behind its wall), the room-facing surface (ray back from the room) and the narrowest door-sized material patch on it (24-200 x 48-240, bottom at the floor; e.g. `dormet4`, `doryes`, `odadoor`), else a 44 x 88 opening at the triggers. B side: the destination's LY_Kapi back to A (nearest the Giris arrival), else the wall behind the original Giris arrival (`LV_*_Giris` `if(_EskiLevel==A){vec_set(player.x,VAR);…}` parsed from the scripts, VAR read live; this covers Ev3 -> Ev2, whose ev2.wmb has no door back). Transform: pan so B's room normal continues A's walk-through direction, door floor points coincide (a zero-thickness wall). Renderers cut current-level fragments behind the doorway plane inside the rect and keep destination fragments beyond it (`render.hpp` DoorPortal rule); the destination is drawn lightmapped only (no dynamic lights, shadows, ray queries, not in the TLAS).

## Eligibility

Seamless only for an LY_Kapi scan transition that is not one of LevelYukle's special cases (evaluated with the live flags the script tests): stair pairs 15/10, 7/10, 38/39, 65/60, 67/68, 83/84 (both directions), kararma 8->55, 64->39, 42->84, movies (New Game -> 1, 30->17 for Demir before `v_korufinal_ok`, 2->25 before `v_doktor_ok`, 62->65, 93->70), dark entries (27 before `v_ebru_ev_ok`, 31 before `v_kizgiris_ok`, 38 before `v_kuyugir_ok`, 98 as Inzar, 62, 64, 101, 103), and only when both doorway sides are found. Scripted transitions (cinematics, `hapisane_load`, elevators, menus, the level sweep's direct `LY_HedefLevel` writes) never pass through the hook and keep the fade. Acikmi refusals: no original LV_*_Acikmi refuses (all return 0); a refusal would still show its error in LevelYukle.

`kabus22 --door-table tests/levels/levels.tsv --data GAME_ROOT` lists every LY_Kapi doorway (static view: flag-dependent cases marked "conditional"; doors without a return door are reported as fade there but become seamless at runtime when the destination's Giris names an arrival, as Ev3 -> Ev2 does). Current table (pan: destination rotation; door: detected opening):

| from | to | result | pan | door |
|---|---|---|---|---|
| 1 start4.wmb | 3 start5.wmb | seamless | 180 | 39x80 |
| 2 sd.wmb | 1 start4.wmb | fade: no return door | 0 | 45x89 |
| 3 start5.wmb | 1 start4.wmb | seamless; conditional: movie after New Game | 180 | 37x87 |
| 3 start5.wmb | 5 start3.wmb | seamless | 0 | 45x90 |
| 4 level4.wmb | 2 sd.wmb | fade: no return door | 0 | 44x88 |
| 4 level4.wmb | 6 nstart7.wmb | seamless | 0 | 38x85 |
| 5 start3.wmb | 3 start5.wmb | seamless | 0 | 45x90 |
| 6 nstart7.wmb | 4 level4.wmb | seamless | 0 | 44x88 |
| 6 nstart7.wmb | 9 mekke.wmb | seamless | 0 | 39x76 |
| 7 cam1.wmb | 10 start7.wmb | fade: stairs | 0 | 46x88 |
| 7 cam1.wmb | 11 mezarlik.wmb | seamless | 0 | 47x95 |
| 8 yuy.wmb | 5 start3.wmb | fade: no return door | 0 | 47x86 |
| 9 mekke.wmb | 6 nstart7.wmb | seamless | 0 | 39x79 |
| 9 mekke.wmb | 14 start2.wmb | seamless | 0 | 38x79 |
| 10 start7.wmb | 7 cam1.wmb | fade: stairs | 0 | 44x88 |
| 10 start7.wmb | 15 untitled44.wmb | fade: stairs | 0 | 44x88 |
| 11 mezarlik.wmb | 7 cam1.wmb | seamless | 0 | 47x94 |
| 12 start1.wmb | 16 iceri1.wmb | seamless | 0 | 47x96 |
| 13 kk.wmb | 26 binaici.wmb | seamless | -180 | 44x88 |
| 14 start2.wmb | 9 mekke.wmb | seamless | 0 | 39x83 |
| 14 start2.wmb | 18 start6.wmb | seamless | 0 | 44x88 |
| 15 untitled44.wmb | 14 start2.wmb | fade: no return door | 0 | 39x87 |
| 16 iceri1.wmb | 12 start1.wmb | seamless | 0 | 43x87 |
| 16 iceri1.wmb | 25 iceri2.wmb | seamless | 0 | 43x85 |
| 17 fiinis.wmb | 30 aralevel.wmb | seamless | 0 | 40x131 |
| 18 start6.wmb | 14 start2.wmb | seamless | 0 | 44x88 |
| 18 start6.wmb | 19 startwc.wmb | seamless | 0 | 44x88 |
| 18 start6.wmb | 20 level7.wmb | seamless | 0 | 44x89 |
| 18 start6.wmb | 21 start10.wmb | seamless | 0 | 44x88 |
| 19 startwc.wmb | 18 start6.wmb | seamless | 0 | 44x88 |
| 20 level7.wmb | 18 start6.wmb | seamless | 0 | 42x79 |
| 21 start10.wmb | 18 start6.wmb | seamless | 0 | 46x90 |
| 21 start10.wmb | 23 kazankol.wmb | seamless | 0 | 46x91 |
| 22 level5.wmb | 20 level7.wmb | fade: no return door | 0 | 44x88 |
| 23 kazankol.wmb | 21 start10.wmb | seamless | 0 | 45x86 |
| 24 camtuvalet.wmb | 7 cam1.wmb | fade: no return door | 0 | 41x91 |
| 25 iceri2.wmb | 16 iceri1.wmb | seamless | 0 | 43x85 |
| 26 binaici.wmb | 13 kk.wmb | seamless | -180 | 155x76 |
| 26 binaici.wmb | 28 ev2.wmb | seamless | 0 | 44x88 |
| 27 ev.wmb | 28 ev2.wmb | seamless | 0 | 44x88 |
| 28 ev2.wmb | 26 binaici.wmb | seamless | 0 | 44x88 |
| 28 ev2.wmb | 27 ev.wmb | seamless; conditional: dark entry (v_ebru_ev_ok) | 0 | 44x88 |
| 29 arasokak.wmb | 13 kk.wmb | fade: no return door | 0 | 44x88 |
| 29 arasokak.wmb | 30 aralevel.wmb | seamless | 0 | 44x88 |
| 30 aralevel.wmb | 17 fiinis.wmb | seamless; conditional: movie (Demir, v_korufinal_ok) | 0 | 44x88 |
| 30 aralevel.wmb | 29 arasokak.wmb | seamless | 0 | 44x88 |
| 31 kriz.wmb | 32 kizicyolwmp.wmb | seamless | 180 | 81x120 |
| 32 kizicyolwmp.wmb | 31 kriz.wmb | seamless; conditional: dark entry (v_kizgiris_ok) | 180 | 75x90 |
| 33 kriz.wmb | 32 kizicyolwmp.wmb | fade: no return door | 0 | 81x120 |
| 34 kriz.wmb | 32 kizicyolwmp.wmb | fade: no return door | 0 | 81x120 |
| 35 kizic3.wmb | 32 kizicyolwmp.wmb | fade: no return door | 0 | 75x88 |
| 35 kizic3.wmb | 36 kizic.wmb | seamless | 180 | 52x97 |
| 36 kizic.wmb | 35 kizic3.wmb | seamless | -180 | 46x78 |
| 36 kizic.wmb | 37 kizic2.wmb | seamless | 0 | 54x104 |
| 37 kizic2.wmb | 36 kizic.wmb | seamless | 0 | 52x94 |
| 38 krizkuyu.wmb | 39 tnel24.wmb | fade: stairs | 0 | 44x88 |
| 39 tnel24.wmb | 38 krizkuyu.wmb | fade: stairs | 0 | 47x88 |
| 39 tnel24.wmb | 40 tnel12.wmb | seamless | 0 | 52x88 |
| 40 tnel12.wmb | 39 tnel24.wmb | seamless | 0 | 49x88 |
| 40 tnel12.wmb | 41 tnel66.wmb | seamless | 0 | 64x193 |
| 41 tnel66.wmb | 40 tnel12.wmb | seamless | 0 | 48x88 |
| 41 tnel66.wmb | 42 tnel13.wmb | seamless | 0 | 77x121 |
| 41 tnel66.wmb | 43 tnel10.wmb | seamless | 0 | 48x88 |
| 42 tnel13.wmb | 41 tnel66.wmb | seamless | 0 | 51x88 |
| 42 tnel13.wmb | 54 tnel5.wmb | seamless | 0 | 51x88 |
| 42 tnel13.wmb | 62 seytan_k.wmb | fade: movie/cutscene | 0 | 179x180 |
| 43 tnel10.wmb | 41 tnel66.wmb | seamless | 0 | 50x88 |
| 43 tnel10.wmb | 45 tnel19.wmb | seamless | 0 | 54x88 |
| 43 tnel10.wmb | 46 tnel16.wmb | seamless | 0 | 53x88 |
| 44 tnel15.wmb | 41 tnel66.wmb | fade: no return door | 0 | 63x94 |
| 44 tnel15.wmb | 49 tnel21.wmb | seamless | 0 | 52x120 |
| 44 tnel15.wmb | 50 tnel17.wmb | seamless | 0 | 50x88 |
| 45 tnel19.wmb | 43 tnel10.wmb | seamless | 0 | 64x120 |
| 46 tnel16.wmb | 43 tnel10.wmb | seamless | 0 | 51x88 |
| 46 tnel16.wmb | 47 tnel14.wmb | seamless | 0 | 51x88 |
| 46 tnel16.wmb | 48 tnel7.wmb | seamless | 0 | 64x121 |
| 47 tnel14.wmb | 46 tnel16.wmb | seamless | 0 | 78x120 |
| 48 tnel7.wmb | 46 tnel16.wmb | seamless | 0 | 48x88 |
| 48 tnel7.wmb | 57 tnel23.wmb | seamless | 0 | 49x88 |
| 49 tnel21.wmb | 44 tnel15.wmb | seamless | 0 | 50x88 |
| 50 tnel17.wmb | 44 tnel15.wmb | seamless | 0 | 55x121 |
| 50 tnel17.wmb | 51 tnel6.wmb | seamless | 0 | 78x121 |
| 51 tnel6.wmb | 50 tnel17.wmb | seamless | 0 | 78x121 |
| 51 tnel6.wmb | 52 tnel11.wmb | seamless | 0 | 51x88 |
| 52 tnel11.wmb | 51 tnel6.wmb | seamless | 0 | 49x88 |
| 52 tnel11.wmb | 53 tnel18.wmb | seamless | 0 | 77x121 |
| 53 tnel18.wmb | 52 tnel11.wmb | seamless | 0 | 52x115 |
| 54 tnel5.wmb | 42 tnel13.wmb | seamless | 0 | 51x88 |
| 54 tnel5.wmb | 55 tnel22.wmb | seamless | 0 | 51x88 |
| 54 tnel5.wmb | 56 tnel20.wmb | seamless | 0 | 65x121 |
| 55 tnel22.wmb | 54 tnel5.wmb | seamless | 0 | 48x88 |
| 56 tnel20.wmb | 54 tnel5.wmb | seamless | 0 | 52x115 |
| 57 tnel23.wmb | 48 tnel7.wmb | seamless | 0 | 48x88 |
| 58 tnel25.wmb | 61 tnel28.wmb | seamless | 0 | 51x88 |
| 59 tnel26.wmb | 60 tnel27.wmb | seamless | -1 | 65x120 |
| 59 tnel26.wmb | 61 tnel28.wmb | seamless | 0 | 64x121 |
| 60 tnel27.wmb | 59 tnel26.wmb | seamless | 1 | 51x88 |
| 61 tnel28.wmb | 58 tnel25.wmb | seamless | 0 | 51x88 |
| 61 tnel28.wmb | 59 tnel26.wmb | seamless | 0 | 49x88 |
| 64 seytan_e.wmb | 42 tnel13.wmb | fade: movie/cutscene | 0 | 96x88 |
| 65 ebrudus.wmb | 60 tnel27.wmb | fade: stairs | 0 | 44x88 |
| 66 tnel8.wmb | 64 seytan_e.wmb | fade: movie/cutscene | 0 | 96x88 |
| 66 tnel8.wmb | 67 tnel9.wmb | seamless | 0 | 64x121 |
| 67 tnel9.wmb | 66 tnel8.wmb | seamless | 0 | 78x120 |
| 67 tnel9.wmb | 68 tttg2.wmb | fade: stairs | 0 | 67x214 |
| 68 tttg2.wmb | 67 tnel9.wmb | fade: stairs | 0 | 30x88 |
| 69 ttn3.wmb | 102 yereson.wmb | seamless | 0 | 40x85 |
| 70 saraygir.wmb | 102 yereson.wmb | seamless | 0 | 40x89 |
| 71 saraygiralt.wmb | 72 masso_lg.wmb | seamless | 0 | 91x123 |
| 72 masso_lg.wmb | 71 saraygiralt.wmb | seamless | 0 | 91x135 |
| 72 masso_lg.wmb | 73 massotaltkat.wmb | seamless | 180 | 66x88 |
| 72 masso_lg.wmb | 74 kolidorsag.wmb | seamless | -180 | 44x88 |
| 72 masso_lg.wmb | 81 kolidorsol.wmb | seamless | 0 | 44x88 |
| 72 masso_lg.wmb | 85 massotsoloda.wmb | seamless | 180 | 38x88 |
| 72 masso_lg.wmb | 90 massotsagoda.wmb | seamless | -180 | 38x88 |
| 73 massotaltkat.wmb | 72 masso_lg.wmb | seamless | 180 | 72x111 |
| 73 massotaltkat.wmb | 78 kanodasi.wmb | seamless | 0 | 48x88 |
| 74 kolidorsag.wmb | 72 masso_lg.wmb | seamless | -180 | 38x87 |
| 74 kolidorsag.wmb | 75 ayinyeri.wmb | seamless | 180 | 38x87 |
| 74 kolidorsag.wmb | 76 kutuphane.wmb | seamless | 0 | 38x81 |
| 74 kolidorsag.wmb | 77 yemekodasi.wmb | seamless | 180 | 38x87 |
| 75 ayinyeri.wmb | 74 kolidorsag.wmb | seamless | -180 | 38x85 |
| 76 kutuphane.wmb | 74 kolidorsag.wmb | seamless | 0 | 122x105 |
| 77 yemekodasi.wmb | 74 kolidorsag.wmb | seamless | 180 | 59x88 |
| 77 yemekodasi.wmb | 79 saatodasi.wmb | seamless | 0 | 44x88 |
| 78 kanodasi.wmb | 73 massotaltkat.wmb | seamless | 0 | 52x105 |
| 79 saatodasi.wmb | 77 yemekodasi.wmb | seamless | 0 | 44x88 |
| 79 saatodasi.wmb | 80 dumur.wmb | seamless | 180 | 38x88 |
| 80 dumur.wmb | 79 saatodasi.wmb | seamless | 180 | 77x120 |
| 80 dumur.wmb | 82 dumurek1.wmb | seamless | 0 | 44x88 |
| 81 kolidorsol.wmb | 72 masso_lg.wmb | seamless | 0 | 38x80 |
| 81 kolidorsol.wmb | 89 massotmerdiven.wmb | seamless | 180 | 38x79 |
| 81 kolidorsol.wmb | 91 solustoda.wmb | seamless | 180 | 38x80 |
| 82 dumurek1.wmb | 80 dumur.wmb | seamless | 0 | 44x88 |
| 83 bedkorgel.wmb | 80 dumur.wmb | fade: no return door | 0 | 48x92 |
| 83 bedkorgel.wmb | 84 buyukes.wmb | fade: stairs | 0 | 43x109 |
| 84 buyukes.wmb | 83 bedkorgel.wmb | fade: stairs | 0 | 0x0 |
| 85 massotsoloda.wmb | 72 masso_lg.wmb | seamless | 180 | 44x88 |
| 85 massotsoloda.wmb | 86 massotsolodad1.wmb | seamless | 0 | 44x88 |
| 85 massotsoloda.wmb | 88 massotsolodad3.wmb | seamless | 0 | 44x88 |
| 86 massotsolodad1.wmb | 85 massotsoloda.wmb | seamless | 0 | 44x88 |
| 86 massotsolodad1.wmb | 87 massotsolodad2.wmb | seamless | 0 | 126x219 |
| 87 massotsolodad2.wmb | 86 massotsolodad1.wmb | seamless | 0 | 44x88 |
| 88 massotsolodad3.wmb | 85 massotsoloda.wmb | seamless | 0 | 119x151 |
| 88 massotsolodad3.wmb | 89 massotmerdiven.wmb | seamless | 0 | 44x88 |
| 89 massotmerdiven.wmb | 81 kolidorsol.wmb | seamless | 180 | 44x88 |
| 89 massotmerdiven.wmb | 88 massotsolodad3.wmb | seamless | 0 | 44x88 |
| 90 massotsagoda.wmb | 72 masso_lg.wmb | seamless | -180 | 38x66 |
| 91 solustoda.wmb | 81 kolidorsol.wmb | seamless | 180 | 46x88 |
| 91 solustoda.wmb | 92 solustodad1.wmb | seamless | 0 | 46x88 |
| 92 solustodad1.wmb | 91 solustoda.wmb | seamless | 0 | 45x88 |
| 93 hapisane.wmb | 70 saraygir.wmb | fade: no return door | 0 | 44x88 |
| 93 hapisane.wmb | 94 hapisaned1.wmb | seamless | 0 | 44x88 |
| 93 hapisane.wmb | 95 hapisaned2.wmb | seamless | 180 | 153x204 |
| 93 hapisane.wmb | 97 hapisaned4.wmb | seamless | 0 | 122x92 |
| 94 hapisaned1.wmb | 93 hapisane.wmb | seamless | 0 | 40x91 |
| 95 hapisaned2.wmb | 93 hapisane.wmb | seamless | 180 | 40x91 |
| 97 hapisaned4.wmb | 93 hapisane.wmb | seamless | 0 | 40x92 |
| 99 ev3.wmb | 28 ev2.wmb | fade: no return door | 0 | 36x87 |
| 100 bosskoli.wmb | 98 kotukoru_d.wmb | fade: no return door | 0 | 161x174 |
| 100 bosskoli.wmb | 101 boss.wmb | fade: movie/cutscene | 0 | 96x88 |
| 102 yereson.wmb | 69 ttn3.wmb | seamless | 0 | 40x87 |
| 102 yereson.wmb | 70 saraygir.wmb | seamless | 0 | 40x90 |

## Verification

- Headless scenarios (`gameplay_scenarios`): `world_door_kizic` (kizic2 <-> kizic, both directions), `world_door_ev3` (Ev3 -> Ev2 via the Giris arrival), `world_door_tunnel` (Tnel24 -> Tnel12; the FLAG1 return door is refused with LV_HataGoster and opens nothing), `world_door_fade_off` (`# graphics: seamless_doors=off`: the original fade, no preview). They assert `LY_AktifLevelID`, `level_yuklendi_ok`, `harita_anagrup`/`harita_no` from Giris, control (`oyuncu_pasif`/`koruyucu_pasif`), `$doorswaps` and `$fadeframes` (presented frames with a visible LYP_Fade: unchanged). Scenario terms: `$door` (0 idle, 1 opening, 2 crossing, 3 holding, 4 revealing), `$doorswaps`, `$fadeframes`.
- `k22-render-check --gl|--vulkan|--vulkan-rt`: synthetic door portal checks (doorway shows the door world, outside unchanged, closed/behind/no-door frames identical to the original, `release_scene(keep)`).

## Limitations

- The camera cuts to the destination's authored camera; frames from Level_Load to the Giris placement are held, not interpolated.
- Cikis plays its door-open sound at the crossing, not when the doorway opens.
- The preview shows destination geometry only (no entities, dynamic lights or ray-traced effects) and uses the current level's fog.
- The detected opening is the authored door face, which can include its frame (kizic2's door face is a little wider than the painted leaf).
