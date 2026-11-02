#!/usr/bin/env python3
"""Fails when a string literal passed to tx/txn/fmt in the launcher sources has no Turkish entry in launcher_text.cpp."""
import re
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
literal = r'"((?:[^"\\\n]|\\.)*)"'
used = {}
for name in ("launcher.cpp", "launcher_settings.cpp"):
    for number, line in enumerate((here / name).read_text(encoding="utf-8").splitlines(), 1):
        for match in re.finditer(r"\b(?:tx|txn|fmt)\(\s*" + literal, line):
            used.setdefault(match.group(1), f"{name}:{number}")
table = re.findall(r"^\s*\{" + literal + r",\s*" + literal + r"\}", (here / "launcher_text.cpp").read_text(encoding="utf-8"), re.M)
known = {en for en, tr in table if tr}
missing = sorted(set(used) - known, key=lambda s: used[s])
for text in missing:
    print(f'{used[text]}: no Turkish entry for "{text}"')
if not used or not table:
    print("no translated literals or no table entries found")
    sys.exit(1)
print(f"{len(used)} translated literals, {len(table)} table entries, {len(missing)} missing")
sys.exit(1 if missing else 0)
