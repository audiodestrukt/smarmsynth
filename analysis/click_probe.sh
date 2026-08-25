#!/usr/bin/env bash
# Open the editor, click a spot in it, and screenshot before/after.
set -u
cd "$(dirname "$0")/.."
OUT=analysis/data/click; rm -rf $OUT; mkdir -p $OUT
CX="${CX:-150}"; CY="${CY:-260}"
WINEDEBUG=-all wine ./vsthost32.exe SwarmSynth.dll --no-midi "$@" >$OUT/host.log 2>&1 &
HP=$!
for i in $(seq 40); do WID=$(xdotool search --name 'SwarmSynth - vsthost32' 2>/dev/null|head -1); [ -n "${WID:-}" ] && break; sleep .25; done
[ -z "${WID:-}" ] && { echo "no window"; kill $HP; exit 1; }
sleep 2
eval $(xdotool getwindowgeometry --shell $WID)
echo "window ${WIDTH}x${HEIGHT} at $X,$Y"
import -window $WID $OUT/before.png 2>/dev/null
xdotool mousemove --sync $((X+CX)) $((Y+CY)) click 1
sleep 1
import -window $WID $OUT/after.png 2>/dev/null
# small drag on the same spot, which is what actually "moves" a knob
xdotool mousemove --sync $((X+CX)) $((Y+CY)) mousedown 1
for d in 2 4 6 8 10; do xdotool mousemove --sync $((X+CX)) $((Y+CY-d)); sleep .05; done
xdotool mouseup 1
sleep 1
import -window $WID $OUT/dragged.png 2>/dev/null
kill $HP 2>/dev/null; sleep .5; pkill -f vsthost32.exe 2>/dev/null
echo captured
