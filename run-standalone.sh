#!/usr/bin/env bash
# Launch the recreated synth's standalone build with a sane audio device
# already chosen.
#
# JUCE's ALSA backend enumerates every PCM the system exposes -- 60-odd entries
# on a PipeWire box, nearly all of them raw hardware or format converters. The
# one you want is "pulse" (or "default"), which routes through PipeWire to
# whatever your normal output is. This seeds that choice so the Options ->
# Audio/MIDI dialog is not the first thing you have to fight.
#
# NOTE: JUCE matches on the ALSA *description*, not the PCM id -- writing
# "pulse" here matches nothing and it silently falls back to a default that
# negotiates 8000 Hz. The name below is what `aplay -L` prints as the
# description line under the id.
#
# Override with:  DEVICE="PipeWire Sound Server" ./run-standalone.sh
set -euo pipefail
cd "$(dirname "$0")"

BIN=juce/build/SwarmSynthJUCE_artefacts/Release/Standalone/SwarmSynth
[ -x "$BIN" ] || { echo "not built yet: cmake -S juce -B juce/build && cmake --build juce/build -j"; exit 1; }

SETTINGS="$HOME/.config/SwarmSynth.settings"
DEVICE="${DEVICE:-PulseAudio Sound Server}"
RATE="${RATE:-48000}"
BUFFER="${BUFFER:-512}"

if ! grep -q 'audioSetup' "$SETTINGS" 2>/dev/null; then
  mkdir -p "$(dirname "$SETTINGS")"
  # JUCE stores this as an escaped XML string in a val attribute, not as a
  # child element -- a child element is silently ignored.
  python3 - "$SETTINGS" "$DEVICE" "$RATE" "$BUFFER" <<'PYEOF'
import sys, html
path, device, rate, buffer = sys.argv[1:5]
inner = ('<DEVICESETUP deviceType="ALSA" audioOutputDeviceName="%s" '
         'audioInputDeviceName="" audioDeviceRate="%s" '
         'audioDeviceBufferSize="%s"/>' % (device, rate, buffer))
open(path, "w").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n'
    '  <VALUE name="audioSetup" val="%s"/>\n'
    '  <VALUE name="shouldMuteInput" val="1"/>\n'
    '</PROPERTIES>\n' % html.escape(inner, quote=True))
PYEOF
  echo "seeded audio device '$DEVICE' at ${RATE} Hz, ${BUFFER} frames"
else
  echo "keeping the audio device already saved in $SETTINGS"
  echo "  (delete that file, or run with DEVICE=... to reset)"
fi

exec "$BIN" "$@"
