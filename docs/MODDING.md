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
