# Ray tracing (Vulkan `vulkan-rt`)

## What is implemented

- **Backend**: Vulkan 1.3 with `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
  `VK_KHR_deferred_host_operations`, buffer device addresses and descriptor indexing. It is selected
  by `--renderer vulkan` (ray tracing defaults on), `--graphics backend=vulkan-rt` or `--preset ultra`.
  Ray queries are issued from the ordinary raster fragment shader (`scene.frag` built with
  `K22_RAY_QUERY=1`). There is no ray-tracing pipeline and no SBT.
- **Geometry** (`native/render_vulkan_rt.cpp`): each displayed world/model pose is written by the
  `deform.comp` compute pass into a per-frame vertex buffer: position, normal, UV, lightmap UV and
  tangent, 13 floats per vertex. Animated frames, interpolation and mutable mesh overrides are
  resolved on the GPU. One BLAS is built per pose. The TLAS is rebuilt per camera from the admitted
  opaque and translucent draws in source order, with per-instance clip planes. The instance custom
  index is capped at 24 bits and enforced with an error.
- **Materials in hits**: one `RtMaterial` record per batch, carrying UV transform, tint, alpha,
  sprite frames, flags (overlay/flare/alpha test/additive/blend/explicit material/unlit/effect) and
  the prepared script-material terms. Hit textures come from a bindless `sampler2D[1024]` array,
  which is the texture-binding ceiling: a request on a device below 1024 samplers fails explicitly.
  With texture packs, hits sample the pack albedo because they share the raster texture cache.
- **Effects**:
  - *Visibility* (`shadows=rt` only): sun and dynamic-light shadow rays (`rtVisibility`) scale the
    native per-vertex dynamic lighting by the per-channel visible/unshadowed ratio. The sun is used only
    on non-lightmapped surfaces. Models with the original SHADOW flag also shadow lightmapped level geometry
    from the baked WMB lights and the sun ([actor shadows](#actor-shadows)), which replaces their fixed-direction
    A6 planar shadow in the primary view. Without `shadows=rt` the ray-traced backend does not occlude any light:
    A6 point/spot lights and the sun are D3D9 fixed-function hardware lights (`D3DLIGHT` slots enabled
    with `LightEnable`, the sun as `D3DLIGHT` type 3; `reverse/decompiled/runtime/graphics-settings-source/a6-worldlight-contract.txt`),
    which never cast shadows; static shadows exist only in the baked lightmaps. Default RT lighting is
    therefore identical to raster.
  - *Reflections*: on mirror materials without an attached portal view, `rtReflection` traces one
    reflection ray, shades the hit with the legacy material model and mixes it by the original
    reflection weight.
- **PBR on RT** (`pbr=1`, preset `ultra`): the PBR path (`docs/GRAPHICS_ROADMAP.md`) applies the same
  visibility as the diffuse term to its sun and dynamic-light specular (RT signal mask, inline ray, or
  shadow maps); with shadows off, specular is unshadowed like the diffuse term.
- **No silent fallback**: an explicit RT request on unsupported hardware fails with the missing
  feature list. Only preset-chosen RT degrades to raster, and it prints a notice.

## Verified on this host

RX 7900 XTX with RADV, Mesa 26.2.4:

- `--list-capabilities` reports `vulkan-rt` available ("KHR ray query").
- The `k22-render-check FX --vulkan-rt` consumer suite passes. It checks that `shadows=rt` occludes the
  sun with an actor, that an actor's shadow on a lightmapped floor follows a baked light and replaces its planar
  shadow, and that the default RT path lights exactly like Vulkan raster.
- Default RT captures are byte-identical to Vulkan raster on the 2560x1377 Start4 test view and on
  imported original saves 1, 8 and 21 (no reflective surface in view). Before this fix the default RT
  Start4 test view had mean 4.36 against 13.66 in raster: the script light (60,40,20) sits inside a solid
  `blo02` pillar brush (faces at x=37/63, y=47, closed by the y=32 brush below), so every visibility ray
  toward the light center crossed the pillar shell. Neither the light's own model (hidden: still 4.36) nor
  ray origin bias (normal offset: unchanged) caused it.
- `--preset ultra` ran 600 campaign frames and a 120-frame Start4 view with a test pack.
- 60-frame Start4 test-view runs under `VK_LAYER_KHRONOS_validation` (VVL 1.4.363, loaded through
  `VK_ADD_LAYER_PATH`): Vulkan raster, RT, `shadows=shadowmap`, `--preset enhanced`, `--preset ultra`,
  `shadows=rt denoise=1`, `gi=1 denoise=1`, `reflections=rough denoise=1`, raster with every raster effect
  plus PBR, and the bathroom-mirror view with `reflections=rough` + PBR + a roughness pack. All report zero
  validation errors. Shadow-map configurations report one warning class,
  `Undefined-Value-ShaderOutputNotConsumed-DynamicRendering` (the depth-only caster pass reuses the scene
  fragment shader, whose color write has no attachment); it predates this change. As a positive control,
  enabling best practices reports warnings with the same setup.

## Wave-2 ray-traced effects (`shadows=rt`, `gi=1`, `reflections=rough`, `denoise=1`)

Screen-space signals of the primary view (`native/render_vulkan_raster.cpp` `rt_signals`,
`native/shaders/rt_signals.comp`, `rt_filter.comp`):

1. **G-buffer prepass**: world + opaque models with the forward pass's own (jittered) view-projection into
   a D32 depth + RGBA16F (octahedral normal, pack roughness, lighting flags) target. Forward fragments
   whose depth equals the prepass depth use the signals; others (translucent, MSAA edge samples) keep the
   original inline path.
2. **Signals** (compute + ray query, 1 sample/pixel/frame):
   - *Soft shadows*: one ray per contributing light to a random point on a sphere of radius
     clamp(0.03 range, 2, 12) units; sun inside a 0.02 rad cone; output is the shadowed/unshadowed ratio of
     the dynamic-light sum and the sun visibility. *Contact shadows*: one 40-unit cosine ray against models
     only (TLAS instance mask 2; level geometry is mask 1), so actors darken baked lightmaps. The TLAS
     already contains every visible non-view-space model; animated poses are re-deformed on the GPU and
     their BLAS updated (`rt_pose`/`rt_scene` version check) every frame. Without `denoise=1` the shadow
     signal is visibly 1-sample noisy (`ultra` enables the denoiser).
   - *Actor shadows* (lightmapped pixels, mask alpha): see [Actor shadows](#actor-shadows).
   - *Occluders* (soft shadows and the inline `shadows=rt` rays): judged as seen from the light, like the
     back-face-culled shadow-map caster pass. A one-sided face blocks only when its front faces the light,
     so a light inside a closed brush or emitter model is not self-occluded. Geometry within 16 units of a
     point/spot light (`rtLightClearance`, the shadow cube near plane) does not occlude it, and additive
     (glow) surfaces add light without blocking it. Contact-shadow rays are alpha-tested like the raster
     pass.
   - *GI*: one cosine ray (1500 units); the hit is lit from its lightmap (`RtMaterial.lightState.z`,
     only when `gi=1`) or native ambient plus unshadowed dynamic lights, linearized; misses return 0. The
     forward shader adds `albedo x irradiance` in linear space. Chosen over a DDGI probe grid because the
     BSP levels are thin-walled (probe leaking) and the lightmaps already hold the static term; one
     screen-space bounce gives dynamic-light and model color bleeding without a probe placement step.
     Measured (2560x1377, 120 frames): Start4 test view mean 13.66 -> 13.68, save 8 21.97 -> 22.04, save 21
     17.44 -> 17.45. The original levels are dark, so the bounce is small and colour bleeding is not visibly
     perceptible on these views; the signal itself is present (checked by displaying it directly).
   - *Rough reflections*: GGX half-vector sample with the pack roughness (0 = mirror), layered
     transparency as `rtReflection`, on the original reflective surfaces only, including unlit ones (the
     camtuvalet bathroom mirror, type 3, received no reflection before this fix). Checked on that mirror:
     roughness 0 matches the default inline reflection, roughness 0.35 (test pack, `pbr=1`) is noisy and
     blurred, and `denoise=1` smooths it.
   - *Smoothed models* (`smooth_normals=1` or `tessellation=1`, `docs/GRAPHICS_ROADMAP.md` Geometry upgrades): hit
     shading already interpolated vertex normals with the hit barycentrics (`hitRadiance`, explicit-material hits); the
     pose buffers now carry the smoothed normals. Shading a low-poly model with smooth normals while tracing against its
     flat triangles lets shadow and contact rays leaving a facet near the terminator re-enter the neighbouring facets
     (the shadow-terminator problem: dark triangle-shaped blocks). For lit, non-lightmapped pixels `rt_signals.comp`
     therefore re-finds the base triangle under the pixel with a camera ray (within `max(4 bias, 0.4% distance)` of the
     G-buffer depth) and starts the rays from the surface its vertex normals describe (Hanika 2021: barycentric blend of
     the hit projected outward onto each vertex tangent plane), offset along the camera-facing geometric normal. The
     G-buffer orients the stored normal by the camera-facing facet normal (from `worldPosition` derivatives) instead of
     flipping any normal facing away from the camera, which inverted smooth normals at model silhouettes. Tessellated
     PN surfaces are traced as their base mesh (the BLAS is not tessellated): a tessellated pixel whose base triangle
     lies within the camera-ray window starts its rays from that triangle's lifted surface; where the PN bulge exceeds
     the window (large patches close to the camera) the pixel keeps the previous origin, offset along its shading
     normal. Lightmapped (world) pixels keep the previous origin.
3. **Denoise** (`denoise=1`): temporal accumulation (reprojection with the previous unjittered
   view-projection, or with the per-pixel motion vector where a moving/animated entity is visible; rejected on
   >5% view-depth change or normal cosine < 0.9; history clipped to the current 3x3 raw signal mean +- 2 sigma
   so a shadow or bounce does not linger where its caster left; alpha max(1/(n+1), 0.15 shadows/reflections,
   0.06 GI)), then three a-trous B3 iterations (strides 1/2/4) with depth/normal edge stopping; mirror-smooth
   reflections are not blurred.
4. **Motion vectors** (`taa=1`, or `denoise=1` with signals): after the G-buffer, entities whose transform
   (keyed by entity id, previous frame only) or GPU-deformed pose (the same actor's pose in the other frame
   slot, `rt_previous_pose`) changed are drawn again (scene pass 3) into an RGBA16F target: previous-minus-current
   UV, view depth, previous view depth. The world and unmoved entities keep camera reprojection, so nothing is
   drawn when nothing moves. TAA and the denoiser use it; with both off the pass and its images do not exist.
   `k22-render-check --vulkan|--vulkan-rt` renders a moving, vertex-animated actor under a following camera
   with the full temporal stack and compares the frame reached through motion with the same frame held still:
   visibly different pixels (any channel > 24) of 57600 went from 6043 (RT) / 2278 (raster) with camera-only
   reprojection to 819 / 790 (remaining: TAA softening of moving edges).

Wall-clock per-frame cost, RX 7900 XTX (RADV, Mesa 26.2.4), Start4 test view 2560x1377 (window-manager
clamped from 2560x1440), MSAA 4, present mode immediate, (t(600 frames) - t(100 frames)) / 500, best of two.
These include CPU time. Run-to-run noise is about +-0.5 ms. Shadow-map casters are recorded once per frame and
replayed by all seven depth passes: Vulkan pushes each face's view-projection as a push constant, and OpenGL
groups the world's WMB batches (about 3000 on Start4) into one multi-draw per alpha-rejection state.

All cheap configurations measure about 4.2 ms in this build, so costs below about 0.3 ms are not resolved.
The previous table had `shadows=shadowmap` at 22.83 ms (Vulkan) and `--preset ultra` at 30.32 ms.

| Configuration | ms/frame |
| --- | --- |
| OpenGL default | 4.24 |
| + `shadows=shadowmap` | 4.21 |
| Vulkan raster default | 4.24 |
| + `shadows=shadowmap` | 4.86 |
| + `ssao=1` | 4.28 |
| + `volumetric_fog=1` | 4.49 |
| + `bloom=1` | 4.54 |
| + `taa=1` | 4.21 |
| + `fxaa=1` | 4.22 |
| `--preset enhanced` | 5.85 |
| Vulkan RT default | 4.15 |
| + `shadows=rt` | 8.06 |
| + `shadows=rt denoise=1` | 7.57 |
| + `gi=1 denoise=1` (full resolution) | 7.83 |
| + `reflections=rough denoise=1` | 5.88 |
| `--preset ultra` (no shadow maps: fog is unshadowed under `shadows=rt`) | 15.08 |

`texture_upscale=1` and `sharpen=1` (see `GRAPHICS_ROADMAP.md`) work the same on `vulkan-rt`: ray-query
material hits sample the same upscaled image views as raster, and CAS runs after TAA in the shared
composite. On Demir level5 at 1920x1080 with the user-style RT setup (PBR, `shadows=rt`, GI, rough
reflections, denoise, bloom, SSAO, FXAA, TAA, fog) the mean luma gradient of the frame rises 1.75 -> 2.19
(upscale alone 1.79), mean luma unchanged (21.21 -> 21.19).

## Actor shadows

The original's only actor shadow is the A6 planar shadow: the caster flattened onto its floor plane along a fixed
`SUN_ANGLE` direction (`frame.shadowSun`), drawn only where the floor light scalar exceeds `shadow_threshold`. Levels are
lightmapped and their WMB lights reach the renderer as baked records, so before this change `shadows=rt` changed
nothing visible on them: Demir in level5 with the user's `ultra` setup differed from Vulkan raster by a mean 0.18/255,
and level5's floor is too dark for the planar shadow, so he had no shadow at all.

With `shadows=rt` (`rt_signals.comp` `actorShadow`, mask alpha; denoised with the other shadow signals):

- **Casters**: models with the original SHADOW flag (`DrawInstance::shadowRequested`, the planar-shadow casters: the
  player, enemies, some props) get TLAS instance mask 4 in addition to 2.
- **Lights**: `render_policy::actor_shadow_lights` picks up to 31 baked WMB lights whose range sphere is nearest to the
  camera, plus the hardware sun, and appends them after the 32 shading records (level5 has 45 baked lights; the
  shading upload keeps native slots first and is unchanged).
- **Receivers**: lightmapped pixels. Each light weighs luma x N.L x linear falloff 1 - d/range (the squared
  `enhancedBaked` falloff left the lamp shadow of a standing actor, which falls beyond the lamp's bright disk, at under
  3% of the light there). One baked light per pixel and frame is traced, chosen in proportion to its weight, toward a
  jittered point on it (soft shadows, as above); a caster hit counts only if the level geometry lets that light reach
  the receiver (otherwise the lightmap holds none of it). The sun is traced as well and counts only where the level
  geometry passes it, so indoor levels get no sun shadow. The lightmap is multiplied by
  1 - SHADOW_ALPHA/100 x (occluded share of the weighted light), so one fully blocked light darkens like the original
  planar shadow, several lights give several partial shadows, and the shadow fades out where the lights fade (total
  weight below 0.05).
- **Planar shadow**: the primary view no longer draws the planar shadow of a visible SHADOW caster.

Unchanged: default ray tracing, Vulkan raster, `shadows=shadowmap` and OpenGL draw the planar shadow as before. Lamp-walk
captures (level5, save slot 01, 4 positions) are byte-identical between the previous and this build for OpenGL default,
Vulkan raster (`original`), Vulkan RT default and Vulkan raster `shadows=shadowmap`.

Measured with the user's launcher setup (`ultra`, `vulkan-rt`, `shadows=rt`, GI, rough reflections, denoise, PBR,
MSAA 2, FSR quality, 1920x1080), level5 lamp walk (Demir at 20-50 units from a 5-light lamp cluster): the shadow now
falls from his feet away from the lamp and turns with his position (he stands north, west and north-east of the lamp
in the three walk positions; in the west position the lamp's lit disk is beside him and the shadow points the other
way). Before -> after RT frames differ by a mean 0.37-0.86/255, 15k-58k pixels by more than 8 levels. Captures (private, game assets): `reverse/rt-actor-shadows/level5-*-{rt-before,rt-after,raster,original}.png`
and `*-crop-before-vs-after.png`. `k22-render-check --vulkan-rt` checks it on a lightmapped floor with one baked light:
the floor 22 units beside the caster drops from 255 to 126/128 on the side away from the light, the shadow changes
sides with the light, and the planar control shadow (128 by default RT) is gone.

Cost (`K22_GPU_TIMING=1`, level5 bench, `ultra`): `rt_signals` 0.77 -> 0.93-0.98 ms, GPU frame 3.21 -> 3.31-3.39 ms.
Tracing every weighted light instead of one sampled light cost 1.10 ms (305 -> 275 FPS in a quiet run). The FPS of the
final runs (206-277) is not comparable: other workloads loaded the host's CPU at the time.

## Performance

Measured 2026-10-10 on an RX 7900 XTX (RADV, Mesa 26.2.4): Demir in level5 (original save slot 01), standing,
1920x1080, `--preset ultra` (MSAA 4, PBR, RT shadows/GI/rough reflections, denoise, bloom, TAA, volumetric fog,
smooth normals, tessellation), immediate present, nested in a headless gamescope. `K22_GPU_TIMING=1` writes Vulkan
timestamps between the renderer's passes and CPU frame times to stderr, averaged over 300 frames. GPU ms per pass:

| Pass | RT before | RT `fsr=off` | RT `fsr=quality` (ultra) | raster before | raster `fsr=off` | raster `fsr=quality` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| BLAS refit + TLAS (`as_build`) | 0.21 | 0.21 | 0.21 | 0.00 | 0.00 | 0.00 |
| G-buffer prepass | 0.26 | 0.27 | 0.22 | - | - | - |
| RT signals (`rt_signals.comp`) | 1.61 | 1.65 | 0.77 | - | - | - |
| denoiser (`rt_filter.comp`, 4 passes) | 1.42 | 1.44 | 0.62 | - | - | - |
| scene draws | 0.89 | 0.87 | 0.58 | 0.51 | 0.50 | 0.45 |
| shadow maps | - | - | - | 0.11 | 0.11 | 0.12 |
| depth effects (volumetric fog) | 1.63 | 0.47 | 0.23 | 1.51 | 0.45 | 0.29 |
| composite (bloom, tone map, TAA) | 0.27 | 0.25 | 0.02 | 0.28 | 0.26 | 0.02 |
| FSR 3.1 (reactive + upscale) | - | - | 0.34 | - | - | 0.37 |
| GPU frame total | 6.44 | 5.30 | 3.12 | 2.56 | 1.47 | 1.40 |
| CPU recording | 1.55 | 1.46 | 1.54 | 1.17 | 1.17 | 1.27 |
| frame time (ms) / FPS | 8.22 / 122 | 5.52 / 181 | 3.27 / 306 | 3.92 / 255 | 2.36 / 424 | 2.47 / 405 |

Root causes found and fixed:
- **CPU/GPU serialization.** The frame-latency limit (1 in a window, `TRIPLEBUFFER` fullscreen 2) waited for the
  previous frame's fence before recording the next frame, so CPU recording (1.2-1.6 ms) and GPU work never overlapped
  (RT: 8.2 ms frames for 6.4 ms of GPU work). The limit now bounds submissions only: the next frame records into the
  other, idle slot while the previous one runs, and `finish()` waits before submitting. Alone: RT 122 -> 154 FPS,
  raster 255 -> 380 FPS, same queue depth.
- **Full-resolution volumetric fog** (24 steps x up to 32 lights per pixel) was the second-largest pass on both
  backends. It now marches at half resolution (alpha keeps each sample's depth) and is added with a depth-aware
  bilinear upsample (`effects.frag` modes 4/14): 1.6 -> 0.47 ms.
- **Per-pixel RT cost at display resolution**: signals and denoiser scale with pixels. `ultra` now runs FSR 3.1
  quality (2/3 per axis, 0.44x pixels), which halves them; the ultra preset is CPU-bound afterwards.
- Not a cause: acceleration structures are already refit (BLAS update for changed poses, TLAS update when the instance
  count is unchanged; 0.21 ms), there are no per-frame queue/device waits, readbacks or descriptor-set rebuilds
  outside the per-slot arenas.

## Limits

- Sun shadows (`shadows=rt` and `shadows=shadowmap` alike) make non-lightmapped models indoors much darker
  than the original. The sun is the main light on those models, and ceilings occlude it, for example the
  bed in save 8 and the pit grate in save 26. In the original the sun was never occluded.
- A light placed inside solid level geometry (as in the Start4 test scene) is still occluded by
  `shadows=rt` for receivers whose path crosses another brush toward it.
- One bounce, one sample per pixel; reflections hit with legacy shading.
- The TLAS is rebuilt per camera when its instance count changes (else updated), BLAS refit for changed poses, without compaction.
- Particles, view-space entities and billboards are not in the acceleration structure.
- With `tessellation=1` the BLAS still holds the base mesh: ray-traced shadows, GI and reflections see model
  silhouettes without the PN bulge.
- Actor shadows fall only on lightmapped level surfaces. Models (radiators, curtains, other actors) and decal models
  (the level5 blood pool) receive none, while the caster's planar shadow is gone in the primary view: in `ev` (save 6)
  the room light is behind the camera, so Ebru's shadow belongs on the radiator and curtain behind her and is not drawn.
- Mirror/portal views, SHADOW entities that are themselves invisible, translucent and MSAA-edge pixels keep the old
  behaviour (planar shadow, or no actor shadow); a baked light inside solid geometry casts no actor shadow (its level
  ray is blocked). `shadows=shadowmap` has no actor shadows from baked lights (one cube map for one dynamic light).
