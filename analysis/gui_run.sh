#!/usr/bin/env bash
# Run the original with a scripted sequence of editor interactions, then
# screenshot it. Coordinates are in the editor's own 680x536 pixel space.
#   analysis/gui_run.sh <outdir> "<gui-script>" [extra host args...]
set -u
cd "$(dirname "$0")/.."
OUT="$1"; SCRIPT="$2"; shift 2
mkdir -p "$OUT"
WINEDEBUG=-all wine ./vsthost32.exe SwarmSynth.dll --no-midi \
    --dump-prefix "$OUT/s" --gui-script "$SCRIPT" "$@" >"$OUT/host.log" 2>&1 &
HP=$!
for i in $(seq 60); do WID=$(xdotool search --name 'SwarmSynth - vsthost32' 2>/dev/null|head -1); [ -n "${WID:-}" ] && break; sleep .25; done
[ -z "${WID:-}" ] && { echo "no window"; kill $HP 2>/dev/null; exit 1; }
# one tick is 25 ms and a step runs every 12 ticks, so allow 0.4 s per step
STEPS=$(printf '%s' "$SCRIPT" | tr -cd ';' | wc -c)
sleep $(python3 -c "print(5 + ($STEPS+1)*0.55)")
import -window "$WID" "$OUT/final.png" 2>/dev/null
kill $HP 2>/dev/null; wait $HP 2>/dev/null
grep -c 'gui step' "$OUT/host.log" | xargs -I{} echo "ran {} steps; chunks: $(ls "$OUT"/*.chunk 2>/dev/null | wc -l)"
