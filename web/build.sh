#!/usr/bin/env bash
# Build the synthesis engine to WebAssembly.
#
# The engine is header-only C++ with no framework in it, so this compiles the
# same sources the plugin and the offline renderer use -- there is no separate
# web port to keep in sync.
#
# Needs Emscripten on PATH (emcc). Output lands in web/dist.
set -euo pipefail
cd "$(dirname "$0")"

command -v em++ >/dev/null || { echo "em++ not found -- install Emscripten"; exit 1; }

mkdir -p dist
em++ src/wasm_entry.cpp -o dist/swarm.js \
  -std=c++17 -O3 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME=createSwarm \
  -s ENVIRONMENT=web,worker \
  -s WASM_ASYNC_COMPILATION=0 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16MB \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","HEAPF32","HEAPU8"]' \
  -s EXPORTED_FUNCTIONS='["_swarm_init","_swarm_num_params","_swarm_get_param","_swarm_set_param","_swarm_param_name","_swarm_param_id","_swarm_param_unit","_swarm_param_display","_swarm_note_on","_swarm_note_off","_swarm_process","_swarm_load_patch","_swarm_anarchy","_swarm_particles","_swarm_particle_buffer","_malloc","_free"]'

echo "built dist/swarm.js ($(du -h dist/swarm.js | cut -f1)) + dist/swarm.wasm ($(du -h dist/swarm.wasm | cut -f1))"

# Notes on the flags:
#
#   MODULARIZE with no EXPORT_ES6 -- a worklet cannot import modules, so the page
#     concatenates this classic-script glue with the processor source and hands
#     the pair to addModule as a blob.
#
#   the .wasm stays a SEPARATE file -- Chrome refuses synchronous WebAssembly
#     compilation above 4 KB, and a worklet constructor cannot await. So the page
#     compiles the module itself and passes the finished WebAssembly.Module in
#     through processorOptions; instantiating an already-compiled module
#     synchronously is allowed at any size.

# The factory patches are NOT copied here -- the page reads them straight out of
# ../presets/original, so there is only one copy in the repository. All this
# writes is the list, since a static server gives no directory index.
if [ -d ../presets/original ]; then
  mkdir -p dist
  ls ../presets/original/*.swarmpatch | while read -r f; do basename "$f"; done \
    | python3 -c "import sys,json;print(json.dumps(sorted(l.strip() for l in sys.stdin)))" \
    > dist/presets.json
  echo "indexed $(python3 -c "import json;print(len(json.load(open('dist/presets.json'))))") patches"
fi
