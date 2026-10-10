# Texture restoration (optional high-resolution packs)

Kabus 22 ships 2006-era textures (mostly 256 px and below, many 8-bit palettised). The native engine can
overlay a **texture pack** on top of the original textures. Packs are optional, live outside the game
directory, and never modify original assets: without a pack the engine renders exactly the original data.

`tools/texture-upscaler/upscale.py` turns the engine's own texture dump into such a pack.

## Techniques investigated

| Technique | Kind | Strengths | Problems for this game |
|---|---|---|---|
| **Real-ESRGAN** (`realesrgan-x4plus`, ncnn/Vulkan build) | GAN super-resolution, trained on synthetic real-world degradations | Convincing detail on photographic surfaces (concrete, rock, foliage, sky); runs on any Vulkan GPU without PyTorch | Hallucinates structure: invents letters, rivets, symbols; can shift colours; tiles get seams unless padded; native model is 4× only |
| **ESRGAN** (original + community models) | GAN super-resolution | Many game-texture-specific models exist | Same hallucination risk; most community models have unclear licences and need PyTorch; no reproducible baseline |
| **SwinIR** | Transformer restoration (PSNR and GAN variants) | PSNR variant is the most faithful learned upscaler; little hallucination | PyTorch + large GPU memory, slow on thousands of textures; GAN variant has the same issues as ESRGAN |
| **Lanczos (+ mild unsharp mask)** | Classical resampling | Deterministic, never invents content, exact colour statistics, fast | Softer than learned methods; ringing on hard edges |
| **Nearest** | Classical | Bit-exact (box-downscale reproduces the original), keeps alpha-test coverage exactly | Blocky |
| **Scale2x / EPX ("xbr-like")** | Pixel-art edge-directed scaling | Smooths diagonal staircases while only ever emitting colours that already exist in the source | Only for flat/low-colour art; repeated for 4× |

Learned models are optional because they cannot be redistributed with the project and are not needed
for a correct game. The tool calls an external `realesrgan-ncnn-vulkan` only when it is on `PATH`; it
is reported as unavailable otherwise (and `--method realesrgan` then fails instead of silently falling
back). SwinIR/ESRGAN were not integrated: they add a PyTorch dependency for no safety benefit over the
guarded Real-ESRGAN path.

## Policy per category

Categories come from the engine dump (`index.tsv`).

| Category | `--method auto` | Why |
|---|---|---|
| `ui`, `font` | Scale2x (`xbr-like`) | Turkish text, digits and HUD symbols must stay exactly as drawn; never AI, never blurred |
| any texture with ≤16 distinct colours, or listed via `--protect FILE` | Scale2x | Treated as text/signage/flat art |
| `world`, `sky` | Real-ESRGAN if available, else Lanczos + mild unsharp; wrap-padded so tiling stays seamless | Photographic surfaces |
| `model` | Real-ESRGAN if available, else Lanczos + mild unsharp; edge-padded (UV atlases do not tile) | |
| `sprite` | Real-ESRGAN if available, else Lanczos; **alpha always nearest** | Alpha-test coverage must not change |
| `other` | Lanczos | Unknown content, stay conservative |

An explicit `--method lanczos|nearest|xbr-like` applies to every texture; `--method realesrgan` still
never touches `ui`/`font`/protected textures.

## Safety rules (enforced by the tool)

* **Pure integer scale.** Output is exactly `original × s` (`s` = 2 or 4; reduced per texture to stay
  ≤ 8192 px). No crop, pad or aspect change in the result, so UVs, lightmaps, collision and gameplay
  are unchanged — packs only change sampled colours, never geometry.
* **Consistency guard.** Every result is box-downscaled back to the original size and compared with the
  original pixels. If the mean absolute error exceeds `--max-error` (default 6/255) or any channel's mean
  shifts more than `--max-shift` (default 1.5/255), the result is discarded and the next method is tried:
  AI → Lanczos → nearest (nearest is exact by construction). This rejects hallucinated symbols, invented
  text and colour drift. Only visible texels (alpha > 0) count for RGB.
* **Alpha.** For Lanczos/Real-ESRGAN alpha is nearest-scaled, so alpha-test coverage is identical at every
  threshold. Before resampling, visible colours are bled a few texels into fully transparent texels, so
  colour-key RGB (e.g. magenta) never fringes visible edges; straight alpha is kept (premultiplying and
  dividing was tried and amplified noise in soft-alpha effect skins, failing the guard).
  Scale2x scales whole RGBA texels, so alpha only takes values present in the original.
* **Key check.** Each `<key>.png` is re-hashed from its decoded pixels; a mismatch with the filename (or
  with `index.tsv` dimensions) excludes that texture and makes the run exit 1.
* **Path safety.** Pack paths are always `textures/<key>[_suffix].png`; the validator rejects absolute
  paths, `..`, and symlinks that resolve outside the pack. `--force` only replaces a directory that is
  already a texture pack; the pack is written to a temporary sibling and renamed into place.
* **No redistribution.** Packs are derived from the original game art; the default licence line says
  personal use only. Do not publish packs built from the commercial textures.

## Optional PBR maps (`--pbr`)

For `world`/`model` textures (not protected ones) the tool derives conservative maps from the upscaled
albedo:

* `normal`: Sobel gradient of a lightly blurred luminance height field, strength 2, tangent space,
  OpenGL convention (+Y up: green > 128 means the surface faces up the texture), wrap sampling.
* `roughness`: 0.75–1.0 from local luminance contrast (mostly rough; no fake gloss).
* `ao`: subtle cavity term (at most 20 % darkening).
* `metallic`: not written; the engine's default is 0.

These are approximations from colour, not measured materials; leave `--pbr` off for the original look.

## In-engine `texture_upscale` (Vulkan, no pack needed)

`--graphics texture_upscale=1` (Vulkan raster and RT; presets `enhanced`/`ultra`) applies the same
non-AI `--method auto` policy at texture upload, on the CPU, without writing anything to disk:

* Only world and model material skins with both sides ≤ 512 px, 2× (`k22::upscale_texture_2x`).
  Lightmaps, sky strips, UI, fonts, particles, dynamic/turbulent images and textures that a pack
  replaces keep their original upload. UV policy keeps the original dimensions.
* ≤ 16 distinct colours: Scale2x. Otherwise separable Lanczos-3 plus the tool's mild unsharp mask
  (35 %, threshold 2); world textures wrap (tiling), model atlases clamp to the edge.
* RGB is resampled after the same 4-texel colour bleed; alpha is always nearest-scaled, so alpha-test
  coverage is identical at every threshold (also for Scale2x, unlike the offline tool's whole-texel
  Scale2x).
* Mipmaps are generated from the upscaled level 0. PBR normal/ORM maps only come from packs, and a pack
  hit skips the upscale, so derived maps always match their albedo.
* Cost (RX 7900 XTX host, Release, row bands on all cores): all 1792 dumped world/model textures
  ≤ 512 px take 3.9 s in total, 2.2 ms mean, 10 ms worst. Demir level5 (`kayit0.sav`) load to first
  frames goes from 3.50 s to 3.58 s on Vulkan RT and raster (three and two runs). The result is not
  cached on disk; it lives as long as the image's GPU texture.

`k22-render-check --policy` checks exact 2×, coverage at alpha 1/3/128/255, that a magenta colour key
never reaches visible texels, the box-downscale guard (≤ 6/255 mean error, ≤ 1.5/255 shift) and that
Scale2x emits only source colours.

## Usage

```sh
# 1. dump the original textures (decoded RGBA, lossless) from your installed game
kabus22 --dump-textures /tmp/k22-dump --data ~/.local/share/kabus22/game
# 2. build a 2x pack into ${XDG_DATA_HOME:-~/.local/share}/kabus22/packs/hd2x
python3 tools/texture-upscaler/upscale.py /tmp/k22-dump --name hd2x --scale 2 [--pbr] [--method auto]
# 3. validate (also checks the integer-scale rule against the dump)
python3 tools/texture-upscaler/upscale.py --validate ~/.local/share/kabus22/packs/hd2x /tmp/k22-dump
# 4. play with it
kabus22 --texture-pack hd2x
```

`kabus22 --list-capabilities` lists installed packs with `valid` and a reason; the launcher's graphics
page selects packs the same way. Several `--texture-pack` options may be given; the first pack that
contains a key supplies all of that texture's maps (kinds are not mixed across packs). `--preset
enhanced|ultra` without `--texture-pack` uses every valid installed pack, in directory-name order.
Requirements: Python 3, Pillow, numpy; optionally `realesrgan-ncnn-vulkan` with its `models/` directory.
Other options: `--output DIR`, `--display-name`, `--license`, `--protect FILE`, `--jobs N`, `--force`.

## Pack format (summary of the engine contract)

```
kabus22-texture-pack 1
name\t<display name>
license\t<text>
<key>\t<albedo|normal|roughness|metallic|ao>\t<relative/path.png>
```

`key` is lowercase 16-hex FNV-1a-64 (offset `0xcbf29ce484222325`, prime `0x100000001b3`) over the
width (LE uint32), height (LE uint32) and the engine-decoded RGBA8 bytes, top row first. Reference
values: 0×0 image → `a8c7f832281a39c5`; 1×1 RGBA (0,0,0,0) → `7e1ef442cd317605`. Files are PNG, at most
8192 px per side and 64 MiB, sized `original × s` with `s ∈ {1,2,4,8}` and the same `s` for every kind
of one key. Normal maps are RGB tangent space (+Y up); roughness/metallic/ao use the red channel.

Tests: `python3 tools/texture-upscaler/test_upscale.py` (also registered in CTest as `texture_upscaler`).
