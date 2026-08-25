#!/usr/bin/env bash
# Launch the plugin GUI with a chosen patch, hold a note, and screenshot the
# swarm visualiser repeatedly so the particle motion can be watched frame by frame.
set -u
cd "$(dirname "$0")/.."
OUT="${OUT:-analysis/data/frames}"; rm -rf "$OUT"; mkdir -p "$OUT"
FRAMES="${FRAMES:-24}"; IVAL="${IVAL:-0.15}"; HOLD="${HOLD:-12}"

python3 - "$OUT/hold.mid" "$HOLD" <<'PY'
import sys,struct
ticks=int(float(sys.argv[2])*2*96)          # 96 ticks/quarter at 120bpm
def vlq(n):
    b=[n&0x7f]; n>>=7
    while n: b.append((n&0x7f)|0x80); n>>=7
    return bytes(reversed(b))
trk=bytes([0,0x90,0x39,0x64])+vlq(ticks)+bytes([0x80,0x39,0])+bytes([0,0xFF,0x2F,0])
open(sys.argv[1],'wb').write(b'MThd'+struct.pack('>IHHH',6,0,1,96)+b'MTrk'+struct.pack('>I',len(trk))+trk)
PY

WINEDEBUG=-all wine ./vsthost32.exe SwarmSynth.dll "$@" >"$OUT/host.log" 2>&1 &
HOSTPID=$!
for i in $(seq 40); do WID=$(xdotool search --name 'SwarmSynth - vsthost32' 2>/dev/null | head -1); [ -n "${WID:-}" ] && break; sleep 0.25; done
[ -z "${WID:-}" ] && { echo "no window"; kill $HOSTPID 2>/dev/null; exit 1; }
sleep 1.5

aplaymidi -p 14:0 "$OUT/hold.mid" &
MPID=$!
for i in $(seq -w 1 "$FRAMES"); do
  import -window "$WID" "$OUT/f$i.png" 2>/dev/null
  sleep "$IVAL"
done
kill $MPID 2>/dev/null; wait $MPID 2>/dev/null
kill $HOSTPID 2>/dev/null; sleep 0.5; pkill -f vsthost32.exe 2>/dev/null
echo "captured $(ls "$OUT"/f*.png 2>/dev/null | wc -l) frames"
