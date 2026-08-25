#!/usr/bin/env bash
# Set one parameter at a time, dump the state chunk, and diff against baseline.
# Recovers the on-disk offset of every parameter, including any hidden state.
set -u
cd "$(dirname "$0")/.."
OUT=analysis/data/chunks
export WINEDEBUG=-all
wine ./vsthost32.exe SwarmSynth.dll --dump-chunk $OUT/base.chunk >/dev/null 2>&1
for i in $(seq 0 100); do
  for v in 0.33 0.77; do
    wine ./vsthost32.exe SwarmSynth.dll --param $i=$v \
         --dump-chunk "$OUT/p${i}_${v}.chunk" >/dev/null 2>&1
  done
done
echo DONE
