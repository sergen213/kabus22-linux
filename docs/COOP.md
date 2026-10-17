# Local two-player coop

Purpose: two players on one screen, each playing Demir, Ebru or Inzar (any combination, duplicates too). This is a native addition; the original scripts are not modified.

Related: [Character lock](CHARACTER_LOCK.md) · [Gameplay testing](GAMEPLAY_TESTING.md) · [Gameplay systems](GAMEPLAY_SYSTEMS.md)

Status: phase 1, the menu and the character selection (`native/coop_menu.cpp`). The selection is stored in `Engine::coop` when the game starts; coop gameplay (P2's body, shared camera, respawn at the partner, saves) is phase 2.

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

Only pad 0 exists in `Input` today; the select screen reads pads through one helper (`pad_pressed` in `native/coop_menu.cpp`) that returns false for pad 1 until the two-pad input lands. The original menus take the d-pad as the arrow keys in both controller modes and A/B as Enter/Escape in modern mode (`Engine::key_state`).

### Limitations

- The original main menu shows no mouse pointer (`mouse_mode` 0); hover and click work on the native item and the select screen without a visible pointer, as on the character lock's selector.

## Testing

`tests/scenarios/coop_menu_select.k22s` (headless, part of `gameplay_scenarios`): main menu navigation to and from 2 PLAYER, the original Down sequence, select screen open/Escape back, P1 and P2 (keyboard fallback) choices, READY/unready, the pending New Game menu and its Escape, then NORMAL starts Demir's opening as P1 Ebru (`Oyuncu_Demir` runs `Oyuncu_Ebru`) with `$coop` 1, `$coop1` 1, `$coop2` 2. Scenario terms: `$coop`, `$coop1`, `$coop2`, `$coopmenu` (0, 1 the item highlighted, 2 select screen), `$pick1`, `$pick2` (the select highlight, −1 not joined).
