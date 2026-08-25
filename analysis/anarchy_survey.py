"""How good is the Anarchy button?

A randomiser is only worth pressing if most presses give something you can
hear. This renders many seeds and reports how many are audible, how loud, and
how much they move -- the last one matters because a static patch is not what
this synth is for.
"""
import os, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe

ROOT = probe.ROOT
OUT  = os.path.join(ROOT, "analysis", "data", "anarchy_survey")
os.makedirs(OUT, exist_ok=True)

def render(seed, note=48, length=1.5, tail=1.0):
    path = os.path.join(OUT, f"{seed}.wav")
    if not os.path.exists(path):
        subprocess.run([os.path.join(ROOT, "swarmrender"), path, "--anarchy", str(seed),
                        "--note", str(note), "--len", str(length), "--tail", str(tail)],
                       capture_output=True, cwd=ROOT)
    return probe._read_wav_f32(path)

def stats(y, sr=probe.SR):
    """Loudness, and how much the patch actually moves.

    Frame-to-frame spectral difference is no good here: broadband noise makes
    it large without the swarm going anywhere. Instead track the spectral
    centroid and the loudness envelope over time and measure their *slow*
    variation, which is what swarm motion produces.
    """
    x = y[:, 0].astype(np.float64)
    peak = float(np.abs(y).max())
    rms  = float(np.sqrt((x ** 2).mean()))

    n, hop = 2048, 512
    if len(x) < n * 4:
        return peak, rms, 0.0, 0.0

    win = np.hanning(n)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    cent, energy = [], []
    for i in range(0, len(x) - n, hop):
        mag = np.abs(np.fft.rfft(x[i:i+n] * win))
        e = mag.sum()
        energy.append(e)
        cent.append(float((mag * freqs).sum() / e) if e > 1e-9 else 0.0)
    cent = np.array(cent); energy = np.array(energy)

    live = energy > energy.max() * 0.05          # ignore the silent tail
    if live.sum() < 8:
        return peak, rms, 0.0, 0.0
    c = cent[live]
    # relative wobble of the centroid, in semitones so it reads musically
    ratio = np.clip(c / max(np.median(c), 1e-9), 1e-6, 1e6)
    centroid_semitones = float(np.std(12.0 * np.log2(ratio)))

    e = energy[live] / energy[live].max()
    tremolo = float(np.std(e) / max(np.mean(e), 1e-9))
    return peak, rms, centroid_semitones, tremolo

def main(count=48):
    rows = []
    for seed in range(1, count + 1):
        rows.append((seed,) + stats(render(seed)))
    peaks = np.array([r[1] for r in rows])
    rmss  = np.array([r[2] for r in rows])
    wob   = np.array([r[3] for r in rows])
    trem  = np.array([r[4] for r in rows])

    audible = rmss > 0.005                 # about -46 dBFS
    loud    = peaks > 0.99                 # hitting full scale
    moving  = (wob > 0.35) | (trem > 0.18)  # audible wobble or tremolo

    print(f"{count} random patches, note 48, 1.5 s + 1 s tail\n")
    print(f"  audible (rms > -46 dBFS)      {audible.sum():3d} / {count}   {100*audible.mean():5.1f}%")
    print(f"  moving  (wobble or tremolo)   {moving.sum():3d} / {count}   {100*moving.mean():5.1f}%")
    print(f"  at or over full scale         {loud.sum():3d} / {count}   {100*loud.mean():5.1f}%")
    print(f"\n  peak            median {np.median(peaks):.3f}")
    print(f"  rms             median {np.median(rmss):.4f} ({20*np.log10(np.median(rmss)+1e-12):.1f} dBFS)")
    print(f"  centroid wobble median {np.median(wob):.2f} semitones   p90 {np.percentile(wob,90):.2f}")
    print(f"  tremolo depth   median {np.median(trem):.2f}            p90 {np.percentile(trem,90):.2f}")
    if (~audible).any():
        print("\n  silent seeds:", [r[0] for r, a in zip(rows, audible) if not a])
    return rows

if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 48)
