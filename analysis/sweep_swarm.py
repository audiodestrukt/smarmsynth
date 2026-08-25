"""Vary one swarm parameter at a time, watch the visualiser, measure the cloud."""
import json, os, sys, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swarmvis, probe

P = probe.P
BASE = {P["resonance"]:0.5, P["pitch_var"]:0.4, P["vol_var"]:0.4, P["pan_var"]:0.4,
        P["res_var"]:0.4, P["speed"]:0.6, P["std_dev"]:0.5,
        P["reflection"]:0.5, P["attract"]:0.5, P["repel"]:0.5, P["proximity"]:0.5,
        P["oscillators"]:probe.osc_norm(24),
        P["vl_atk_t"]:0.0, P["vl_atk_l"]:1.0, P["vl_dec_t"]:0.0,
        P["vl_dec_l"]:1.0, P["vl_rel_t"]:0.05}

SWEEPS = [
    ("std_dev",    [0.0, 0.25, 0.5, 0.75, 1.0], {}),
    ("pitch_var",  [0.0, 0.25, 0.6, 1.0],        {}),
    ("speed",      [0.0, 0.15, 0.5, 1.0],        {}),
    ("attract",    [0.0, 0.5, 1.0],  {P["repel"]:0.0}),
    ("repel",      [0.0, 0.5, 1.0],  {P["attract"]:0.0}),
    ("reflection", [0.0, 0.5, 1.0],  {}),
]

out = {}
for name, vals, extra in SWEEPS:
    for v in vals:
        p = dict(BASE); p.update(extra); p[P[name]] = v
        paths = swarmvis.capture(p, frames=12, ival=0.18, hold=6,
                                 out=os.path.join(os.path.dirname(__file__), "data", "sw"))
        st = [s for s in swarmvis.series(paths) if s["n"] > 200]
        if not st:
            out[f"{name}={v}"] = None
            print(f"{name:11s} {v:5.2f}  (no particles)"); continue
        sx = np.mean([s["sx"] for s in st]); sy = np.mean([s["sy"] for s in st])
        cx = np.mean([s["cx"] for s in st]); cy = np.mean([s["cy"] for s in st])
        xr = np.mean([s["xmax"]-s["xmin"] for s in st])
        yr = np.mean([s["ymax"]-s["ymin"] for s in st])
        # frame-to-frame centroid movement = how fast the cloud drifts
        drift = float(np.mean(np.abs(np.diff([s["cx"] for s in st]))) +
                      np.mean(np.abs(np.diff([s["cy"] for s in st]))))
        cov = np.mean([s["cover"] for s in st])
        out[f"{name}={v}"] = dict(sx=sx, sy=sy, cx=cx, cy=cy, xr=xr, yr=yr,
                                  drift=drift, cover=cov, frames=len(st))
        print(f"{name:11s} {v:5.2f}  spread x{sx:.3f} y{sy:.3f} | extent x{xr:.3f} y{yr:.3f}"
              f" | centre ({cx:.3f},{cy:.3f}) | drift {drift:.4f} | ink {cov*100:.2f}%")
json.dump(out, open(os.path.join(os.path.dirname(__file__), "data", "sweep.json"), "w"), indent=1)
print("done")
