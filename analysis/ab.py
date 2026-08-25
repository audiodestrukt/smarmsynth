"""Render the same patch through the original (via Wine) and the recreation,
and compare. This is the loop the rebuild is driven by."""
import hashlib, os, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe

ROOT = probe.ROOT
NEW  = os.path.join(ROOT, "swarmrender")
CACHE = os.path.join(ROOT, "analysis", "data", "ab")
os.makedirs(CACHE, exist_ok=True)

def render_new(params=None, note=60, vel=100, length=1.0, tail=0.5, sr=probe.SR):
    params = dict(params or {})
    key = repr((sorted(params.items()), note, vel, length, tail, sr, "new"))
    out = os.path.join(CACHE, hashlib.sha1(key.encode()).hexdigest()[:16] + ".wav")
    if not os.path.exists(out):
        cmd = [NEW, out, "--note", str(note), "--vel", str(vel),
               "--len", str(length), "--tail", str(tail), "--sr", str(sr)]
        for i, v in sorted(params.items()):
            cmd += ["--param", "%d=%.9g" % (i, v)]
        subprocess.run(cmd, capture_output=True, cwd=ROOT)
    return probe._read_wav_f32(out)

def log_spectrum(x, sr=probe.SR, n=16384):
    f, m = probe.spectrum(x[:n], sr, n=n)
    return f, 20 * np.log10(m + 1e-9)

def spectral_distance(a, b, sr=probe.SR):
    """Mean absolute difference of log spectra over 40 Hz .. 16 kHz, in dB."""
    n = min(len(a), len(b), 16384)
    fa, A = log_spectrum(a[:n], sr, 16384)
    fb, B = log_spectrum(b[:n], sr, 16384)
    band = (fa > 40) & (fa < 16000)
    A, B = A[band], B[band]
    A -= A.max(); B -= B.max()          # level-independent
    return float(np.mean(np.abs(A - B)))

def compare(params, note=57, length=1.0, tail=0.5, label=""):
    full = dict(probe.STATIC); full.update(params)
    o = probe.render(full, note=note, length=length, tail=tail)
    n = render_new(full, note=note, length=length, tail=tail)
    k = min(len(o), len(n))
    d = spectral_distance(o[5000:k, 0], n[5000:k, 0])
    print(f"{label:<28s} spectral distance {d:6.2f} dB   "
          f"peak orig {np.abs(o).max():.3f} new {np.abs(n).max():.3f}")
    return d
