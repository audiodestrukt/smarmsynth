"""Extract swarm-particle positions from captured visualiser frames.

The cube is drawn in greys; the particles are the only saturated pixels, so a
saturation threshold segments them cleanly.
"""
import glob, os, subprocess, numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CUBE = (760, 215, 1660, 1050)      # visualiser panel, below the Res/Noise colour bars

def particles(path, sat_thresh=60, val_thresh=40):
    im = Image.open(path).convert("RGB").crop(CUBE)
    a = np.asarray(im).astype(np.int16)
    mx = a.max(axis=2); mn = a.min(axis=2)
    mask = ((mx - mn) > sat_thresh) & (mx > val_thresh)
    ys, xs = np.nonzero(mask)
    h, w = mask.shape
    if len(xs) == 0:
        return np.empty((0, 2)), mask, a
    # normalise to 0..1 with y flipped so up = higher pitch
    return np.stack([xs / w, 1 - ys / h], axis=1), mask, a

def frame_stats(path):
    pts, mask, a = particles(path)
    if len(pts) == 0:
        return dict(n=0, cx=np.nan, cy=np.nan, sx=np.nan, sy=np.nan, cover=0.0)
    return dict(n=len(pts), cx=pts[:,0].mean(), cy=pts[:,1].mean(),
                sx=pts[:,0].std(),  sy=pts[:,1].std(),
                cover=mask.mean(),
                xmin=pts[:,0].min(), xmax=pts[:,0].max(),
                ymin=pts[:,1].min(), ymax=pts[:,1].max())

def capture(params, frames=16, ival=0.18, hold=10, out=None):
    """Run capture_swarm.sh with --param args and return the frame paths."""
    out = out or os.path.join(ROOT, "analysis", "data", "frames")
    args = []
    for i, v in sorted(params.items()):
        args += ["--param", "%d=%.6g" % (i, v)]
    env = dict(os.environ, OUT=out, FRAMES=str(frames), IVAL=str(ival), HOLD=str(hold))
    subprocess.run([os.path.join(ROOT, "analysis", "capture_swarm.sh")] + args,
                   env=env, cwd=ROOT, capture_output=True)
    return sorted(glob.glob(os.path.join(out, "f*.png")))

def series(paths):
    return [frame_stats(p) for p in paths]
