# SwarmSynth in the browser

The same C++ synthesis engine the plugin uses, compiled to WebAssembly.

```sh
web/build.sh                 # needs Emscripten (emcc) on PATH
python3 -m http.server 8731  # from the repository root
# then open http://localhost:8731/web/
```

## What is shared and what is not

**The engine is shared, literally.** `src/wasm_entry.cpp` includes the same
headers the plugin and the offline renderer compile — `SwarmEngine.h`,
`SwarmParams.h`, `SwarmEnvModel.h`, `SwarmAnarchy.h`, `SwarmPreset.h`. There is
no separate web port of the DSP to keep in sync, and no reimplementation. It
compiled under Emscripten unchanged, first try, because none of those headers
reference a framework.

**The interface is not shared.** JUCE does not target the browser, so the
editor is rewritten here in HTML, canvas and JS. It follows the same layout and
the same measured laws — particle hue is `0.79 * Res`, HSL lightness is
`0.21 + 0.60 * Noise` — but it is a second implementation, and the two can
drift.

JUCE 8 supports WebView-based plugin UIs (`juce_WebControlRelays`), so this
interface could eventually become the *single* one, hosted inside the native
plugin as well. That would remove the duplication rather than living with it.

## Two things worth knowing if you build on this

An `AudioWorkletProcessor` can neither `import` nor `fetch`, so the page
concatenates the Emscripten glue with `src/processor.js` and hands the pair to
`addModule` as a blob.

And Chrome refuses **synchronous** WebAssembly compilation above 4 KB, while a
worklet constructor cannot `await`. So the page compiles the module itself and
passes the finished `WebAssembly.Module` in through `processorOptions`;
instantiating an already-compiled module synchronously is allowed at any size.
Getting this wrong fails as a `processorerror` with no message, which is not a
fun afternoon.

The 45 factory patches are read straight out of `../presets/original`, so there
is only one copy of them in the repository. `dist/presets.json` is just the
list, since a static server offers no directory index.
