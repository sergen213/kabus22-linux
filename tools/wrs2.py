#!/usr/bin/env python3
"""List/extract GameStudio WRS2 archives without QuickBMS or engine binaries.

Format reference: https://raw.githubusercontent.com/iseeeva/kabus22/main/
kabus_tools/src/external/gamestudio.bms. Actual K22 data uses Okumura LZSS:
4096-byte space-filled window, initial cursor 4078, LSB-first flags, absolute
12-bit window references and lengths 3..18. Filename padding is raw NUL,
not XOR-encoded NUL. Entries and decoded output are streamed in 64 KiB chunks.
"""

import argparse
from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import struct
import sys

MAX_OUTPUT = 128 * 1024 * 1024
CHUNK = 64 * 1024


@dataclass(frozen=True)
class Entry:
    name: str
    offset: int
    compressed_size: int
    output_size: int


def safe_name(name):
    """Normalize DOS separators, refusing ambiguous or escaping paths."""
    name = name.replace("\\", "/")
    parts = name.split("/")
    if not name or any(part in ("", ".", "..") for part in parts):
        raise ValueError(f"unsafe member path: {name!r}")
    if any(ord(c) < 32 or ord(c) == 127 or c in ':<>"|?*' for c in name):
        raise ValueError(f"unsafe member path: {name!r}")
    for part in parts:
        if part.endswith((".", " ")) or part.split(".")[0].upper() in {
            "CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)),
            *(f"LPT{i}" for i in range(1, 10)),
        }:
            raise ValueError(f"nonportable member path: {name!r}")
    return name


def _index(source, max_output):
    if max_output < 0:
        raise ValueError("maximum output size must be nonnegative")
    total = os.fstat(source.fileno()).st_size
    if source.read(4) != b"WRS2":
        raise ValueError("not a WRS2 archive")
    entries, seen = [], set()
    while source.tell() < total:
        header = source.read(40)
        if len(header) != 40:
            raise ValueError("truncated member header")
        raw, compressed, size = struct.unpack(">32sII", header)
        encoded, separator, padding = raw.partition(b"\0")
        if separator and any(padding):
            raise ValueError("nonzero filename padding")
        name = safe_name(bytes(c ^ 0xA5 for c in encoded).decode("latin1"))
        key = name.casefold()
        if key in seen:
            raise ValueError(f"duplicate casefold member path: {name}")
        seen.add(key)
        offset = source.tell()
        if compressed > total - offset:
            raise ValueError(f"truncated payload: {name}")
        if size > max_output:
            raise ValueError(f"member exceeds maximum output size: {name} ({size})")
        if bool(compressed) != bool(size):
            raise ValueError(f"inconsistent empty member sizes: {name}")
        entries.append(Entry(name, offset, compressed, size))
        source.seek(compressed, 1)
    # Files and directories cannot share a case-insensitive extraction path.
    for key in seen:
        parts = key.split("/")
        if any("/".join(parts[:i]) in seen for i in range(1, len(parts))):
            raise ValueError(f"file/directory member conflict: {key}")
    return entries


def index_archive(path, max_output=MAX_OUTPUT):
    with open(path, "rb") as source:
        return _index(source, max_output)


def decode_lzss(source, compressed_size, output_size, destination):
    if compressed_size < 0 or output_size < 0:
        raise ValueError("negative LZSS size")

    def encoded_bytes():
        remaining = compressed_size
        while remaining:
            chunk = source.read(min(CHUNK, remaining))
            if not chunk:
                raise ValueError("truncated LZSS payload")
            remaining -= len(chunk)
            yield from chunk

    encoded = iter(encoded_bytes())
    window = bytearray(b" " * 4096)
    cursor, produced, consumed = 4078, 0, 0
    buffer = bytearray()
    while consumed < compressed_size:
        flags = next(encoded)
        consumed += 1
        if consumed == compressed_size:
            raise ValueError("LZSS flag without a token")
        for bit in range(8):
            if consumed == compressed_size:
                break
            if flags & (1 << bit):
                token = next(encoded)
                consumed += 1
                start, length = 0, 1
            else:
                if compressed_size - consumed < 2:
                    raise ValueError("truncated LZSS reference")
                lo, hi = next(encoded), next(encoded)
                consumed += 2
                start, length = lo | ((hi & 0xF0) << 4), (hi & 15) + 3
                token = None
            if length > output_size - produced:
                raise ValueError("LZSS output exceeds declared size")
            for k in range(length):
                value = token if token is not None else window[(start + k) & 4095]
                buffer.append(value)
                window[cursor] = value
                cursor = (cursor + 1) & 4095
            produced += length
            if len(buffer) >= CHUNK:
                destination.write(buffer)
                buffer.clear()
    if produced != output_size:
        raise ValueError(f"LZSS output size mismatch: {produced} != {output_size}")
    if buffer:
        destination.write(buffer)


def _checked_directory(path):
    """Refuse symlinks in every existing output ancestor, including the root."""
    path = Path(os.path.abspath(path))
    for current in (path, *path.parents):
        if current.is_symlink():
            raise ValueError(f"symlink output directory: {current}")
        if current.exists() and not current.is_dir():
            raise ValueError(f"not an output directory: {current}")
    path.mkdir(parents=True, exist_ok=True)
    return path


def _destination(root, name):
    """Use directory descriptors on POSIX to prevent symlink-swap escapes."""
    parts = name.split("/")
    if os.open in os.supports_dir_fd and hasattr(os, "O_NOFOLLOW"):
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        directory = os.open(root.anchor, flags)
        try:
            for part in root.parts[1:]:
                child = os.open(part, flags, dir_fd=directory)
                os.close(directory)
                directory = child
            for part in parts[:-1]:
                try:
                    os.mkdir(part, dir_fd=directory)
                except FileExistsError:
                    pass
                child = os.open(part, flags, dir_fd=directory)
                os.close(directory)
                directory = child
            fd = os.open(parts[-1], os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                         0o600, dir_fd=directory)
            return os.fdopen(fd, "wb"), directory, parts[-1]
        except BaseException:
            os.close(directory)
            raise
    # Windows: O_EXCL refuses both existing files and existing links.
    parent = _checked_directory(root.joinpath(*parts[:-1]))
    target = parent / parts[-1]
    fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    return os.fdopen(fd, "wb"), None, target


def extract_archive(archive, output, members=None, max_output=MAX_OUTPUT):
    with open(archive, "rb") as source:
        entries = _index(source, max_output)
        requested = None if members is None else {safe_name(m).casefold() for m in members}
        if requested is not None:
            missing = requested - {entry.name.casefold() for entry in entries}
            if missing:
                raise ValueError(f"members not found: {', '.join(sorted(missing))}")
        selected = [e for e in entries if requested is None or e.name.casefold() in requested]
        root = _checked_directory(output)
        # Refuse existing targets before writing any member; creation is exclusive too.
        for entry in selected:
            target = root / entry.name
            if os.path.lexists(target):
                raise ValueError(f"refusing to overwrite output: {target}")
            for parent in target.parents:
                if parent == root:
                    break
                if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
                    raise ValueError(f"unsafe output parent: {parent}")
        recovered = []
        for entry in selected:
            destination, directory, leaf = _destination(root, entry.name)
            try:
                with destination:
                    source.seek(entry.offset)
                    decode_lzss(source, entry.compressed_size, entry.output_size, destination)
            except BaseException:
                if directory is None:
                    os.unlink(leaf)
                else:
                    os.unlink(leaf, dir_fd=directory)
                raise
            finally:
                if directory is not None:
                    os.close(directory)
            recovered.append(entry.name)
        return recovered


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("list", "extract"):
        sub = commands.add_parser(command)
        sub.add_argument("archive", type=Path)
        sub.add_argument("--max-output", type=int, default=MAX_OUTPUT,
                         help="maximum decoded bytes per entry (default: 128 MiB)")
        if command == "list":
            sub.add_argument("--json", action="store_true", help="emit a machine-readable index")
        else:
            sub.add_argument("output", type=Path)
            sub.add_argument("members", nargs="*", help="optional member names (case-insensitive)")
    args = parser.parse_args()
    try:
        if args.command == "list":
            entries = index_archive(args.archive, args.max_output)
            if args.json:
                json.dump([asdict(entry) for entry in entries], sys.stdout, indent=2)
                print()
            else:
                for entry in entries:
                    print(f"{entry.output_size:12d} {entry.compressed_size:12d} {entry.offset:12d} {entry.name}")
        else:
            for name in extract_archive(args.archive, args.output, args.members or None, args.max_output):
                print(name)
    except (ValueError, OSError) as error:
        parser.exit(1, f"wrs2: {error}\n")


if __name__ == "__main__":
    main()
