#!/usr/bin/env bash
# Extract the original's factory presets from YOUR OWN copy of SwarmSynth.dll.
#
# The 45 factory patches are embedded in the plugin's resources, not shipped as
# files. This walks the Presets menu, loads each entry, and dumps the resulting
# state chunk.
#
# Navigation is by keyboard (menuitem), not by clicking the rect Win32 reports
# for an item: under Wine those rects are offset by several rows, and a menu
# this long scrolls, which puts the last few entries out of reach entirely.
# Arrow keys handle both and map 1:1 to the item index. Each chunk still carries
# its own patch name, which is what the file is named after.
#
# The extracted patches are the original author's work. They are deliberately
# NOT committed to this repository; run this against your own DLL.
set -u
cd "$(dirname "$0")/.."
OUT="${OUT:-presets/original}"
FROM="${FROM:-0}"; TO="${TO:-44}"
mkdir -p "$OUT"

for i in $(seq "$FROM" "$TO"); do
  TMP=$(mktemp -d)
  analysis/gui_run.sh "$TMP" \
    "press:35,17; wait; release:35,17; wait; menuitem:$i; wait; wait; dump" >/dev/null 2>&1
  CH=$(ls "$TMP"/s*.chunk 2>/dev/null | head -1)
  if [ -n "$CH" ]; then
    NAME=$(python3 -c "
import sys
d=open(sys.argv[1],'rb').read()[:24].split(b'\0')[0].decode('latin1').strip()
print(''.join(c if c.isalnum() or c in ' -' else '_' for c in d).strip().replace(' ','_'))
" "$CH")
    if [ -n "$NAME" ] && [ "$NAME" != "new_patch_1" ]; then
      cp "$CH" "$OUT/$NAME.swarmpatch"
      printf "  [%2d] %s\n" "$i" "$NAME"
    else
      printf "  [%2d] (default patch, skipped)\n" "$i"
    fi
  else
    printf "  [%2d] (no chunk)\n" "$i"
  fi
  rm -rf "$TMP"
done
echo "extracted $(ls "$OUT"/*.swarmpatch 2>/dev/null | wc -l) distinct presets into $OUT"
