#!/usr/bin/env bash
# Open one of the editor's menus and capture the whole screen, since Win32
# menus are separate top-level windows that a per-window grab would miss.
set -u
cd "$(dirname "$0")/.."
OUT="$1"; SCRIPT="$2"
mkdir -p "$OUT"
WINEDEBUG=-all wine ./vsthost32.exe SwarmSynth.dll --no-midi \
    --dump-prefix "$OUT/s" --gui-script "$SCRIPT" >"$OUT/host.log" 2>&1 &
HP=$!
for i in $(seq 60); do WID=$(xdotool search --name 'SwarmSynth - vsthost32' 2>/dev/null|head -1); [ -n "${WID:-}" ] && break; sleep .25; done
[ -z "${WID:-}" ] && { echo "no window"; kill $HP 2>/dev/null; exit 1; }
STEPS=$(printf '%s' "$SCRIPT" | tr -cd ';' | wc -c)
sleep $(python3 -c "print(4 + ($STEPS+1)*0.75)")
import -window root "$OUT/screen.png" 2>/dev/null || grim "$OUT/screen.png"
import -window "$WID" "$OUT/window.png" 2>/dev/null
kill $HP 2>/dev/null; wait $HP 2>/dev/null
echo "captured $OUT"
