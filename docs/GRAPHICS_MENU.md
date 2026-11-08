# In-game graphics menu

Not in the original game. Press Escape during play to open the pause menu, then choose **GRAPHICS** (Down from MAIN MENU; [CONSOLE.md](CONSOLE.md)) to change the graphics settings without restarting. The menu covers the paused game; every key, mouse and pad press goes to the menu while it is open. **Escape**, pad **B**, a right click or **BACK** returns to the pause menu.

| Input | Action |
|---|---|
| Up / Down, d-pad, mouse wheel | move between rows (greyed rows are skipped; the list wraps) |
| Left / Right, d-pad | change the value (stops at the first and last value) |
| Enter, pad A, click on the label | step to the next value (wraps around) |
| click on the left or right half of the value | previous / next value |

## Rows

The rows are the launcher's Rendering settings ([LAUNCHER.md](LAUNCHER.md)). A row is greyed when it cannot apply: the setting does not exist on the active renderer (the same backend tables the engine uses to drop features at start-up, `render_policy::graphicsOptions` and `admit_features`), or a setting it depends on is off.

| Row | Values | Applies |
|---|---|---|
| Renderer | OpenGL, Vulkan, Vulkan RT (only the ones this computer runs, probed in the background when the menu first opens) | renderer restart |
| Preset | Original, Enhanced, Ultra, Custom | as its settings |
| Window mode | Windowed, Fullscreen, Borderless (original campaign with the native display) | at once |
| Resolution | the desktop size, the display's modes and the current size, from 1280x720 (not in Borderless, which uses the desktop size) | at once |
| VSync | on/off (off while Frame limit is Unlimited) | live |
| Frame limit | Original (the game's own cap: campaign 72, Bonus 60), 30–240, Unlimited | at once |
| AMD FSR, Frame generation | Vulkan; frame generation needs FSR | renderer restart when FSR or frame generation turns on or off, live between FSR modes |
| MSAA, Anisotropic filtering, Bloom, Ambient occlusion, FXAA, Volumetric fog, PBR materials | | live |
| Shadows | Original, Shadow maps, Ray traced (Vulkan RT) | live on Vulkan; OpenGL restarts when shadow maps turn on or off |
| Global illumination, Reflections, Denoise | Vulkan RT | live |
| TAA, Sharpen, Smooth normals, Tessellation, Texture upscale | Vulkan; TAA is greyed while FSR replaces it | live (tessellation restarts the renderer the first time it is turned on) |
| Texture packs | the packs checked in the launcher, on/off (greyed when the game started without packs) | live |
| Seamless doors | on/off, campaign only, not while a door is opening ([SEAMLESS_DOORS.md](SEAMLESS_DOORS.md)) | at once |

Choosing a preset applies its whole set of settings, as `--preset` does; Frame limit and Seamless doors stay as they are under every preset. Changing any of the individual settings switches the preset to Custom. The renderer is part of neither: like the launcher, a preset keeps the chosen renderer, and settings the renderer cannot run stay off.

Renderer settings apply 0.4 seconds after the last change (or when the menu closes), so stepping through several values does not rebuild the renderer each time. "Live" changes keep the window and the game exactly where they are; textures and models re-upload where the change needs it, and temporal effects (TAA, FSR, the ray-tracing denoiser) start over. A renderer restart (another renderer, or a change the running one cannot make) closes and reopens the game window at the same size and mode between two frames, then the game continues. If the new renderer cannot start, the previous settings come back with a message, and the menu shows them again. A message at the top of the screen confirms each applied change.

## Saved to the launcher

Each applied change is written to the launcher's settings file, `${XDG_CONFIG_HOME:-~/.config}/kabus22/launcher.ini`, so the next start and the launcher's Rendering tab use it:

| Setting | `launcher.ini` |
|---|---|
| Renderer | `[Launcher] renderer=opengl\|vulkan-raster\|vulkan-rt` |
| Preset | `[Launcher] preset=original\|enhanced\|ultra\|custom` |
| Window mode, Resolution | `[Launcher] window=windowed\|fullscreen\|borderless`, `resolution=WIDTHxHEIGHT` (resolution only outside Borderless; both only with the native display) |
| Frame limit, Seamless doors | `[Graphics] fps_limit=…`, `seamless_doors=off` under every preset |
| the other rows | `[Graphics] KEY=VALUE` with the `--graphics` key and value, under the Custom preset only |

A `[Graphics]` key whose value is the engine default is removed rather than written, as the launcher does. Under Enhanced, Ultra and Original the custom set in `[Graphics]` is left as it was, so switching back to Custom in the launcher finds it. Every other line, section and comment of the file stays byte for byte; a missing file is created with just these keys. The file is replaced atomically (written to a private temporary file, synchronized, renamed) under the launcher's own settings lock (`${XDG_DATA_HOME:-~/.local/share}/kabus22/.launcher.lock`), so it never interleaves with a launcher Apply. The launcher reloads the file when the game exits.

VSync and Texture packs have no launcher key: they last until the game exits. The launcher's VSync behavior is the engine default (on, off under an Unlimited frame limit), and its texture packs are the checked list on the Rendering tab.

## Implementation

`native/graphics_menu.cpp` (rows, input, overlay, apply), `native/launcher_config.cpp` (the GLib-free `launcher.ini` merge and locked atomic write; `kabus22-launcher --self-check` round-trips it through the launcher's own parser and command line), `native/main.cpp` (pacing reads `Engine::fpsLimit` every frame; renderer recreation at the top of a frame, before input polling). Live changes go through `Renderer::reconfigure` ([GRAPHICS_ROADMAP.md](GRAPHICS_ROADMAP.md)); when it refuses, or the renderer changes, `Engine::take_renderer_request` hands main.cpp the new options, main.cpp destroys the old renderer, creates the new one with the current window size and mode, and calls `Engine::set_renderer` (drops the engine's cached TTF atlases and this frame's UI, re-applies the script's window title and mouse mode) and `Engine::renderer_recreated`. Scenario probes `$gfxrow`, `$fpslimit` and `$doors` ([GAMEPLAY_TESTING.md](GAMEPLAY_TESTING.md)); regression `tests/scenarios/graphics_menu.k22s`.
