"""What do the factory patches actually use?

Reads the extracted .swarmpatch files and reports, per parameter, the range the
original's own patches occupy -- in normalised 0..1 units, which is what the
Anarchy randomiser samples in. Useful for tuning that randomiser to produce
patches that sit where real ones do rather than uniformly over the space.
"""
import csv, glob, json, os, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

CMAP  = json.load(open(os.path.join(HERE, "data", "chunk_map.json")))
SPEC  = json.load(open(os.path.join(HERE, "data", "spec.json")))
BYIDX = {p["index"]: p for p in SPEC["params"]}

# integer-quantised parameters and the divisor that returns them to 0..1
INT_SCALE = {**{i: 100.0 for i in list(range(0, 19)) + [35]},
             19: 63.0, 20: 999.0, 27: 24.0, 43: 50.0, 51: 50.0}

def normalised(chunk, index):
    off = CMAP.get(str(index))
    if off is None:
        return None
    if index == 1:                                   # Pan is signed -100..100
        return (struct.unpack("<i", chunk[off:off+4])[0] + 100.0) / 200.0
    if index in INT_SCALE:
        n = struct.unpack("<i", chunk[off:off+4])[0]
        if index == 19: return (n - 1) / 63.0         # Oscillators 1..64
        if index == 20: return (n - 1) / 999.0        # Portamento 1..1000 ms
        return n / INT_SCALE[index]
    if index == 21:
        return struct.unpack("<i", chunk[off:off+4])[0] / 134217728.0
    return struct.unpack("<f", chunk[off:off+4])[0]   # times are stored in seconds

def load(pattern=None):
    pattern = pattern or os.path.join(ROOT, "presets", "original", "*.swarmpatch")
    return [open(f, "rb").read() for f in sorted(glob.glob(pattern))
            if "new_patch" not in os.path.basename(f)]

def main(upto=22):
    chunks = load()
    if not chunks:
        print("no patches found -- run analysis/extract_presets.sh first"); return
    print(f"{len(chunks)} factory patches, normalised 0..1\n")
    print(f"  {'idx':>3} {'parameter':<14} {'p10':>6} {'p50':>6} {'p90':>6} {'max':>6}  shape")
    for i in range(0, upto):
        vals = [normalised(c, i) for c in chunks]
        vals = np.array([v for v in vals if v is not None], dtype=float)
        if vals.size == 0: continue
        p10 = float(np.percentile(vals, 10)); p50 = float(np.percentile(vals, 50))
        p90 = float(np.percentile(vals, 90)); mx = float(vals.max())
        # a crude shape hint for choosing a sampling curve
        shape = ("bottom-heavy" if p50 < 0.25 else
                 "top-heavy"    if p50 > 0.75 else
                 "spread")
        print(f"  {i:3d} {BYIDX[i]['name']:<14} {p10:6.2f} {p50:6.2f} {p90:6.2f} {mx:6.2f}  {shape}")

if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 22)
