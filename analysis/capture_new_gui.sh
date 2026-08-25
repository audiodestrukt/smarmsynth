#!/usr/bin/env bash
# Launch the recreated standalone, hold a chord on its on-screen keyboard, and
# screenshot it -- so the 3D swarm view is captured with particles alive.
set -u
cd "$(dirname "$0")/.."
OUT="${OUT:-analysis/data/new_gui.png}"
BIN=juce/build/SwarmSynthJUCE_artefacts/Release/Standalone/SwarmSynth
"$BIN" >/dev/null 2>&1 &
PID=$!
for i in $(seq 60); do WID=$(xdotool search --name '^SwarmSynth$' 2>/dev/null | tail -1); [ -n "${WID:-}" ] && break; sleep 0.25; done
[ -z "${WID:-}" ] && { echo "no window"; kill $PID 2>/dev/null; exit 1; }
sleep 2
eval $(xdotool getwindowgeometry --shell $WID)
S=$(python3 -c "print($WIDTH/680.0)")
CX=$(python3 -c "print(int($X + 130*$S))")
CY=$(python3 -c "print(int($Y + 505*$S))")
echo "window ${WIDTH}x${HEIGHT}  scale $(printf %.2f $S)  keyboard click at $CX,$CY"
xdotool mousemove --sync "$CX" "$CY" mousedown 1
sleep 2.5
import -window "$WID" "$OUT" 2>/dev/null
xdotool mouseup 1
sleep 0.3
kill $PID 2>/dev/null; wait $PID 2>/dev/null
echo "wrote $OUT"
