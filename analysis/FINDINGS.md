# What the original SwarmSynth is doing

Everything below was established by driving `SwarmSynth.dll` through the Wine
host in this repo — setting parameters, playing notes, measuring the audio, and
watching the plugin's own visualiser. No plugin code was disassembled or copied.
Each claim says how it was established, and what is still a guess is labelled a
guess.

## The shape of the instrument

A cloud of oscillators ("particles") moves inside a **5-dimensional box**:

| dimension | centre ("home") | half-width ("range") |
|---|---|---|
| Vol   | p0 Volume    | p4 Vol Var.   |
| Pitch | the note     | p5 Pitch Var. |
| Pan   | p1 Pan       | p6 Pan Var.   |
| Res   | p2 Resonance | p7 Res Var.   |
| Noise | p3 Noise     | p8 Noise Var. |

**How we know:** the plugin's own 3D display plots Vol / Pan horizontally and
Pitch vertically, and colours each dot by Res (rainbow key) and Noise (greyscale
key) — the two axis keys sit above the cube. Sweeping `Pitch Var.` from 0 to
100% grows the measured vertical extent of the dot cloud from 0.063 to 0.552 of
the cube, and nothing else moves it. Particles are drawn as short streaks, i.e.
the display shows their velocity.

Each dimension has its own envelope **and** a second envelope for its variance,
plus one for Speed — 11 in total. Confirmed twice over: the GUI's two envelope
panels are labelled "Vol Env" and "Vol Range Env" and re-label when you select a
different column, and the state chunk contains exactly 11 envelope records of
108 bytes each.

## The oscillator: a formant grain

At `Resonance = 0` a single oscillator emits a **pure sine** at f0 — every
harmonic above the first measures as exactly zero. As Resonance rises the output
becomes a short burst repeating at f0. Measured:

```
spectral peak     f_res = f0 * M(Res)
                  M(Res) = 1 + 46 * Res^3
```

Fitted over Res 0..100% at A0/A1 (low notes, so the harmonic grid is fine). Free
fit gives `1 + 46.93*Res^3.043`, rms 0.25 harmonics; the round numbers fit with
rms 0.375, which is inside the ±0.5-harmonic measurement resolution.

Burst length × M ≈ 1 across the range, so **each grain is about one cycle of the
formant sine**, then silence until the next f0 period. That is FOF / VOSIM style
formant synthesis, and it is why the synth sounds raw — hard-gated bursts.

At high Res and high notes the formant clamps around 3.5–4 kHz rather than
following M, so there is a ceiling somewhere in the oscillator. Not characterised.

The grain window: a time plot *looks* unwindowed (both lobes reach full
amplitude, sharp edges at zero), but A/B against the original says a raised
cosine fits better — 14.8 dB spectral distance vs 24.6 dB with no window. The
window is real; the eyeball was wrong.

There is also a `Vowel Swarm` preset name in the binary, which is what you would
expect from a bank of independently-tuned formant grains.

## Parameter laws — exact, zero residual

`vsthost32 --map-params 33` sweeps every parameter and asks the plugin what each
value means. Every law below reproduces the plugin's own readback with **zero
error**:

| params | quantiser |
|---|---|
| 0, 2–18, 35 | `v' = floor(100v)/100` — integer 0..100 |
| 1 (Pan) | `n = trunc(200v-100)` — signed integer -100..100 |
| 19 (Oscillators) | `floor(63v)/63` — integer 1..64 |
| 20 (Portamento) | `floor(999v)/999` — integer 1..1000 ms |
| 27 (Pt Env Rg) | `floor(24v)/24` — integer 0..24 semitones |
| 43, 51 (Rs/Ns Env Rg) | `floor(50v)/50` — integer 0..50 |
| 21 (Seed), 22–100 | raw float, no quantisation |

Note it **truncates**, it does not round.

Display laws:

- Volume is a linear gain shown as `20*log10(v)` dB.
- Every envelope time is `t_ms = 0.1 + 9999.9 * v²` — fit residual under
  0.005 ms across the full 0.1 ms .. 10 s range.
- Seed is `v * 2^27`.
- Everything else is linear in its stated unit.

## Preset format

The plugin sets `effFlagsProgramChunks`; its state is a **1908-byte** opaque
chunk. Setting one parameter at a time and diffing the chunk resolved **100 of
101 parameters to a single 4-byte slot each**. The 101st (Seed, at `0x550`) is
re-randomised on every instantiation, which is why it appeared in every diff and
had to be identified by elimination.

```
0x000..0x017   patch name, 24 bytes ASCII
0x018          Volume            int32
0x024..0x064   Pan, Resonance, Noise, the five Var., Q, Lowpass, Overdrive,
               Speed, Std.Dev., Reflection, Attract, Repel, Proximity
0x098 +108*n   11 envelope records (vol, pitch, pan, res, noise, then those
               five again as variance envelopes, then speed)
0x50c          Oscillators       int32
0x514          Portamento        int32
0x550          Seed              int32
0x760          Speed LFO         int32
total          1908 bytes
```

Full offset table: `analysis/data/chunk_map.json`. The envelope record is 108
bytes but only 7–8 fields are exposed as VST parameters, so the GUI's envelope
editor has breakpoint freedom the parameter list does not.

One trap: **envelope times are stored in seconds**, not as normalised parameter
values. The default patch holds 0.1 / 0.5 / 0.2 in the slots whose parameters
read 100 / 500 / 200 ms. Levels are stored normalised. This was caught by
drawing the envelope panels in the recreated GUI and noticing they disagreed
with the original's own readout.

This is enough to **load original patches into the recreation**, which
`juce/Source/SwarmPreset.h` does.

## The motion law — the weak spot

What is measured:

- `Speed = 0` freezes the cloud **exactly** (frame-to-frame centroid drift
  measures 0.0000). Raising Speed raises drift and then saturates.
- `Pitch Var.` sets the vertical extent of the box, monotonically.
- `Reflection` changes how far the cloud spreads (extent 0.313 at 0% down to
  0.243 at 100%), consistent with particles bouncing off the walls of the box
  and keeping some fraction of their energy.
- `Std. Dev.` *tightens* the cloud as it rises (ink coverage 1.11% → 0.57%),
  which is the opposite of what the name suggests. Not understood.
- `Attract = 100%` (with Repel 0) and `Repel = 100%` (with Attract 0) both park
  the cloud centre at a different height without collapsing it. Not understood.

What is **not** measured: the actual pairwise force law. The engine currently
uses a boids-style attract/repel with a proximity radius, which is a stand-in.
Pinning this down needs per-particle tracking across frames rather than
cloud-level statistics — that is the obvious next experiment.

## Where the recreation stands

`analysis/ab.py` renders the same patch through both and reports mean absolute
log-spectrum difference in dB (0 = identical).

| patch | distance |
|---|---|
| pure sine, 1 osc | 4.1 dB |
| lowpass 50% | 13.8 dB |
| resonant grain, 1 osc | 14.8 dB |
| 8 osc moving | 22.8 dB |
| 8 osc static | 52.4 dB |
| noise 50% | 67.1 dB |

The single-oscillator tone is close. Multi-oscillator summing and the noise
source are not — those are the next things to work on, in that order.
