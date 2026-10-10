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
    on non-lightmapped surfaces. Without `shadows=rt` the ray-traced backend does not occlude any light:
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
  sun with an actor and that the default RT path lights exactly like Vulkan raster.
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

## Limits

- Sun shadows (`shadows=rt` and `shadows=shadowmap` alike) make non-lightmapped models indoors much darker
  than the original. The sun is the main light on those models, and ceilings occlude it, for example the
  bed in save 8 and the pit grate in save 26. In the original the sun was never occluded.
- A light placed inside solid level geometry (as in the Start4 test scene) is still occluded by
  `shadows=rt` for receivers whose path crosses another brush toward it.
- One bounce, one sample per pixel; reflections hit with legacy shading.
- The BLAS/TLAS are rebuilt per frame/camera (BLAS refit for changed poses) without compaction.
- Particles, view-space entities and billboards are not in the acceleration structure.
