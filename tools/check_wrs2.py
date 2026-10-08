#!/usr/bin/env python3
"""Deterministic WRS2 format, corruption, and extraction-safety checks."""
import io
from pathlib import Path
import struct
import tempfile

import wrs2


def rejects(call, errors=(ValueError,)):
    try:
        call()
    except errors:
        return
    raise AssertionError("corrupt or unsafe input was accepted")


def record(name, payload=b"\x07ABC\xee\xf3", size=9):
    raw = bytes(value ^ 0xA5 for value in name.encode("ascii"))
    assert len(raw) < 32
    return raw.ljust(32, b"\0") + struct.pack(">II", len(payload), size) + payload


def main():
    for encoded, expected in ((b"\x07ABC\xee\xf3", b"ABCABCABC"),
                              (b"\x00\x00\x00", b"   ")):
        destination = io.BytesIO()
        assert wrs2.decode_lzss(io.BytesIO(encoded), len(encoded), len(expected), destination) is None
        assert destination.getvalue() == expected
    for encoded, size in ((b"\x01", 1), (b"\x00\xee", 3),
                          (b"\x01A", 2), (b"\x00\x00\x00", 2),
                          (b"\x01A\x00", 1)):
        rejects(lambda: wrs2.decode_lzss(io.BytesIO(encoded), len(encoded), size, io.BytesIO()))
    rejects(lambda: wrs2.decode_lzss(io.BytesIO(b"\x01A"), 3, 1, io.BytesIO()))

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        archive = root / "sample.wrs"
        valid = b"WRS2" + record("folder/a.txt") + record("spaces.txt", b"\0\0\0", 3)
        archive.write_bytes(valid)
        entries = wrs2.index_archive(archive)
        assert [(e.name, e.offset, e.compressed_size, e.output_size) for e in entries] == [
            ("folder/a.txt", 44, 6, 9), ("spaces.txt", 90, 3, 3)]
        output = root / "output"
        assert wrs2.extract_archive(archive, output) == ["folder/a.txt", "spaces.txt"]
        assert (output / "folder/a.txt").read_bytes() == b"ABCABCABC"
        assert (output / "spaces.txt").read_bytes() == b"   "
        selected = root / "selected"
        assert wrs2.extract_archive(archive, selected, ["spaces.txt"]) == ["spaces.txt"]
        assert (selected / "spaces.txt").read_bytes() == b"   "
        assert not (selected / "folder").exists()
        rejects(lambda: wrs2.index_archive(archive, max_output=8))
        rejects(lambda: wrs2.extract_archive(archive, root / "limited", max_output=8))
        rejects(lambda: wrs2.extract_archive(archive, output), (ValueError, OSError))
        assert (output / "folder/a.txt").read_bytes() == b"ABCABCABC"

        outside = root / "outside"
        outside.mkdir()
        linked = root / "linked"
        linked.symlink_to(outside, target_is_directory=True)
        rejects(lambda: wrs2.extract_archive(archive, linked), (ValueError, OSError))
        member_link = root / "member-link"
        member_link.mkdir()
        (member_link / "folder").symlink_to(outside, target_is_directory=True)
        rejects(lambda: wrs2.extract_archive(archive, member_link), (ValueError, OSError))
        assert not list(outside.iterdir())
        file_link = root / "file-link"
        (file_link / "folder").mkdir(parents=True)
        sentinel = root / "sentinel"
        sentinel.write_bytes(b"preserve")
        (file_link / "folder/a.txt").symlink_to(sentinel)
        rejects(lambda: wrs2.extract_archive(archive, file_link), (ValueError, OSError))
        assert sentinel.read_bytes() == b"preserve"

        bad_padding = bytearray(record("a.txt"))
        bad_padding[6] = 1
        malformed = [b"", b"WRS", b"WRS2" + b"\0" * 39,
                     b"WRS2" + record("a.txt")[:-1], b"WRS2" + bad_padding,
                     b"WRS2" + record("A.txt") + record("a.TXT")]
        malformed += [b"WRS2" + record(name) for name in
                      ("/a.txt", "../a.txt", "folder/../a.txt", "C:/a.txt", "C:a.txt", "\\\\host\\a.txt")]
        for data in malformed:
            archive.write_bytes(data)
            rejects(lambda: wrs2.index_archive(archive))
    print("WRS2 checks passed")


if __name__ == "__main__":
    main()
