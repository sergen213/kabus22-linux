#!/usr/bin/env python3
"""Assemble an asset-free native Kabus22 Type-2 AppImage, never build the engine.

Run only after verifying the real native engine and installer against user media.
Requires Ubuntu 24.04's readelf, patchelf, ldconfig, mksquashfs, unsquashfs,
glib-compile-schemas, dpkg-query and authenticated APT deb-src metadata.
Build and assemble in the same pinned container; the report is not release proof.
The dependency source bundle includes the pinned ODE archive, CMake modification
recipes, hash-checked built contact source and its diff; it licenses no native code.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
RUNTIME_NAME = "uruntime-appimage-squashfs-lite-x86_64"
RUNTIME_URL = "https://github.com/VHSgunzo/uruntime/releases/download/v0.8.1/" + RUNTIME_NAME
RUNTIME_SHA = "32c6024a14ff375749ee3bed3330c1586a6dd2bfeddbbd7ceacc053aec2b27a3"
RUNTIME_SIZE = 1226208
HELPER_SHA = "7a8ad941deeb0de8d1f804830f339fd17e27cfbce473615d79d1d65c106884e9"
NOTICE_INVENTORIES = {
    "innoextract": "495fb1b729d7542ffa328b076d55c16ef15fe09dfdbc18d9a3d316eea2315479",
    "uruntime": "552a81beaeb4192a05ea57aecc3695e45f693a8e1ab40f3a4a62e485c61bf1c5",
}
MAX_FILE = 512 * 1024 * 1024
MAX_TOTAL = 2 * 1024 * 1024 * 1024
BASE_IMAGE = "ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55"
APT_SNAPSHOT = "20261007T000000Z"
RELEASE_SHA = {
    "noble": "cdb2f31d809f589719a53c6ad15f255b27569c4059542ada282aaa21b8e164b0",
    "noble-updates": "1ba35ee59c0c7c430601e8715ad6379c9e0bbd77960d4246175abc2eb132da42",
    "noble-security": "27b649589af352e3c6980b1e88431b5555209191fc52a02decf8f4bab74f1aff",
}
FFMPEG_ROOT = Path("/opt/kabus22-ffmpeg")
FFMPEG_SHA = "8684f4b00f94b85461884c3719382f1261f0d9eb3d59640a1f4ac0873616f968"
ODE_SHA = "05c0a0ea84c58c213d05e220adbc6559c473227505d5668a51493eed21b6c2f8"
# Applied pinned-ODE modifications: source path -> (original, modified SHA-256, recipe, retained diff).
ODE_MODIFICATIONS = {
    "ode/src/collision_trimesh_box.cpp": ("748b26ef299b6abeb0ddf23d910889673ecf18da01d34f6c94eedb69ccc72970",
                                          "b4e81e712cd5c833e38d95287b4ce415bc8db32e20deacc2f9eae93b5c6ddd42",
                                          "cmake/KabusODEContact.cmake", "GenerateContact.patch"),
    "ode/src/lcp.cpp": ("76b3771a02736a29fa552ec49a163953420eb98aa98d8a3cbe0da21f5ed9d03c",
                        "3467227bf68f38e2ea72785006545a998672952994aca6158492018da9c5ddfa",
                        "cmake/KabusODELcp.cmake", "LcpStateInit.patch"),
}
FFMPEG_VERSIONS = {"avformat": "60.16.100", "avcodec": "60.31.102", "avutil": "58.29.100",
                   "swscale": "7.5.100", "swresample": "4.12.100"}
FFMPEG_LIBRARIES = {"lib" + name + ".so." + version.split(".")[0]: "lib" + name + ".so." + version
                    for name, version in FFMPEG_VERSIONS.items()}
# GPU loaders/drivers and their Wayland protocol ABI are host-owned; glibc is never copied.
# Bundling older Wayland libraries can prevent a newer host ICD from loading.
HOST = re.compile(
    r"(?:ld-linux[^/]*|linux-vdso[^/]*|"
    r"lib(?:c|m|dl|pthread|rt|resolv|util|anl|nss_[^.]+)\.so(?:\.[0-9]+)*|"
    r"lib(?:GL|GLX(?:_[^.]+)?|GLdispatch|OpenGL|EGL(?:_[^.]+)?|GLESv[12]|vulkan(?:_[^.]+)?|drm(?:_[^.]+)?|gbm|"
    r"wayland-(?:client|egl|cursor|server)|"
    r"cuda|nvidia[^.]*|nvoptix|vdpau(?:_[^.]+)?|va(?:-x11|-drm|-wayland)?)\.so(?:\.[0-9]+)*)\Z"
)
APPRUN = '''#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
export GSETTINGS_SCHEMA_DIR="$HERE/usr/share/glib-2.0/schemas"
# Do not load host GTK modules into the bundled GTK ABI.
unset GTK_MODULES GTK3_MODULES
exec "$HERE/usr/bin/kabus22-setup" "$@"
'''
DESKTOP = '''[Desktop Entry]
Type=Application
Name=Kabus22 Native Installer
Comment=Install from your original Kabus22 media
Exec=kabus22-setup
Icon=kabus22-installer
Terminal=false
Categories=Game;
'''
# Generic original vector, not copyrighted artwork from the game/disc.
ICON = '''<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128"><rect x="8" y="8" width="112" height="112" rx="18" fill="#18202b"/><circle cx="64" cy="58" r="32" fill="#c7d3df"/><circle cx="64" cy="58" r="10" fill="#18202b"/><path d="M48 98h32M64 82v32m-12-10 12 12 12-12" stroke="#f3aa44" stroke-width="6" fill="none"/></svg>'''


def fail(message: str) -> None:
    raise RuntimeError(message)


def command(*args: str | Path, cwd: Path | None = None, timeout: int = 180) -> str:
    environment = os.environ.copy()
    for key in tuple(environment):
        if key.startswith("LD_") or key in ("GTK_MODULES", "GTK3_MODULES"):
            environment.pop(key)
    environment["LC_ALL"] = "C"
    result = subprocess.run([str(arg) for arg in args], env=environment,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=timeout, cwd=cwd)
    if result.returncode:
        fail(f"Command failed ({result.returncode}): {args!r}\n{result.stdout}")
    return result.stdout


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def regular(path: Path, limit: int = MAX_FILE) -> os.stat_result:
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= limit:
        fail(f"Expected a bounded, non-symlink regular file: {path}")
    return info


def snapshot(source: Path, target: Path, executable: bool = False) -> dict:
    regular(source)
    target.parent.mkdir(parents=True, exist_ok=True)
    checksum = hashlib.sha256()
    descriptor = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    with os.fdopen(descriptor, "rb") as incoming, target.open("xb") as outgoing:
        before = os.fstat(incoming.fileno())
        if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= MAX_FILE:
            fail(f"Input changed type or size: {source}")
        if executable and not before.st_mode & 0o111:
            fail(f"Input is not executable: {source}")
        count = 0
        while block := incoming.read(1024 * 1024):
            count += len(block)
            if count > MAX_FILE:
                fail(f"Input exceeds file bound: {source}")
            checksum.update(block)
            outgoing.write(block)
        after = os.fstat(incoming.fileno())
        if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
                after.st_size, after.st_mtime_ns, after.st_ctime_ns) or count != before.st_size:
            fail(f"Input changed while snapshotting: {source}")
    target.chmod(0o755 if executable else 0o644)
    return {"source": str(source), "size": count, "sha256": checksum.hexdigest()}


def elf(path: Path, executable: bool = False, expected_soname: str | None = None) -> dict:
    regular(path)
    with path.open("rb") as incoming:
        header = incoming.read(64)
    if (len(header) != 64 or header[:6] != b"\x7fELF\x02\x01" or
            struct.unpack_from("<H", header, 18)[0] != 62 or
            struct.unpack_from("<H", header, 16)[0] not in (2, 3)):
        fail(f"Not a native x86_64 ELF executable/shared object: {path}")
    dynamic = command("readelf", "--dynamic", "--wide", path)
    elf_type = struct.unpack_from("<H", header, 16)[0]
    sonames = re.findall(r"\(SONAME\).*\[([^\]]*)\]", dynamic)
    if len(sonames) > 1 or any(not re.fullmatch(r"[A-Za-z0-9_+.-]+", name) for name in sonames):
        fail(f"Unsafe/ambiguous DT_SONAME: {path}")
    soname = sonames[0] if sonames else None
    # Some real libraries also have PT_INTERP for a diagnostic entry point.
    # ET_DYN plus the exact imported SONAME distinguishes them from executables.
    if expected_soname is not None and (elf_type != 3 or soname != expected_soname):
        fail(f"Dependency is not an ET_DYN shared library with SONAME {expected_soname}: {path}")
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic)
    if any(not re.fullmatch(r"[A-Za-z0-9_+.-]+", name) for name in needed):
        fail(f"Unsafe or absolute DT_NEEDED import: {path}")
    programs = command("readelf", "--program-headers", "--wide", path)
    loader = re.search(r"Requesting program interpreter:\s*([^\]]+)", programs)
    if loader and loader[1] not in ("/lib64/ld-linux-x86-64.so.2", "/lib/ld-linux-x86-64.so.2"):
        fail(f"Non-system ELF interpreter: {path}: {loader[1]}")
    if executable and not path.stat().st_mode & 0o111:
        fail(f"ELF is not executable: {path}")
    version_text = command("readelf", "--version-info", "--wide", path)
    required = version_text.split("Version needs section", 1)[-1] if "Version needs section" in version_text else ""
    symbols = command("readelf", "--dyn-syms", "--wide", path)
    imports = sorted(set(re.findall(r"\bUND[ \t]+(\S+)", symbols)))
    return {"architecture": "ELF64 little-endian x86_64", "needed": needed,
            "elf_type": elf_type, "soname": soname,
            "interpreter": loader[1] if loader else None,
            "rpaths": re.findall(r"\((?:RPATH|RUNPATH)\).*\[([^\]]*)\]", dynamic),
            "required_symbol_versions": sorted(set(re.findall(r"Name:\s*(\S+)", required))),
            "undefined_dynamic_symbols": imports,
            "gnu_notes": command("readelf", "--notes", "--wide", path),
            "compiler_comment": command("readelf", "--string-dump=.comment", path)}


def loader_cache() -> dict[str, Path]:
    result = {}
    for line in command("ldconfig", "-p").splitlines():
        match = re.match(r"\s*(\S+)\s+\([^)]*x86-64[^)]*\)\s+=>\s+(/\S+)", line)
        if match and match[1] not in result:
            result[match[1]] = Path(match[2]).resolve(strict=True)
    return result


def dependency(name: str, source: Path, metadata: dict, cache: dict[str, Path]) -> Path:
    # Undeclared neighboring directories are not ELF loader search paths.
    directories = []
    for rpath in metadata["rpaths"]:
        for directory in rpath.split(":"):
            directory = directory.replace("${ORIGIN}", str(source.parent)).replace("$ORIGIN", str(source.parent))
            candidate = Path(directory)
            if not directory or "$" in directory or not candidate.is_absolute():
                fail(f"Unresolved or cwd-dependent source ELF search path: {source}: {directory!r}")
            directories.append(candidate)
    for directory in directories:
        candidate = directory / name
        if candidate.exists():
            resolved = candidate.resolve(strict=True)
            regular(resolved)
            return resolved
    if name in cache:
        regular(cache[name])
        return cache[name]
    fail(f"Unresolved native dependency {source} -> {name}")


def notices(root: Path, appdir: Path, prefix: str) -> dict:
    manifest_path = root / "inventory.json"
    regular(manifest_path, 4 * 1024 * 1024)
    manifest_data = manifest_path.read_bytes()
    manifest_sha = hashlib.sha256(manifest_data).hexdigest()
    if manifest_sha != NOTICE_INVENTORIES[prefix]:
        fail(f"Notice inventory differs from pinned acquisition evidence: {manifest_path}")
    inventory = json.loads(manifest_data)
    if not isinstance(inventory, dict) or not inventory:
        fail(f"Invalid pinned notice inventory: {manifest_path}")
    retained = {}
    for name, expected in sorted(inventory.items()):
        parts = PurePosixPath(name).parts
        if (not parts or PurePosixPath(name).is_absolute() or ".." in parts or
                "\\" in name or any(ord(char) < 32 for char in name)):
            fail(f"Unsafe notice inventory path: {name!r}")
        target = appdir / "usr/share/licenses/kabus22" / prefix / name
        actual = snapshot(root / name, target)
        if actual["sha256"] != expected["sha256"] or actual["size"] != expected["size"]:
            fail(f"Notice/source inventory mismatch: {root / name}")
        retained[name] = {"sha256": actual["sha256"], "size": actual["size"]}
    return {"inventory_sha256": manifest_sha, "files": retained}


def installed_package(owner: str) -> dict:
    if not re.fullmatch(r"[a-z0-9][a-z0-9+.-]*(?::amd64)?", owner):
        fail(f"Unsafe distro package identity: {owner!r}")
    fields = command("dpkg-query", "-W", "-f",
                     "${binary:Package}\t${Version}\t${Architecture}\t${source:Package}\t${source:Version}\t${db:Status-Status}\n",
                     owner).strip().split("\t")
    if (len(fields) != 6 or fields[5] != "installed" or fields[2] not in ("amd64", "all") or
            not re.fullmatch(r"[a-z0-9][a-z0-9+.-]*", fields[3]) or
            not re.fullmatch(r"[A-Za-z0-9.+:~_-]+", fields[4])):
        fail(f"No installed amd64/all package evidence: {owner}")
    return dict(zip(("package", "version", "architecture", "source_package", "source_version", "status"), fields))


def installed_owner(source: Path) -> str:
    resolved = source.resolve(strict=True)
    regular(resolved)
    candidates = {source.absolute(), resolved}
    # dpkg may record /lib while usrmerge resolves /usr/lib, or vice versa.
    for path in tuple(candidates):
        text = str(path)
        for old, new in (("/usr/lib/", "/lib/"), ("/lib/", "/usr/lib/"),
                         ("/usr/bin/", "/bin/"), ("/bin/", "/usr/bin/"),
                         ("/usr/sbin/", "/sbin/"), ("/sbin/", "/usr/sbin/")):
            if text.startswith(old):
                alias = Path(new + text[len(old):])
                if alias.exists() and alias.resolve(strict=True) == resolved:
                    candidates.add(alias)
    owners = set()
    for path in sorted(candidates):
        try:
            lines = command("dpkg-query", "-S", path).splitlines()
        except RuntimeError:
            continue
        for line in lines:
            prefix, separator, name = line.rpartition(": ")
            if separator and name == str(path):
                for owner in prefix.split(", "):
                    owners.add(installed_package(owner)["package"])
    if len(owners) != 1:
        fail(f"No unambiguous installed distro ownership: {source}: {sorted(owners)}")
    return owners.pop()


def package_notices(source: Path, appdir: Path, packages: dict) -> str:
    owner = installed_owner(source)
    if owner in packages:
        return owner
    info = installed_package(owner)
    target = appdir / "usr/share/licenses/kabus22/dependencies" / owner
    target.mkdir(parents=True)
    raw_status = command("dpkg-query", "-s", owner)
    file_list = command("dpkg-query", "-L", owner)
    (target / "package-status.txt").write_text(raw_status)
    (target / "package-files.txt").write_text(file_list)
    notice = Path("/usr/share/doc") / owner.split(":")[0] / "copyright"
    resolved = notice.resolve(strict=True)
    if not resolved.is_relative_to(Path("/usr/share/doc")):
        fail(f"Copyright symlink escapes distro documentation: {notice}")
    notice_owner = installed_owner(resolved)
    copied = snapshot(resolved, target / "copyright")
    packages[owner] = {**info, "raw_status": raw_status, "file_list": file_list,
                       "copyright": {**copied, "requested_path": str(notice),
                                     "resolved_owner": installed_package(notice_owner)},
                       "source_obligation": "Exact distro corresponding source is bundled separately. Public redistribution requires license-specific compliance review and distribution authorization; no license is granted to the native project by this packager."}
    return owner


def ffmpeg_evidence(appdir: Path, source_root: Path) -> dict:
    evidence = FFMPEG_ROOT / "share/kabus22-ffmpeg"
    manifest_path = evidence / "inventory.json"
    regular(manifest_path, 4 * 1024 * 1024)
    manifest = json.loads(manifest_path.read_text())
    if (manifest.get("version") != "6.1.1" or manifest.get("license") != "LGPL-2.1-or-later" or
            manifest.get("library_versions") != FFMPEG_VERSIONS):
        fail("Wrong approved FFmpeg component/version/license manifest")
    files = manifest.get("files")
    if not isinstance(files, dict) or not 1 <= len(files) <= 1024:
        fail("Missing bounded FFmpeg component inventory")
    retained = {}
    for name, expected in sorted(files.items()):
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name or any(ord(char) < 32 for char in name):
            fail(f"Unsafe FFmpeg component inventory path: {name!r}")
        allowed = (name in {"lib/" + filename for filename in FFMPEG_LIBRARIES.values()} or
                   any(name.startswith("include/lib" + library + "/") and name.endswith(".h") for library in FFMPEG_VERSIONS) or
                   name in {"lib/pkgconfig/lib" + library + ".pc" for library in FFMPEG_VERSIONS} or
                   path.parent == PurePosixPath("share/kabus22-ffmpeg"))
        if not allowed:
            fail(f"Unapproved custom FFmpeg component file: {name}")
        source = FFMPEG_ROOT / name
        record = snapshot(source, source_root / "ffmpeg-6.1.1" / name)
        if record["sha256"] != expected.get("sha256") or record["size"] != expected.get("size"):
            fail(f"Custom FFmpeg payload/evidence changed: {name}")
        retained[name] = record
        if path.parent == PurePosixPath("share/kabus22-ffmpeg") and name != "share/kabus22-ffmpeg/ffmpeg-6.1.1.tar.xz":
            snapshot(source, appdir / "usr/share/licenses/kabus22/dependencies/ffmpeg-6.1.1" / path.name)
    required = {"lib/" + filename for filename in FFMPEG_LIBRARIES.values()}
    required.update({"lib/pkgconfig/lib" + library + ".pc" for library in FFMPEG_VERSIONS})
    required.update({"share/kabus22-ffmpeg/" + name for name in
                     ("ffmpeg-6.1.1.tar.xz", "ffmpeg-6.1.1.tar.xz.asc", "ffmpeg-devel.asc",
                      "signature-status.txt", "config.h", "config.mak", "config.log", "LICENSE.md",
                      "COPYING.LGPLv2.1", "IJG-NOTICE.txt", "jfdctfst.c", "jfdctint_template.c", "jrevdct.c")})
    if not required <= retained.keys() or any(not any(name.startswith("include/lib" + library + "/") for name in retained) for library in FFMPEG_VERSIONS):
        fail("Incomplete FFmpeg libraries/headers/pkg-config/source/license evidence")
    if retained["share/kabus22-ffmpeg/ffmpeg-6.1.1.tar.xz"]["sha256"] != FFMPEG_SHA:
        fail("FFmpeg corresponding source differs from authenticated official 6.1.1")
    config = (evidence / "config.h").read_text()
    if any(f"#define CONFIG_{option} 0" not in config for option in ("GPL", "NONFREE", "VERSION3")):
        fail("FFmpeg enables GPL, nonfree or version3 components")
    recipe = (evidence / "config.mak").read_text()
    if any(option not in recipe for option in ("--disable-gpl", "--disable-nonfree", "--disable-autodetect",
                                               "--enable-shared", "--disable-static", "--cpu=x86-64",
                                               "-march=x86-64", "-mtune=generic", "--enable-zlib")):
        fail("FFmpeg build recipe differs from approved shared baseline configuration")
    if "VALIDSIG FCF986EA15E6E293A5644F10B4322F04D67658D8 " not in (evidence / "signature-status.txt").read_text():
        fail("Missing exact official FFmpeg release signature verification")
    versions = {}
    for library, expected in FFMPEG_VERSIONS.items():
        actual = command("pkg-config", "--modversion", "lib" + library).strip()
        if actual != expected:
            fail(f"Wrong FFmpeg development library version: {library}: {actual}")
        versions[library] = actual
    snapshot(manifest_path, source_root / "ffmpeg-6.1.1/inventory.json")
    snapshot(manifest_path, appdir / "usr/share/licenses/kabus22/dependencies/ffmpeg-6.1.1/inventory.json")
    return {"version": "6.1.1", "license": "LGPL-2.1-or-later", "source_url": "https://ffmpeg.org/releases/ffmpeg-6.1.1.tar.xz",
            "source_sha256": FFMPEG_SHA, "signing_fingerprint": "FCF986EA15E6E293A5644F10B4322F04D67658D8",
            "library_versions": versions, "files": retained, "manifest_sha256": digest(manifest_path),
            "ownership": "Explicit approved source-built component, not a dpkg package or a generic unowned-library fallback."}


def baseline(info: dict, name: str) -> str | None:
    versions = [value[6:] for value in info["required_symbol_versions"]
                if re.fullmatch(r"GLIBC_[0-9]+(?:\.[0-9]+)*", value)]
    maximum = max(versions, key=lambda value: tuple(map(int, value.split(".")))) if versions else None
    if maximum and tuple(map(int, maximum.split("."))) > (2, 39):
        fail(f"ELF exceeds Ubuntu 24.04 GLIBC_2.39 baseline: {name}: GLIBC_{maximum}")
    for needed in re.findall(r"x86 ISA needed:\s*([^\n]+)", info["gnu_notes"]):
        if needed.strip() != "x86-64-baseline":
            fail(f"ELF requires ISA above x86-64 baseline or unknown ISA: {name}: {needed}")
    return maximum


def deb822(text: str) -> list[dict]:
    records = []
    for paragraph in text.strip().split("\n\n"):
        record, key = {}, None
        for line in paragraph.splitlines():
            if line.startswith((" ", "\t")) and key:
                record[key] += ("\n" if record[key] else "") + line.strip()
            elif ":" in line:
                key, value = line.split(":", 1)
                record[key] = value.lstrip(" \t")
        if record:
            records.append(record)
    return records


def source_evidence(packages: dict, cache: Path, target: Path, acquire: bool) -> dict:
    """APT authenticates indexes; archives must match their exact SHA256 stanza."""
    sources = {}
    for item in packages.values():
        name, version = item["source_package"], item["source_version"]
        identity = name + "=" + version
        if identity in sources:
            continue
        matches = [record for record in deb822(command("apt-cache", "showsrc", "--only-source", name))
                   if record.get("Package") == name and record.get("Version") == version]
        if not matches or any(record.get("Checksums-Sha256") != matches[0].get("Checksums-Sha256") for record in matches):
            fail(f"Missing/ambiguous authenticated exact source stanza: {identity}")
        stanza = matches[0]
        directory = cache / name / version
        if acquire:
            directory.mkdir(parents=True, exist_ok=True)
            # Only this opt-in provisioning phase needs network; ordinary assembly is offline.
            command("apt-get", "--download-only", "--only-source", "source", identity,
                    cwd=directory, timeout=900)
        archives = {}
        for line in stanza.get("Checksums-Sha256", "").splitlines():
            fields = line.split()
            if len(fields) != 3:
                fail(f"Invalid source SHA256 stanza: {identity}")
            checksum, size, filename = fields
            if not re.fullmatch(r"[0-9a-f]{64}", checksum) or Path(filename).name != filename or filename in (".", ".."):
                fail(f"Unsafe source archive stanza: {identity}: {filename}")
            source = directory / filename
            regular(source)
            if source.stat().st_size != int(size) or digest(source) != checksum:
                fail(f"Exact corresponding source missing/changed: {identity}: {source}")
            archives[filename] = snapshot(source, target / name / version / filename)
        if not archives or not any(filename.endswith(".dsc") for filename in archives):
            fail(f"No complete corresponding source .dsc inventory: {identity}")
        sources[identity] = {"stanza": stanza, "archives": archives,
                             "recipe": "Exact Ubuntu source package includes debian/rules, patches and build dependency declarations; this is distro dependency source, not a grant of the native project's license."}
    return sources


def ubuntu_evidence(target: Path) -> dict:
    if 'VERSION_ID="24.04"' not in Path("/etc/os-release").read_text() or command("dpkg", "--print-architecture").strip() != "amd64":
        fail("Build and dependency assembly require the pinned Ubuntu 24.04 amd64 environment")
    root = Path("/opt/kabus22-baseline")
    if (root / "base-image.txt").read_text().strip() != BASE_IMAGE:
        fail("Missing pinned container base-image provenance")
    if (root / "snapshot.txt").read_text().strip() != APT_SNAPSHOT:
        fail("Wrong authenticated Ubuntu archive snapshot")
    bootstrap_package = installed_package("ca-certificates")
    bootstrap_deb = root / "bootstrap/ca-certificates_20260601~24.04.1_all.deb"
    bootstrap_sha = "6bac2a01979e210d9eac1d4d56747ec709ea60654744d66705dc3c36e7629e50"
    regular(bootstrap_deb)
    if bootstrap_package["version"] != "20260601~24.04.1" or bootstrap_deb.stat().st_size != 139430 or digest(bootstrap_deb) != bootstrap_sha:
        fail("HTTPS bootstrap package differs from authenticated snapshot provenance")
    regular(root / "bootstrap/payload/usr/share/doc/ca-certificates/copyright")
    regular(root / "bootstrap/ca-bundle.pem")
    retained = {}
    for path in sorted(root.rglob("*")):
        if path.is_file():
            retained[str(path.relative_to(root))] = snapshot(path, target / "container" / path.relative_to(root))
    releases = {}
    for path in sorted(Path("/var/lib/apt/lists").glob("*")):
        if path.is_file() and path.name != "lock":
            retained["apt-lists/" + path.name] = snapshot(path, target / "apt-lists" / path.name)
            for suite, checksum in RELEASE_SHA.items():
                if path.name.endswith("_dists_" + suite + "_InRelease"):
                    if digest(path) != checksum:
                        fail(f"Authenticated snapshot Release differs from pinned metadata: {suite}")
                    releases[suite] = checksum
    if releases != RELEASE_SHA or not any("_Sources" in name for name in retained):
        fail("Missing authenticated source indexes or signed snapshot Release evidence")
    retained["archive-keyring.gpg"] = snapshot(Path("/usr/share/keyrings/ubuntu-archive-keyring.gpg"), target / "archive-keyring.gpg")
    (target / "apt-indextargets.txt").write_text(command("apt-get", "indextargets"))
    return {"base_image": BASE_IMAGE, "snapshot": APT_SNAPSHOT, "signed_release_sha256": releases,
            "files": retained, "installed_toolchain": command("dpkg-query", "-W"),
            "https_bootstrap": {**bootstrap_package, "deb_sha256": bootstrap_sha,
                               "url": "https://snapshot.ubuntu.com/ubuntu/20261007T000000Z/pool/main/c/ca-certificates/ca-certificates_20260601~24.04.1_all.deb",
                               "policy": "BuildKit checksum-pinned official deb; Mozilla PEM bootstrap preserves TLS peer verification, then normal system trust. Bootstrap corresponding source is included separately."},
            "authentication": "APT update requires the Ubuntu archive keyring and authenticated indexes; exact InRelease hashes are pinned. No unauthenticated download fallback."}


def runtime(path: Path, target: Path) -> tuple[bytes, dict]:
    record = snapshot(path, target, executable=True)
    data = target.read_bytes()
    if record["sha256"] != RUNTIME_SHA or len(data) != RUNTIME_SIZE:
        fail("Runtime differs from the pinned official uruntime v0.8.1 release")
    if data[:6] != b"\x7fELF\x02\x01" or data[8:11] != b"AI\x02" or struct.unpack_from("<H", data, 18)[0] != 62:
        fail("Runtime is not an x86_64 Type-2 AppImage ELF")
    offset = struct.unpack_from("<Q", data, 32)[0]
    width, count = struct.unpack_from("<HH", data, 54)
    if width != 56 or offset + width * count > len(data):
        fail("Invalid pinned runtime program headers")
    if any(struct.unpack_from("<I", data, offset + width * index)[0] in (2, 3) for index in range(count)):
        fail("Runtime must be fully static, without PT_DYNAMIC/PT_INTERP")
    if data.count(b"URUNTIME_EXTRACT=3") != 1:
        fail("Unrecognized static runtime extraction policy")
    patched = data.replace(b"URUNTIME_EXTRACT=3", b"URUNTIME_EXTRACT=2")
    return patched, {**record, "url": RUNTIME_URL, "version": "v0.8.1",
                     "patched_sha256": hashlib.sha256(patched).hexdigest(),
                     "policy_change": "fixed-width URUNTIME_EXTRACT=3 -> 2: FUSE first, unconditional extraction fallback, upstream cleanup enabled"}


def acquire_runtime(cache: Path) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / RUNTIME_NAME
    if not path.exists():
        with urllib.request.urlopen(RUNTIME_URL, timeout=90) as response:
            data = response.read(RUNTIME_SIZE + 1)
        if len(data) != RUNTIME_SIZE or hashlib.sha256(data).hexdigest() != RUNTIME_SHA:
            fail("Official static runtime download checksum mismatch")
        with path.open("xb") as output:
            output.write(data)
        path.chmod(0o755)
    regular(path, RUNTIME_SIZE)
    return path


def inventory(root: Path) -> dict:
    result, total = {}, 0
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            fail(f"Unexpected symlink in asset-free AppDir: {path}")
        if path.is_dir():
            continue
        info = regular(path)
        total += info.st_size
        if len(result) >= 8192 or total > MAX_TOTAL:
            fail("Native AppDir exceeds inventory bounds")
        result[str(path.relative_to(root))] = {"sha256": digest(path), "size": info.st_size,
                                              "mode": stat.S_IMODE(info.st_mode)}
    return result


def identities(appdir: Path) -> dict:
    versions = {}
    for name, marker, flags in (
            ("kabus22", "kabus22 native engine ", ("--data",)),
            ("kabus22-setup", "kabus22 native installer ", ("--iso", "--engine", "--launcher", "--extractor")),
            ("kabus22-launcher", "kabus22 native launcher ", ("--install-dir", "--engine", "--renderer", "--frames", "--self-check")),
            ("kabus22-updater", "kabus22 native updater ", ("--install-dir", "--check", "--apply", "--rollback", "--status")),
            ("innoextract", "innoextract 1.9", ("--output-dir",))):
        binary = appdir / "usr/bin" / name
        version = command(binary, "--version").strip()
        help_text = command(binary, "--help")
        if marker not in version.lower() or any(flag not in help_text for flag in flags):
            fail(f"Missing real executable identity/CLI contract: {name}")
        versions[name] = {"version_output": version, "help_output": help_text}
    # One CMake project version: the updater/manifest rely on every native binary reporting it.
    if len({versions[name]["version_output"].split()[-1] for name in ("kabus22", "kabus22-setup", "kabus22-launcher", "kabus22-updater")}) != 1:
        fail("Native executables report different project versions")
    engine_symbols = command("readelf", "--dyn-syms", "--wide", appdir / "usr/bin/kabus22")
    if any(symbol not in engine_symbols for symbol in ("SDL_CreateWindow", "SDL_GL_CreateContext", "avformat_open_input")):
        fail("Engine lacks actual native window/OpenGL/media imports; refusing placeholder")
    installer_symbols = command("readelf", "--dyn-syms", "--wide", appdir / "usr/bin/kabus22-setup")
    if any(symbol not in installer_symbols for symbol in ("gtk_init_check", "archive_read_new")):
        fail("Installer lacks actual GTK/native archive imports; refusing placeholder")
    launcher_symbols = command("readelf", "--dyn-syms", "--wide", appdir / "usr/bin/kabus22-launcher")
    if any(symbol not in launcher_symbols for symbol in ("gtk_init_check", "g_spawn_async_with_pipes")):
        fail("Launcher lacks actual GTK/explicit child-spawn imports; refusing placeholder")
    updater_symbols = command("readelf", "--dyn-syms", "--wide", appdir / "usr/bin/kabus22-updater")
    if any(symbol not in updater_symbols for symbol in ("curl_easy_perform", "EVP_DigestVerify", "archive_read_new")):
        fail("Updater lacks actual HTTPS/Ed25519/archive imports; refusing placeholder")
    if b"KABUS22_UPDATER_TEST_API" in (appdir / "usr/bin/kabus22-updater").read_bytes():
        fail("Test-only HTTP updater build must never be packaged")
    return versions


def ode_evidence(build_root: Path, source_root: Path) -> dict:
    """Retain the official archive, applied recipes, actual modified sources and diffs."""
    target = source_root / "ode-0.16.6"
    files = {}
    for name in ("third_party/ode-0.16.6.tar.gz", "third_party/ode-LICENSE-BSD.TXT",
                 "third_party/ode-ou-LICENSE-BSD.TXT", "cmake/KabusODE.cmake",
                 *sorted({recipe for _, _, recipe, _ in ODE_MODIFICATIONS.values()})):
        files[name] = snapshot(ROOT / name, target / name)
    archive_path = target / "third_party/ode-0.16.6.tar.gz"
    if files["third_party/ode-0.16.6.tar.gz"]["sha256"] != ODE_SHA:
        fail("ODE source archive differs from the pinned official 0.16.6")
    modifications = []
    with tarfile.open(archive_path, "r:gz") as archive:
        for name, (original_sha, modified_sha, recipe_name, diff_name) in ODE_MODIFICATIONS.items():
            members = [member for member in archive.getmembers() if member.name.endswith("/" + name)]
            if len(members) != 1 or not members[0].isfile() or not 0 < members[0].size <= MAX_FILE:
                fail(f"Missing/ambiguous official ODE source: {name}")
            with archive.extractfile(members[0]) as stream:
                original = stream.read()
            if hashlib.sha256(original).hexdigest() != original_sha:
                fail(f"Official ODE source hash changed: {name}")
            modified_path = target / "modified" / name
            modified = snapshot(build_root / "_deps/kabus_ode-src" / name, modified_path)
            if modified["sha256"] != modified_sha:
                fail(f"Built ODE source is not the applied recipe result: {name}")
            files["modified/" + name] = modified
            recipe = (target / recipe_name).read_text()
            if original_sha not in recipe or modified_sha not in recipe:
                fail(f"ODE recipe does not declare the retained source relationship: {recipe_name}")
            # Bytes keep upstream CRLF line endings, so the diff shows only the applied change.
            diff_path = target / diff_name
            diff_path.write_bytes("".join(difflib.unified_diff(
                original.decode("utf-8").splitlines(keepends=True),
                modified_path.read_bytes().decode("utf-8").splitlines(keepends=True),
                fromfile="a/" + name, tofile="b/" + name)).encode("utf-8"))
            regular(diff_path)
            files[diff_name] = {"size": diff_path.stat().st_size, "sha256": digest(diff_path)}
            modifications.append({"path": name, "original_sha256": original_sha, "modified_sha256": modified_sha,
                                  "diff": diff_name, "recipe": recipe_name})
    return {"source_archive_sha256": ODE_SHA, "files": files, "modifications": modifications,
            "scope": "BSD-selected ODE dependency source and applied modifications; no license assigned to the native project."}


def build(args) -> None:
    output = args.output.absolute()
    report_path = output.with_name(output.name + ".json")
    source_bundle_path = output.with_name(output.name + ".sources.tar.gz")
    if any(path.exists() or path.is_symlink() for path in (output, report_path, source_bundle_path)):
        fail("Refusing to overwrite an existing image/report/source bundle; choose a new output path")
    output.parent.mkdir(parents=True, exist_ok=True)
    tools = ("readelf", "ldconfig", "mksquashfs", "unsquashfs", "glib-compile-schemas", "dpkg-query", "dpkg", "apt-cache", "apt-get")
    for tool in tools:
        if not shutil.which(tool):
            fail(f"Missing build-time native tool: {tool}")
    patcher = Path(args.patchelf).absolute() if os.sep in args.patchelf else Path(shutil.which(args.patchelf) or args.patchelf).absolute()
    regular(patcher)
    elf(patcher, executable=True)
    patch_version = command(patcher, "--version").strip()
    if not patch_version.startswith("patchelf "):
        fail("Invalid native patchelf executable identity")
    with tempfile.TemporaryDirectory(prefix="kabus22-appimage-", dir=output.parent) as scratch:
        work = Path(scratch)
        appdir = work / "AppDir"
        (appdir / "usr/bin").mkdir(parents=True)
        (appdir / "usr/lib").mkdir()
        source_root = work / "corresponding-source"
        source_root.mkdir()
        distro = ubuntu_evidence(source_root / "evidence")
        distro["build_recipe"] = snapshot(ROOT / "tools/Dockerfile.ubuntu24", source_root / "Dockerfile.ubuntu24")
        distro["build_caches"] = {}
        distro["assembly_recipe"] = snapshot(ROOT / "tools/package_kabus_appimage.py", source_root / "package_kabus_appimage.py")
        distro["baseline_driver"] = snapshot(ROOT / "tools/build_kabus_baseline.sh", source_root / "build_kabus_baseline.sh")
        for name, binary in (("engine", args.engine), ("installer", args.installer), ("launcher", args.launcher), ("updater", args.updater)):
            build_cache = binary.absolute().parent / "CMakeCache.txt"
            text = build_cache.read_text()
            for language in (("C", "CXX") if name == "engine" else ("CXX",)):
                match = re.search(r"^CMAKE_" + language + r"_FLAGS:STRING=(.*)$", text, re.MULTILINE)
                if not match or "-march=x86-64" not in match[1].split() or "-mtune=generic" not in match[1].split():
                    fail(f"Missing generic x86-64 build flags: {build_cache}: {language}")
                if any(flag.startswith(("-march=", "-mtune=")) and flag not in ("-march=x86-64", "-mtune=generic") for flag in match[1].split()):
                    fail(f"Conflicting CPU build flags: {build_cache}")
            distro["build_caches"][name] = snapshot(build_cache, source_root / (name + "-CMakeCache.txt"))
        ffmpeg_report = ffmpeg_evidence(appdir, source_root)
        cache = loader_cache()
        inputs, metadata, packages, host_dependencies = {}, {}, {}, {}
        pending = []
        for name, source in (("kabus22", args.engine), ("kabus22-setup", args.installer),
                             ("kabus22-launcher", args.launcher), ("kabus22-updater", args.updater),
                             ("innoextract", args.innoextract)):
            source = source.absolute()
            target = appdir / "usr/bin" / name
            record = snapshot(source, target, executable=True)
            if name == "innoextract" and record["sha256"] != HELPER_SHA:
                fail("Only the signed upstream 1.9 amd64 helper is trusted; private x86_64_v3 helper is not a release payload")
            info = elf(target, executable=True)
            inputs[str(target.relative_to(appdir))] = record
            pending.append((source, target, info))
        while pending:
            source, target, info = pending.pop()
            baseline(info, str(target.relative_to(appdir)))
            metadata[str(target.relative_to(appdir))] = info
            for name in info["needed"]:
                resolved = dependency(name, source, info, cache)
                if HOST.fullmatch(name):
                    host_dependencies[name] = {"build_host_path": str(resolved), "build_host_sha256": digest(resolved),
                                               "policy": "Host-provided ABI; never bundled"}
                    continue
                if name in FFMPEG_LIBRARIES:
                    expected_path = FFMPEG_ROOT / "lib" / FFMPEG_LIBRARIES[name]
                    if resolved != expected_path or digest(resolved) != ffmpeg_report["files"]["lib/" + resolved.name]["sha256"]:
                        fail(f"FFmpeg dependency is not the approved LGPL source-built component: {resolved}")
                    ownership = {"component": "ffmpeg-6.1.1"}
                else:
                    if re.fullmatch(r"lib(?:avcodec|avformat|avutil|swscale|swresample)\.so(?:\.[0-9]+)*", name):
                        fail(f"Unapproved FFmpeg SONAME/version: {name}")
                    ownership = {"package": package_notices(resolved, appdir, packages)}
                destination = appdir / "usr/lib" / name
                if destination.exists():
                    if inputs[str(destination.relative_to(appdir))]["sha256"] != digest(resolved):
                        fail(f"Colliding native dependency SONAME: {name}")
                    continue
                record = snapshot(resolved, destination)
                library = elf(destination, expected_soname=name)
                record.update(ownership)
                inputs[str(destination.relative_to(appdir))] = record
                pending.append((resolved, destination, library))
        for name, info in metadata.items():
            target = appdir / name
            if info["needed"]:
                rpath = "$ORIGIN/../lib" if name.startswith("usr/bin/") else "$ORIGIN"
                command(patcher, "--force-rpath", "--set-rpath", rpath, target)
                patched = elf(target, expected_soname=info["soname"] if name.startswith("usr/lib/") else None)
                baseline(patched, name)
                if patched["rpaths"] != [rpath] or patched["needed"] != info["needed"]:
                    fail(f"Native RPATH/dependency mutation failed: {name}")
                for imported in patched["needed"]:
                    if not HOST.fullmatch(imported) and not (appdir / "usr/lib" / imported).is_file():
                        fail(f"Incomplete packaged dependency closure: {name} -> {imported}")
                metadata[name] = patched
        license_report = {
            "helper": notices(ROOT / "packaging/helper-innoextract", appdir, "innoextract"),
            "runtime": notices(ROOT / "packaging/runtime-uruntime", appdir, "uruntime")}
        license_report["packaging_notice"] = snapshot(
            ROOT / "packaging/NOTICE.txt", appdir / "usr/share/licenses/kabus22/dependencies/NOTICE.txt")
        license_report["ode"] = {
            "version": "0.16.6",
            "commit": "a5fc0bc282703ab34d7d3d2500bafae7428eeaa9",
            **ode_evidence(args.engine.absolute().parent, source_root),
            "notices": [
                snapshot(ROOT / "third_party" / name,
                         appdir / "usr/share/licenses/kabus22/dependencies/ode" / name)
                for name in ("ode-LICENSE-BSD.TXT", "ode-ou-LICENSE-BSD.TXT")]}
        license_report["fidelityfx"] = {
            "version": "FidelityFX SDK v1.1.4 (FSR 3.1.4 upscaler + frame generation, statically linked)",
            "notices": [
                snapshot(ROOT / "third_party/FidelityFX-SDK" / name,
                         appdir / "usr/share/licenses/kabus22/dependencies/fidelityfx" / name)
                for name in ("LICENSE.txt", "KABUS-PROVENANCE.txt")]}
        license_report["ffmpeg"] = ffmpeg_report
        common = Path("/usr/share/common-licenses")
        if not common.is_dir():
            fail("Missing distro common license texts")
        for source in sorted(common.rglob("*")):
            if source.is_file():
                snapshot(source.resolve(strict=True), appdir / "usr/share/licenses/kabus22/dependencies/common" / source.relative_to(common))
        schemas = appdir / "usr/share/glib-2.0/schemas"
        schemas.mkdir(parents=True)
        schema_root = Path("/usr/share/glib-2.0/schemas")
        for name in ("org.gtk.Settings.ColorChooser", "org.gtk.Settings.Debug", "org.gtk.Settings.EmojiChooser", "org.gtk.Settings.FileChooser"):
            source = schema_root / (name + ".gschema.xml")
            record = snapshot(source, schemas / source.name)
            record["package"] = package_notices(source, appdir, packages)
            inputs[str((schemas / source.name).relative_to(appdir))] = record
        command("glib-compile-schemas", "--strict", schemas)
        # Header-only MIT nlohmann/json is compiled into kabus22-updater: retain its distro notice and source.
        license_report["compiled_in"] = {"nlohmann_json": package_notices(Path("/usr/include/nlohmann/json.hpp"), appdir, packages)}
        source_packages = {**packages, "bootstrap:ca-certificates": distro["https_bootstrap"]}
        sources = source_evidence(source_packages, args.source_cache.absolute(), source_root / "packages", args.acquire_sources)
        if args.acquire_sources:
            print(f"Acquired exact corresponding sources in {args.source_cache}; rerun without --acquire-sources on --network=none to assemble")
            return
        (source_root / "source-inventory.json").write_text(json.dumps(sources, indent=2, sort_keys=True) + "\n")
        source_inventory = inventory(source_root)
        source_bundle = work / "sources.tar.gz"
        with tarfile.open(source_bundle, "w:gz") as archive:
            archive.add(source_root, arcname="corresponding-source")
        source_report = {"filename": source_bundle_path.name, "sha256": digest(source_bundle),
                         "size": source_bundle.stat().st_size, "inventory": source_inventory,
                         "sources": sources, "ode": license_report["ode"],
                         "scope": "Exact Ubuntu dependencies, BSD-selected modified ODE, and approved LGPL FFmpeg source/build evidence; not native project corresponding source or a public source offer."}
        (appdir / "AppRun").write_text(APPRUN)
        (appdir / "AppRun").chmod(0o755)
        (appdir / "kabus22-installer.desktop").write_text(DESKTOP)
        (appdir / "kabus22-installer.svg").write_text(ICON)
        versions = identities(appdir)
        runtime_path = args.runtime or acquire_runtime(args.cache)
        runtime_data, runtime_report = runtime(runtime_path, work / "runtime")
        metadata["AppImage static runtime"] = elf(work / "runtime", executable=True)
        baseline(metadata["AppImage static runtime"], "AppImage static runtime")
        requirements = sorted({version for info in metadata.values() for version in info["required_symbol_versions"]})
        glibc = [version[6:] for version in requirements if re.fullmatch(r"GLIBC_[0-9.]+", version)]
        glibc_min = max(glibc, key=lambda version: tuple(map(int, version.split(".")))) if glibc else None
        cpu_architectures = sorted({item["architecture"] for item in packages.values()})
        report = {"format": "native Kabus22 Type-2 ELF + SquashFS AppImage", "asset_free": True,
                  "inputs": inputs, "elf": metadata, "packages": packages,
                  "host_dependencies": host_dependencies, "executable_identities": versions,
                  "required_symbol_versions": requirements, "minimum_glibc_from_symbol_versions": glibc_min,
                  "distro_dependency_cpu_architectures": cpu_architectures,
                  "cpu_policy": "Reject GNU ISA-needed properties above x86-64-baseline and GLIBC imports above 2.39. Absence of an ISA note is not proof of baseline CPU compatibility; amd64 is package architecture, not CPU proof. No portable-release assertion.",
                  "runtime": runtime_report, "licenses": license_report,
                  "ubuntu": distro, "corresponding_source": source_report,
                  "public_release_gate": "Publication requires project-owner authorization, matching corresponding sources/notices, LGPL replacement/relink access and separate runtime qualification. This packager assigns no native-project license and does not publish or certify legal compliance. Ubuntu GPL FFmpeg binaries are not permitted in this closure.",
                  "patchelf": {"path": str(patcher), "sha256": digest(patcher), "version": patch_version},
                  "installed_payload_policy": "versions/<v>/bin carries kabus22, kabus22-launcher, kabus22-updater (signed-update client; release build only, no test HTTP hooks), kabus22-setup and innoextract (the launcher's Installation page runs them), plus the bundled lib closure, GTK schemas and dependency notices. Desktop/post-install launch opens settings; renderer actions select a UI preference, not automatic play.",
                  "acceptance": "Assembly/integrity checks only. Parent must exercise actual AppImage GTK launch, original ISO installation, standalone installed launcher and engine, shortcuts, cancellation and private saves; ELF identities cannot prove gameplay."}
        internal_report = appdir / "usr/share/licenses/kabus22/dependencies/packaging.json"
        internal_report.parent.mkdir(parents=True, exist_ok=True)
        internal_report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        expected = inventory(appdir)
        squash = work / "payload.squashfs"
        command("mksquashfs", appdir, squash, "-noappend", "-all-root", "-no-xattrs", "-no-exports",
                "-nopad", "-comp", "gzip", "-mkfs-time", "0", "-all-time", "0",
                "-processors", "2", "-no-progress")
        with squash.open("rb") as incoming:
            if incoming.read(4) != b"hsqs":
                fail("mksquashfs did not produce a real SquashFS payload")
        image = work / "image.AppImage"
        with image.open("xb") as outgoing, squash.open("rb") as incoming:
            outgoing.write(runtime_data)
            shutil.copyfileobj(incoming, outgoing, length=1024 * 1024)
        image.chmod(0o755)
        extracted = work / "verified"
        command("unsquashfs", "-no-progress", "-o", str(len(runtime_data)), "-d", extracted, image)
        if inventory(extracted) != expected:
            fail("Assembled AppImage inventory differs from validated snapshots")
        report["inventory"] = expected
        report["appimage"] = {"sha256": digest(image), "size": image.stat().st_size,
                              "squashfs_offset": len(runtime_data)}
        report_file = work / "report.json"
        report_file.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        # Atomic, no-replace publication: never clobber an unrelated image or report.
        published = []
        try:
            for source, destination in ((source_bundle, source_bundle_path), (report_file, report_path), (image, output)):
                os.link(source, destination)
                published.append(destination)
        except BaseException:
            for destination in reversed(published):
                destination.unlink()
            raise
        print(f"Packaged {output}: sha256:{report['appimage']['sha256']}")
        print(f"ABI/license/inventory report: {report_path}; required glibc >= {glibc_min}; CPU compatibility not asserted")


def self_check() -> None:
    """Small runnable security/policy check; does not create a fake game or AppImage."""
    assert HOST.fullmatch("libc.so.6") and HOST.fullmatch("libGLX_nvidia.so.0")
    assert not HOST.fullmatch("libstdc++.so.6") and not HOST.fullmatch("libSDL2-2.0.so.0")
    for name in ("libwayland-client.so.0", "libwayland-egl.so.1",
                 "libwayland-cursor.so.0", "libwayland-server.so.0"):
        assert HOST.fullmatch(name)
    assert not HOST.fullmatch("libwayland-custom.so.0")
    # Exact signed-snapshot GTK rows: empty-valued headers precede continuations.
    gtk_sha256 = (
        "47da61487af3087a94bc49296fd025ca0bc02f96ef06c556e7c8988bd651b6fa 13188312 gtk+3.0_3.24.41.orig.tar.xz",
        "1a82c70d722296f15af0a00a737a0c552580c6bf3f527f9ef7e66f192fe1831c 3666360 gtk+3.0_3.24.41-4ubuntu1.3.debian.tar.xz",
        "b7ea9dda7ffd3f01f97d7ee673f130a82db5b346ade2d003e609e27886b35269 4996 gtk+3.0_3.24.41-4ubuntu1.3.dsc")
    stanza = deb822("Package: gtk+3.0\nVersion: 3.24.41-4ubuntu1.3\nChecksums-Sha256:\n "
                   + "\n ".join(gtk_sha256) + "\n\nPackage: separate\nEmpty:\n")[0]
    assert stanza["Package"] == "gtk+3.0" and stanza["Version"] == "3.24.41-4ubuntu1.3"
    assert stanza["Checksums-Sha256"].splitlines() == list(gtk_sha256)
    for version in ("2.9", "2.39"):
        assert baseline({"required_symbol_versions": ["GLIBC_" + version], "gnu_notes": ""}, "regression") == version
    for info in ({"required_symbol_versions": ["GLIBC_2.43"], "gnu_notes": ""},
                 {"required_symbol_versions": [], "gnu_notes": "x86 ISA needed: x86-64-v2"},
                 {"required_symbol_versions": [], "gnu_notes": "x86 ISA needed: x86-64-v4"}):
        try:
            baseline(info, "regression")
        except RuntimeError:
            pass
        else:
            raise AssertionError("above-baseline ELF admitted")
    library_path = loader_cache()["libcap.so.2"].resolve(strict=True)
    library = elf(library_path, expected_soname="libcap.so.2")
    assert library["elf_type"] == 3 and library["soname"] == "libcap.so.2"
    for path, soname in ((library_path, "lib-not-the-import.so"),
                         (Path(sys.executable).resolve(strict=True), "libcap.so.2")):
        try:
            elf(path, expected_soname=soname)
        except RuntimeError:
            pass
        else:
            raise AssertionError("mismatched SONAME or executable admitted as dependency")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "input").write_bytes(b"checked snapshot")
        (root / "lib-choice.so").write_bytes(b"undeclared neighboring file")
        declared = root / "declared"
        declared.mkdir()
        (declared / "lib-choice.so").write_bytes(b"declared loader path")
        assert dependency("lib-choice.so", root / "input",
                          {"rpaths": ["$ORIGIN/declared"]}, {}) == declared / "lib-choice.so"
        assert dependency("lib-choice.so", root / "input", {"rpaths": []},
                          {"lib-choice.so": declared / "lib-choice.so"}) == declared / "lib-choice.so"
        try:
            dependency("lib-choice.so", root / "input", {"rpaths": []}, {})
        except RuntimeError:
            pass
        else:
            raise AssertionError("undeclared neighboring library admitted")
        if shutil.which("dpkg-query"):
            try:
                installed_owner(root / "input")
            except RuntimeError:
                pass
            else:
                raise AssertionError("unowned dependency admitted")
        (root / "symlink").symlink_to(root / "input")
        try:
            regular(root / "symlink")
        except RuntimeError:
            pass
        else:
            raise AssertionError("symlink accepted")
        (root / "script").write_bytes(b"#!/bin/sh\nexit 0\n")
        try:
            elf(root / "script")
        except RuntimeError:
            pass
        else:
            raise AssertionError("shell accepted as ELF")
    print("Passed GLIBC/ISA baseline, loader precedence, symlink, native ELF and ownership policy checks")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, help="Parent-verified real native kabus22 ELF")
    parser.add_argument("--installer", type=Path, help="Real native GTK kabus22-setup ELF")
    parser.add_argument("--launcher", type=Path, help="Real standalone GTK kabus22-launcher ELF")
    parser.add_argument("--updater", type=Path, help="Real release-build kabus22-updater ELF (never the -testhttp build)")
    parser.add_argument("--innoextract", type=Path, default=ROOT / "packaging/helper-innoextract/bin/amd64/innoextract")
    parser.add_argument("--runtime", type=Path, help="Optional exact pinned official unmodified static uruntime ELF")
    parser.add_argument("--cache", type=Path, default=ROOT / "build-appimage/cache")
    parser.add_argument("--source-cache", type=Path, default=Path("/work/sources"), help="Exact authenticated distro source archives acquired during provisioning")
    parser.add_argument("--acquire-sources", action="store_true", help="Network-enabled source provisioning only; no image publication. Rerun ordinary assembly offline.")
    parser.add_argument("--patchelf", default="patchelf", help="Build-time native patchelf path or PATH command")
    parser.add_argument("--output", type=Path, help="New output .AppImage path (never overwritten)")
    parser.add_argument("--self-check", action="store_true", help="Run policy regressions without packaging any engine")
    args = parser.parse_args()
    if args.self_check:
        self_check()
        return
    if not args.engine or not args.installer or not args.launcher or not args.updater or not args.output:
        parser.error("--engine, --installer, --launcher, --updater and --output are required")
    if args.output.suffix != ".AppImage":
        parser.error("--output must end in .AppImage")
    build(args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        sys.exit(1)
