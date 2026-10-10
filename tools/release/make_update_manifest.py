#!/usr/bin/env python3
"""Build kabus22-<version>-linux-x86_64.tar.gz and kabus22-update.json (format 1).

Input is either a staged payload tree containing only bin/ lib/ share/ (--tree) or an
assembled/extracted AppImage AppDir (--appdir: usr/bin -> bin, usr/lib -> lib,
usr/share -> share). The archive is deterministic (sorted, root-owned, mtime 0) and
never contains game assets. Sign the manifest afterwards with sign_manifest.py.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import stat
import sys
import tarfile
from pathlib import Path, PurePosixPath

TOP = ("bin", "lib", "share")
REQUIRED = ("bin/kabus22", "bin/kabus22-launcher", "bin/kabus22-setup", "bin/kabus22-updater")
VERSION = re.compile(r"(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?")
# Defense in depth: original game media must never enter an update payload.
GAME = re.compile(r".*\.(?:wrs|wdl|wmb|wmp|mdl|hmp|pcx|tga|bmp|pal|raw|wav|ogg|mid|mpg|avi|k22|sav|exe|dll|iso)", re.I)
MAX_TOTAL = 2 * 1024 * 1024 * 1024


def fail(message: str) -> None:
    sys.exit("make_update_manifest: " + message)


def collect(roots: dict[str, Path]) -> list[tuple[str, Path]]:
    entries = []
    for top, base in roots.items():
        if not base.is_dir() or base.is_symlink():
            fail(f"missing real directory for {top}/: {base}")
        entries.append((top, base))
        for path in sorted(base.rglob("*")):
            entries.append((top + "/" + path.relative_to(base).as_posix(), path))
    return sorted(entries)


def tarinfo(name: str, path: Path) -> tuple[tarfile.TarInfo, bytes | None]:
    info = path.lstat()
    entry = tarfile.TarInfo(name)
    entry.mtime, entry.uid, entry.gid, entry.uname, entry.gname = 0, 0, 0, "", ""
    if info.st_mode & (stat.S_ISUID | stat.S_ISGID | stat.S_ISVTX):
        fail(f"setuid/setgid/sticky bit: {name}")
    if any(ord(char) < 32 for char in name) or "\\" in name:
        fail(f"unsafe name: {name!r}")
    if stat.S_ISDIR(info.st_mode):
        entry.type, entry.mode = tarfile.DIRTYPE, 0o755
        return entry, None
    if stat.S_ISLNK(info.st_mode):
        target = os.readlink(path)
        resolved = PurePosixPath(os.path.normpath(PurePosixPath(name).parent / target))
        if target.startswith("/") or resolved.parts[:1] == ("..",) or resolved.parts[0] not in TOP:
            fail(f"symlink escapes payload: {name} -> {target}")
        entry.type, entry.linkname, entry.mode = tarfile.SYMTYPE, target, 0o777
        return entry, None
    if not stat.S_ISREG(info.st_mode):
        fail(f"not a regular file/directory/symlink: {name}")
    if GAME.fullmatch(name):
        fail(f"refusing game-asset-like file in update payload: {name}")
    data = path.read_bytes()
    entry.type, entry.size = tarfile.REGTYPE, len(data)
    entry.mode = 0o755 if info.st_mode & 0o111 else 0o644
    return entry, data


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--tree", type=Path, help="Directory containing exactly bin/ lib/ share/")
    source.add_argument("--appdir", type=Path, help="Assembled AppImage AppDir (usr/bin, usr/lib, usr/share)")
    parser.add_argument("--version", required=True)
    parser.add_argument("--channel", choices=("stable", "experimental"), required=True)
    parser.add_argument("--min-glibc", default="2.38")
    parser.add_argument("--components", default="engine,launcher,updater,shaders,converters")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if not VERSION.fullmatch(args.version) or len(args.version) > 64:
        fail(f"invalid semantic version: {args.version}")
    if not re.fullmatch(r"[0-9]+\.[0-9]+", args.min_glibc):
        fail("invalid --min-glibc")
    if args.tree:
        extra = sorted(path.name for path in args.tree.iterdir() if path.name not in TOP)
        if extra:
            fail(f"payload tree may contain only bin/ lib/ share/, found: {extra}")
        roots = {top: args.tree / top for top in TOP}
    else:
        roots = {top: args.appdir / "usr" / top for top in TOP}
    entries = collect(roots)
    names = {name for name, _ in entries}
    for required in REQUIRED:
        path = dict(entries).get(required)
        if not path or not path.is_file() or path.is_symlink() or not path.stat().st_mode & 0o111:
            fail(f"payload lacks executable {required}")
    if len(names) > 16384:
        fail("too many payload entries")
    buffer, total = io.BytesIO(), 0
    with gzip.GzipFile(fileobj=buffer, mode="wb", mtime=0, compresslevel=9) as compressed:
        with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for name, path in entries:
                entry, data = tarinfo(name, path)
                total += entry.size
                if total > MAX_TOTAL:
                    fail("payload exceeds 2 GiB")
                archive.addfile(entry, io.BytesIO(data) if data is not None else None)
    payload = buffer.getvalue()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    payload_name = f"kabus22-{args.version}-linux-x86_64.tar.gz"
    (args.output_dir / payload_name).write_bytes(payload)
    manifest = {"format": 1, "version": args.version, "channel": args.channel, "min_glibc": args.min_glibc,
                "payload": {"name": payload_name, "sha256": hashlib.sha256(payload).hexdigest(), "size": len(payload)},
                "components": [item for item in args.components.split(",") if item]}
    (args.output_dir / "kabus22-update.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(args.output_dir / payload_name)
    print(args.output_dir / "kabus22-update.json")


if __name__ == "__main__":
    main()
