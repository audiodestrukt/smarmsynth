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

## Envelopes are breakpoint lists, not ADSRs

The state chunk gives each of the eleven envelopes a 108-byte record holding a
**count** plus parallel time and level arrays, with room for about ten points.
Only the first and last segment appear in the VST parameter list, so the extra
breakpoints are GUI-and-preset state, not automatable parameters.

The editor's verbs were established by scripting the original's own editor
(`analysis/gui_run.sh`, which posts mouse messages straight to the plugin
window) and dumping the state chunk after each interaction:

| action | effect |
|---|---|
| single click | nothing |
| **double-click on empty plot** | adds a breakpoint |
| **drag a point** | moves it in time and level |
| **right-click a point** | deletes it |

A point's level is exactly the vertical fraction of the plot: clicking at 0.829
of the height stored 0.8333, at 0.512 stored 0.5238 — a pixel's worth of
difference. The stored time is a fraction that reaches 1.0 around 40% of the
way across the plot, and inserts past that point are refused, so the editable
span is not simply the whole width. That constant is not pinned down.

Deleting a point folds its duration into the neighbouring segment, so the
envelope keeps its overall length. Adding one splits the segment it landed in,
again preserving total length.

## The factory patches

The original ships **45 factory patches**, and they are not files: they live
inside the plugin's own resources (`.rsrc` is 3.2 MB of a 3.6 MB DLL). The
Presets menu lists them:

```
Brass, Buzz Mania, Cheeky, Chewbacca, Clarinet, Cosmic Kazoo Piano, Crazy Saw,
Crunchy, Digi Tamba, Dischord, Drunk Pixies, Enola, Filth Bass, Firework,
Freaky Note, Funky Slap, Gentle Gong, Ghostly Wail, Growing Bass, Harpsichord,
Heroic Horn, High And Dangerous, Juicy Plastic, Kick Bass, Large Pad, Mad Pad,
Metallic Thwack, Mulling Horn, Passing Gust, Pizzicato, Possessed Organ,
Power Bass, Resonant Rip, Rhythmic, Saxophone, Shreek, Siren, Soft Organ,
Spooky Organ, Squelchy, String Pad, Thunderclap, Tuned Perc, Vowel Swarm,
Wobble Bass
```

`analysis/extract_presets.sh` walks that menu, loads each entry and dumps the
resulting state chunk, giving a `.swarmpatch` file per patch that both the
original and the recreation can load. It keys on the name inside each chunk
rather than the menu index, because Wine reports menu item rects with a
consistent vertical offset -- clicking "index 43" actually selects the entry
five rows up.

These patches are the original author's work, so they are **not** distributed
with this project. Extract them from your own copy of the plugin.

Having them turns the A/B into a real benchmark: `analysis/preset_ab.py`
renders every factory patch through both engines and scores them, which is a
far more honest measure than one-parameter synthetic cases.

**Where the recreation stands on real patches** (median absolute log-spectrum
difference, note 48):

```
median 20.8 dB    best 6.6 (Dischord)    worst 99.6 (Pizzicato)
```

The best nine are all under 9 dB -- Dischord, Metallic Thwack, Wobble Bass,
High And Dangerous, Shreek, Mad Pad, Growing Bass, Mulling Horn, Freaky Note.
The worst are Pizzicato, Cheeky, Digi Tamba and Brass, all around 80-100 dB;
they are the plucked and brassy ones, which points at the attack transient and
the high-resonance grain rather than the swarm.

Two things worth recording from the peak column:

- **The original clips.** It hits exactly 1.000 on 18 of the 45 factory
  patches. Full-scale output is normal for this synth, not a fault, which also
  means the recreation's randomiser reaching full scale is not unfaithful.
- Levels diverge in both directions, so the gap is not a single missing gain
  stage.

### What the factory patches use

Across all 45, in normalised units:

| parameter | p10 | p50 | p90 |
|---|---|---|---|
| Volume | 0.68 | **1.00** | 1.00 |
| Resonance | 0.00 | **0.07** | 0.52 |
| Pan Var. | 0.33 | **1.00** | 1.00 |
| Res Var. | 0.50 | **1.00** | 1.00 |
| Std. Dev. | 0.00 | **0.04** | 0.50 |
| Oscillators | 0.06 | **0.11** (about 8) | 0.22 |
| Portamento | 0.00 | **0.02** (about 20 ms) | 0.08 |

Real patches are particular: Pan Var and Res Var pinned near maximum, Resonance
and Std. Dev. near zero, Volume at full, Portamento barely moving. That is the
house style, and the Anarchy randomiser now samples from it rather than
uniformly over the space (`analysis/preset_stats.py`).

## The Anarchy button

It lives under **Options -> `<anarchy button>`** -- that is the literal menu
text. The binary also carries the format string `random patch #%X`, so a press
renames the patch as well as randomising it.

Finding it needed a way to read a Win32 popup menu, which no screenshot could
capture here: the host now asks the menu window (class `#32768`) for its HMENU
via `MN_GETHMENU` and prints the item text directly (`menudump`). The Options
menu is:

```
[0] Synthesis            [5] Visualisation
[1] Speed LFO            [7] Reset Devices
[2] Midi Controller Map  [8] <anarchy button>
[3] Determinism seeding
```

What it randomises internally was **not** recovered, so the recreation's
version is a designed randomiser rather than a reproduction. It is measured
instead of assumed: `analysis/anarchy_survey.py` renders many seeds and reports
how many are audible and how much they actually move. Movement is measured as
the wobble of the spectral centroid and the depth of the loudness envelope, not
as frame-to-frame spectral difference -- that first metric turned out to be
measuring noise, since every "moving" patch it liked simply had a high Noise
Var. Current numbers over 64 seeds: 98% audible, 100% moving, median centroid
wobble 1.6 semitones.

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
