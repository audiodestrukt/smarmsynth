# Ideas and observations

A running list. Nothing here is committed to — it is a place to park things
noticed while playing, so they are not lost.

## Ideas

### An intentional distortion stage

There is something distorting inside the original's signal path, upstream of
the output. Noticed while playing: some factory patches are not especially
loud yet still have an obvious clipped or bit-reduced character, which output
clipping alone would not explain.

This refines an earlier measurement rather than contradicting it. The benchmark
recorded the original hitting exactly 1.000 on 18 of the 45 factory patches
(`analysis/FINDINGS.md`), and that was read as the output clipping. An internal
stage explains both that and the quiet-but-crunchy patches.

**Not necessarily to emulate.** The interesting version is a deliberate
distortion somewhere in the recreation's path, as its own feature, rather than
a reproduction of whatever the original happens to do.

Worth deciding before building:

- Where it sits. Per-particle (each grain distorts on its own, so the swarm
  smears differently) is a very different instrument from one stage on the
  summed output.
- What kind. Hard clip, soft saturation, and bit/sample-rate reduction all fit
  the description but sound nothing alike — and the "bit reduced" reading
  points at quantisation rather than clipping.
- How it relates to the existing `Overdrive` parameter, which is already a
  tanh waveshaper on the summed output.

If it ever matters to pin down what the original is doing, the cheap test is to
render a quiet factory patch and look for harmonic distortion or amplitude
quantisation in a signal nowhere near full scale.

### The 3D view's trails

The particle trails do not look like the original's. **Deliberately parked** —
the current view is fast enough, and a true 3D engine is not a direction worth
taking for this variant. Recreating the original effect comes first; an
enhanced version later is the place for that, if anywhere.

Recorded so the reasons are not lost. Two separate differences, and they are
not equally expensive:

- **The trail itself is not a trail.** `SwarmView` draws a straight line from
  the particle's position back along its *instantaneous velocity vector*, scaled
  by a constant. The original's streaks look like an actual path through recent
  positions. A short ring buffer of past positions per particle would be much
  closer and costs nothing structurally — no 3D engine involved. This is the
  cheap half.
- **The streaks are clamped far too short.** `SwarmView` limits a streak to
  ±14 px. In the original a fast, high-Noise particle draws a pale line right
  across the cube — which is what those white lines after an Anarchy press are
  (see `analysis/FINDINGS.md`). Raising or removing the clamp is a one-line
  change and is the most visible difference of the three.
- **The projection is faked.** The box is drawn as a front and a back rectangle
  with particle positions linearly interpolated between them by their Vol value,
  rather than a real perspective divide. Straight-line motion through the box
  therefore does not curve on screen the way true perspective would. This is the
  half that would pull in real 3D.

The axis assignment itself is believed right — Pitch vertical, Pan across, Vol
into the screen, with Res as hue and Noise desaturating — since that came from
reading the original's own axis labels and colour keys.

### Run it in a browser

The engine is header-only C++ with no JUCE anywhere in the DSP —
`SwarmEngine.h`, `SwarmParams.h`, `SwarmEnvModel.h`, `SwarmAnarchy.h` and
`SwarmPreset.h` only use the standard library, which `swarmrender` already
proves by building with plain `g++`. Confirmed: it compiles under Emscripten
unchanged, first try.

So a browser version is mostly plumbing rather than a port:

- Emscripten the engine to WASM.
- An `AudioWorkletProcessor` calling `process()`.
- A canvas for the swarm view — the current view is a faked projection anyway,
  so 2D canvas is enough and nothing is lost.
- The `.swarmpatch` files are static assets the same parser reads.

Would make the write-up land much harder: read how it was reverse-engineered,
then play the thing in the same page.

## Open questions

Carried over from the reverse-engineering work; see `analysis/FINDINGS.md` for
detail.

- **The attack transient.** The worst benchmark scores are all plucked or brassy
  patches — Pizzicato 99.6 dB, Cheeky 95.8, Digi Tamba 82.6, Brass 80.5 —
  against 6.6 dB for the best. Points at the attack and the high-resonance
  grain, not the swarm.
- **The motion law.** The pairwise attract/repel/proximity force law was never
  recovered; the engine uses a boids-style stand-in. `Std. Dev.` *tightens* the
  cloud as it rises, which is the opposite of what the name suggests, and is
  the loose thread most likely to explain the rest.
- **The formant ceiling.** At high Resonance and high notes the formant clamps
  around 3.5–4 kHz instead of following `f0 * (1 + 46·Res³)`. Not characterised.
- **Scroll wheel on the visualiser.** The original does something with the
  scroll wheel over the 3D view, and the mouse cursor changes colour with it.
  Unexplored. The colour-changing cursor suggests it is selecting *something*
  and showing you what — plausibly which dimension the view is colouring by, or
  which one the wheel is about to adjust. The two unimplemented sliders on that
  panel and the Options → Visualisation entry are probably part of the same
  feature; worth looking at all three together rather than separately.

## Smaller things

- **Pitch coarse and fine** exist in the original's state chunk (`0x1c`, `0x20`)
  but are not exposed as VST parameters, so that knob is inert in the
  recreation. The recreation could expose them as its own parameters.
- The 3D panel's **zoom and rotate sliders** are not implemented.
- The original uses a chunky **pixel font**; the recreation uses the system
  sans. Closest single change to the overall feel.
