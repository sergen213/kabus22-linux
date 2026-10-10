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
- **Composite** (`pbr`, `bloom`, `taa`, `fxaa`): background views render into an RGBA16F target. With
  PBR off the values stay LDR and the tone curve is identity (bloom/TAA/FXAA work in LDR). Bloom: soft
  threshold 0.7 (knee 0.2), 5-level down/up chain, strength 0.15. On saves 1 and 21 (dark streets) the
  capture is unchanged. On save 26 the torch and the white poster glow and the mean rises 13.21 -> 13.58.
  The old 0.85/0.08 setting changed only 0.9% of pixels there and was not perceptible. In LDR the
  threshold cannot tell light sources from white paper. TAA (Vulkan): Halton(2,3) jitter, reprojection
  through the previous view-projection, 3x3 neighborhood clamp, blend 0.1. Consecutive frames of a
  walking player under the following camera (save 19) show no ghost trail, only slight softening. FXAA
  was chosen over SMAA: single pass, no lookup textures, and MSAA already resolves geometric edges.
- Upscaling: not implemented (not exposed).

## Not implemented

A prefiltered sky cubemap for IBL, PBR in ray-traced reflection hits, mirrored-UV bitangent signs, TAA on
OpenGL, upscaling.
