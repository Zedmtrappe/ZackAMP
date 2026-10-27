#!/usr/bin/env bash
# Renders the synthetic DI test set and every factory preset to WAV for listening checks.
#   Scripts/render_listening_set.sh [build-dir] [out-dir] [your-di.wav]
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${1:-build/release}"
OUT="${2:-listening}"
RENDER=$(find "$BUILD" -type f -name ampforge_render -perm -u+x | head -1)
[ -n "$RENDER" ] || { echo "ampforge_render not found in $BUILD — build first"; exit 1; }
mkdir -p "$OUT/di" "$OUT/presets"
"$RENDER" --generate "$OUT/di"
if [ -n "${3:-}" ]; then
  "$RENDER" --all-presets "$OUT/presets" --in "$3"
else
  "$RENDER" --all-presets "$OUT/presets"
fi
echo "Rendered to $OUT/"
