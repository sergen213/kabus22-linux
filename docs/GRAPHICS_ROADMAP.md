# Graphics roadmap

Status of the native renderer's optional graphics features (engine 0.2.0). With no `--preset`,
`--graphics` or `--texture-pack` option, OpenGL and Vulkan raster take exactly the original code path:
their captures of the original Start4 level are byte-identical to the pre-0.2.0 renderer. Vulkan ray
tracing by default no longer occludes the original lights (`docs/RAYTRACING.md`). It now lights exactly
like Vulkan raster and differs only on reflective surfaces.

## Command line (Contract D)

| Option | Meaning |
| --- | --- |
| `kabus22 --list-capabilities` | One JSON object: version, probed backends (`opengl`, `vulkan-raster`, `vulkan-rt` with `available`/`detail` from a real hidden-window initialization), presets, implemented `options`, installed `texture_packs`. Needs no game data; without a display every backend reports `available:false` with the SDL reason. |
| `kabus22 --dump-textures DIR --data GAME [--mode bonus]` | Writes every decodable original texture as `DIR/<key>.png` (lossless RGBA as decoded by the engine) and `DIR/index.tsv` (`key width height category source`, UTF-8). Refuses to write inside `GAME`. Lightmaps are not dumped. |
| `--preset original\|enhanced\|ultra\|custom` | See below. Absent = `original`. |
| `--graphics KEY=VALUE` | Repeatable, overrides the preset. Unknown keys/values are errors. |
| `--texture-pack NAME` | Repeatable; earlier packs win. Implies `texture_packs=1`. |

Implemented `--graphics` keys:

| Key | Values | Default | Notes |
| --- | --- | --- | --- |
| `backend` | `opengl`, `vulkan-raster`, `vulkan-rt` | `opengl` | Explicit; conflicts with `--renderer/--ray-tracing`. |
| `pbr` | `0`, `1` | `0` | Metallic-roughness shading, HDR target, tone mapping. |
| `anisotropy` | `1 2 4 8 16` | `8` | Explicit values apply even when the script leaves D3D anisotropy off. |
| `msaa` | `1 2 4 8` | `4` | Clamped to the device maximum with a notice. |
| `texture_packs` | `0`, `1` | `0` | `1` without `--texture-pack` loads every valid installed pack. |
| `shadows` | `original`, `shadowmap`, `rt` | `original` | `rt` only on `vulkan-rt` (capabilities `value_backends`); elsewhere it degrades to `shadowmap` with a notice. |
| `gi`, `denoise` | `0`, `1` | `0` | `vulkan-rt` only. |
| `reflections` | `original`, `rough` | `original` | `vulkan-rt` only. |
| `bloom`, `ssao`, `fxaa`, `volumetric_fog` | `0`, `1` | `0` | All backends. |
| `taa` | `0`, `1` | `0` | Vulkan raster and RT only. |
| `texture_upscale`, `sharpen` | `0`, `1` | `0` | Vulkan raster and RT only; on in `enhanced`/`ultra`. |
| `fsr` | `off`, `native`, `quality`, `balanced`, `performance`, `ultra_performance` | `off` | AMD FSR 3.1.4 upscaling, Vulkan raster and RT only; `quality` in `ultra`. Replaces TAA. |
| `frame_generation` | `off`, `on` | `off` | AMD FSR 3.1.4 frame generation: one interpolated frame presented between every two rendered frames. Vulkan raster and RT with `fsr` on (any mode); without `fsr` (or on OpenGL) it is turned off with a notice. No preset enables it. See [FSR 3.1.4 frame generation](#fsr-314-frame-generation-frame_generationon-vulkan). |
| `fps_limit` | `original`, `30 60 72 90 120 144 165 180 240`, `unlimited` | `original` | Frame pacing only, all backends, every preset leaves it `original`. `original` is the A6 `00414650` sleep on the script's `fps_max` (campaign 72, Bonus 60). A number replaces that period; the WDL `fps_max` global is not changed (no script reads it back). Vsync stays on, so the display refresh rate still caps values above it. `unlimited` also turns vsync off and paces at the A6 TIME floor (raw 51 = 3.11 ms, ~321 FPS): faster frames would be clamped to that floor and run the game faster than real time, so no value above it is offered. Scenarios use the period as their fixed step (`1/60` without the key). |
| `seamless_doors` | `on`, `off` | `on` | Engine option, all backends, every preset leaves it `on`: LY_Kapi doors open as the player walks up, show the next zone through the open doorway and swap without the LYP_Fade fade when walked into; `off` is the original path. [SEAMLESS_DOORS.md](SEAMLESS_DOORS.md). |

Features a backend cannot run are dropped by `render_policy::admit_features` with one
`kabus22: graphics: ...` notice each (also after a preset backend degrades). Capabilities list each
option only for the backends that implement it.

## Presets

| Preset | Backend | Textures | Lighting | Filtering |
| --- | --- | --- | --- | --- |
| `original` | OpenGL | original | original | script-controlled anisotropy (8x when enabled), MSAA 4 |
| `enhanced` | Vulkan raster | installed packs | original + shadow maps + SSAO | 16x anisotropy forced, MSAA 4, FXAA |
| `ultra` | Vulkan ray tracing | installed packs | PBR + HDR, RT shadows/GI/rough reflections, denoise, bloom, volumetric fog | 16x anisotropy forced, MSAA 4, TAA |
| `custom` | `original` plus explicit `--graphics` only | | | |

A backend that a preset picked degrades `vulkan-rt -> vulkan-raster -> opengl`, and each step prints
`kabus22: graphics: <backend> unavailable (<reason>); disabling it, using <next>`. Explicit
`--renderer`, `--ray-tracing` or `--graphics backend=` never fall back. Missing packs, MSAA and
anisotropy limits also print a notice.

## Texture packs (Contract E)

`${XDG_DATA_HOME:-~/.local/share}/kabus22/packs/<NAME>/pack.txt` contains a header, `name`, `license`
and `key kind path.png` lines. The key is FNV-1a-64 over the LE width, LE height and engine-decoded
RGBA8. Manifests are checked strictly. An entry is dropped, with a logged reason, if it is not a PNG,
escapes the pack directory (`..`, absolute path or symlink), is larger than 8192 px or 64 MiB, or its
kinds have different sizes. A pack scale other than 1/2/4/8 times the original is rejected when the
key is first looked up. PNGs are decoded through the existing FFmpeg dependency, and gray, palette and
16-bit PNGs are accepted.

- Albedo replaces the upload in all three backends (world, model, sprite, sky and UI images), keeps
  alpha and gets full mip chains. UV policy keeps using the original dimensions. Dynamic images
  (movie/canvas targets) and turbulent water surfaces keep the original, because the native warp
  table addresses original texels.
- Normal (tangent space, OpenGL +Y) and roughness/metallic/AO are only used when PBR is on.
  Roughness, metallic and AO are packed into one ORM texture (R=AO, G=roughness, B=metallic). A
  missing kind means a flat normal, legacy roughness, metallic 0 and AO 1.
- Font atlases are re-coloured with the active palette at run time, so dumped font keys match only
  when the runtime atlas is byte-identical.

## PBR / HDR (`pbr=1`)

- **Shading** (`native/shaders/pbr.glsl`, shared by OpenGL and Vulkan): GGX/Smith/Schlick direct
  specular for the sun (non-lightmapped surfaces) and every dynamic light. Diffuse is the legacy
  `texel x illumination` product, linearized with gamma 2.2, then multiplied by `(1-F)(1-metallic)`,
  AO and a normal-map term. Normals come from per-vertex tangents derived from the UVs
  (`render_policy::derive_tangents`, animated and deformed poses included). Surfaces with degenerate
  UVs keep the geometric normal. Legacy materials get roughness `sqrt(sqrt(2/(power+2)))` from their
  specular power (0.8 when they have none) and metallic 0. Unlit, explicit script-material and effect
  (`Normal_Mix`/`Normal_SunMix`) draws keep their original shading.
- **IBL**: ambient-only approximation. The level has no prefiltered sky cubemap, so the
  reflected-environment radiance is the local diffuse exitance (albedo x legacy irradiance) with
  Karis' analytic split-sum BRDF. Black surfaces stay black and dielectrics keep the legacy
  brightness.
- **Specular calibration**: like the legacy terms it extends, per-light radiance is clamped to 1, and
  each light's highlight is capped at its radiance. The sun and dynamic-light specular get the same
  shadow visibility as the diffuse term (shadow maps, RT signal mask or inline `shadows=rt` ray; OpenGL
  through `K22_PBR_LIGHT_VISIBILITY`). On the Start4 test view with shadow maps this removes the specular
  that previously leaked into shadowed areas (mean 16.73 -> 15.88, identical in OpenGL and Vulkan, only
  darker pixels).
- **HDR**: background views render into an MSAA RGBA16F target. Values stay in the game's gamma
  space, unbounded. The target is resolved and tone mapped into the LDR scene before the UI and
  foreground views. Clears are seeded with the inverse tone curve, so untouched pixels round-trip
  exactly. Foreground and portal views (LDR targets) tone map in the shader.
- **Tone curve** (`render_policy::tone_map`): identity up to 0.6 linear (0.793 in gamma space), then a
  Reinhard shoulder `k + (1-k) t/(1+t)` above it. It is C1-continuous, strictly increasing and
  bounded by 1.
- **Calibration result**: on the dark original Start4 view, mean capture luminance changes by +2.1%
  in OpenGL when PBR is on. On the brighter Start4 *test* view (script light near a wall) PBR raises the
  mean from 13.66 to 17.63, mostly as a broad grey specular sheen on dark surfaces near the light. Unit
  tests cover monotonicity, identity below the knee and the exact 8-bit inverse.

## Wave-2 effects (shared math: `native/shaders/effects.glsl`, helpers: `render_policy.hpp`)

All effects are off by default; with no option the frame takes exactly the original path (default
captures of the 2560x1377 Start4 test view are byte-identical to the pre-wave-2 build in OpenGL and
Vulkan raster; Vulkan RT see above). The *primary view* is the first full-window, non-foreground, opaque
world camera; depth-based effects apply only to it.

- **Shadow maps** (`shadows=shadowmap`): sun = 2048^2 orthographic depth map fitted to the camera
  frustum up to 2048 units, depth range widened to every world caster, texel-snapped
  (`sun_shadow_matrix`), 3x3 PCF; point/spot = one 1024^2 depth cube for the strongest nearby dynamic
  light (`shadow_light_index`, range > 32), near plane 16 so fixtures around a light do not occlude it.
  Casters are world + opaque models (animated poses, alpha test), back faces culled. The forward shader
  scales the sun term and per-vertex dynamic light by the same visibility ratio the RT path uses.
  Verified on Vulkan: the sun visibility on the test character agrees with the RT ground truth
  (character-region mean 5.33 shadow map vs 5.37 RT). Casters are prepared once per frame and replayed by
  the seven depth passes (`docs/RAYTRACING.md` timing table). Captures are byte-identical to the
  per-pass implementation in both backends. Indoors the occluded sun leaves non-lightmapped models much
  darker than the original (`docs/RAYTRACING.md` Limits).
- **SSAO** (`ssao=1`): 12-sample hemisphere AO from linear depth captured before view-space entities,
  depth-aware 4x4 blur, multiplied into the view (strength 0.65, radius 20 units).
- **Volumetric fog** (`volumetric_fog=1`): single scattering of the scene lights along the view ray
  (24 steps, HG g=0.25), density from the level fog (3/(end-start)) or a faint haze when the level has no
  fog; only in-scattered light is added, so the original linear fog stays untouched. Shadowed by the
  cube shadow map under `shadows=shadowmap`; unshadowed under `shadows=rt` (no depth maps are rendered
  just for fog). No original level has fog: all 970 WMBs have fog range 0..0 and no script sets
  `fog_start`/`fog_end`. Original content therefore always gets the haze path (checked on the Start4 test
  view, light shaft around the script light, and saves 8 and 26). Both backends march at half resolution
  (alpha keeps each sample's linear depth) and add the result through a depth-aware upsample, so fog does not
  bleed across silhouettes; OpenGL marched at full resolution up to 0.4.15 ([OpenGL performance](#opengl-performance)).
- **Composite** (`pbr`, `bloom`, `taa`, `fxaa`, `sharpen`): background views render into an RGBA16F target. With
  PBR off the values stay LDR and the tone curve is identity (bloom/TAA/FXAA work in LDR). Bloom: soft
  threshold 0.7 (knee 0.2), 5-level down/up chain, strength 0.15. On saves 1 and 21 (dark streets) the
  capture is unchanged. On save 26 the torch and the white poster glow and the mean rises 13.21 -> 13.58.
  The old 0.85/0.08 setting changed only 0.9% of pixels there and was not perceptible. In LDR the
  threshold cannot tell light sources from white paper. TAA (Vulkan): Halton(2,3) jitter, reprojection
  through the previous view-projection for the static scene and through per-pixel motion vectors for moving or
  vertex-animated entities (nearest motion sample of the 3x3 neighborhood unless the scene depth hides it;
  [RAYTRACING.md](RAYTRACING.md) step 4), history clipped to the neighborhood's YCoCg mean +- 1.25 sigma, blend
  0.1. Camera-only reprojection smeared the walking player (save 19, walking under the following camera, user
  feature set with `vulkan-rt`: hair and limbs doubled). With motion vectors the pixels that differ visibly from
  a TAA/denoise-off capture of the same frame drop from 914 to 273 (walking) and 1723 to 618 (walking while
  turning), close to the 232 of a standing frame. Moving edges stay slightly softer than without TAA. FXAA
  was chosen over SMAA: single pass, no lookup textures, and MSAA already resolves geometric edges.
- **Sharpen** (`sharpen=1`, Vulkan): AMD FidelityFX CAS-style contrast-adaptive sharpening
  (`effects.frag` mode 12, sharpness 0.4) as the last LDR composite pass, after TAA and FXAA (FXAA then
  writes an extra RGBA16F target). Per-channel strength falls where the 3x3 cross is already contrasty
  or near 0/1, so edges do not halo. It mainly restores texture detail that TAA softens.
- **Texture upscale** (`texture_upscale=1`, Vulkan): 2x of original world/model skins ≤ 512 px at upload
  (Scale2x or Lanczos-3 + mild unsharp, nearest alpha), see `docs/TEXTURE_RESTORATION.md`. No AI, no
  pack needed; a pack hit wins.
- **FSR** (`fsr=native|quality|balanced|performance|ultra_performance`, Vulkan): AMD FidelityFX Super Resolution
  3.1.4 (SDK v1.1.4 upscaler + Vulkan backend vendored in `third_party/FidelityFX-SDK`, MIT, provenance and the 5 local
  patches in `KABUS-PROVENANCE.txt`; wrapper `native/fsr_vk.cpp`, shader blobs `tools/k22_fsr3_blobs.py`). Background
  views (HDR target), their effects (SSAO, fog, bloom) and the ray-traced signals/denoiser render at the quality mode's
  render size (1.0x, 1.5x, 1.7x, 2x, 3x per axis); the tone-mapped composite is upscaled to the display before the
  UI/HUD and VIEW-entity layers, which stay at display resolution. Inputs: FSR's Halton jitter on the primary view,
  device depth captured before view-space entities, full unjittered motion (the TAA motion-vector pass for moving or
  animated entities, camera reprojection of the depth elsewhere; `effects.frag` mode 13), and a reactive mask that
  FSR's autogen pass derives from the primary view's colour before translucent/particle/view-space draws. History
  resets on scene release, resize and every frame without a primary 3D view. FSR replaces TAA; its RCAS (0.4) replaces
  CAS when `sharpen=1`, and FXAA is skipped. Foreground VIEW entities start from cleared depth/stencil instead of the
  (differently sized) background depth. A device without storage-image write-without-format / extended formats or
  compute subgroup quad operations is rejected like other missing features (explicit choices fail, preset backends
  degrade). `k22-render-check --vulkan[-rt]` runs the temporal-ghosting consumer with `fsr=quality`: 28 (raster) /
  34 (RT) visibly different pixels between the moving and the converged frame, against 790 / 819 with TAA.

### FSR 3.1.4 frame generation (`frame_generation=on`, Vulkan)

Opt-in (`--graphics frame_generation=on`, launcher Graphics → custom options; no preset sets it). Needs `fsr` on Vulkan raster
or RT, any quality mode including `native`; otherwise `render_policy::admit_features` turns it off with a notice. A device
without compute subgroup arithmetic, formatless storage-image reads or RGBA16F storage keeps FSR and drops frame generation
with a notice. The SDK's optical-flow and frame-interpolation components (vendored with the upscaler,
`third_party/FidelityFX-SDK/KABUS-PROVENANCE.txt`, patches 6, 7 and 9) run in the frame's command buffer; the SDK's Win32-only
frame-interpolation swapchain is not used. Contexts ported from IKV (`src/fsr_vk.cpp`); presentation and pacing as
OpenUnderworld and ETI Yami (below).

- **Inputs:** interpolation reuses the upscaler's dilated depth, dilated motion and reconstructed previous depth of the
  same frame and interpolates the HUD-less upscaled view (`fx.upscaled`, RGBA16F, display size, before the UI/foreground
  layers and the post/gamma pass) halfway from the previous one (`render_vulkan_raster.cpp` `interpolate()`).
- **HUD/UI:** never interpolated. The frame's foreground VIEW layers, panels, text, video and cursor are drawn a second
  time over the interpolated view, with the carried depth restored, then the same post pass writes the generated frame
  (`overlay()` run twice: generated, then the rendered frame exactly as with frame generation off). The generated frame
  thus shows this frame's HUD; world-anchored UI can lead the interpolated scene by half a frame.
- **Shading changes (local patch 7):** where the surface that the game motion follows has a clearly different colour in the
  two rendered frames (a shadow or light moving over a static floor, a lit character's own body), upstream blends both
  shadings into two half-strength shadows. Those pixels take the current frame's motion-placed sample instead (luma
  difference smoothstep 0.05..0.15 on display values), so such shadows step at the rendered cadence; small differences
  (ray-tracing noise) still blend. Geometry stays interpolated: the sample is still placed by the game motion.
- **Resets:** after an FSR history reset (level load, scene release, resize, a frame without a primary 3D view) only the
  rendered frame is presented; interpolation still runs to keep its previous-frame copy.
- **Presentation and pacing:** the frame's own command buffer acquires two swapchain images and blits the generated and the
  rendered frame into them (`State::finish`; the swapchain gets `minImageCount + 3` images while frame generation is on,
  as the previous frame's pair may still wait for presentation) and signals a timeline semaphore; a present thread only
  presents them (every queue submit, present and wait goes through one mutex). Per rendered frame it waits for the
  frame's timeline value, presents the generated image and, half a rendered-frame period later, the rendered image.
  The period is measured on the present thread between those frame completions (the last 8, without the smallest and the
  largest: a hitch or a skipped present does not move it, a stall of 250 ms or more is not sampled, a new frame rate is
  followed within 8 frames), so every wait of the render thread counts: GPU, FIFO acquire, `fps_limit`, CPU. Small
  frame-time differences are absorbed: a generated image is held until one period (less 0.1%) after the previous generated
  one, but never more than 1/16 period after its frame completed, so a period measured too long cannot hold the rendered
  rate down, and no pacing wait starts once a later frame is already complete. There is no compositor lead and no
  dependence on the refresh rate; the thread's timer slack is 1 ns (the default 50 µs is 1.2% of a 240 Hz refresh).
  History: before 0.4.16 the IKV-derived thread acquired and blitted after the fence itself: on the single graphics queue
  those blits ran behind the next frame's rendering, so a GPU-bound frame reached the display together with the next
  generated one, and the 2 ms "compositor lead" (`K22_FG_PACING_LEAD_MS`, removed) split every interval unevenly. 0.4.16
  measured the period on the render thread between submissions minus its wait on the present thread; GPU-bound that wait
  hid about a third of the period, so the rendered image followed the generated one after a third of the real period
  instead of half (4.0/7.1 ms at 90 rendered fps). The present mode is unchanged (FIFO with vsync: a full swapchain
  back-pressures the render thread's acquire; immediate/mailbox without). Before a frame acquires its two images
  (`State::finish`, after recording) the render thread waits for the present job two frames back (`drain_presents(1)`).
- **Latency:** the rendered image is presented half a rendered-frame period after the generated one, which follows the
  frame's completion by the jitter hold of at most 1/16 period: at most 9/16 of a rendered frame (6.2 ms at 90 rendered
  fps) after the frame would have been shown without frame generation, plus the interpolation's GPU time. 0.4.16 had no
  bound on that hold (its generated image waited a full half period after the previous rendered one).
- **Trace:** `K22_FG_TRACE=FILE` writes one line per present call (`microseconds G|R frame periodMs
  msSinceFrameCompletion`); `tools/check_frame_pacing.py --fg-trace FILE [--tail N] [--max-split F] [--max-p99 F]`
  prints the distribution (interval percentiles, both halves, the split, completion-to-present delays). With
  `K22_GPU_TIMING=1`, `present_ms` is the render thread's wait on the present thread and `acquire_ms` the two acquires.
- **`fps_limit`:** a numeric limit caps the presented rate, so rendering is paced at half of it (240 → 120 rendered);
  `original` keeps the script's `fps_max` pacing of the game ticks and presents twice that; `unlimited` is unchanged.
- **Log:** `kabus22: frame generation presents=600 generated=… real=… presentedFps=… renderedFps=… presentIntervalMs
  p50=… p99=… generatedToRealMs=… realToGeneratedMs=… renderedFrameMs=…` every 600 presents (present-call times; the two
  medians are equal when the cadence is even). Overlays that hook `vkQueuePresentKHR` (MangoHud) count both presents.
- **Captures:** `Renderer::capture(F.ppm)` (scenario `capture`, `--capture`) also writes `F-fg-generated.ppm` (the frame
  presented just before F, when this frame produced one) and `F-fg-previous.ppm` (the previous rendered frame).
- **Checks:** `k22-render-check --vulkan[-rt]` (`frame_generation_checks`): a solid actor sliding at constant speed under a
  HUD rectangle that jumps 108 px between frames must sit halfway in the generated frame (raster/RT: previous 97.0, generated
  93.5, current 90.0 px), keep its area (no cross-fade) and carry exactly this frame's HUD; on RT a SHADOW caster moving
  over a lamp-lit floor must not leave a cross-faded shadow (82 changed pixels on the shadow rows, 4 blended; upstream
  interpolation: 72). `graphics_policy` checks the option and its admission.
- **Validation:** Khronos layer with synchronization validation and no duplicate limit, level5 walk (raster and RT) and
  `k22-render-check`: frame generation on adds no message kind over off, and off equals main (the remaining
  depth-resolve WAW hazard, FSR autogen-reactive storage format mismatch, `vkDestroyImageView` in-use and raster
  unconsumed fragment output predate it).
- **Measured** (private virtual KWin 2560x1440 at 60 Hz, FIFO, RX 7900 XTX RADV, all `ultra`-like effects, `fsr=quality`,
  level5 walk from `kayit0.sav`): RT `presents=600 generated=300 real=300 presentedFps=60.0 renderedFps=30.0`,
  present interval p50 16.68 ms, p99 16.81 ms; rendered frames are bit-identical with frame generation on and off.
- **Measured pacing** (private virtual KWin 3840x2160 at 240 Hz, RX 7900 XTX RADV, vulkan-rt, `fsr=balanced`, all effects,
  engine log line, medians of clean runs, 0.4.16 → now; `tools/check_frame_pacing.py --fg-trace` for the delays):

  | Condition | presented fps | interval p50 / p99 ms | generated→real / real→generated ms |
  | --- | --- | --- | --- |
  | `fps_limit=unlimited` (no vsync), tunnel save, rendering-bound | 175.4 → 175.8 | 5.92 / 10.32 → 5.52 / 8.78 | 4.00 / 6.88 → 5.57 / 5.43 |
  | same, kizic2 save | 166.0 → 166.0 | 6.12 / 10.15 → 5.90 / 8.30 | 4.34 / 7.15 → 5.93 / 5.78 |
  | `fps_limit=240` (FIFO), tunnel save, rendering-bound | 172.9 → 172.3 | 6.46 / 9.61 → 5.67 / 8.19 | 4.12 / 6.92 → 5.69 / 5.59 |
  | `fps_limit=240`, raster `fsr=performance` (120 rendered) | 237.7 → 237.8 | 4.17 / 5.92 → 4.17 / 5.92 | 4.05 / 4.21 → 4.17 / 4.16 |
  | `fps_limit=original` (scenario: 60 rendered) | 119.9 → 119.9 | 8.35 / 8.48 → 8.34 / 9.59 | 8.35 / 8.35 → 8.34 / 8.32 |
  | 60 Hz output, `fps_limit=240` (FIFO, refresh-bound) | 59.9 → 60.0 | 16.60 / 20.76 → 16.69 / 19.84 | 16.52 / 16.68 → 16.70 / 16.65 |

  Rendering-bound at about 87 rendered fps (11.4 ms) and 240 Hz, each image is on screen for one or two whole refreshes
  under FIFO whatever the present cadence; the present calls themselves are now 5.7 ms apart both ways. The rendered frame
  is presented 6.0 ms after its GPU work completes (0.4 ms hold + half a period; 0.4.16: 4.0 ms, the uneven split), and
  4.5 ms after it at 120 rendered fps (0.3 ms + 4.2 ms; 0.4.16: 4.3–5.3 ms).
- **0.4.16 → 0.4.17 at the same settings** (the pacing above together with the release's faster rendering; one run at a time
  on an otherwise idle machine, both builds interleaved, two runs each, medians; the two runs of a build agree within 1%
  on the rates and within 1.6 ms on p99):

  | Condition | presented fps | rendered fps | interval p50 / p99 ms | generated→real / real→generated ms |
  | --- | --- | --- | --- | --- |
  | `fps_limit=240`, street with rain (kayit1) | 100.3 → 197.2 | 50.2 → 98.6 | 9.97 / 10.11 → 4.95 / 7.54 | 9.97 / 9.97 → 4.96 / 4.88 |
  | `fps_limit=240`, tunnel (kayit14) | 173.0 → 218.4 | 86.5 → 109.2 | 5.97 / 9.59 → 4.42 / 7.13 | 4.12 / 6.88 → 4.43 / 4.36 |
  | `fps_limit=240`, kizic2 (kayit10) | 164.4 → 194.2 | 82.2 → 97.1 | 6.43 / 9.89 → 5.03 / 6.85 | 4.46 / 7.11 → 5.04 / 4.98 |
  | `fps_limit=unlimited`, tunnel | 176.1 → 222.9 | 88.1 → 111.5 | 6.11 / 9.07 → 4.34 / 7.40 | 4.00 / 6.88 → 4.37 / 4.29 |
  | `fps_limit=unlimited`, kizic2 | 166.8 → 197.4 | 83.4 → 98.7 | 5.94 / 9.68 → 4.91 / 8.25 | 4.34 / 7.13 → 4.93 / 4.87 |

## Geometry upgrades (`smooth_normals`, `tessellation`; Vulkan only)

Both are off by default (`enhanced` and `ultra` enable them). With both off the Vulkan pipelines, pose buffers and shaders
take exactly the previous path (the new code sits behind a pipeline key bit and a specialization constant that defaults
off).

- **Smooth normals** (`smooth_normals=1`, `render_policy::smooth_normals`): MDL/HMP entity models get angle-weighted
  vertex normals recomputed from every animation frame's positions when the model's pose data is built
  (`render_vulkan_rt.cpp` `RayTracing::model`; script position overrides are re-smoothed when they change), so
  the GPU pose blend (`deform.comp`) interpolates smoothed normals between frames. This removes MDL's 162-direction
  normal quantization (`assets.cpp mdl_normals`), which shaded low-poly characters in visible facets. Display vertices are
  welded through `Model::vertexSources` (UV seams share one normal; authored split vertices stay split), and faces more
  than 60 degrees from a vertex group's mean are left out of it. WMB brushes keep their authored smoothing groups
  (flag 16384, already smoothed by the loader): brush edges stay hard and lightmaps keep matching. Raster shading, the
  G-buffer, shadow casters and ray-traced hits all read the same pose buffer, so every path sees the smoothed normals.
- **PN-triangle tessellation** (`tessellation=1`, `scene.tesc`/`scene.tese`): MDL draws that are not billboards are
  drawn as 3-point patches and evaluated as curved point-normal triangles over world-space positions and the smoothed
  normals (it enables the smoothing for these models). The level is distance-adaptive per edge
  (`edge length * 128 / distance`, clamped 1..8, 1 beyond 2000 units); it depends only on the edge, so neighbouring
  patches never crack. Each vertex carries a PN weight (MDL models have no lightmap, so it rides in `lightmap_uv.x`):
  1 where the shared normal fits every adjacent face, fading to 0 where a face deviates by more than 60 degrees, and an
  edge bends by the smaller weight of its corners. Without it, welded hard props (chair rails, seat plates, boxes)
  inflated into pillows. The scene pass, the RT G-buffer prepass, the sky stencil mask and the shadow-map casters all use
  the same tessellated pipeline, so prepass and forward depths match exactly and tessellated models cast tessellated
  shadow maps; planar (`draw_planar`) shadows and the ray-tracing BLAS stay on the base mesh. The device feature
  `tessellationShader` is optional: without it `tessellation` is dropped with a stderr notice.
- Cost on the RX 7900 XTX (level5 loaded view, `kayit0.sav`, 2560x1377, immediate present, 500 frames, two runs): user RT
  settings 12.00/12.12 ms before, 12.14/12.17 ms with both upgrades (12.07/12.12 smoothing only); Vulkan raster with
  shadow maps/SSAO/FXAA/TAA/bloom/fog/PBR 5.54/5.56 ms before, 5.60/5.60 ms with both. Within run-to-run noise.

## OpenGL performance

Measured on the RX 7900 XTX (radeonsi, Mesa 26.2), 3840x2160 borderless in a private `kwin_wayland --virtual`,
`fps_limit=unlimited` (vsync off), 600 steady frames after a save load (`realtime off`): kayit10 (kizic2 café interior)
and kayit1 (level 7 street). "User" = `pbr=1 anisotropy=16 bloom=1 ssao=1 fxaa=1 volumetric_fog=1 shadows=shadowmap`
(the Vulkan-only keys of a launcher custom set are dropped on OpenGL). Before and after binaries ran interleaved in one
session; frame ms is p50/p99 of `K22_FRAME_TIMING`.

| View | Settings | OpenGL 0.4.15 | OpenGL now | Vulkan raster |
| --- | --- | ---: | ---: | ---: |
| kizic2 | original | 1.31/3.70 | 1.33/3.75 | 1.08/1.27 |
| kizic2 | user, MSAA 1 | 13.7/18.6 (70 fps) | 6.6/9.5 (155 fps) | 3.8/8.6 |
| kizic2, switched from Vulkan in the graphics menu | user, MSAA 1 | 13.6/19.2 | 6.7/11.4 | - |
| level 7 street | original | 23.0/34.2 | 22.6/31.8 | 22.1/30.2 |
| level 7 street | user, MSAA 1 | 24.7/33.0 | 24.1/36.0 | 26.1/34.5 |

`K22_GL_TIMINGS=1` prints GPU time per pass (timestamp queries read four frames late) and the CPU completion-fence
wait, recording and swap times, averaged over 300 frames. What it found and what changed:
- **Full-resolution volumetric fog** was 70-80% of the GPU frame (kizic2 user: SSAO+fog 8.8 of 10.2 ms; street:
  16.4 of 20.6 ms). It now marches at half resolution with the Vulkan depth-aware upsample: fog 1.9 ms (kizic2),
  5.7-6.9 ms (street, many lights in range); GPU frame 10.2 -> 4.5 ms and 20.6 -> 12-13 ms. Captures differ by at
  most 0.25% of pixels by more than 8/255 (fog noise).
- **MSAA 1 rendered 2x MSAA**: `glRenderbufferStorageMultisample(1)` means "at least 1" and radeonsi gives 2 samples.
  Single-sample targets now request 0, and `presentation_state().samples` reports the count the driver allocated.
- The completion limit bounds submissions, not recording (as the Vulkan slots): the fence of the previous frame is
  waited on just before the swap instead of before recording.
- VAOs keep their default attribute pointers; only a mutable pose (`bind_override`) repoints them, and the next draw
  restores them, instead of six `glVertexAttribPointer` calls per draw. Captures are byte-identical.
- A renderer recreated by the in-game menu runs as fast as a fresh OpenGL launch (no per-frame re-uploads).
- The street rows above were bound by the engine tick (18-20 ms of WDL runtime CPU with rain), not by rendering. 0.4.17
  removed that limit (docs/ENGINE_ARCHITECTURE.md §3.4): OpenGL with the user set runs the rain street (kayit1) at 216 fps
  instead of 44 and kizic2 (kayit10) at 381 instead of 215 (frame p99 23.9 -> 5.1 ms and 6.3 -> 4.4 ms; one run at a
  time, two runs per build, runs of a build within 1%). OpenGL captures of both saves are byte-identical to 0.4.16.

## Vulkan CPU recording

`cpu_record_ms` in the `K22_GPU_TIMING=1` line is the CPU time `Raster::draw` spends recording one frame. Measured on a
Ryzen 9 9950X3D + RX 7900 XTX (RADV), 3840x2160 borderless in a private `kwin_wayland --virtual`, the launcher's
vulkan-rt custom set (`fsr=balanced`, PBR, RT shadows/GI/reflections, denoise, all effects), `fps_limit=unlimited`,
900 frames after a save load (`realtime off`). 0.4.16 against 0.4.17, one run at a time on an otherwise idle machine,
both builds interleaved, three runs each for Vulkan RT and two for raster (medians of the last two 300-frame lines; the
runs of a build within 5%). "Raster" is the same set with `--ray-tracing off` (shadow maps). The GPU and fps columns
include the release's other changes (ray-tracing passes, script runtime).

| View | Backend | `cpu_record_ms` 0.4.16 | 0.4.17 | GPU frame ms 0.4.16 | 0.4.17 | fps 0.4.16 | 0.4.17 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| kayit1, level 7 street (rain) | Vulkan RT | 5.63 | 1.07 | 12.5 | 6.8 | 42 | 138 |
| kayit5, level 7 street | Vulkan RT | 5.11 | 1.10 | 10.6 | 6.8 | 89 | 138 |
| kayit9, kriz | Vulkan RT | 2.23 | 1.40 | 9.2 | 7.7 | 102 | 123 |
| kayit10, kizic2 | Vulkan RT | 1.18 | 0.56 | 8.7 | 7.0 | 109 | 135 |
| kayit14, tunnel | Vulkan RT | 2.27 | 0.64 | 8.1 | 5.8 | 116 | 161 |
| kayit1, level 7 street (rain) | Vulkan raster | 6.55 | 1.46 | 9.5 | 3.0 | 40 | 305 |
| kayit10, kizic2 | Vulkan raster | 0.82 | 0.36 | 2.6 | 2.3 | 353 | 407 |

Captures of kayit1 and kayit10 on Vulkan raster, `--preset original` and OpenGL are byte-identical to 0.4.16 (the changes
of this section alone leave Vulkan RT captures byte-identical too; 0.4.17's RT image differs by the denoiser rounding
described in docs/RAYTRACING.md "Third pass").

The table compares both versions built locally with one compiler. The released binaries (gcc 13 in the Ubuntu 24
toolchain container), measured the same way: Vulkan RT kayit1 38 -> 141 fps (`cpu_record_ms` 7.59 -> 1.48), kayit10
112 -> 139 fps (1.32 -> 0.70), kayit14 120 -> 164 fps (3.11 -> 0.67); Vulkan raster kayit1 37 -> 176 fps (8.18 -> 2.54);
OpenGL kayit1 44 -> 210 fps. With frame generation and `fps_limit=240`: kayit1 90 -> 201 presented fps, kayit14
181 -> 225. The release toolchain's code records up to 1.7 times slower than the local compiler's (0.4.17: 1.4 times on
the RT street, 1.7 on the raster street, 1.05 in the tunnel; 0.4.16: 1.1-1.4 times): part of a release build's
remaining recording cost is the compiler, not the algorithm.

What a draw costs now, and what it may assume:

- **Scene descriptor sets are cached, not built per draw.** Set 0 binds the per-draw `Uniform` block (binding 0) and the
  light and instance/previous-pose storage buffers (5, 6) as *dynamic* descriptors: a draw copies its block into the
  frame slot's uniform buffer and passes three offsets to `vkCmdBindDescriptorSets`. The set depends only on the six
  material images and samplers, the six global images (shadow maps, RT signals, G-buffer depth) and the three buffers
  with their ranges, so draws that share them share one set, and the set outlives the frame (`SetCache`, one per frame
  slot). The shader interface is unchanged.
- **Identity, not handles.** `vk22::Image::id` and `Buffer::id` (assigned by `State::image`/`State::buffer`, never
  reused) are part of the key next to the view handle, so a cached set is never found again for a recreated texture
  whose Vulkan handle the driver reused. Sets naming destroyed resources are never bound. A slot's cache is emptied
  after a scene release (level change, reload), after `reconfigure`, and when it holds 8192 sets; the emptying happens
  at that slot's next frame start, when its previous frame has retired. A resource not created by
  `State::image`/`buffer` (id 0) gets a set for one frame only.
- **Binds are skipped while a rendering scope stays open**: the pipeline when it is already bound, the ray-query set
  (set 1) after its first bind. `Raster::end()` forgets both, so every scope starts clean. Consecutive UI commands share
  one scope.
- **Per-frame lookups are flat.** `prepared` (per-instance pose, lights, replay admission, model matrix, batch
  visibility), the actor-light map and the motion-vector matrices are insertion-ordered open-addressing maps cleared
  every frame; `mesh()` and `texture()` consult a direct-mapped pointer index in front of their maps, valid for one
  frame (nothing is erased while a frame draws). A model's material textures are visited once per frame, not once per
  instance.
- **Static world batches outside a view are not drawn.** A WMB mesh has one batch per material (about 3900 on the
  level 7 street, about 300 of them in view). Each batch has the bounds of its own triangles, and the G-buffer, motion
  and colour passes of a view skip a batch that fails `render_policy::geometry_eligible` against that view's
  view-projection and clip plane: the test that already admits whole instances. The result is computed once per view
  and reused by its passes; a mirror, portal or seamless-door view has its own view-projection and its own result.
  What is never culled this way: the ray-tracing acceleration structure and its materials (rays reach off-screen
  geometry), sky mask passes, script-deformed or script-material draws, models other than WMB (animated and
  PN-tessellated models keep the per-instance test), and water (type 9), whose raster encounter advances the shared
  warp bitmap. The motion pass rasterizes at the current position, so a batch off-screen now writes no vector.
- **Shadow-map casters are culled per face.** A caster is prepared and drawn only for the sun map and the cube faces
  whose frustum (the matrix pushed for that face) its bounds intersect: static WMB batches by batch bounds, other
  models by instance bounds; PN-tessellated models are always drawn.
- **Particles** are sorted through 16-byte keys (blend group, squared distance, index) instead of through particle
  pointers; the comparison, and so the order, is the old one.

### Pipeline cache and diagnostics

- The `VkPipelineCache` is saved to `$XDG_CACHE_HOME/kabus22/vulkan-pipelines-<vendor>-<device>.bin` (default
  `~/.cache/kabus22`) at a level change and at exit, through a temporary file renamed over the target. The file has a
  checksum; the driver data is used only when its header names this vendor, device and `pipelineCacheUUID` (another
  GPU or a driver update starts empty). `K22_PIPELINE_CACHE=0` disables the file.
- The file also lists the raster pipeline keys built so far, tagged with a hash of the SPIR-V and of the options
  compiled into pipelines. `Raster` builds the matching ones at start and after `reconfigure`, so from the second run
  a pipeline is not compiled at its first draw. Records with another signature, an unusable sample count or a field the
  renderer cannot produce are ignored. A missing, truncated, corrupted or unwritable file only costs a cold start (one
  notice when it cannot be saved).
- `K22_RENDER_EVENTS=1` prints a `render-event` line for every frame that created resources (`pipeline`, `texture`,
  `mesh`, in ms) or recorded for more than three times the running typical time, with the CPU time between GPU-timing
  marks, and a `render-stats` line every 600 frames (average recording ms, scene binds, culled batches, sets written).
- Measured with it (vulkan-rt custom set, save 14): first run without a cache file 19 pipelines compiled at first draw
  (largest 1.6 ms); second run none; with the file of another build, a truncated or a corrupted file 19 created at
  first draw from the driver's own caches (largest 0.2 ms).
- First-use costs that remain (the same in 0.4.16): an entity that was `invisible` is not in the frame packet, so its
  mesh and textures upload on the first frame it is shown, and a world (level load, or the destination world of a
  seamless door while the player approaches it) is prepared in one or two frames. With `texture_upscale=1` a texture
  costs 3-18 ms of CPU there: those frames record for 60-190 ms (`world_door_tunnel`, `world_door_kizic`).

## Live changes (`Renderer::reconfigure`)

`Renderer::reconfigure(next)` admits `next` for the running backend (`render_policy::admit_features`) and either applies
every difference from the next draw or returns false having changed nothing; the caller then builds a new `Renderer`.
Window size/mode/title stay on `set_mode`/`set_title`. `Renderer::options()` reports the settings in effect after backend
clamps (MSAA count, anisotropy limit, dropped tessellation/frame generation).

| Backend | Live | Needs a new renderer |
| --- | --- | --- |
| OpenGL | vsync, anisotropy (+force), MSAA, texture packs, `pbr`, bloom, FXAA, SSAO, fog (effect programs build on first enable) | backend, `shadows` original↔shadowmap (`K22_SHADOW_MAPS` is compiled into the scene program) |
| Vulkan raster/RT | vsync, anisotropy (+force), MSAA, texture packs, `pbr`, every effect toggle, `shadows`, `gi`/`reflections`/`denoise` (RT signal pipelines build on first enable), `fsr` level between non-off modes, `smooth_normals`, `texture_upscale`, `tessellation` off (and back on) when the device was created with it | backend, raster↔RT, `fsr` off↔on (device features), `frame_generation` on↔off, `tessellation` on when the device was created without it |

A Vulkan change drains every frame (and the frame-generation present thread) and idles the queue, then drops the frame
targets, the FSR context, effect images and pipelines, which rebuild lazily; TAA, FSR, RT-denoiser and frame-generation
history restart so the previous look does not smear into the new one. Changes that alter uploads (packs, `pbr`, smoothing,
tessellation, upscaling) release the scene's GPU meshes, poses and textures for re-upload, as a level load does.

## Not implemented

A prefiltered sky cubemap for IBL, PBR in ray-traced reflection hits, mirrored-UV bitangent signs, TAA on
OpenGL, screen-space upscaling.
