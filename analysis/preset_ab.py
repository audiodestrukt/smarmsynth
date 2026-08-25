"""Score the recreation against the original across the real factory patches.

Synthetic one-parameter tests only go so far. The 45 factory patches are what
the synth actually sounds like, so they are the honest benchmark. Both engines
render the same patch under identical conditions and the log spectra are
compared.

Needs presets/original/*.swarmpatch -- see analysis/extract_presets.sh.
"""
import glob, hashlib, os, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe, ab

ROOT   = probe.ROOT
CACHE  = os.path.join(ROOT, "analysis", "data", "preset_ab")
PRESET = os.path.join(ROOT, "presets", "original")
os.makedirs(CACHE, exist_ok=True)

def _cached(tag, cmd, cwd=ROOT, env=None):
    out = os.path.join(CACHE, tag + ".wav")
    if not os.path.exists(out):
        subprocess.run(cmd + [], capture_output=True, cwd=cwd,
                       env=env or dict(os.environ, WINEDEBUG="-all"))
    return probe._read_wav_f32(out) if os.path.exists(out) else None

def render_original(patch, note, length, tail):
    tag = "orig_" + hashlib.sha1(f"{patch}{note}{length}{tail}".encode()).hexdigest()[:12]
    out = os.path.join(CACHE, tag + ".wav")
    if not os.path.exists(out):
        subprocess.run(["wine", os.path.join(ROOT, "vsthost32.exe"), "SwarmSynth.dll",
                        "--chunk", patch, "--render", out, "--no-midi",
                        "--note", str(note), "--len", str(length), "--tail", str(tail)],
                       capture_output=True, cwd=ROOT,
                       env=dict(os.environ, WINEDEBUG="-all"))
    return probe._read_wav_f32(out) if os.path.exists(out) else None

def render_new(patch, note, length, tail):
    tag = "new_" + hashlib.sha1(f"{patch}{note}{length}{tail}".encode()).hexdigest()[:12]
    out = os.path.join(CACHE, tag + ".wav")
    if not os.path.exists(out):
        subprocess.run([os.path.join(ROOT, "swarmrender"), out, "--preset", patch,
                        "--note", str(note), "--len", str(length), "--tail", str(tail)],
                       capture_output=True, cwd=ROOT)
    return probe._read_wav_f32(out) if os.path.exists(out) else None

def main(note=48, length=1.5, tail=1.0):
    patches = sorted(glob.glob(os.path.join(PRESET, "*.swarmpatch")))
    patches = [p for p in patches if "new_patch" not in os.path.basename(p)]
    if not patches:
        print(f"no patches in {PRESET} -- run analysis/extract_presets.sh first")
        return []

    rows = []
    for p in patches:
        o = render_original(p, note, length, tail)
        n = render_new(p, note, length, tail)
        name = os.path.basename(p).replace(".swarmpatch", "").replace("_", " ")
        if o is None or n is None:
            print(f"  {name:<24} render failed"); continue
        k = min(len(o), len(n))
        if k < 8000: continue
        d = ab.spectral_distance(o[4000:k, 0], n[4000:k, 0])
        rows.append((name, d, float(np.abs(o).max()), float(np.abs(n).max())))

    rows.sort(key=lambda r: r[1])
    print(f"{len(rows)} factory patches, note {note}, {length}s + {tail}s tail")
    print(f"(0 dB would be identical; the synthetic single-oscillator case scores 4 dB)\n")
    print(f"  {'patch':<24} {'distance':>9}  {'peak orig':>9} {'peak new':>9}")
    for name, d, po, pn in rows:
        print(f"  {name:<24} {d:8.2f} dB {po:9.3f} {pn:9.3f}")
    ds = np.array([r[1] for r in rows])
    print(f"\n  median {np.median(ds):.2f} dB   best {ds.min():.2f}   worst {ds.max():.2f}")
    silent = [r[0] for r in rows if r[3] < 1e-4]
    if silent: print(f"  recreation is silent on: {silent}")
    return rows

if __name__ == "__main__":
    main()
