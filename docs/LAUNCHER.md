# Launcher (`kabus22-launcher`)

The launcher opens an **existing native installation**. It needs no ISO and extracts nothing. The installer's application-menu entry and desktop shortcut open it.

```sh
~/.local/share/kabus22/bin/kabus22-launcher --install-dir ~/.local/share/kabus22
# From a source build:
./build/installer/kabus22-launcher --install-dir ~/.local/share/kabus22
```

Shortcut actions for Vulkan RT and Vulkan raster only preselect the renderer; they do not start the game. Your desktop may ask you to mark a desktop shortcut as trusted before it opens.

## Interface language

The launcher interface is English or Turkish. **Interface language** (below the installation path) offers Automatic (system), English and Türkçe; changing it restarts the launcher. Automatic picks Turkish when the system locale is Turkish, or when the installer was run in Turkish. It is stored as `interface-language=auto|en|tr` in the `[Launcher]` section of `launcher.ini`. Command-line output and logs stay in English.

## Tabs

The tabs are Launch, Graphics, Rendering, Mods, Audio, Keyboard, Joystick, Controller, Bonus, Installation, Updates and Logs. **Apply** writes settings without starting the game; **Play** saves and starts the separate native engine. Campaign and installed offline Bonus data are selectable. OpenGL, Vulkan raster and Vulkan hardware RT remain explicit choices without fallback.

The **Mods** tab lists every folder in `${XDG_DATA_HOME:-$HOME/.local/share}/kabus22/mods/`. A mod is `<id>/mod.txt` (first line `kabus22-mod 1`, then `name`, optional `version`, `author`, `description` as `key<TAB>value` lines) plus `<id>/data/`, whose files replace or add game resources by their game-relative name (case-insensitive), e.g. `data/demir_2007.mdl`. Checked mods are saved as `[Launcher] mods=` in priority order and passed to the engine as one `--mod ID` each; the first listed wins when two mods provide the same file, and the details pane lists such conflicts. Invalid mods are shown with the reason and cannot be checked. **New mod…** creates the folder skeleton, **Install from .zip…** accepts `mod.txt` at the zip root or inside exactly one top folder (absolute, `..`, symlink and hard-link entries are refused; an existing id is never overwritten), **Remove** deletes a mod after confirmation and **Export game files…** runs `kabus22 --export-files DIR` to write every original resource, with the names a mod uses, into an empty folder for editing. Texture packs (Rendering tab) are a separate system. Format details: [MODDING.md](MODDING.md).

The **3-D viewer** page (beside the mod details) lists the selected mod's models and, with **Show originals**, every game model, marking the ones the mod replaces. A selected model is exported with `kabus22 --export-model` into `${XDG_CACHE_HOME:-~/.cache}/kabus22/viewer/` and drawn with its skins in an OpenGL view (drag to orbit, wheel to zoom, double-click to reset) with a slider over its animation frames. **Export for Blender…** copies that glTF export to a folder, **Replace model…** copies a `.gltf`/`.glb` (with the files its relative URIs name) or `.mdl` into the mod under the model's name after a confirmation, and **Remove replacement** deletes it again. See [MODDING.md](MODDING.md#model-viewer).

The **Updates** page checks GitHub Releases and applies or rolls back a signed update; after Apply or Roll back the launcher restarts into the newly active version. See [RELEASE_PROCESS.md](RELEASE_PROCESS.md). The **Logs** page shows the engine logs, which Play writes to `${XDG_STATE_HOME:-$HOME/.local/state}/kabus22/logs/`.

## Campaign and Bonus settings

Campaign controls retain original authored resolution and 16/32-bit color-depth compatibility records, lighting quality, completion buffering, mipmapping, anisotropy, reflections, blood color/amount and brightness/contrast/gamma. The color-depth record is a script/video compatibility choice, **not a promise of the old D3D framebuffer's physical format**. Blood amount and the three effects/music/dialogue volumes use 0–100; brightness/gamma use −100–100 and contrast −80–80. Original scripts may floor audio gains, so zero is not guaranteed exact mute. Ten keyboard actions and six joystick buttons are editable; joystick buttons use 1–12.

Bonus exposes its five source-consumed settings: authored resolution, shader level, anisotropy, music and stored brightness (50 is neutral; accepted native fixed-point range −2097102–2097151, not a recovered Windows UI range). Its separate AA choices request off/basic/4/9 samples, negotiated against native hardware. Campaign and Bonus text support `tr`/`en`/`de`; Campaign voice/movie audio supports `tr`/`en` (German text defaults to English voices). Saved patch locales are preserved unless intentionally changed; “Keep saved language / game default” also preserves patch indices not advertised as native locales. Patched/custom display remains game-owned. No speculative sensitivity, software-light, stencil-shadow or online-score controls are added.

## Where settings are stored

Launcher choices live in `${XDG_CONFIG_HOME:-$HOME/.config}/kabus22/launcher.ini`. Game settings use the engine's existing save root, `${XDG_DATA_HOME:-$HOME/.local/share}/kabus22`, including private Campaign/Bonus records; original installed media and source defaults remain unchanged. Use private XDG directories for experiments.

The in-game **GRAPHICS** menu (pause menu, [GRAPHICS_MENU.md](GRAPHICS_MENU.md)) writes its changes into the same `launcher.ini`: `[Launcher] renderer`, `preset`, `window` and `resolution`, and the `[Graphics]` keys (`fps_limit` and `seamless_doors` under every preset, the other keys under Custom). Every other line, section and comment is kept as it was, and it takes the launcher's settings lock. While the game runs the launcher's settings are locked; when the game exits the launcher reloads the file, so the Rendering tab shows what was chosen in game and the next Apply or Play does not undo it.
