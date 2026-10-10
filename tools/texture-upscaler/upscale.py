#!/usr/bin/env python3
"""Build a Kabus 22 texture pack (Contract E) from an engine --dump-textures directory (Contract D)."""
import argparse, os, re, shutil, subprocess, sys, tempfile
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
import numpy as np
from PIL import Image, ImageFilter

KEY_RE = re.compile(r"[0-9a-f]{16}")
KINDS = ("albedo", "normal", "roughness", "metallic", "ao")
CATEGORIES = ("world", "model", "sprite", "sky", "ui", "font", "other")
PROTECTED = ("ui", "font")  # never AI, never smoothed: keeps Turkish glyphs/signage exact
AI_CATEGORIES = ("world", "model", "sky", "sprite")
MAX_SIDE, MAX_BYTES = 8192, 64 << 20
REALESRGAN = "realesrgan-ncnn-vulkan"


def fnv1a64_bytes(data, h=0xcbf29ce484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def fnv1a64(width, height, rgba):
    """Contract E key: FNV-1a-64 over LE uint32 width, LE uint32 height, then RGBA8 rows top-first."""
    return f"{fnv1a64_bytes(bytes(rgba), fnv1a64_bytes(width.to_bytes(4, 'little') + height.to_bytes(4, 'little'))):016x}"


def texture_key(img):
    img = img.convert("RGBA")
    return fnv1a64(img.width, img.height, img.tobytes())


def read_index(dump):
    rows = []
    with open(os.path.join(dump, "index.tsv"), encoding="utf-8", errors="surrogateescape") as f:  # source is informational; tolerate raw CP1254
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            p = line.split("\t", 4)
            if len(p) != 5 or not KEY_RE.fullmatch(p[0]) or not p[1].isdigit() or not p[2].isdigit() or p[3] not in CATEGORIES:
                raise ValueError(f"index.tsv:{n}: malformed row {line!r}")
            rows.append((p[0], int(p[1]), int(p[2]), p[3], p[4]))
    return rows


def realesrgan_path():
    return shutil.which(REALESRGAN)


# --- scalers: uint8 HxWxC -> uint8 (H*s)x(W*s)xC, pure integer scale, no crop/pad of the result ---

def nearest(a, s):
    return a.repeat(s, 0).repeat(s, 1)


def scale2x(a):
    """EPX/Scale2x: edge-preserving, only ever emits colors already present in the source."""
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode="edge")
    A, B, C, D, P = p[:-2, 1:-1], p[1:-1, 2:], p[1:-1, :-2], p[2:, 1:-1], a
    eq = lambda x, y: (x == y).all(-1, keepdims=True)
    e0 = np.where(eq(C, A) & ~eq(C, D) & ~eq(A, B), A, P)
    e1 = np.where(eq(A, B) & ~eq(A, C) & ~eq(B, D), B, P)
    e2 = np.where(eq(D, C) & ~eq(D, B) & ~eq(C, A), C, P)
    e3 = np.where(eq(B, D) & ~eq(B, A) & ~eq(D, C), D, P)
    h, w, c = a.shape
    out = np.empty((h * 2, w * 2, c), a.dtype)
    out[0::2, 0::2], out[0::2, 1::2], out[1::2, 0::2], out[1::2, 1::2] = e0, e1, e2, e3
    return out


def xbr_like(a, s):
    while s > 1:
        a, s = scale2x(a), s // 2
    return a


def _padded(a, pad, wrap):
    return np.pad(a, ((pad, pad), (pad, pad), (0, 0)), mode="wrap" if wrap else "edge")


def lanczos(a, s, wrap=False, sharpen=False):
    """Lanczos on a wrap/edge-padded copy, then crop exactly pad*s: tiling seams stay continuous, UVs stay aligned."""
    pad = min(8, a.shape[0], a.shape[1])
    p = _padded(a, pad, wrap)
    chans = []
    for c in range(a.shape[2]):
        im = Image.fromarray(p[..., c]).resize((p.shape[1] * s, p.shape[0] * s), Image.LANCZOS)
        if sharpen:
            im = im.filter(ImageFilter.UnsharpMask(radius=0.6 * s, percent=35, threshold=2))
        chans.append(np.asarray(im))
    out = np.stack(chans, -1)
    return out[pad * s: (pad + a.shape[0]) * s, pad * s: (pad + a.shape[1]) * s]


def realesrgan(a, s, wrap=False, exe=None):
    """External Real-ESRGAN (ncnn/Vulkan) on RGB; x4plus model, Lanczos-reduced for 2x."""
    exe = exe or realesrgan_path()
    if not exe:
        raise RuntimeError(f"{REALESRGAN} not found on PATH")
    pad = min(8, a.shape[0], a.shape[1])
    p = _padded(a[..., :3], pad, wrap)
    with tempfile.TemporaryDirectory(prefix="k22-esrgan-") as d:
        src, dst = os.path.join(d, "in.png"), os.path.join(d, "out.png")
        Image.fromarray(p, "RGB").save(src)
        try:
            r = subprocess.run([exe, "-i", src, "-o", dst, "-s", "4", "-n", "realesrgan-x4plus", "-f", "png"],
                               capture_output=True, text=True, timeout=600)
        except (OSError, subprocess.TimeoutExpired) as e:
            raise RuntimeError(f"could not run: {e}") from e
        if r.returncode or not os.path.exists(dst):
            raise RuntimeError(f"{REALESRGAN} failed ({r.returncode}): {(r.stderr or r.stdout).strip()[-300:]}")
        im = Image.open(dst).convert("RGB")
    if im.size != (p.shape[1] * 4, p.shape[0] * 4):
        raise RuntimeError(f"{REALESRGAN} returned {im.size}, expected 4x")
    if s != 4:
        im = im.resize((p.shape[1] * s, p.shape[0] * s), Image.LANCZOS)
    out = np.asarray(im)
    return out[pad * s: (pad + a.shape[0]) * s, pad * s: (pad + a.shape[1]) * s]


def bleed(a, passes=4):
    """Extend visible colours into fully transparent texels (alpha 0) so resampling never pulls colour-key RGB into
    visible edges. Straight alpha stays straight: no premultiply/divide, which amplifies noise at low alpha."""
    rgb, w = a[..., :3].astype(np.float32), (a[..., 3] > 0).astype(np.float32)
    for _ in range(passes):
        if w.all() or not w.any():
            break
        n = sum(np.roll(np.roll(w, dy, 0), dx, 1) for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        c = sum(np.roll(np.roll(rgb * w[..., None], dy, 0), dx, 1) for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        grow = (w == 0) & (n > 0)
        rgb[grow] = c[grow] / n[grow][:, None]
        w[grow] = 1
    return rgb.round().astype(np.uint8)


def upscale_rgba(a, s, method, wrap=False, sharpen=False, exe=None):
    """Smooth methods: alpha nearest-scaled (exact alpha-test coverage at every threshold), RGB resampled after colour
    bleeding. xbr-like scales whole RGBA texels (alpha keeps only original values)."""
    if method == "nearest":
        return nearest(a, s)
    if method == "xbr-like":
        return xbr_like(a, s)
    rgb = bleed(a) if (a[..., 3] == 0).any() else a[..., :3]
    rgb = realesrgan(rgb, s, wrap, exe) if method == "realesrgan" else lanczos(rgb, s, wrap, sharpen)
    return np.concatenate([rgb, nearest(a[..., 3:], s)], -1)


def box_down(a, s):
    h, w = a.shape[0] // s, a.shape[1] // s
    return a.astype(np.float64).reshape(h, s, w, s, -1).mean((1, 3))


def consistency(orig, up, s):
    """(mean abs error, max per-channel mean shift) after box-downscaling back to original size.
    RGB is only compared where the original texel is visible; alpha everywhere."""
    d, o = box_down(up, s), orig.astype(np.float64)
    vis = orig[..., 3] > 0
    if not vis.any():
        vis = np.ones(orig.shape[:2], bool)
    err = np.concatenate([np.abs(d[..., :3] - o[..., :3])[vis].ravel(), np.abs(d[..., 3] - o[..., 3]).ravel()])
    shift = [abs(d[..., c][vis].mean() - o[..., c][vis].mean()) for c in range(3)] + [abs(d[..., 3].mean() - o[..., 3].mean())]
    return float(err.mean()), float(max(shift))


# --- PBR derivation from the (upscaled) albedo; conservative by design ---

def _box3(x):
    return sum(np.roll(np.roll(x, dy, 0), dx, 1) for dy in (-1, 0, 1) for dx in (-1, 0, 1)) / 9


def derive_pbr(rgba, strength=2.0):
    lum = (rgba[..., :3].astype(np.float32) @ np.array([0.2126, 0.7152, 0.0722], np.float32)) / 255
    h = _box3(lum)
    r = lambda x, dy, dx: np.roll(np.roll(x, dy, 0), dx, 1)  # wrap: tiling textures stay seamless
    gx = (r(h, 1, -1) + 2 * r(h, 0, -1) + r(h, -1, -1)) - (r(h, 1, 1) + 2 * r(h, 0, 1) + r(h, -1, 1))
    grow = (r(h, -1, 1) + 2 * r(h, -1, 0) + r(h, -1, -1)) - (r(h, 1, 1) + 2 * r(h, 1, 0) + r(h, 1, -1))
    # gx = dh/dx (column right), grow = dh/drow (row down). OpenGL +Y up: n = (-dh/dx, +dh/drow, 1).
    n = np.stack([-gx * strength, grow * strength, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    normal = ((n * 0.5 + 0.5) * 255).round().astype(np.uint8)
    std = np.sqrt(np.maximum(_box3(lum * lum) - _box3(lum) ** 2, 0))
    rough = ((0.75 + 0.25 * np.clip(std / 0.08, 0, 1)) * 255).round().astype(np.uint8)
    cavity = np.clip(_box3(_box3(h)) - h, 0, None)
    ao = ((1 - np.clip(cavity * 4, 0, 0.2)) * 255).round().astype(np.uint8)
    return normal, rough, ao


# --- policy ---

def choose(category, method, protected, has_ai):
    """Returns (method, wrap, sharpen)."""
    if protected or category in PROTECTED:
        return ("xbr-like" if method in ("auto", "realesrgan") else method), False, False
    wrap = category in ("world", "sky")
    if method == "auto":
        if category in AI_CATEGORIES and has_ai:
            return "realesrgan", wrap, False
        return "lanczos", wrap, category in ("world", "model", "sky")
    return method, wrap, False


def process(job):
    """Returns (key, category, method_used, files{kind: relpath}, note). Never raises for per-texture problems."""
    key, w, h, cat, src, dump, out, s, method, protected, has_ai, exe, pbr, max_err, max_shift = job
    try:
        img = Image.open(os.path.join(dump, key + ".png"))
        img.load()
        a = np.asarray(img.convert("RGBA"))
    except Exception as e:  # noqa: BLE001 - report and continue
        return key, cat, "rejected", {}, f"unreadable: {e}"
    if (a.shape[1], a.shape[0]) != (w, h):
        return key, cat, "rejected", {}, f"size {a.shape[1]}x{a.shape[0]} != index {w}x{h}"
    if fnv1a64(w, h, a.tobytes()) != key:
        return key, cat, "rejected", {}, "key mismatch: pixels do not hash to filename key"
    while s > 1 and max(w, h) * s > MAX_SIDE:
        s //= 2
    if s == 1 and not pbr:
        return key, cat, "skipped", {}, f"{w}x{h} cannot be upscaled within {MAX_SIDE}px"
    is_protected = protected or cat in PROTECTED or len(np.unique(a.view(np.uint32))) <= 16
    m, wrap, sharpen = choose(cat, method, is_protected, has_ai)
    note, up = "", None
    chain = [(m, sharpen)]  # guard fallback: sharpened -> plain Lanczos -> nearest (exact)
    if m not in ("nearest", "xbr-like"):
        chain += [c for c in (("lanczos", False), ("nearest", False)) if c != (m, sharpen)]
    for cand, sharp in chain:
        try:
            up = upscale_rgba(a, s, cand, wrap, sharp, exe) if s > 1 else a
        except RuntimeError as e:
            note += f"{cand} failed ({e}); "
            continue
        err, shift = consistency(a, up, s)
        label = cand + ("+unsharp" if sharp else "")
        if err <= max_err and shift <= max_shift or cand in ("nearest", "xbr-like"):
            m = label
            break
        note += f"{label} rejected by guard (err {err:.2f}, shift {shift:.2f}); "
    files, tex = {}, os.path.join(out, "textures")
    if s > 1:
        Image.fromarray(up, "RGBA").save(os.path.join(tex, key + ".png"), optimize=False)
        files["albedo"] = f"textures/{key}.png"
    if pbr and cat in ("world", "model") and not is_protected:
        for kind, arr, suffix in zip(("normal", "roughness", "ao"), derive_pbr(up), ("n", "r", "ao")):
            Image.fromarray(arr, "RGB" if arr.ndim == 3 else "L").save(os.path.join(tex, f"{key}_{suffix}.png"))
            files[kind] = f"textures/{key}_{suffix}.png"
    for kind, rel in list(files.items()):
        if os.path.getsize(os.path.join(out, rel)) > MAX_BYTES:
            for r in files.values():
                os.unlink(os.path.join(out, r))
            return key, cat, "skipped", {}, f"{kind} exceeds {MAX_BYTES} bytes"
    if not files:
        return key, cat, "skipped", {}, f"{w}x{h} cannot be upscaled within {MAX_SIDE}px"
    return key, cat, m if s > 1 else "pbr-only", files, note.rstrip("; ")


# --- pack manifest ---

def manifest_text(name, license_text, entries):
    for v in (name, license_text):
        if not v or any(c in v for c in "\t\r\n"):
            raise ValueError("pack name/license must be non-empty single-line text without tabs")
    lines = ["kabus22-texture-pack 1", f"name\t{name}", f"license\t{license_text}"]
    lines += [f"{k}\t{kind}\t{rel}" for k, kind, rel in sorted(entries)]
    return "\n".join(lines) + "\n"


def validate_pack(pack):
    """Contract E checks. Returns a list of problems (empty = valid)."""
    problems, root = [], os.path.realpath(pack)
    try:
        with open(os.path.join(pack, "pack.txt"), encoding="utf-8") as f:
            lines = f.read().split("\n")
    except OSError as e:
        return [f"pack.txt: {e}"]
    if lines and lines[-1] == "":
        lines.pop()
    if len(lines) < 3 or lines[0] != "kabus22-texture-pack 1" or not lines[1].startswith("name\t") or not lines[2].startswith("license\t"):
        return ["pack.txt: malformed header"]
    seen, scale = set(), {}
    for n, line in enumerate(lines[3:], 4):
        p = line.split("\t")
        if len(p) != 3 or not KEY_RE.fullmatch(p[0]) or p[1] not in KINDS:
            problems.append(f"pack.txt:{n}: malformed entry {line!r}")
            continue
        key, kind, rel = p
        if (key, kind) in seen:
            problems.append(f"pack.txt:{n}: duplicate {key} {kind}")
        seen.add((key, kind))
        full = os.path.realpath(os.path.join(root, rel))
        if os.path.isabs(rel) or ".." in rel.replace("\\", "/").split("/") or os.path.commonpath([root, full]) != root:
            problems.append(f"pack.txt:{n}: path escapes pack: {rel!r}")
            continue
        if not rel.lower().endswith(".png") or not os.path.isfile(full):
            problems.append(f"pack.txt:{n}: missing or non-PNG file {rel!r}")
            continue
        if os.path.getsize(full) > MAX_BYTES:
            problems.append(f"pack.txt:{n}: {rel} larger than 64 MiB")
            continue
        try:
            with Image.open(full) as im:
                fmt, size = im.format, im.size
        except Exception as e:  # noqa: BLE001
            problems.append(f"pack.txt:{n}: unreadable {rel}: {e}")
            continue
        if fmt != "PNG" or max(size) > MAX_SIDE:
            problems.append(f"pack.txt:{n}: {rel} not PNG or larger than {MAX_SIDE}px")
            continue
        scale.setdefault(key, set()).add(size)
    for key, sizes in scale.items():
        if len(sizes) != 1:
            problems.append(f"{key}: kinds have different dimensions {sorted(sizes)}")
    return problems


def check_dimensions(pack, index_rows):
    """Engine-side rule we can check with the dump: every entry = original x s, s in {1,2,4,8}."""
    dims = {k: (w, h) for k, w, h, _, _ in index_rows}
    bad = []
    with open(os.path.join(pack, "pack.txt"), encoding="utf-8") as f:
        for line in f.read().splitlines()[3:]:
            key, kind, rel = line.split("\t")
            with Image.open(os.path.join(pack, rel)) as im:
                W, H = im.size
            w, h = dims.get(key, (0, 0))
            if not any((W, H) == (w * s, h * s) for s in (1, 2, 4, 8)):
                bad.append(f"{key} {kind}: {W}x{H} is not an integer scale of {w}x{h}")
    return bad


def main(argv=None):
    data_home = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dump", nargs="?", help="--dump-textures output directory (index.tsv + <key>.png)")
    ap.add_argument("--name", default="upscaled", help="pack directory name and default display name")
    ap.add_argument("--display-name")
    ap.add_argument("--license", default="Derived from original Kabus 22 textures; personal use only, do not redistribute")
    ap.add_argument("--output", help=f"pack directory (default {data_home}/kabus22/packs/NAME)")
    ap.add_argument("--scale", type=int, choices=(2, 4), default=2)
    ap.add_argument("--method", choices=("auto", "lanczos", "nearest", "xbr-like", "realesrgan"), default="auto")
    ap.add_argument("--pbr", action="store_true", help="derive normal/roughness/ao maps for world/model textures")
    ap.add_argument("--protect", action="append", default=[], help="file of keys (one per line) to treat like UI text")
    ap.add_argument("--max-error", type=float, default=6.0, help="guard: mean abs error (0-255) after box downscale")
    ap.add_argument("--max-shift", type=float, default=1.5, help="guard: max per-channel mean shift (0-255)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--force", action="store_true", help="replace an existing pack directory")
    ap.add_argument("--validate", metavar="PACK", help="only validate an existing pack directory")
    a = ap.parse_args(argv)
    if a.validate:
        problems = validate_pack(a.validate)
        if a.dump:
            problems += check_dimensions(a.validate, read_index(a.dump))
        print("\n".join(problems) or "valid")
        return 1 if problems else 0
    if not a.dump:
        ap.error("dump directory required")
    if not re.fullmatch(r"[A-Za-z0-9._-]+", a.name) or a.name in (".", ".."):
        ap.error("--name must be a plain directory name")
    exe = realesrgan_path()
    if a.method == "realesrgan" and not exe:
        print(f"error: --method realesrgan requested but {REALESRGAN} is not on PATH", file=sys.stderr)
        return 2
    if a.method == "auto":
        print(f"realesrgan: {'using ' + exe if exe else 'unavailable (' + REALESRGAN + ' not on PATH); auto uses Lanczos'}", file=sys.stderr)
    protected = set()
    for p in a.protect:
        with open(p, encoding="utf-8") as f:
            protected |= {ln.strip().lower() for ln in f if ln.strip()}
    rows = list({r[0]: r for r in reversed(read_index(a.dump))}.values())[::-1]  # one entry per key, first wins
    out = os.path.abspath(a.output or os.path.join(data_home, "kabus22", "packs", a.name))
    if os.path.lexists(out):
        if not a.force:
            print(f"error: {out} exists (use --force to replace)", file=sys.stderr)
            return 2
        try:  # --force only ever replaces a previous pack, never an arbitrary directory
            with open(os.path.join(out, "pack.txt"), encoding="utf-8") as f:
                is_pack = f.readline() == "kabus22-texture-pack 1\n"
        except OSError:
            is_pack = False
        if not is_pack or os.path.islink(out):
            print(f"error: {out} is not a texture pack directory; refusing to replace it", file=sys.stderr)
            return 2
    os.makedirs(os.path.dirname(out), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix=f".{os.path.basename(out)}.tmp-", dir=os.path.dirname(out))
    try:
        os.mkdir(os.path.join(tmp, "textures"))
        jobs = [(k, w, h, c, s, a.dump, tmp, a.scale, a.method, k in protected, bool(exe), exe, a.pbr, a.max_error, a.max_shift)
                for k, w, h, c, s in rows]
        with ProcessPoolExecutor(max(1, a.jobs)) as pool:
            results = list(pool.map(process, jobs, chunksize=4))
        entries, counts, rejected = [], Counter(), 0
        for key, cat, m, files, note in results:
            counts[(cat, m)] += 1
            rejected += m == "rejected"
            entries += [(key, kind, rel) for kind, rel in files.items()]
            if note:
                print(f"{key} [{cat}] {m}: {note}", file=sys.stderr)
        with open(os.path.join(tmp, "pack.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write(manifest_text(a.display_name or a.name, a.license, entries))
        problems = validate_pack(tmp) + check_dimensions(tmp, rows)
        if problems:
            raise RuntimeError("generated pack failed validation:\n" + "\n".join(problems))
        if os.path.exists(out):
            old = tempfile.mkdtemp(prefix=f".{os.path.basename(out)}.old-", dir=os.path.dirname(out))
            os.rename(out, old)  # replaces the empty placeholder
            os.rename(tmp, out)
            shutil.rmtree(old)
        else:
            os.rename(tmp, out)
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    print(f"pack: {out} ({len(entries)} entries, {len(rows)} textures, scale {a.scale})")
    for (cat, m), n in sorted(counts.items()):
        print(f"  {cat}\t{m}\t{n}")
    return 1 if rejected else 0


if __name__ == "__main__":
    sys.exit(main())
