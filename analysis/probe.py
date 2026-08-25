"""Render SwarmSynth through the Wine host and analyse the result.

Everything here is black-box: we set parameters, play notes, and measure the
audio that comes out. No plugin code is read or copied.
"""
import hashlib, os, struct, subprocess, sys
import numpy as np

ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST   = os.path.join(ROOT, "vsthost32.exe")
PLUGIN = os.path.join(ROOT, "SwarmSynth.dll")
CACHE  = os.path.join(ROOT, "analysis", "data", "renders")
SR     = 44100
os.makedirs(CACHE, exist_ok=True)

# ---------------------------------------------------------------------------
# parameter index map, straight from --info
# ---------------------------------------------------------------------------
P = {
    "volume":0, "pan":1, "resonance":2, "noise":3,
    "vol_var":4, "pitch_var":5, "pan_var":6, "res_var":7, "noise_var":8,
    "speed":9, "speed_lfo":10, "std_dev":11, "reflection":12,
    "attract":13, "repel":14, "proximity":15,
    "lowpass":16, "q":17, "overdrive":18,
    "oscillators":19, "portamento":20, "seed":21,
    "vl_atk_t":22, "vl_atk_l":23, "vl_dec_t":24, "vl_dec_l":25, "vl_rel_t":26,
    "pt_rg":27, "pt_ini":28, "pt_atk_t":29, "pt_atk_l":30, "pt_dec_t":31,
    "pt_dec_l":32, "pt_rel_t":33, "pt_rel_l":34,
    "pn_rg":35, "pn_ini":36, "pn_atk_t":37, "pn_atk_l":38, "pn_dec_t":39,
    "pn_dec_l":40, "pn_rel_t":41, "pn_rel_l":42,
    "rs_rg":43, "rs_ini":44, "rs_atk_t":45, "rs_atk_l":46, "rs_dec_t":47,
    "rs_dec_l":48, "rs_rel_t":49, "rs_rel_l":50,
    "ns_rg":51, "ns_ini":52, "ns_atk_t":53, "ns_atk_l":54, "ns_dec_t":55,
    "ns_dec_l":56, "ns_rel_t":57, "ns_rel_l":58,
    "spd_ini":94, "spd_atk_t":95, "spd_atk_l":96, "spd_dec_t":97,
    "spd_dec_l":98, "spd_rel_t":99, "spd_rel_l":100,
}

def osc_norm(n):      return (n - 1) / 63.0 + 1e-6      # 1..64 oscillators
def time_norm(ms):    return float(np.sqrt(max(ms - 0.1, 0) / 9999.9))
def pct_norm(p):      return p / 100.0 + 1e-6           # 0..100 int params

# A patch with every source of randomness and motion switched off: one
# oscillator, no noise, no variance, no swarm motion, flat sustain.
STATIC = {
    P["noise"]: 0.0, P["vol_var"]: 0.0, P["pitch_var"]: 0.0, P["pan_var"]: 0.0,
    P["res_var"]: 0.0, P["noise_var"]: 0.0,
    P["speed"]: 0.0, P["speed_lfo"]: 0.0, P["std_dev"]: 0.0,
    P["oscillators"]: osc_norm(1),
    P["portamento"]: 0.0,
    P["lowpass"]: 1.0, P["q"]: 0.0, P["overdrive"]: 0.0,
    P["pan"]: 0.5,
    # flat volume envelope: instant attack, sustain at full
    P["vl_atk_t"]: time_norm(0.1), P["vl_atk_l"]: 1.0,
    P["vl_dec_t"]: time_norm(0.1), P["vl_dec_l"]: 1.0,
    P["vl_rel_t"]: time_norm(0.1),
    # all modulation envelope ranges to zero so nothing drifts
    P["pt_rg"]: 0.0, P["pn_rg"]: 0.0, P["rs_rg"]: 0.0, P["ns_rg"]: 0.0,
}

# ---------------------------------------------------------------------------

def _read_wav_f32(path):
    d = open(path, "rb").read()
    i = d.find(b"data")
    n = struct.unpack("<I", d[i+4:i+8])[0]
    a = np.frombuffer(d[i+8:i+8+n], dtype="<f4")
    return a.reshape(-1, 2) if a.size % 2 == 0 else a.reshape(-1, 1)

def render(params=None, note=60, vel=100, length=1.0, tail=0.5,
           program=0, sr=SR, block=512):
    """Render one note and return an (N, 2) float32 array. Cached on disk."""
    params = dict(params or {})
    key = repr((sorted(params.items()), note, vel, length, tail, program, sr, block))
    tag = hashlib.sha1(key.encode()).hexdigest()[:16]
    out = os.path.join(CACHE, tag + ".wav")
    if not os.path.exists(out):
        cmd = ["wine", HOST, PLUGIN, "--render", out, "--note", str(note),
               "--vel", str(vel), "--len", str(length), "--tail", str(tail),
               "--program", str(program), "--sr", str(sr), "--block", str(block),
               "--no-midi"]
        for i, v in sorted(params.items()):
            cmd += ["--param", "%d=%.9g" % (i, v)]
        env = dict(os.environ, WINEDEBUG="-all")
        r = subprocess.run(cmd, capture_output=True, env=env, cwd=ROOT)
        if not os.path.exists(out):
            sys.stderr.write(r.stdout.decode(errors="replace") + "\n")
            raise RuntimeError("render failed")
    return _read_wav_f32(out)

def static_patch(**over):
    """STATIC with named overrides, e.g. static_patch(resonance=0.5)."""
    p = dict(STATIC)
    for k, v in over.items():
        p[P[k]] = v
    return p

# ---------------------------------------------------------------------------
# measurement helpers
# ---------------------------------------------------------------------------

def spectrum(x, sr=SR, n=None):
    x = np.asarray(x, dtype=float)
    n = n or len(x)
    w = np.hanning(len(x))
    X = np.fft.rfft(x * w, n)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    return f, np.abs(X) / (np.sum(w) / 2)

def harmonics(x, f0, sr=SR, count=24):
    """Amplitude at each harmonic of f0, normalised to the fundamental."""
    f, m = spectrum(x, sr)
    out = []
    for k in range(1, count + 1):
        target = f0 * k
        if target > sr / 2 - 50:
            out.append(0.0); continue
        band = (f > target - 12) & (f < target + 12)
        out.append(m[band].max() if band.any() else 0.0)
    out = np.array(out)
    return out / (out[0] if out[0] > 0 else 1.0)

def env_follow(x, sr=SR, hop=64):
    """Peak envelope, one point per hop samples."""
    n = len(x) // hop
    return np.abs(np.asarray(x[:n*hop]).reshape(n, hop)).max(axis=1), hop / sr

def est_f0(x, sr=SR, fmin=40, fmax=2000):
    f, m = spectrum(x, sr)
    band = (f >= fmin) & (f <= fmax)
    return float(f[band][np.argmax(m[band])])

def note_hz(n): return 440.0 * 2 ** ((n - 69) / 12.0)
