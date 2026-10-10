#!/usr/bin/env python3
"""Plain-assert checks for upscale.py. Run: python3 test_upscale.py"""
import os, stat, sys, tempfile
import numpy as np
from PIL import Image

sys.dont_write_bytecode = True  # CTest runs from the source tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import upscale as u  # noqa: E402

FAKE_ESRGAN = """#!/usr/bin/env python3
import sys
from PIL import Image, ImageDraw
a = sys.argv; i, o, s = a[a.index('-i') + 1], a[a.index('-o') + 1], int(a[a.index('-s') + 1])
im = Image.open(i).convert('RGB'); im = im.resize((im.width * s, im.height * s), Image.LANCZOS)
if {hallucinate}:
    ImageDraw.Draw(im).rectangle((0, 0, im.width // 2, im.height // 2), fill=(255, 255, 0))
im.save(o)
"""


def make_dump(d, items):
    """items: [(rgba uint8 array, category)] -> writes <key>.png + index.tsv exactly as Contract D."""
    os.makedirs(d, exist_ok=True)
    rows = []
    for i, (a, cat) in enumerate(items):
        key = u.fnv1a64(a.shape[1], a.shape[0], a.tobytes())
        Image.fromarray(a, "RGBA").save(os.path.join(d, key + ".png"))
        rows.append(f"{key}\t{a.shape[1]}\t{a.shape[0]}\t{cat}\tfixture/\udcddzmir{i}.pcx\n")  # raw CP1254 'İ', not UTF-8
    with open(os.path.join(d, "index.tsv"), "w", encoding="utf-8", errors="surrogateescape") as f:
        f.writelines(rows)
    return [r.split("\t")[0] for r in rows]


def fake_exe(d, hallucinate):
    p = os.path.join(d, f"realesrgan-{hallucinate}")
    with open(p, "w") as f:
        f.write(FAKE_ESRGAN.format(hallucinate=hallucinate))
    os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR)
    return p


rng = np.random.default_rng(22)
# smooth "world" texture: gradients + low-amplitude noise, opaque
yy, xx = np.mgrid[0:32, 0:48]
world = np.stack([(xx * 5) % 256, (yy * 7) % 256, 128 + 40 * np.sin(xx / 5.0), np.full_like(xx, 255)], -1)
world = (world + rng.integers(-6, 7, world.shape) * [1, 1, 1, 0]).clip(0, 255).astype(np.uint8)
# alpha-tested sprite: coloured disc with binary alpha, magenta colour key in transparent texels
sprite = np.zeros((24, 24, 4), np.uint8)
disc = (yy[:24, :24] - 11.5) ** 2 + (xx[:24, :24] - 11.5) ** 2 < 90
sprite[...] = (255, 0, 255, 0)
sprite[disc] = np.stack([60 + rng.integers(0, 120, disc.sum()), 120 + rng.integers(0, 60, disc.sum()), rng.integers(0, 50, disc.sum()), np.full(disc.sum(), 255)], -1)
# UI text: two colours, glyph-like strokes (an "Ş"-ish shape)
ui = np.zeros((16, 16, 4), np.uint8)
ui[...] = (0, 0, 0, 255)
ui[2:4, 3:12] = ui[2:8, 3:5] = ui[6:8, 3:12] = ui[6:12, 10:12] = ui[10:12, 3:12] = ui[13:15, 7:9] = (255, 255, 255, 255)


def test_fnv_vectors():
    assert u.fnv1a64_bytes(b"") == 0xcbf29ce484222325
    assert u.fnv1a64_bytes(b"a") == 0xaf63dc4c8601ec8c
    assert u.fnv1a64_bytes(b"foobar") == 0x85944171f73967e8
    assert u.fnv1a64(0, 0, b"") == "a8c7f832281a39c5"  # empty image: hash of 8 zero bytes
    assert u.fnv1a64(1, 1, bytes(4)) == "7e1ef442cd317605"  # 1x1 RGBA (0,0,0,0)
    assert u.fnv1a64(2, 1, bytes([255, 0, 0, 255, 0, 0, 255, 128])) == "226f812b8d0ad763"
    assert u.fnv1a64(1, 2, bytes(8)) != u.fnv1a64(2, 1, bytes(8))  # dimensions are part of the key
    assert u.texture_key(Image.fromarray(world, "RGBA")) == u.fnv1a64(48, 32, world.tobytes())


def test_scale():
    for s in (2, 4):
        for m in ("nearest", "xbr-like", "lanczos"):
            for a in (world, sprite, ui):
                out = u.upscale_rgba(a, s, m, wrap=True, sharpen=True)
                assert out.shape == (a.shape[0] * s, a.shape[1] * s, 4) and out.dtype == np.uint8, (m, s)
        n = u.nearest(world, s)
        assert (n[::s, ::s] == world).all() and np.allclose(u.box_down(n, s), world)
        x = u.xbr_like(ui, s)
        assert set(map(tuple, x.reshape(-1, 4))) <= set(map(tuple, ui.reshape(-1, 4)))  # never invents colours
        solid = np.full((5, 7, 4), 77, np.uint8)
        assert (u.xbr_like(solid, s) == u.nearest(solid, s)).all()
    # Scale2x rounds a staircase: the inner corner of the notch takes the neighbouring colour, the far corner stays
    st = np.zeros((2, 2, 4), np.uint8); st[0, 0] = st[0, 1] = st[1, 0] = 255
    x = u.scale2x(st)
    assert (x[2, 2] == 255).all() and (x[3, 3] == 0).all() and (x[2, 3] == 0).all() and (x[3, 2] == 0).all()


def test_alpha_coverage():
    for m in ("lanczos", "xbr-like", "nearest"):
        for s in (2, 4):
            out = u.upscale_rgba(sprite, s, m, sharpen=True)
            if m == "xbr-like":  # whole-RGBA Scale2x: alpha only takes existing values, corners round
                assert set(np.unique(out[..., 3])) <= set(np.unique(sprite[..., 3]))
                assert abs((out[..., 3] > 0).mean() - (sprite[..., 3] > 0).mean()) < 0.02
            else:  # alpha nearest: identical alpha-test coverage at every threshold
                assert (out[..., 3] == u.nearest(sprite[..., 3:], s)[..., 0]).all(), m
                for t in (1, 128, 255):
                    assert (out[..., 3] >= t).mean() == (sprite[..., 3] >= t).mean()
            vis = out[..., 3] > 0
            assert not ((out[..., 0] > 200) & (out[..., 2] > 200) & vis).any(), "colour key bled into visible texels"
    # soft-alpha effect skin (alpha 0..40 everywhere): colour must survive resampling without low-alpha amplification
    fx = world[:, :32].copy(); fx[..., 3] = np.linspace(0, 40, 32).astype(np.uint8)[None, :]
    err, shift = u.consistency(fx, u.upscale_rgba(fx, 2, "lanczos"), 2)
    assert err < 6.0 and shift < 1.5, (err, shift)


def test_guard_and_ai_path():
    with tempfile.TemporaryDirectory() as d:
        orig = world
        bad = u.nearest(orig, 2).copy(); bad[:16, :16, :3] = (255, 255, 0)  # hallucinated symbol
        err, shift = u.consistency(orig, bad, 2)
        assert err > 6.0 or shift > 1.5
        assert u.consistency(orig, u.nearest(orig, 2), 2) == (0.0, 0.0)
        assert max(u.consistency(orig, u.lanczos(orig, 2, True), 2)) < 1.5
        dump, out = os.path.join(d, "dump"), os.path.join(d, "out")
        (k,) = make_dump(dump, [(world, "world")])
        os.makedirs(os.path.join(out, "textures"))
        job = lambda exe, has_ai, method="auto", cat="world": (k, 48, 32, cat, "x", dump, out, 2, method, False, has_ai, exe, False, 6.0, 1.5)
        r = u.process(job(fake_exe(d, True), True))
        assert r[2] == "lanczos" and "realesrgan rejected by guard" in r[4], r  # plain Lanczos fallback
        assert u.process(job(fake_exe(d, False), True))[2] == "realesrgan"
        assert u.process(job(None, False))[2] == "lanczos+unsharp"
        assert u.process(job(fake_exe(d, False), True, cat="ui"))[2] == "xbr-like"  # never AI on UI text
        assert u.process(job(os.path.join(d, "missing-exe"), True))[4].startswith("realesrgan failed")


def test_pack_end_to_end():
    with tempfile.TemporaryDirectory() as d:
        dump, packs = os.path.join(d, "dump"), os.path.join(d, "packs")
        kw, ks, ku = make_dump(dump, [(world, "world"), (sprite, "sprite"), (ui, "ui")])
        os.environ["PATH"], path = d, os.environ["PATH"]  # no realesrgan on PATH: auto reports unavailable, explicit fails
        assert u.main([dump, "--method", "realesrgan", "--output", os.path.join(packs, "x")]) == 2
        os.environ["PATH"] = path
        assert not os.path.exists(os.path.join(packs, "x"))
        out = os.path.join(packs, "hd")
        assert u.main([dump, "--name", "hd", "--output", out, "--pbr", "--jobs", "2"]) == 0
        expected = (
            "kabus22-texture-pack 1\nname\thd\nlicense\tDerived from original Kabus 22 textures; personal use only, do not redistribute\n"
            + "".join(sorted(f"{k}\t{kind}\ttextures/{k}{suf}.png\n" for k, kind, suf in
                             [(kw, "albedo", ""), (kw, "normal", "_n"), (kw, "roughness", "_r"), (kw, "ao", "_ao"),
                              (ks, "albedo", ""), (ku, "albedo", "")])))
        assert open(os.path.join(out, "pack.txt")).read() == expected
        assert u.validate_pack(out) == [] and u.check_dimensions(out, u.read_index(dump)) == []
        assert Image.open(os.path.join(out, f"textures/{ku}.png")).size == (32, 32)
        assert sorted(os.listdir(packs)) == ["hd"]  # temp dirs cleaned up
        assert u.main([dump, "--name", "hd", "--output", out]) == 2  # exists, no --force
        assert u.main([dump, "--name", "hd", "--output", out, "--force", "--scale", "4"]) == 0
        assert Image.open(os.path.join(out, f"textures/{ku}.png")).size == (64, 64)
        assert sorted(os.listdir(packs)) == ["hd"]
        os.makedirs(os.path.join(d, "notapack")); open(os.path.join(d, "notapack", "save.sav"), "w").close()
        assert u.main([dump, "--output", os.path.join(d, "notapack"), "--force"]) == 2
        assert os.path.exists(os.path.join(d, "notapack", "save.sav"))
        # refuse texture whose pixels do not hash to its filename key
        os.rename(os.path.join(dump, f"{ks}.png"), os.path.join(dump, f"{'0' * 16}.png"))
        p = os.path.join(dump, "index.tsv")
        idx = open(p, "rb").read().replace(ks.encode(), b"0" * 16)
        open(p, "wb").write(idx)
        assert u.main([dump, "--output", os.path.join(packs, "mm")]) == 1
        assert "0" * 16 not in open(os.path.join(packs, "mm", "pack.txt")).read()


def test_path_safety():
    with tempfile.TemporaryDirectory() as d:
        pack, outside = os.path.join(d, "pack"), os.path.join(d, "outside.png")
        os.makedirs(os.path.join(pack, "t"))
        Image.new("RGBA", (4, 4)).save(outside)
        Image.new("RGBA", (4, 4)).save(os.path.join(pack, "t", "ok.png"))
        os.symlink(outside, os.path.join(pack, "t", "link.png"))
        k = "0123456789abcdef"
        def check(entry):
            open(os.path.join(pack, "pack.txt"), "w").write(f"kabus22-texture-pack 1\nname\tp\nlicense\tl\n{entry}\n")
            return u.validate_pack(pack)
        assert check(f"{k}\talbedo\tt/ok.png") == []
        assert "escapes" in check(f"{k}\talbedo\t../outside.png")[0]
        assert "escapes" in check(f"{k}\talbedo\t{outside}")[0]
        assert "escapes" in check(f"{k}\talbedo\tt/link.png")[0]
        assert "malformed" in check(f"{k}\tspecular\tt/ok.png")[0]
        assert "malformed" in check(f"{k.upper()}\talbedo\tt/ok.png")[0]
        assert check("") and check(f"{k}\talbedo\tt/ok.png\textra")
        open(os.path.join(pack, "pack.txt"), "w").write("kabus22-texture-pack 2\nname\tp\nlicense\tl\n")
        assert u.validate_pack(pack) == ["pack.txt: malformed header"]
        for bad in ("a\tb", "a\nb", ""):
            try:
                u.manifest_text(bad, "l", [])
                assert False
            except ValueError:
                pass
        with open(os.path.join(d, "index.tsv"), "w") as f:
            f.write("../../etc/passwd\t1\t1\tworld\tx\n")
        try:
            u.read_index(d)
            assert False
        except ValueError:
            pass


def test_pbr():
    flat = np.full((8, 8, 4), 100, np.uint8)
    n, r, ao = u.derive_pbr(flat)
    assert (n == (128, 128, 255)).all() and (ao == 255).all() and (r >= 191).all()
    ramp = np.repeat(np.linspace(255, 0, 16, dtype=np.uint8)[:, None], 16, 1)  # brighter (higher) toward the top
    img = np.dstack([ramp, ramp, ramp, np.full_like(ramp, 255)])
    n, _, _ = u.derive_pbr(img)
    mid = n[4:12, 4:12].astype(int)
    assert (mid[..., 1] < 128).all() and (abs(mid[..., 0] - 128) <= 1).all(), "OpenGL +Y: slope rising upward tilts normal down"
    assert (mid[..., 2] > 200).all()  # conservative strength


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
