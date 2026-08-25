"""What decides a particle's colour in the original's 3D view?

The two keys above the cube are labelled Res (a rainbow) and Noise (a
greyscale), which says those two dimensions are colour-coded. This pins down
the actual mapping: hold one dimension fixed with its variance at zero, so
every particle shares a value, capture the view, and read the colour back.
"""
import colorsys, os, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swarmvis, probe

ROOT = probe.ROOT
OUT  = os.path.join(ROOT, "analysis", "data", "colour")
P    = probe.P


def capture(params, tag):
    """Hold a note in the original's editor and screenshot the visualiser."""
    d = os.path.join(OUT, tag)
    args = []
    for i, v in sorted(params.items()):
        args += ["--param", "%d=%.6g" % (i, v)]
    env = dict(os.environ, OUT=d, FRAMES="4", IVAL="0.25", HOLD="6")
    subprocess.run([os.path.join(ROOT, "analysis", "capture_swarm.sh")] + args,
                   env=env, cwd=ROOT, capture_output=True)
    import glob
    frames = sorted(glob.glob(os.path.join(d, "f*.png")))
    return frames[-1] if frames else None


def dominant_colour(path):
    """Median hue/saturation/value of the saturated (particle) pixels."""
    im = Image.open(path).convert("RGB").crop(swarmvis.CUBE)
    a = np.asarray(im).astype(float) / 255.0
    mx = a.max(axis=2); mn = a.min(axis=2)
    mask = ((mx - mn) > 0.18) & (mx > 0.25)
    if mask.sum() < 40:
        return None
    px = a[mask]
    hsv = np.array([colorsys.rgb_to_hsv(*p) for p in px])
    return (float(np.median(hsv[:, 0])), float(np.median(hsv[:, 1])),
            float(np.median(hsv[:, 2])), int(mask.sum()))


BASE = {
    P["oscillators"]: probe.osc_norm(10),
    P["pitch_var"]: probe.pct_norm(25), P["pan_var"]: probe.pct_norm(40),
    P["vol_var"]: probe.pct_norm(20),
    P["res_var"]: 0.0, P["noise_var"]: 0.0,     # every particle shares the value
    P["speed"]: probe.pct_norm(20), P["std_dev"]: probe.pct_norm(30),
    P["vl_atk_t"]: 0.0, P["vl_atk_l"]: 1.0, P["vl_dec_t"]: 0.0,
    P["vl_dec_l"]: 1.0, P["vl_rel_t"]: 0.05,
}


def sweep(param, label, values, other=None):
    print(f"\n== {label} ==")
    print(f"  {label:>6} {'hue':>6} {'sat':>6} {'val':>6} {'px':>7}   our model")
    rows = []
    for v in values:
        p = dict(BASE); p.update(other or {})
        p[P[param]] = probe.pct_norm(v)
        f = capture(p, f"{param}_{v}")
        if f is None:
            print(f"  {v:6d}   (no capture)"); continue
        c = dominant_colour(f)
        if c is None:
            print(f"  {v:6d}   (no particles found)"); continue
        h, s, val, n = c
        # what SwarmComponents.h currently does
        ours = (0.83 * (v / 100.0)) if param == "resonance" else None
        note = f"hue {ours:.2f}" if ours is not None else ""
        print(f"  {v:6d} {h:6.3f} {s:6.3f} {val:6.3f} {n:7d}   {note}")
        rows.append((v, h, s, val))
    return rows


def main():
    os.makedirs(OUT, exist_ok=True)
    res   = sweep("resonance", "Res %", [0, 25, 50, 75, 100])
    noise = sweep("noise", "Noise %", [0, 25, 50, 75, 100],
                  other={P["resonance"]: probe.pct_norm(50)})

    if len(res) >= 3:
        v = np.array([r[0] for r in res]) / 100.0
        h = np.array([r[1] for r in res])
        print(f"\n  hue vs Res: {np.polyfit(v, h, 1)[0]:+.3f} * res "
              f"{np.polyfit(v, h, 1)[1]:+.3f}")
    if len(noise) >= 3:
        v = np.array([r[0] for r in noise]) / 100.0
        s = np.array([r[2] for r in noise])
        print(f"  sat vs Noise: {np.polyfit(v, s, 1)[0]:+.3f} * noise "
              f"{np.polyfit(v, s, 1)[1]:+.3f}")


if __name__ == "__main__":
    main()
