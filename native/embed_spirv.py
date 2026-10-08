#!/usr/bin/env python3
"""Embed build-produced SPIR-V as aligned C++ words; no runtime shader files."""
import pathlib
import re
import struct
import sys


def main(arguments):
    if len(arguments) < 3 or len(arguments) % 2 == 0:
        raise ValueError("usage: embed_spirv.py OUTPUT NAME SPIRV [NAME SPIRV ...]")
    output = pathlib.Path(arguments[0])
    chunks = ["#pragma once\n#include <cstdint>\nnamespace k22::vk22::spirv {\n"]
    seen = set()
    for name, path in zip(arguments[1::2], arguments[2::2]):
        if not re.fullmatch(r"[a-zA-Z_][a-zA-Z_0-9]*", name) or name in seen:
            raise ValueError("invalid or duplicate shader symbol: " + name)
        seen.add(name)
        data = pathlib.Path(path).read_bytes()
        if len(data) < 20 or len(data) % 4:
            raise ValueError("invalid SPIR-V size: " + path)
        words = struct.unpack("<" + "I" * (len(data) // 4), data)
        if words[0] != 0x07230203:
            raise ValueError("invalid SPIR-V magic: " + path)
        chunks.append("inline constexpr std::uint32_t " + name + "[] = {\n")
        for start in range(0, len(words), 8):
            chunks.append("    " + ",".join(f"0x{word:08x}u" for word in words[start:start+8]) + ",\n")
        chunks.append("};\n")
    chunks.append("}\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("".join(chunks), encoding="utf-8")


if __name__ == "__main__":
    main(sys.argv[1:])
