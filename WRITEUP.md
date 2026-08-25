# Reverse engineering a 2003 synth plugin by measuring it

<!--
DRAFT. Written as raw material in your voice; rewrite freely. The bits marked
[FILL IN] are things only you know and are what will make this read as yours.
Everything numerical is real and comes from analysis/FINDINGS.md.
-->

SwarmSynth is a VST plugin from 2003 by Anarchy Sound Software. It was
freeware from 2012 on. It's a swarm of oscillators that move around a
parameter space, attracting and repelling each other and bouncing off the
walls, and it sounds rough in a way I've never heard from another synth.

[FILL IN: when you first used it, what you used it on, what it was to you.
Two or three sentences. This is the part nobody else can write.]

The only thing that exists is a 32-bit Windows DLL. No source, and the company
doesn't appear to be around. I wanted to be able to use the sound in a modern
setup, and I wanted to know how it worked. Disassembling it seemed like the
obvious route, but I ended up not doing that at all.

I did this with Claude Code (Opus 5). It wrote the host, the analysis scripts,
the JUCE recreation and the web build. I played the original and the
recreation side by side, told it what was wrong, and remembered things about
the plugin it had no way to know. Everything below that's a number came out of
a script that's in the repo, and you can rerun them against a copy of the DLL.

You can play the result in a browser: https://audiodestrukt.github.io/smarmsynth/
Code: https://github.com/audiodestrukt/smarmsynth

## Getting it to run

Step one was just running it. That meant a VST2 host: a clean-room header for
the published VST2 ABI (struct layout and opcode numbers, nothing from the
Steinberg SDK), cross-compiled to a 32-bit Windows exe with mingw, run under
Wine. Audio goes out through WinMM to PipeWire, MIDI comes in through Wine's
ALSA bridge. About 900 lines to start with. It loaded the plugin on the first
try and the editor came up.

The important feature turned out to be offline rendering. Same patch, same
note, same block size, identical output every time. Once that worked, most
questions about the synth became things you could measure instead of guess at.

## The parameters

The host sweeps every parameter through 33 values and asks the plugin what each
one means, using the plugin's own display strings and readback. 101 parameters.

Every envelope time follows `t_ms = 0.1 + 9999.9 * v^2`, with the fit off by
less than 0.005 ms anywhere in the range. The integer parameters truncate
rather than round. The fitted quantisers reproduce the plugin's own readback
exactly on all 101.

## The oscillator

With one oscillator and all the variance controls at zero, Resonance at 0 gives
a pure sine. Every harmonic above the fundamental measures as zero. Turn
Resonance up and the output becomes a short burst that repeats at the
fundamental. Sweeping Resonance and tracking the spectral peak gives

    f_res = f0 * (1 + 46 * Res^3)

A free fit lands on 46.93 and an exponent of 3.043. The burst is about one
cycle of the formant frequency, then silence until the next period. That's
FOF-style formant synthesis, and the hard gating is where the roughness comes
from.

## The patch format, by poking it

The plugin stores its state as a 1908-byte opaque chunk. Rather than read it,
the script changes one parameter at a time, dumps the chunk, and diffs. 99
parameters resolved to a single 4-byte slot each on the first pass. The last
one, Seed, showed up in every diff because it's re-randomised each time the
plugin is instantiated. That's how it got identified.

This produced a bug I would not have found by reading a spec. Envelope times
are stored in seconds, not as normalised values. The default patch has 0.1,
0.5 and 0.2 in slots whose parameters read 100, 500 and 200 ms. I only noticed
because the recreated envelope panel showed 224 ms where the original showed
500. Every imported patch had been getting the wrong envelopes.

## Driving the GUI

Some of the synth isn't in the parameter list. The envelope editors take
breakpoints the VST parameters never expose. The 45 factory patches aren't
files, they're compiled into the DLL's resources.

Driving the editor through X was unreliable under this compositor: some
synthetic clicks landed, some didn't. So the host got the ability to post mouse
messages directly to the plugin's child windows in editor coordinates, and to
dump the state chunk after each step. Each interaction shows up as a diff.

That's how the envelope editor's behaviour came out: single click does nothing,
double-click adds a breakpoint, drag moves it, right-click deletes it. A point's
level is exactly its vertical fraction of the plot. Clicking at 0.829 of the
height stored 0.8333.

Menus were worse. A Win32 popup menu is its own top-level window that the
compositor won't screenshot, and it runs a modal message loop that a
synchronous SendMessage never returns from. What worked was finding the menu
window by class name, asking it for its HMENU, and reading the item text
through the API. The Options menu has an item literally named
`<anarchy button>`. I remembered that one existed but not where it was.

Walking the Presets menu the same way pulled out all 45 factory patches. Wine
reports menu item rectangles offset by several rows, and the menu scrolls, so
the last five were only reachable by keyboard.

## The recreation, and the number that decides things

The recreation is a JUCE plugin. The engine is header-only C++ with no
framework in it, which is why it later compiled to WebAssembly without changes.

With both engines rendering the same patch under identical conditions, the
comparison is a spectral distance in dB. That number overruled me several
times.

The time-domain plot of a grain looked unwindowed: both lobes at full
amplitude, sharp edges at zero. Implemented that way, the score got worse. A
raised-cosine window scores 14.8 dB against 24.6 dB for no window. The code
has the windowed version and a comment about why.

Random patches from the Anarchy button looked like only 25% of them moved. The
metric was frame-to-frame spectral change, and every patch it liked just had a
lot of noise in it. Measured as spectral-centroid wobble and loudness-envelope
depth instead, 100% of them move.

A probe for the particle colour mapping came back with two values repeating and
53,000 "particle" pixels. It was screenshotting the recreation. A leftover
standalone had the same window title. Once pointed at the right window: the
original colours particles in HSL, hue `0.79 * Res`, lightness
`0.21 + 0.60 * Noise`, saturation fixed. At zero noise a particle is a dark
version of its colour. I had it in HSV, which gets the wash-out at high noise
and misses the darkening at low noise.

Two more things came from playing it rather than measuring it. Two instances
loading the same patch play a bit-identical first note, but three notes in a
row inside one instance are all different. The Seed sets the starting state
and the generator runs forward. The recreation was reseeding on every note, so
every note was the same. And some quiet factory patches sound clipped or
bit-reduced even though the output isn't near full scale, so there's some
distortion stage inside the original that I haven't found yet.

## Where it stands

Across all 45 factory patches, the recreation scores a median of 20.8 dB from
the original. Best is Dischord at 6.6 dB. Worst is Pizzicato at 99.6 dB. The
worst four are all plucked or brassy, which points at the attack and the
high-resonance grain rather than the swarm.

The original hits exactly 1.000 on 18 of the 45 factory patches. It clips as a
matter of course.

What's still a guess: the motion law. Speed at zero freezes the cloud exactly,
Pitch Var sets the vertical extent, Reflection behaves like particles bouncing
off walls. But the pairwise attract/repel/proximity forces were never
recovered, and Std. Dev. makes the cloud tighter as it goes up, which is the
opposite of what the name suggests. The engine has a boids-style stand-in and
says so in a comment.

[FILL IN: your own take on how it sounds next to the original. You said it's
not the same but better in places. Which places?]

## In the browser

The engine compiled under Emscripten on the first try. The interface didn't
come along, since JUCE doesn't target the browser, so the web UI is a second
implementation in HTML and canvas. Two things cost time: an AudioWorklet can't
import or fetch, so the Emscripten glue and the processor get concatenated into
one blob; and Chrome refuses synchronous WebAssembly compilation above 4 KB
while a worklet constructor can't await, so the page compiles the module and
passes it in through processorOptions. Getting that wrong fails with a
processorerror and no message.

https://audiodestrukt.github.io/smarmsynth/
