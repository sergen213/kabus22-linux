#!/usr/bin/env python3
"""End-to-end kabus22-updater check against a local HTTP fixture (CTest).

Uses the test-only kabus22-updater-testhttp build (K22_UPDATER_TEST_HTTP: admits
http://127.0.0.1 and env-supplied API base/public key) and the real release tools in
tools/release. The shipped kabus22-updater is only checked offline (identity, status,
absence of the test hooks). Usage: check_updater_e2e.py TESTHTTP_UPDATER RELEASE_UPDATER
"""
from __future__ import annotations

import functools
import hashlib
import http.server
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import threading
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent / "tools/release"


def run(*args, check=True, env=None) -> subprocess.CompletedProcess:
    result = subprocess.run([str(arg) for arg in args], env=env, capture_output=True, text=True, timeout=120)
    if check and result.returncode:
        raise AssertionError(f"{args} failed {result.returncode}:\n{result.stdout}\n{result.stderr}")
    return result


def snapshot(root: Path) -> dict:
    """Everything except layout-owned entries; must never change."""
    result = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if relative.parts[0] in ("bin", "lib", "share", "current", "versions"):
            continue
        result[str(relative)] = path.read_bytes() if path.is_file() and not path.is_symlink() else "dir"
    return result


def full(root: Path) -> dict:
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            result[str(path.relative_to(root))] = "->" + os.readlink(path)
        elif path.is_file():
            result[str(path.relative_to(root))] = path.read_bytes()
    return result


def tree(base: Path, marker: str) -> Path:
    for name in ("kabus22", "kabus22-launcher", "kabus22-setup", "kabus22-updater", "innoextract"):
        (base / "bin").mkdir(parents=True, exist_ok=True)
        (base / "bin" / name).write_text(f"#!/bin/sh\necho {name} {marker}\n")
        (base / "bin" / name).chmod(0o755)
    (base / "lib").mkdir()
    (base / "lib/libk22test.so.1").write_text(marker)
    (base / "lib/libk22test.so").symlink_to("libk22test.so.1")
    (base / "share/licenses").mkdir(parents=True)
    (base / "share/licenses/NOTICE.txt").write_text("notice " + marker)
    return base


def release(tag: str, prerelease: bool, port: int, directory: str, names: list[str]) -> dict:
    return {"tag_name": "v" + tag, "draft": False, "prerelease": prerelease, "body": f"Notes for {tag}",
            "published_at": "2026-10-09T00:00:00Z",
            "assets": [{"name": name, "size": 0, "browser_download_url": f"http://127.0.0.1:{port}/{directory}/{name}"}
                       for name in names]}


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args) -> None:  # noqa: A002 (base signature)
        pass


def main() -> None:
    testhttp, shipped = Path(sys.argv[1]), Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="k22-updater-e2e-") as scratch:
        work = Path(scratch)
        key, wrong = work / "key", work / "wrong-key"
        public = run(sys.executable, TOOLS / "sign_manifest.py", "--generate-key", key).stdout.strip()
        run(sys.executable, TOOLS / "sign_manifest.py", "--generate-key", wrong)
        assert oct(key.stat().st_mode & 0o777) == "0o600" and public not in key.read_text()
        assert run(sys.executable, TOOLS / "sign_manifest.py", "--public-key", "--key", key).stdout.strip() == public
        server = work / "server"
        def payload_names(version):
            return ["kabus22-update.json", "kabus22-update.json.sig", f"kabus22-{version}-linux-x86_64.tar.gz"]

        def build(version: str, channel: str, directory: str, signer: Path = key) -> Path:
            out = server / directory
            run(sys.executable, TOOLS / "make_update_manifest.py", "--tree", tree(work / ("tree-" + directory), version),
                "--version", version, "--channel", channel, "--output-dir", out)
            run(sys.executable, TOOLS / "sign_manifest.py", out / "kabus22-update.json", "--key", signer)
            return out

        def handmade(version: str, directory: str, members: list[tuple[str, bytes]], manifest_version: str | None = None) -> None:
            out = server / directory
            out.mkdir(parents=True)
            buffer = io.BytesIO()
            with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
                for name, data in members:
                    info = tarfile.TarInfo(name)
                    info.size, info.mode = len(data), 0o755
                    archive.addfile(info, io.BytesIO(data))
            data = buffer.getvalue()
            name = f"kabus22-{manifest_version or version}-linux-x86_64.tar.gz"
            (out / f"kabus22-{version}-linux-x86_64.tar.gz").write_bytes(data)
            (out / name).write_bytes(data)
            manifest = {"format": 1, "version": manifest_version or version, "channel": "stable", "min_glibc": "2.17",
                        "payload": {"name": name, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}}
            (out / "kabus22-update.json").write_text(json.dumps(manifest))
            run(sys.executable, TOOLS / "sign_manifest.py", out / "kabus22-update.json", "--key", key)

        # Release tooling refuses non-payload content.
        bad_tree = tree(work / "bad-tree", "x")
        (bad_tree / "game").mkdir()
        assert run(sys.executable, TOOLS / "make_update_manifest.py", "--tree", bad_tree, "--version", "9.9.9",
                   "--channel", "stable", "--output-dir", work / "refused", check=False).returncode
        (bad_tree / "game").rmdir()
        (bad_tree / "share/K22.wrs").write_text("asset")
        assert run(sys.executable, TOOLS / "make_update_manifest.py", "--tree", bad_tree, "--version", "9.9.9",
                   "--channel", "stable", "--output-dir", work / "refused", check=False).returncode

        build("0.4.11", "stable", "good-0.4.11")
        build("0.5.0-beta.1", "experimental", "good-0.5.0-beta.1")
        tampered = build("0.4.12", "stable", "bad-0.4.12")
        payload = tampered / "kabus22-0.4.12-linux-x86_64.tar.gz"
        payload.write_bytes(payload.read_bytes()[:-1] + bytes([payload.read_bytes()[-1] ^ 1]))
        build("0.4.13", "stable", "bad-0.4.13", signer=wrong)
        edited = build("0.4.14", "stable", "bad-0.4.14")
        (edited / "kabus22-update.json").write_text((edited / "kabus22-update.json").read_text().replace("2.38", "2.17"))
        build("0.4.15", "experimental", "bad-0.4.15")
        handmade("0.4.16", "bad-0.4.16", [("bin/kabus22", b"x"), ("bin/kabus22-launcher", b"x"), ("bin/kabus22-setup", b"x"), ("bin/kabus22-updater", b"x"),
                                         ("../escaped", b"x")])
        handmade("0.4.17", "bad-0.4.17", [("bin/kabus22", b"x"), ("bin/kabus22-launcher", b"x"), ("bin/kabus22-setup", b"x"), ("bin/kabus22-updater", b"x")],
                 manifest_version="0.4.11")

        handler = functools.partial(Quiet, directory=str(server))
        httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        port = httpd.server_address[1]
        good = [release("0.4.11", False, port, "good-0.4.11", payload_names("0.4.11")),
                release("0.5.0-beta.1", True, port, "good-0.5.0-beta.1", payload_names("0.5.0-beta.1"))]
        bad = [release(version, False, port, "bad-" + version, payload_names(version))
               for version in ("0.4.12", "0.4.13", "0.4.14", "0.4.15", "0.4.16", "0.4.17")]
        for repository, releases in (("o/n", good), ("o/bad", bad)):
            (server / "repos" / repository).mkdir(parents=True)
            (server / "repos" / repository / "releases").write_text(json.dumps(releases))
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        try:
            root = work / "install"
            for name in ("kabus22", "kabus22-launcher"):
                (root / "bin").mkdir(parents=True, exist_ok=True)
                (root / "bin" / name).write_text("legacy " + name)
                (root / "bin" / name).chmod(0o755)
            (root / "lib").mkdir()
            (root / "lib/libold.so.1").write_text("old")
            (root / "share/licenses").mkdir(parents=True)
            (root / ".kabus22-owned").write_text("Kabus22 native installation 1\n")
            (root / "game").mkdir()
            (root / "game/K22.wrs").write_text("original asset")
            (root / "kayit1.sav").write_text("save slot")
            (root / "options.k22").write_text("options")
            untouched = snapshot(root)
            config = work / "config/kabus22"
            config.mkdir(parents=True)
            (config / "updates.ini").write_text("[Updates]\nrepository=o/n\nchannel=stable\n")
            env = {**os.environ, "HOME": str(work), "XDG_CONFIG_HOME": str(work / "config"),
                   "KABUS22_UPDATER_TEST_API": f"http://127.0.0.1:{port}", "KABUS22_UPDATER_TEST_PUBLIC_KEY": public}
            def updater(*args, check=True):
                return run(testhttp, "--install-dir", root, *args, check=check, env=env)

            for binary in (testhttp, shipped):
                assert run(binary, "--version").stdout.startswith("kabus22 native updater 0.4.10")
                assert "--rollback" in run(binary, "--help").stdout
            assert b"KABUS22_UPDATER_TEST_API" not in shipped.read_bytes() and b"KABUS22_UPDATER_TEST_API" in testhttp.read_bytes()
            assert run(shipped, "--install-dir", root, "--check", "--repository", "bad", check=False).returncode == 2

            check = json.loads(updater("--check", "--json").stdout)
            assert check == {"current": "0.4.10", "latest": "0.4.11", "channel": "stable", "update_available": True,
                             "notes": "Notes for 0.4.11", "published": "2026-10-09T00:00:00Z", "repository": "o/n"}, check
            assert json.loads(updater("--check", "--json", "--channel", "experimental").stdout)["latest"] == "0.5.0-beta.1"
            assert "update available" in updater("--check").stdout

            applied = updater("--apply")
            lines = [line for line in applied.stdout.splitlines() if line.startswith("progress ")]
            assert lines[0].startswith("progress 0 ") and lines[-1].startswith("progress 100 "), applied.stdout
            assert all(0 <= int(line.split()[1]) <= 100 for line in lines)
            assert os.readlink(root / "current") == "versions/0.4.11" and os.readlink(root / "bin") == "current/bin"
            assert "kabus22 0.4.11" in run(root / "bin/kabus22").stdout
            assert (root / "versions/0.4.10/bin/kabus22").read_text() == "legacy kabus22"
            inventory = json.loads((root / "versions/0.4.11/.kabus22-payload.json").read_text())
            assert inventory["version"] == "0.4.11" and any(item["path"] == "bin/kabus22-updater" for item in inventory["files"])
            assert json.loads(updater("--status").stdout) == {"active": "0.4.11", "previous": "0.4.10", "installed": ["0.4.10", "0.4.11"]}
            assert snapshot(root) == untouched
            assert "Already up to date" in updater("--apply").stdout

            before = full(root)
            for version, reason in (("0.4.12", "SHA-256 mismatch"), ("0.4.13", "signature is invalid"),
                                    ("0.4.14", "signature is invalid"), ("0.4.15", "not a stable release"),
                                    ("0.4.16", "Unsafe payload path"), ("0.4.17", "does not match release")):
                result = updater("--apply", "--repository", "o/bad", "--version", version, check=False)
                assert result.returncode == 1 and reason in result.stderr, (version, result.stdout, result.stderr)
                assert full(root) == before, f"{version} changed the installation"
            assert not (work / "escaped").exists() and not (root / "escaped").exists()

            updater("--apply", "--channel", "experimental")
            status = json.loads(updater("--status").stdout)
            assert status == {"active": "0.5.0-beta.1", "previous": "0.4.11", "installed": ["0.4.11", "0.5.0-beta.1"]}, status
            assert "0.5.0-beta.1 -> 0.4.11" in updater("--rollback").stdout
            assert "kabus22 0.4.11" in run(root / "bin/kabus22").stdout
            assert json.loads(run(shipped, "--install-dir", root, "--status", env=env).stdout)["active"] == "0.4.11"
            assert snapshot(root) == untouched
        finally:
            httpd.shutdown()
    print("Updater end-to-end check/apply/reject/rollback passed")


if __name__ == "__main__":
    main()
