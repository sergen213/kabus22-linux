# Mods

A Kabus 22 mod is a folder of game files that replace (or add) original resources by name: models
(`.mdl`), levels (`.wmb`), images, sounds, music, movies and the campaign's WDL scripts. Nothing is
patched on disk; the engine looks a mod's files up before the installed game's.

Texture packs are a separate, hash-keyed PBR system — see [TEXTURE_RESTORATION.md](TEXTURE_RESTORATION.md).

## Where mods live

`${XDG_DATA_HOME:-~/.local/share}/kabus22/mods/<id>/` — one directory per mod; the directory name is
its id (not empty, at most 128 bytes, no `/`, no leading `-`, not `.`/`..`, no control characters).

```
mods/better-demir/
  mod.txt
  data/demir_2007.mdl
  data/sounds/whatever.wav
  readme.txt            (anything outside data/ is ignored by the game)
```

## mod.txt

```
kabus22-mod 1
name<TAB>Better Demir
version<TAB>1.0
author<TAB>someone
description<TAB>One line about the mod
```

The first line must be exactly `kabus22-mod 1`. Then `key<TAB>value` lines; `name` is required and
not empty, `version`, `author` and `description` are optional; each key at most once; blank lines are
ignored and CRLF line ends are accepted. Anything else makes the mod invalid. `mod.txt` must be a
regular file (not a symlink) of at most 64 KiB, and `data/` must exist (it may be empty).

## data/

Each file under `data/` replaces the game resource with the same relative name, compared like the
game does: case-insensitive, `\` and `/` equivalent (`data/Demir_2007.MDL` replaces `demir_2007.mdl`).
A name the game does not have is added. Symlinks are ignored. Two files in one mod with the same
name after that normalization make the mod invalid. Archives themselves (`K22.wrs`, `Kaynak.k22`)
cannot be replaced — replace their members instead.

To get the original files to edit, export them in exactly this layout:

```sh
kabus22 --export-files /tmp/k22-files --data ~/.local/share/kabus22/game   # [--mode bonus]
```

`OUT` must not exist (or be empty) and must not be inside the game directory.

## Using mods

The launcher's Mods page enables mods and sets their order. On the command line:

```sh
kabus22 --data GAME_ROOT --mod better-demir --mod other-mod
```

`--mod` is repeatable, in priority order: when two mods provide the same file the **first listed wins**.
Mod files take precedence over loose game files and archive members. A missing or invalid mod named
by `--mod` stops the engine with the reason; each loaded mod is logged as `kabus22: mod <id>: <N> files`.
Without `--mod` no mod is used.

- **Scripts:** campaign WDL scripts (`K22.wdl`, `K22Widescreen.wdl` and everything they include) can be
  replaced. Bonus mode scripts stay archive-only; other Bonus resources can be modded.
- **Saves:** a save records the hash of the entry script it was made with. Saves made with a mod that
  changes scripts may not load correctly without it, and the other way round.

## Models (Blender)

A mod can replace a model with a glTF 2.0 file: `data/<dir>/<stem>.gltf` (with its `.bin` and images
beside it, as relative URIs) or a self-contained `data/<dir>/<stem>.glb` replaces `<dir>/<stem>.mdl`
(case-insensitive). A mod containing both `<stem>.mdl` and `<stem>.gltf`/`.glb` in the same directory,
or both `<stem>.gltf` and `<stem>.glb`, is invalid. The game's own `.mdl` must exist: it supplies the
animation, the frame names, the collision box (frame 1, as in A6), the flags and the shadow LOD
companions (`*_1.mdl`…), so gameplay is unchanged.

Export a model as the engine loads it (with `--mod`, a mod's replacement as the engine sees it):

```sh
kabus22 --list-models --data ~/.local/share/kabus22/game [--mode bonus] [--mod ID ...]   # <name>\t<original|mod id>
kabus22 --export-model demir_2007.mdl /tmp/demir --data ~/.local/share/kabus22/game        # [--mode bonus] [--mod ID ...]
```

This writes `/tmp/demir/demir_2007.gltf`, `demir_2007.bin` and one PNG per skin (`demir_2007_skin1.png`, …).
Import it in Blender with *File → Import → glTF 2.0*, edit, and export with *File → Export → glTF 2.0*
into the mod's `data/` (*glTF Separate* or *glTF Binary*), under the original's name.

- **Axes and scale:** 1 glTF unit = 1 A6 quant. glTF is Y-up: glTF `(x, y, z)` = A6 `(x, z, −y)`, so in
  Blender (Z-up) the coordinates are exactly the game's. The mesh is the model's first frame.
- **Animation:** every A6 frame is a morph target (Blender shape key) named after the frame
  (`tBekle0`, …; also listed in `mesh.extras.targetNames`). If the exported file has **exactly as many
  shape keys as the original has frames**, in the same order, they are the animation: edit every shape
  key you need (an edit to the Basis alone does not move the other keys), and keep *Shape Keys* enabled in
  the exporter. The vertex count and topology may change, as long as the shape keys come along.
  Otherwise (no or other shape keys, e.g. a new mesh) the model is **wrap-deformed** on the original's
  animation: each vertex follows the original's nearest triangles, so the mesh should sit on or near
  the original's first-frame surface. Parts far from it (a long coat tail) follow whatever they are
  nearest to.
- **Textures:** a material named `skinN` (N = A6 skin number, 1-based) with a base colour texture
  replaces skin N's bitmap; `skinN` without a texture keeps the original skin N. Other materials become
  extra skins after the original ones (white if untextured). Images are PNG or JPEG files (or GLB
  buffer views); `data:` URIs are not supported. As in A6, a campaign model draws every triangle with the
  entity's current skin, so per-material textures only matter as skins.
- **Not used:** glTF normals (the engine computes smooth normals per frame), skins/bones and glTF
  animations, cameras and lights. Node transforms are applied. Only triangle primitives are accepted.
- **Limits:** a warning above 20 000 triangles (the original characters have about 2 400); at most
  1 000 000 vertices and 64 million vertex×frame morph values.

## Model viewer

The launcher's Mods tab has a **3-D viewer** page beside the mod details. Its list shows the selected
mod's model files (`.mdl`, `.gltf`, `.glb` under `data/`); **Show originals** adds every game model
(`kabus22 --list-models` for the Launch tab's game mode) and marks the originals the selected mod
replaces. Selecting an entry runs `kabus22 --export-model` — with `--mod <id>` for a mod's file, so a
replacement is shown exactly as the engine loads it — into
`${XDG_CACHE_HOME:-~/.cache}/kabus22/viewer/<mod id or _original>/<name>/` (Bonus: `…/<mod id or
_original>/@bonus/<name>/`). The export is reused while it is newer than the engine and every file of
the mod; deleting the cache folder is always safe.

The view draws the first frame with its skins and a head light: drag to orbit, wheel to zoom,
double-click to reset. The slider scrubs the animation frames (frame name shown below it) together with
the vertex, triangle and frame counts.

- **Export for Blender…** copies the shown export (`.gltf`, `.bin`, skin PNGs) into a folder you choose;
  see [Models (Blender)](#models-blender).
- **Replace model…** (a mod must be selected; otherwise it offers to create one) asks for a `.gltf`,
  `.glb` or `.mdl` for the selected model and copies it into the mod as `data/<dir>/<stem>.<ext>`
  together with the `.bin` and images its relative URIs name, at the same relative place. An existing
  file of the same name (any letter case) is overwritten in place. Absolute, `scheme:` and `../` URIs
  are refused and listed. A confirmation lists the copies, the files overwritten, the mod's other file
  for the same model (`.mdl`/`.gltf`/`.glb`, removed — one file per model) and warnings (no mesh, more
  than 20 000 triangles, a URI's file missing). The viewer then shows the mod's version.
- **Remove replacement** deletes, after a confirmation listing them, the mod's file for the selected
  model and the side files of its glTF that no other glTF in the mod names; emptied folders are removed.
