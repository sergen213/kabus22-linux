# Kabus 22 native installer (`kabus22-setup`)

`kabus22-setup` installs the native Linux port from your own original game media. No game
asset is shipped. Wine/Proton is never used: neither the installer nor the finished game runs
any Windows program. Windows files are only read as data (the original `K22.exe` is parsed,
never executed, to recover the engine font).

## Workflow

1. **Select the source** — `--source PATH` (or the GUI "Game media" field) accepts the original
   installer or an installed Windows game directory. `--iso`/`--setup` remain as explicit routes.
2. **Detect the edition** — the source type is detected by content, then the edition is derived
   from the game files and recorded in the inventory (`edition`):
   - `tr-en-de` — ISO version (TR/EN/DE text archives `yazi_*.k22`),
   - `merscom` — Merscom international release (`turkce.k22` language archive),
   - suffix `+bonus` when the A7 Bonus mode (`Kabus 22 Mode/K22_mode.wrs`) is present,
   - suffix `+kabus22patch` when Kabus22Patch files are present (`K22Widescreen.wrs`, `_data/`, `kabus_dll.dll`).
3. **Validate source files** — ISO9660 magic, the complete Inno `setup.exe` + `setup-N.bin` slice
   chain and its Inno MD5 listing, or for directories a pinned no-symlink snapshot of every file
   (identity, size, timestamps re-checked while and after copying). Paths are checked for traversal,
   control characters and case-fold collisions.
4. **Extract game assets** — ISO members via libarchive, Inno archives via the bundled native
   `innoextract`, directories by direct copy, all into a private staging directory next to the
   destination.
5. **Convert required resources** — the original engine font is recovered from `K22.exe` into
   `game/engine/_a4font.pcx` (+ provenance JSON), the desktop icon is encoded from the original
   logo, and the original setup's Inno wizard images become `wizard-image.png` /
   `wizard-small-image.png` in the install root (ISO and `setup.exe` sources only; cosmetic, skipped
   silently when unavailable).
6. **Install native binaries** — `kabus22`, `kabus22-launcher`, `kabus22-updater` and bundled
   libraries/notices go into `versions/<version>/`.
7. **Validate the installation** — extracted members are checked against the Inno MD5 listing;
   SHA-256 inventories are recorded for game files and for the version payload. `--verify` re-checks
   them at any time.
8. **Create desktop entries** — `kabus22-native.desktop` in `$XDG_DATA_HOME/applications` and the
   XDG desktop directory, both running `<root>/bin/kabus22-launcher --install-dir <root>`.
9. **Enable Play** — the settings launcher starts the native engine with `--data <root>/game`.

## Look

The GTK installer reproduces the original Inno Setup 5 wizard of the Windows ISO: a
"Select Setup Language" dialog (Deutsch / English / Türkçe, preselected from the locale), then
Welcome → game media → Select Destination Location → Select Additional Tasks (optional community
extras) → Ready to Install → Installing → Completing, with Back / Next / Cancel and an
"Exit Setup?" confirmation. Welcome and Completing show the original large wizard image, inner
pages the small one in their white header strip. The images are never bundled: they are read from
the user's own setup header (`media::wizardImages`: the CRC-checked, LZMA-compressed `setup-0`
block, whose WizardImageFile and WizardSmallImageFile follow the entries as two consecutive
length-prefixed BMPs) while the media page previews the disc, and copied into the install root on
install. A reinstall shows them from the existing root right away; otherwise the wizard starts with
a plain gradient panel. The settings launcher uses the same style (`installer/wizard_style.hpp`,
GTK CSS on a fixed light Adwaita theme) and the root's `wizard-small-image.png` in its header;
installations made before this change simply have no header image. Reference screenshots of the
original (Wine, private Xvfb) live in `reverse/original-installer/` (local only, not in git).

The media-page preview doubles as the media check: Next on that page stays disabled while it runs
(status line shows its stage) and is enabled only once it succeeds for the current path. Browse,
Enter in the path field or `--source` start it; a failure, Cancel or a changed path keeps Next
disabled.

For an ISO the preview stages the setup slices (~2 GB) once, and the install reuses them instead of
reading the ISO again. They live on disk in a private `$XDG_CACHE_HOME/kabus22/preview-XXXXXX`
(default `~/.cache`, never `/tmp`, which is often RAM), owned by the wizard under an exclusive `flock`
on its `.lock`. The directory is removed when the path changes, the preview fails or is cancelled, the
install succeeds or the wizard exits; a failed install keeps it for Back and retry. Each new preview
first removes `preview-XXXXXX` directories of ours whose lock is free (crashed or killed
installers). The install reuses them only for the same ISO path with unchanged device, inode, size
and mtime, from a private (0700) directory of ours outside the destination. Otherwise it stages
again (progress shows "Staging original ISO setup slices" rather than "Reusing original ISO setup
slices staged by the media preview"). Listing and extraction still revalidate the slice set, sizes,
setup header and Inno checksums. `--unattended` stages once inside the install staging directory,
as before.

## Supported sources

| Source | Detection | Notes |
| --- | --- | --- |
| ISO image | `CD001` at byte 32769 | must contain exactly one Inno setup + contiguous `-N.bin` slices |
| Original `setup.exe` | `MZ` header + sibling `<stem>-1.bin` | a folder containing exactly one such pair (mounted/copied disc) is accepted too |
| Installed Windows game folder | `K22.wrs` + `K22.exe` (case-insensitive) | e.g. a Lutris/Wine prefix; read only |

From a Windows folder only the file set that an ISO install produces is imported (`.k22`, `.wrs`,
`.dat`, help JPG/HTML, gallery JPG, menu OGG) plus Kabus22Patch data (`_data/` native asset types,
`K22.ini`, `K22Widescreen.wrs`). Deliberately skipped, and why:

- `*.exe`, `*.dll`, `acknex.wdf`, `*.url`, `readme.txt` — Windows-only components, not used by the native engine (`K22.exe` is read once for the font only);
- `kayit*.sav`, `kysd.sav`, `_data/saves/` — saves are user data, not game data;
- `ftz.k22`, `rms.k22`, `rmo.k22` — runtime-written state that is not on the original media;
- `unins*` — Inno uninstaller log/program;
- ReShade/DXVK files and any symlinks or special files that are not game data.

`options.k22` and `mdc.k22` are original media files and are imported (from a played Windows
install they carry its settings as defaults).

## Transactions

Installation builds the complete tree in `<parent>/.kabus22-setup-XXXXXX`, then publishes it:

- missing destination: one non-replacing `renameat2(RENAME_NOREPLACE)` of the whole tree;
- empty folder or earlier Kabus22 installation (has `.kabus22-owned`): reinstall. Each top-level entry
  the new installation owns (`game/`, `versions/`, `current`, `bin`, `lib`, `share`, `kabus22.png`,
  `.kabus22-owned`, `.kabus22-inventory.json`) is swapped in with `renameat2(RENAME_EXCHANGE)`
  while holding the updater lock; the old entries are deleted only after everything committed.
  Every other entry in the folder — saves, `options.k22`, Bonus settings, launcher state, texture
  packs, which default to this same `${XDG_DATA_HOME:-~/.local/share}/kabus22` folder — is never
  moved or touched, so a reinstall keeps them. Refused while setup itself runs from that installation;
- any other non-empty folder (e.g. home or Documents) is refused untouched, foreign data is never
  deleted. The folder must be owned by you and not group/other-writable.

Existing `kabus22-native.desktop` shortcuts (file or symlink) are replaced the same atomic way; a
directory in their place is refused. Any failure or cancellation (SIGINT/SIGTERM) restores the old
installation and shortcuts (or removes only what this run created); files changed concurrently are
preserved and reported. Sources are never written.

## Layout

```
<root>/.kabus22-owned, .kabus22-inventory.json
<root>/game/                         original assets (never touched by updates)
<root>/versions/<version>/{bin,lib,share}/ and .kabus22-payload.json
<root>/current -> versions/<version>
<root>/bin, lib, share -> current/…  compatibility links used by desktop entries
<root>/versions/.state
```

Saves and options written by the engine may live directly in `<root>`; installer, repair and
updater only touch `game/` (repair), `bin`, `lib`, `share`, `current` and `versions/`.

`.kabus22-inventory.json` lists `game/` and root files (`path`, `size`, `sha256`) plus `source`,
`source_kind`, `edition` and `version`. Each `versions/<v>/.kabus22-payload.json` lists that
version's payload, so `--verify` stays correct after an update switches `current`. Legacy flat
inventories (v0.1.x) still verify; their `bin/lib/share` rows are ignored once the root is versioned.

## Verify and repair

```
kabus22-setup --verify [--install-dir ROOT] [--json]
kabus22-setup --repair [--install-dir ROOT] --source PATH [--unattended] [--extractor INNOEXTRACT]
```

`--verify --json` prints `{"ok":bool,"checked":N,"missing":[…],"modified":[…]}` (root-relative
paths; payload rows as `versions/<v>/…`). Exit 0 when everything matches, 3 on mismatch, 1 on error.

`--repair` re-runs verify, then fetches only the missing/modified `game/` files from the source into
a private staging directory inside the root, checks each against the inventory's size and SHA-256,
and atomically renames it into place. Bytes that differ from the inventory (e.g. a different
edition) are not written and stay reported. Saves, options, launcher settings and `versions/` are
never touched; payload damage is the updater's job. Progress lines `progress <0-100> <message>` go
to stdout; the final verify decides the exit code (0 clean, 3 mismatches remain).
