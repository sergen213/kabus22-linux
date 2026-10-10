#!/usr/bin/env python3
"""Compare native captures with reference captures of the original (wineref.py) and check thresholds.

  compare.py REF_DIR NATIVE_DIR [--thresholds FILE] [--pairs DIR] [--ref-gamma G]
             [--run K22 GAME SAVES CASES_DIR]

REF_DIR/<case>/<image>.png are the original's captures; NATIVE_DIR/<case>/<image>.ppm the native ones.
--run first regenerates NATIVE_DIR/<case>/ by running CASES_DIR/<case>.k22s on the native engine
(rendered OpenGL, --display authored, 1024x768, fresh private XDG tree with SAVES imported; needs DISPLAY).
--thresholds FILE: lines "case/image min_ssim [w:h:x:y] [mask=x:y:w:h;...]" (# comments); only listed
images are compared and the exit code is 1 if any SSIM (RGB, ffmpeg ssim, on the optional crop region,
with mask rectangles blacked out in both images first) is below min_ssim.
--ref-gamma G applies RenkAyar.dll dl_PaKaGa(0,0,G) to the reference first: the original's windowed
D3D9 device does not present SetGammaRamp, the native engine applies it (GA_Gamma default 30).
Without --thresholds every REF_DIR image with a native counterpart is reported. --pairs writes
original|native|diff montages (diff amplified x4, same preprocessing) for inspection. Only ffmpeg is needed.
"""
import argparse, os, re, shutil, subprocess, sys


def prefilter(crop, masks, gamma=0):
    f = "format=rgb24"
    if gamma:  # RenkAyar.dll 10001080 with b=c=0: n = trunc(i + (255-i)*i*g/25600)
        lut = f"trunc(val+(255-val)*val*{gamma}/25600)"
        f += f",lutrgb=r='{lut}':g='{lut}':b='{lut}'"
    for m in masks:
        x, y, w, h = m.split(":")
        f += f",drawbox=x={x}:y={y}:w={w}:h={h}:color=black:t=fill"
    return f + (f",crop={crop}" if crop else "")


def ffmpeg_metric(ref, nat, metric, crop, masks, gamma):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", ref, "-i", nat, "-lavfi",
                        f"[0]{prefilter(crop, masks, gamma)}[a];[1]{prefilter(crop, masks)}[b];[a][b]{metric}",
                        "-f", "null", "-"], capture_output=True, text=True)
    m = re.search(r"All:([0-9.]+)" if metric == "ssim" else r"average:([0-9.]+|inf)", r.stderr)
    if not m:
        raise SystemExit(f"ffmpeg {metric} failed for {ref} {nat}: {r.stderr[-400:]}")
    return float(m.group(1))


def pair(ref, nat, out, crop, masks, gamma):
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", ref, "-i", nat, "-filter_complex",
                    f"[0]{prefilter(crop, masks, gamma)},split[a][a2];[1]{prefilter(crop, masks)},split[b][b2];"
                    "[a2][b2]blend=all_mode=difference,lutrgb=r=4*val:g=4*val:b=4*val[d];[a][b][d]hstack=inputs=3",
                    "-frames:v", "1", out], check=True)


def run_native(k22, game, saves, cases, native, names):
    for case in names:
        root = os.path.abspath(f"{native}/{case}.xdg")
        shutil.rmtree(root, ignore_errors=True)
        shutil.rmtree(f"{native}/{case}", ignore_errors=True)
        env = dict(os.environ, SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER=os.environ.get("SDL_AUDIODRIVER", "dummy"))
        for k in ("data", "config", "state", "runtime"):
            os.makedirs(f"{root}/{k}", mode=0o700)
            env[f"XDG_{k.upper()}_HOME" if k != "runtime" else "XDG_RUNTIME_DIR"] = f"{root}/{k}"
        subprocess.run([k22, "--import-saves", saves, "--data", game], env=env, check=True, stdout=subprocess.DEVNULL)
        with open(f"{root}/log.txt", "w") as log:
            code = subprocess.run([k22, "--data", game, "--display", "authored", "--renderer", "opengl",
                                   "--scenario", f"{cases}/{case}.k22s", "--scenario-out", f"{native}/{case}"],
                                  env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800).returncode
        print(f"native {case}: exit {code} (log {root}/log.txt)", flush=True)
        if code:
            raise SystemExit(f"native scenario {case} failed")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("ref")
    p.add_argument("native")
    p.add_argument("--thresholds")
    p.add_argument("--pairs")
    p.add_argument("--run", nargs=4, metavar=("K22", "GAME", "SAVES", "CASES_DIR"))
    p.add_argument("--ref-gamma", type=int, default=0)
    a = p.parse_args()
    rows = []  # (case, image, min_ssim or None, crop, masks)
    if a.thresholds:
        for line in open(a.thresholds):
            w = line.split("#")[0].split()
            if w:
                case, image = w[0].split("/")
                crop = next((t for t in w[2:] if not t.startswith("mask=")), None)
                masks = [m for t in w[2:] if t.startswith("mask=") for m in t[5:].split(";")]
                rows.append((case, image, float(w[1]), crop, masks))
    else:
        for case in sorted(os.listdir(a.ref)):
            if os.path.isdir(f"{a.ref}/{case}"):
                rows += [(case, f[:-4], None, None, []) for f in sorted(os.listdir(f"{a.ref}/{case}"))
                         if f[-4:] in (".png", ".ppm") and not f.startswith("pair-") and (a.run or os.path.exists(f"{a.native}/{case}/{f[:-4]}.ppm"))]
    if a.run:
        run_native(*a.run, a.native, sorted({r[0] for r in rows}))
    failed = 0
    for case, image, limit, crop, masks in rows:
        ref, nat = f"{a.ref}/{case}/{image}.png", f"{a.native}/{case}/{image}.ppm"
        if not os.path.exists(ref):  # a native run used as reference (noise measurement)
            ref = ref[:-4] + ".ppm"
        if not os.path.exists(nat):
            print(f"FAIL {case}/{image}: no native capture {nat}")
            failed += 1
            continue
        s, q = (ffmpeg_metric(ref, nat, m, crop, masks, a.ref_gamma) for m in ("ssim", "psnr"))
        ok = limit is None or s >= limit
        failed += not ok
        print(f"{'PASS' if ok else 'FAIL'} {case}/{image}{' crop ' + crop if crop else ''}: SSIM {s:.4f} PSNR {q:.2f} dB"
              + (f" (min {limit})" if limit is not None else ""))
        if a.pairs:
            os.makedirs(f"{a.pairs}/{case}", exist_ok=True)
            pair(ref, nat, f"{a.pairs}/{case}/pair-{image}.png", crop, masks, a.ref_gamma)
    print(f"{len(rows) - failed}/{len(rows)} passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
