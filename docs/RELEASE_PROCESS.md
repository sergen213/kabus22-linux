# Release process

How Kabus22 Linux versions are numbered, built, signed, published, updated and rolled back.
Original game media is **never** part of any release artifact (see [What never ships](#what-never-ships)).

## Versioning

- Semantic versioning `MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]`.
- Single source: `project(... VERSION X.Y.Z)` in both `CMakeLists.txt` and `installer/CMakeLists.txt`
  (keep them equal). CMake passes it to every native and installer target as `K22_VERSION="X.Y.Z"`;
  `--version` of `kabus22`, `kabus22-launcher`, `kabus22-setup` and `kabus22-updater` reports it.
- Git tag `vX.Y.Z` (stable) or `vX.Y.Z-rc.N` / `-beta.N` (experimental). The release workflow refuses
  a tag whose `X.Y.Z` differs from the CMake project version.

## Channels

| Channel | GitHub release | Signed manifest `channel` | Offered to |
| --- | --- | --- | --- |
| `stable` | normal (not prerelease) | `stable` | everyone (default) |
| `experimental` | prerelease | `experimental` | users who set `channel=experimental` |

Drafts are never offered. A stable-channel updater additionally refuses a manifest whose **signed**
channel is not `stable`, so the unsigned GitHub prerelease flag alone cannot push experimental builds
to stable users.

User settings: `${XDG_CONFIG_HOME:-~/.config}/kabus22/updates.ini`

```ini
[Updates]
repository=sergen213/kabus22-linux
channel=stable
```

## Installation layout

```
<root>/game/                    original extracted assets — never touched by updates
<root>/versions/<v>/{bin,lib,share}/   immutable per-version payload (+ .kabus22-payload.json inventory)
<root>/current -> versions/<v>  relative symlink, swapped atomically
<root>/bin|lib|share -> current/…      compat symlinks (desktop entries use <root>/bin/kabus22-launcher)
<root>/versions/.state          active=<v> / previous=<v>
```

`installer/layout.{hpp,cpp}` (`k22::layout`) is the only code that changes this layout. It only creates,
moves or deletes `bin`, `lib`, `share`, `current` and entries inside `versions/`; saves (`kayit*.sav`),
options and every other root entry stay untouched, even when the root is also the engine save root.
Activation is crash-safe: a temporary symlink is created inside `versions/` and `rename(2)`d over
`current`; `.state` is written afterwards by temp-file + `rename(2)`; a crash between the two is
detected (`current` wins). Legacy flat installs (real `bin/lib/share` directories) are migrated into
`versions/<updater version>` on the first `--apply`; migration is idempotent and resumable.
Only the active and previous version (plus the version the updater runs from) are retained.

## Update artifacts and signing

Every release carries three update assets:

| Asset | Content |
| --- | --- |
| `kabus22-update.json` | `{"format":1,"version","channel","min_glibc","payload":{"name","sha256","size"},"components":[…]}` |
| `kabus22-update.json.sig` | base64 of the raw 64-byte Ed25519 signature over the exact JSON bytes |
| `kabus22-<v>-linux-x86_64.tar.gz` | deterministic `bin/ lib/ share/` tree for `versions/<v>` |

Tools (`tools/release/`, require Python 3 with `cryptography`):

```sh
# Payload + manifest from an assembled/extracted AppImage AppDir (or --tree DIR with bin/ lib/ share/)
python3 tools/release/make_update_manifest.py --appdir AppDir --version 0.2.1 --channel stable --output-dir dist
# Sign (key file or env KABUS22_UPDATE_SIGNING_KEY = base64 raw 32-byte seed)
python3 tools/release/sign_manifest.py dist/kabus22-update.json --key ~/.local/share/kabus22-release-keys/update-ed25519.key
python3 tools/release/sign_manifest.py --public-key --key KEYFILE      # print the matching public key
python3 tools/release/sign_manifest.py --generate-key NEWKEYFILE       # new 0600 key; prints public key only
```

`make_update_manifest.py` refuses anything but regular files, directories and in-tree relative
symlinks under `bin/ lib/ share/`, setuid/setgid bits, and game-asset-like files (`.wrs`, `.wdl`, `.sav`, …).

`kabus22-updater` verifies, in order: HTTPS transport (libcurl, TLS peer/host verification, HTTPS-only
redirects, size limits, timeouts) → Ed25519 signature with the public key compiled in from CMake cache
variable `K22_UPDATE_PUBLIC_KEY` → manifest fields, version equals the release tag, signed channel,
`min_glibc` → payload size and SHA-256 → safe extraction (no absolute/`..` paths, no escaping links,
no hard links, devices or setuid bits; only `bin/ lib/ share/`) into a private stage inside `versions/`
→ atomic activation. Any failure leaves the installation unchanged.

### Key custody and rotation

- Development/project key: `~/.local/share/kabus22-release-keys/update-ed25519.key` on the maintainer
  workstation (mode 0600, outside the repository, never printed or committed). Public key (default
  `K22_UPDATE_PUBLIC_KEY`): `2mfjUSeWRcSIPa4B3uaLUWxnBFPWulQYvdq1uv4PVVM=`.
- CI signing uses the repository secret `KABUS22_UPDATE_SIGNING_KEY` (same base64 seed). The release
  workflow aborts if the secret's public key differs from the embedded default.
- Keep an offline backup of the key file. Losing it means no further update can be verified by
  existing installations; they must reinstall from a new AppImage.
- Rotation: generate a new key, set the new public key as the `K22_UPDATE_PUBLIC_KEY` default, and ship
  one release **signed with the old key** whose payload contains the updater with the new key. After
  users have updated past it, replace the CI secret with the new key. A compromised key cannot be
  revoked remotely: publish an advisory and require a manual AppImage reinstall.

## CI

- `.github/workflows/ci.yml` (push/PR, Ubuntu 24.04): builds the native engine and installer/launcher/
  updater with distro packages, runs asset-free CTests (fixture-dependent tests self-skip in a public
  checkout), `tools/check_wrs2.py` and the packaging policy self-check.
- `.github/workflows/release.yml` (tag `v*`): acquires the pinned innoextract helper and uruntime (hash
  and signature checked), builds the pinned toolchain image from `tools/Dockerfile.ubuntu24` (stdin, no
  build context), runs `tools/build_kabus_baseline.sh`, extracts the AppImage SquashFS, builds and signs
  the update payload, and creates a **draft** GitHub release with the AppImage, its report and
  dependency-source bundle, native source archive, update assets and `SHA256SUMS`.

## Publishing a release

1. Bump `VERSION` in both `project()` calls; update README/changelog.
2. Run the local test suites (native and installer CTest, including `updater_end_to_end`).
3. Tag `vX.Y.Z` (or `vX.Y.Z-rc.N`) and push the tag.
4. Qualify the draft release artifacts (install from ISO, launcher, OpenGL/Vulkan startup, update from
   the previous version on a private copy of an installation).
5. Publish the draft (prerelease flag set automatically for experimental versions). Only then do
   updaters see it.

Local equivalent of steps 3–4 without CI: follow README "Optional AppImage assembly", then extract the
AppImage (`unsquashfs -o <squashfs_offset from report> …`) and run the two `tools/release` commands.

## AppImage assembly

`tools/package_kabus_appimage.py` requires `--engine`, `--installer`, `--launcher` and `--updater`
(release build only; it refuses the `-testhttp` test build). It bundles the updater's libcurl/OpenSSL
closure with distro notices and corresponding sources, and records the compiled-in header-only
nlohmann/json package notice. The installed and update payload (`versions/<v>/bin`) contains `kabus22`,
`kabus22-launcher`, `kabus22-setup` (spawned by the launcher's Installation page), `kabus22-updater`
and the `innoextract` helper; the updater and `make_update_manifest.py` require the first four.

## Rollback

```sh
kabus22-updater --install-dir ~/.local/share/kabus22 --rollback   # previous <-> active
kabus22-updater --install-dir ~/.local/share/kabus22 --status     # {"active","previous","installed"}
```

The launcher exposes the same actions. Rolling back only swaps `current`; saves and options are untouched.

## Testing the updater

`installer/check_update.cpp` (CTest `updater_layout_signature_archive`) covers layout migrate/commit/
rollback/crash recovery with save files present, semantic-version/channel selection, signature
acceptance/rejection and hostile archives. `installer/check_updater_e2e.py` (CTest
`updater_end_to_end`) drives the real CLI against a local `http.server` fixture through
`kabus22-updater-testhttp`: a test-only executable (built only with `BUILD_TESTING`, define
`K22_UPDATER_TEST_HTTP`) that additionally accepts `http://127.0.0.1:PORT` and reads the API base and
public key from `KABUS22_UPDATER_TEST_API` / `KABUS22_UPDATER_TEST_PUBLIC_KEY`. The shipped
`kabus22-updater` contains neither hook; the E2E test and the AppImage packager both check that.

## Private qualification notes — 0.2.0 candidate (2026-10-10, not published)

Local assembly per README (pinned innoextract 1.9 signature `VALIDSIG ADE96537…DEC9` + SHA-256, uruntime
v0.8.1 SHA-256 checked), toolchain image `sha256:9c494891…0b755`, Git `df8bc71` plus the uncommitted
working tree as of 2026-10-10 05:34 (render/asset parity fixes, `tests/reference`, `kriz_enemy_ai`, controller
fixes, ODE `lcp.cpp` state[] recipe; no source file changed between the CTest configure and assembly).
Image build 453 s (2026-10-09); full baseline 312 s.
The report and source sidecar record both ODE modifications (`GenerateContact.patch`, `LcpStateInit.patch`
with original/modified hashes and the modified files).

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Kabus22-Linux-Installer-x86_64.AppImage` | 61136743 | `004401caccefc7cc63e16f52066572debbdde6f4fac62cace8c2e00ab2914ddc` |
| `….AppImage.json` (report, 718 inventory rows, 116 libs, glibc ≥ 2.38) | 2002427 | `2dd3e8b694693915b2f52948469dba682aa73f61e0090b41bd83168d25426c5f` |
| `….AppImage.sources.tar.gz` (89 source packages) | 483323915 | `0db70ca59b3e2dca81d66a08e3661709874f304470066da1b2ccc35daa343688` |
| `kabus22-0.2.0-linux-x86_64.tar.gz` (stable, from the AppImage `usr/`, 924 members) | 59567067 | `8b7ffd8d0bbb8dff29975836085610c55d18872a518b23fa415bc93cb05d4ac0` |
| `kabus22-update.json` | 358 | `eb37efa78cea799c179dd69140a54c1bec62cdbd6d36264c03572c411c147d97` |
| `kabus22-update.json.sig` (dev key, verifies against `2mfjUSeW…VVM=`) | 89 | `03081f09b40d5b297072c8388be231f8d2e07b9e2cd7260fb96ad23167c4c158` |

Payload binaries: `kabus22` `9e837e7c…2aac8a`, `kabus22-launcher` `72beb6a5…295e3`, `kabus22-setup`
`87af87a9…d38e0`, `kabus22-updater` `ddb0c2b3…11ba3`, `innoextract` `f39ed0e0…64f51`.

Before assembly, the same tree passed: Release native CTest 11/11 including `gameplay_scenarios` (1086 s)
and `level_sweep` (265 s), installer CTest 5/5, `k22-render-check K22_mode.wrs --gl|--vulkan|--vulkan-rt`,
packager `--self-check`, and the sanitizer fixture CTests in [PROGRESS.md](PROGRESS.md#tests-and-build).

Observed on CachyOS / RX 7900 XTX (Mesa 26.2.4), private HOME/XDG, X11:

- Unattended ISO-only install (`--iso "Kabus 22.iso" --unattended`, default root): 83 s, `versions/0.2.0`
  with `kabus22`, `kabus22-launcher`, `kabus22-setup`, `kabus22-updater`, `innoextract`; `current`/`bin`/
  `lib`/`share` relative links, `.state active=0.2.0`; 137 game files; both shortcuts open the launcher.
  `kabus22-setup --verify`: 618 files, 0 missing/modified. All four native `--version` report 0.2.0;
  launcher `--self-check` passed; `kabus22-updater --status` correct.
- Installed engine `--frames 600 --capture` in OpenGL, Vulkan raster and Vulkan RT: exit 0 each (~9.5 s),
  2560×1440 captures of the original FIP intro panel.
- Gameplay with the installed engine after `--import-saves` of the original Windows save set (30 files):
  `world_cam1_padlock.k22s` rendered in OpenGL PASS (70 passed, 7293 frames, 92 s; pickups, padlocked door,
  movie, cutscene, level change). One scenario, not a campaign playthrough.
- Update (test updater `kabus22-updater-testhttp` + local HTTP fixture, real dev-key signature, fabricated
  `0.2.1` = the same `usr/` tree): from (a) the AppImage 0.2.0 install with imported saves and (b) a legacy
  flat v0.1.1 install made by the v0.1.1 AppImage plus copied saves/options. Apply 0.3 s, 715 payload files
  in `versions/0.2.1` byte-identical to the AppImage `usr/` tree, launcher self-check passed, re-apply
  "Already up to date", rollback `0.2.1 -> 0.2.0` restored the prior engine (0.1.1 for the flat case).
  SHA-256 of every non-layout entry (198 and 144: game/, 29 and 2 saves, options, inventories) unchanged
  after apply and after rollback; `--verify` afterwards ok. The shipped updater refused the HTTP fixture.
- The installer refuses install roots below a group/world-writable non-sticky ancestor (e.g. a `0770`
  mount point); qualify on such hosts with a root under a `0700`/`0755` directory.
- Known limitations: a legacy flat install is migrated as `versions/<updater version>` regardless of its
  real version (v0.1.1 became `versions/0.2.0`), so `--check` reports it as current 0.2.0 and an update
  *to* 0.2.0 would be "Already up to date". Rolling such an install back reactivates the old flat payload,
  which has no `kabus22-setup`/`kabus22-updater`; updating again needs `versions/<new>/bin/kabus22-updater`.

## What never ships

- Original game media: ISO, `setup.exe`/`setup-1.bin`, extracted `game/` assets, `Kabus22Patch/`.
- User data: saves, options, configuration.
- Private keys, private reverse-engineering exports and qualification captures.
- The `kabus22-updater-testhttp` test build.
