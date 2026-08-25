#!/usr/bin/env bash
# Run the VST2 host under Wine. Builds first if the exe is missing or stale.
set -euo pipefail
cd "$(dirname "$0")"

PLUGIN="${PLUGIN:-SwarmSynth.dll}"

make -q vsthost32.exe 2>/dev/null || make

export WINEDEBUG="${WINEDEBUG:--all}"
exec wine ./vsthost32.exe "$PLUGIN" "$@"
