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
    from the baked WMB lights, native point/spot lights and the sun ([actor shadows](#actor-shadows)), which replaces
    their fixed-direction A6 planar shadow (never drawn under `shadows=rt`); models are shadowed by themselves and other
    models and get ray-traced ambient occlusion ([model shadows and AO](#model-shadows-and-ambient-occlusion)). Without `shadows=rt` the ray-traced backend does not occlude any light:
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
  sun with an actor, that an actor's shadow on a lightmapped floor follows a baked light, that the planar shadow is never
  drawn (with no light in range the actor shadow falls toward the planar shadow's light instead), that models shadow
  themselves and receive ambient occlusion, and that the default RT path lights exactly like Vulkan raster.
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
     the dynamic-light sum and the sun visibility. *Ambient occlusion*: one 40-unit cosine ray. On lightmapped
     pixels it sees models only (TLAS instance mask 2; level geometry is mask 1), so actors darken baked lightmaps
     (floor 0.3, as it scales the whole lightmap); on model and vertex-lit pixels it sees all geometry, the model itself
     included, and scales their ambient term ([model shadows and AO](#model-shadows-and-ambient-occlusion)). The TLAS
     already contains every visible non-view-space model; animated poses are re-deformed on the GPU and
     their BLAS updated (`rt_pose`/`rt_scene` version check) every frame. Without `denoise=1` the shadow
     signal is visibly 1-sample noisy (`ultra` enables the denoiser).
   - *Actor shadows* (lightmapped pixels, mask alpha): see [Actor shadows](#actor-shadows).
   - *Occluders* (soft shadows and the inline `shadows=rt` rays): judged as seen from the light, like the
     back-face-culled shadow-map caster pass. A one-sided face blocks only when its front faces the light,
     so a light inside a closed brush or emitter model is not self-occluded. Geometry within 16 units of a
     point/spot light (`rtLightClearance`, the shadow cube near plane) does not occlude it, and additive
     (glow) surfaces add light without blocking it. Ambient-occlusion rays are alpha-tested like the raster
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
   - *Shadow-terminator origin* (lit, non-lightmapped pixels, with or without `smooth_normals`): hit shading already
     interpolated vertex normals with the hit barycentrics (`hitRadiance`, explicit-material hits); with
     `smooth_normals=1` or `tessellation=1` (`docs/GRAPHICS_ROADMAP.md` Geometry upgrades) the pose buffers carry the
     smoothed normals. Shading a low-poly model with vertex normals while tracing against its flat triangles lets shadow
     and occlusion rays leaving a facet near the terminator re-enter the neighbouring facets (the shadow-terminator
     problem: dark triangle-shaped blocks, acne in self-shadow). `rt_signals.comp` therefore re-finds the base triangle
     under the pixel with a camera ray (within `max(4 bias, 0.4% distance)` of the G-buffer depth) and starts the rays
     from the surface its vertex normals describe (Hanika 2021: barycentric blend of the hit projected outward onto each
     vertex tangent plane), offset along the camera-facing geometric normal. With smoothing the G-buffer orients the
     stored normal by the camera-facing facet normal (from `worldPosition` derivatives) instead of flipping any normal
     facing away from the camera, which inverted smooth normals at model silhouettes. Tessellated PN surfaces are traced
     as their base mesh (the BLAS is not tessellated): a tessellated pixel whose base triangle lies within the camera-ray
     window starts its rays from that triangle's lifted surface; where the PN bulge exceeds the window (large patches
     close to the camera) the pixel keeps the previous origin, offset along its shading normal. Lightmapped (world)
     pixels keep the previous origin.
3. **Denoise** (`denoise=1`): temporal accumulation (reprojection with the previous unjittered
   view-projection, or with the per-pixel motion vector where a moving/animated entity is visible; rejected on
   >5% view-depth change or normal cosine < 0.9; history clipped to the current 3x3 raw signal mean +- 2 sigma
   so a shadow or bounce does not linger where its caster left; alpha max(1/(n+1), 0.15 shadows/reflections,
   0.06 GI)), then three a-trous B3 iterations (strides 1/2/4) with depth/normal edge stopping; mirror-smooth
   reflections and the lightmapped actor-shadow alpha are not blurred (model-pixel alpha is, among model pixels).
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

With `shadows=rt` (`rt_signals.comp` `actorShadow`, in the alpha of the shadow signal):

- **Casters**: models with the original SHADOW flag (`DrawInstance::shadowRequested`, the planar-shadow casters: the
  player, enemies, some props) get TLAS instance mask 4 in addition to 2.
- **Lights**: `render_policy::actor_shadow_lights` picks the baked WMB lights and native point/spot lights (entity
  lights such as `ev`'s wall lamp, a D3D light the lightmap does not hold) whose range contains a caster, deepest first,
  up to 31, plus the hardware sun. They are appended after the 32 shading records, whose upload is unchanged (level5 has
  45 baked lights).
- **Receivers**: lightmapped pixels. Each light weighs luma x N.L x falloff (baked: linear 1 - d/range, as the squared
  `enhancedBaked` falloff left a standing actor's lamp shadow beyond the lamp's bright disk at under 3% of the light
  there; native: its D3D attenuation and cone; sun: 1), at most 1. Per pixel and frame the dominant light, one other light
  picked in proportion to its weight (standing for all the others) and the sun are traced against the casters, toward a
  point jittered within clamp(0.005 range, 0.5, 2) units: lamps are small, and the 2-12 unit soft-shadow sphere blurred a
  body's shadow away a few metres from it. A blocked baked light counts only where the level geometry lets it reach the
  receiver (otherwise the lightmap holds none of it); native lights shine through walls like the D3D lights they are
  (and `ev`'s sits inside its wall fixture). The sun counts only where the level lets it through, so indoor levels get no
  sun shadow.
- **Darkness**: SHADOW_ALPHA/100 x min(occluded weight / strongest weight, 1). Blocking the dominant light darkens the
  surface exactly like the original planar shadow (`rgb *=` the factor after lighting, PBR and GI, before fog: the planar
  shadow is black alpha-blended over the lit surface); a weaker light gives a proportionally lighter shadow, several
  lights several shadows. The signal is accumulated over frames like the other shadow signals but not blurred spatially
  (the a-trous blur halved a body's shadow strip).
- **No light**: where the strongest weight is below 0.02 (no traced light in range, or too faint), the shadow blends
  into one cast at SHADOW_ALPHA toward the planar shadow's own light (`render_policy::planar_light_direction`, from
  `frame.shadowSun`: the direction along which `planar_projection` flattens the caster), against the casters only, like
  the planar shadow that ignores the level. So every caster has a shadow, in the original's direction where nothing
  else lights the floor.
- **Planar shadow**: never drawn under `shadows=rt`, in any view, and its shadow-LOD poses are not prepared. (0.4.0
  removed it only after the GPU reported at least 64 shadow pixels per caster, read back two frames later, so a caster
  without a light in range, or with a small shadow, showed the original fixed-direction shadow, and it flickered as the
  count crossed the threshold.)

Unchanged: default ray tracing, Vulkan raster, `shadows=shadowmap` and OpenGL draw the planar shadow as before. Lamp-walk
captures (level5, save slot 01, 4 positions) are byte-identical between 0.3.0 and this build for OpenGL default, Vulkan
raster (`original`), Vulkan RT default and Vulkan raster `shadows=shadowmap`.

Measured with the user's launcher setup (`ultra`, `vulkan-rt`, `shadows=rt`, GI, rough reflections, denoise, PBR,
MSAA 2, FSR quality, 1920x1080) against the same setup on `vulkan-raster` (RT off: `shadows=shadowmap` plus the planar
shadow). Darkness = 1 - frame / the same frame without that shadow (display luma, pixels darkened by at least 0.1;
mean includes the soft edges, p90 is the core). The planar shadow is 0.50 by construction (SHADOW_ALPHA 50).

| View (save) | RT actor shadow: pixels, mean, p90 | Raster planar shadow: pixels, mean, p90 |
| --- | --- | --- |
| level5 spawn (01) | 86433, 0.30, 0.50 | 40007, 0.47, 0.54 |
| level5 north of lamp | 194151, 0.36, 0.50 | 60344, 0.44, 0.54 |
| level5 west of lamp | 118761, 0.39, 0.53 | 52107, 0.45, 0.53 |
| level5 north-east of lamp | 98413, 0.31, 0.50 | 51046, 0.47, 0.54 |
| ev (06) | 72155, 0.40, 0.50 | 8483, 0.47, 0.50 |
| kriz (09) | 8834, 0.32, 0.49 | 565, 0.41, 0.49 |
| tnel24 (15) | 119313, 0.46, 0.54 | 2, - , - |
| cam1 (01) | 23282, 0.43, 0.52 | 7773, 0.49, 0.53 |

The level5 counts include chairs and desks (also SHADOW casters). In `ev`, Ebru's shadow from the wall lamp (a native
light 30 units from her) now falls from her feet across the floor toward the television, where the planar shadow was a
blob in the fixed sun direction; in level5 Demir's shadow points away from the 5-light lamp cluster and turns as he walks
round it; tnel24's floor was too dark for any planar shadow and now shows the player's. Captures (private, game assets):
`reverse/rt-actor-shadows/*-{raster,rt}.png`, `*-crop-raster-vs-rt.png` and `darkness-maps-*.png` (raster/RT frames
above, planar/actor darkness below). `k22-render-check --vulkan-rt` checks it on a lightmapped floor with one baked
light: the floor 22 units beside the caster drops from 255 to 126/128 on the side away from the light and the shadow
changes sides with the light; the planar control shadow (128 by default RT, from a copy placed so it falls on the other
side) is never drawn; with the light out of range the floor on the planar light's far side drops to 125/252 (actor shadow
toward `shadowSun`) while the planar control's side stays 252.

Cost (`K22_GPU_TIMING=1`, level5 bench, `ultra`, two runs each): `rt_signals` 0.76 -> 0.99-1.00 ms, GPU frame
3.17-3.18 -> 3.34-3.38 ms, 307 -> 289-292 FPS. Tracing every weighted light instead of the dominant one plus one sampled
light cost 1.10 ms (305 -> 275 FPS).

## Model shadows and ambient occlusion

Every campaign model carries a script material (A6 assigns `mat_model`, `mat_sprite`, `mat_terrain` to entities
without one; `engine.cpp`), so it is lit as an *ordinary* D3D material: Gouraud vertex lighting from the material's
ambient x the floor-light ambient (the A6 lightmap sample under the entity), the sun (`materialSunStrength`) and the
native lights; baked WMB lights do not light it. 0.4.0 marked these pixels unlit/explicit (G-buffer flag 4), so no RT
signal reached any character. They now have their own flag (16):

- `rt_signals.comp` computes the dynamic-light (D3D attenuation) and sun visibility for them against the SHADOW-flag
  models only (instance mask 4: actors, and the receiver itself when it is one, so an arm shadows the torso). In the
  original those lights shone through the level and every prop, and only SHADOW models cast shadows; the level's static
  shadow already reaches them through the floor light they sample. Tracing the level took the sun off every character
  indoors; tracing all models let curtains and radiators block a sun that passes through the walls.
- Ambient occlusion sees all geometry (level and models, the model itself) within 40 units: occlusion =
  1 - (1 - d/40)^2 for a hit at distance d (full darkness at contact, little from distant geometry).
- `scene.frag` `ordinaryOcclusion` takes the occluded light off the vertex lighting: ambient x (1 - AO), the sun x
  (1 - its visibility), other native lights x (1 - their visibility). Where nothing is occluded the original vertex
  lighting is unchanged. Ray origins use the terminator origin above, which keeps self-shadow free of acne.
- Models without a script material (Bonus/A7 profile) and non-lightmapped level surfaces use the per-pixel model
  lighting: the ambient term x AO, the sun and native lights as before, and the baked WMB lights (`enhancedBaked`, the
  model light loop) x their visibility (`bakedVisibility`: the dominant baked light and one other picked by weight,
  traced against all geometry, in the mask alpha, blurred among model pixels only).

`k22-render-check --vulkan-rt` renders a floor model at z=2 under the actor-shadow fixture's camera. With an overhang in
the same model, the floor point in its shadow drops from 78.9 to 27 (luma; the lit side 149.9 -> 149.5) for a baked lamp
(per-pixel model lighting) and from 199.4 to 0 for a native lamp on an ordinary material (SHADOW-flag model); a wall
5 units beside a floor point lowers it from 127 to 90 (per-pixel ambient) and from 128 to 100 (ordinary material).

In the game (`--preset ultra`, `vulkan-rt`, 1920x1080, original saves 01 and 07): Demir in level5 drops from mean luma
27.7 to 26.6 over his body column, Ebru in `ev` (standing by the radiator, under the wall lamp) from 49.3 to 43.6, with
the darkening under the arms, between the legs, at the collar and the hands. Cost (`K22_GPU_TIMING=1`, level5, 900
steady frames per run, two runs each, 0.4.0 -> this): `rt_signals` 1.01-1.03 -> 1.13 ms, scene 0.52-0.53 -> 0.56 ms
(`ordinaryOcclusion`), GPU frame 3.33-3.38 -> 3.49-3.51 ms; the planar shadow draws are gone.

Lightmapped surfaces keep the models-only occlusion term: their lightmaps already hold the level's occlusion.

## Performance

Measured 2026-10-10 on an RX 7900 XTX (RADV, Mesa 26.2.4): Demir in level5 (original save slot 01), standing,
1920x1080, `--preset ultra` (MSAA 4, PBR, RT shadows/GI/rough reflections, denoise, bloom, TAA, volumetric fog,
smooth normals, tessellation), immediate present, nested in a headless gamescope. `K22_GPU_TIMING=1` writes Vulkan
timestamps between the renderer's passes and CPU frame times to stderr, averaged over 300 frames (`K22_FRAME_TIMING=FILE`
writes one line per frame on any backend: frame, poll+scenario, tick, draw and finish_frame microseconds, live particles, seamless-door phase and held flag). GPU ms per pass:

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

### Second pass (2026-10-17)

Same RX 7900 XTX/RADV, headless gamescope with immediate flips, the user's settings (`vulkan-rt`, MSAA 2, PBR,
`shadows=rt`, `gi=1`, `reflections=rough`, `denoise=1`, bloom, SSAO, FXAA, TAA, volumetric fog, smooth normals,
tessellation, texture upscale, sharpen, `fsr=quality`, anisotropy 16, `fps_limit=240`), frame generation off,
1500 steady frames after a save load (`realtime off`). GPU ms from `K22_GPU_TIMING=1`; fps uncapped.

| View (save) | Output | GPU ms before -> after | scene pass ms | fps before -> after |
| --- | --- | ---: | ---: | ---: |
| kriz infirmary (8) | 2560x1440 | 8.85 -> 5.73 | 3.90 -> 0.64 | 110 -> 169 |
| kriz infirmary (8) | 3840x2160 | 17.84 -> 12.10 | 6.92 -> 1.14 | 52 -> 75 |
| kriz, frame generation on | 2560x1440 | 10.05 -> 6.76 | 4.04 -> 0.63 | 96 -> 134 |
| level5 (0) | 2560x1440 | 5.64 -> 5.68 | 0.77 -> 0.80 | 166 -> 172 |
| house (5), tunnel 24 (14) | 2560x1440 | 7.12 -> 7.11, 4.80 -> 4.81 | unchanged | 136 -> 137, 201 -> 199 |

Other views move within the +-2% run-to-run noise. The cost was inline ray queries in `scene.frag` (fragment shaders
run them slowly), not the compute passes:
- **Overdraw rays.** Fragments that fail the G-buffer prepass depth comparison traced their own `shadows=rt` rays
  before the depth test discarded them. A fragment behind the prepass at all 3x3 neighbour pixels now traces nothing.
- **Repeated rays.** The legacy, dynamic-light and PBR loops traced the same light ray up to three times per
  fragment; each light/sun ray is now traced once (8-bit cached visibility), and lights with N.L <= 0 trace nothing.
- **Edges and decals.** MSAA edge fragments that continue a neighbour's prepass plane, and decals (blood sprites)
  within 2 units of the prepass surface, reuse that pixel's denoised RT signal. Captures (static and walking) differ
  from before by at most 11/255 on single edge pixels.
- TAA no longer runs under FSR (its output was never read).

Bound: GPU. CPU recording is 0.9-2.5 ms (5 ms in the house, save 5) and the frame waits on the previous fence for the
rest. One frame is submitted while the next records (`limit` 1 windowed, 2 for `TRIPLEBUFFER` exclusive fullscreen).
`fps_limit=240` sleeps only below its 4.17 ms period, and a numeric limit keeps vsync (FIFO), so the display refresh
(or VRR) caps it. `display=native` on a 3840x2160 desktop renders at 2560x1440 under `fsr=quality`. The remaining
cost at 4K output is `rt_signals` (4-5 ms) and the denoiser (2.8 ms) at that render size.

Validation (Khronos layer, sync validation; kriz bench with and without frame generation, `k22-render-check
--vulkan/--vulkan-rt`): 0 messages, down from three kinds on main (about one each per frame):
`SYNC-HAZARD-WRITE-AFTER-WRITE` on the MSAA depth resolve (its barrier lacked the resolve's color-output stage),
`Undefined-Value-StorageImage-FormatMismatch` on the FSR reactive mask (now R32_SFLOAT, the shader's `r32f`), and
`VUID-vkDestroyImageView-imageView-01026` (FidelityFX local patch 8: the backend recycles dynamic views per dispatch,
and reactive mask + upscale per frame freed the previous frame's views while it ran).

## Limits

- Sun shadows (`shadows=rt` and `shadows=shadowmap` alike) make non-lightmapped models indoors much darker
  than the original. The sun is the main light on those models, and ceilings occlude it, for example the
  bed in save 8 and the pit grate in save 26. In the original the sun was never occluded. Ordinary-material models
  (D3D-lit, flag 16) are exempt under `shadows=rt`: only SHADOW casters occlude their sun.
- A light placed inside solid level geometry (as in the Start4 test scene) is still occluded by
  `shadows=rt` for receivers whose path crosses another brush toward it.
- One bounce, one sample per pixel; reflections hit with legacy shading.
- The TLAS is rebuilt per camera when its instance count changes (else updated), BLAS refit for changed poses, without compaction.
- Particles, view-space entities and billboards are not in the acceleration structure.
- With `tessellation=1` the BLAS still holds the base mesh: ray-traced shadows, GI and reflections see model
  silhouettes without the PN bulge.
- Actor shadows on lightmapped surfaces come from SHADOW casters only. Ordinary-material models (every campaign model)
  are shadowed only by SHADOW casters (themselves included) from their D3D lights; props without the flag occlude only
  their ambient (AO). Decal models (the level5 blood pool) take actor shadows as models do, not as the floor below.
- Mirror/portal views have no actor shadows (the planar shadow is not drawn there either; the game sets `noshadow` on its
  mirror view); SHADOW entities that are themselves invisible are not in the acceleration structure and cast none
  (the scripts turn SHADOW off with INVISIBLE). Translucent and MSAA-edge pixels keep the inline path without actor
  shadows or AO. A baked light inside solid geometry casts no actor shadow (its level ray is blocked).
  `shadows=shadowmap` has no actor shadows from baked lights (one cube map for one dynamic light).
- Ordinary materials subtract the occluded light from their clamped Gouraud lighting, so a vertex saturated above 1
  darkens less than the occluded share; their specular is not shadowed.
