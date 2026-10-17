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
  view, light shaft around the script light, and saves 8 and 26).
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
`third_party/FidelityFX-SDK/KABUS-PROVENANCE.txt`, patches 6 and 7) run in the frame's command buffer; the SDK's Win32-only
frame-interpolation swapchain is not used. Ported from IKV (`src/fsr_vk.cpp`, `renderer_vk.cpp` present thread).

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
- **Presentation and pacing:** a present thread owns the swapchain while frame generation is on (every queue submit,
  present and wait goes through one mutex). Per rendered frame it waits for the frame's fence, acquires and blits the
  generated frame and presents it, sleeps half the measured rendered-frame interval minus a 2 ms compositor lead
  (`K22_FG_PACING_LEAD_MS`, 0-20, calibrates it), then presents the rendered frame. Presents never wait for display; the
  present mode is unchanged (FIFO with vsync, so a full swapchain back-pressures the renderer; immediate with
  `fps_limit=unlimited`). Before a frame reuses its slot's images the render thread waits until the present job two
  frames back is done (`drain_presents(1)`): about half a rendered frame of extra latency.
- **`fps_limit`:** a numeric limit caps the presented rate, so rendering is paced at half of it (240 → 120 rendered);
  `original` keeps the script's `fps_max` pacing of the game ticks and presents twice that; `unlimited` is unchanged.
- **Log:** `kabus22: frame generation presents=600 generated=… real=… presentedFps=… renderedFps=… presentIntervalMs
  p50=… p99=… renderedFrameMs=…` every 600 presents (present-call times).
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

## Not implemented

A prefiltered sky cubemap for IBL, PBR in ray-traced reflection hits, mirrored-UV bitangent signs, TAA on
OpenGL, screen-space upscaling.
