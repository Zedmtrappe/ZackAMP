#!/usr/bin/env bash
# Packages a built Z.AMP.app for download: ad-hoc signs the whole bundle (no Apple
# Developer ID; see README "Installing a downloaded build") and zips it together with the
# installer script.  Usage: Scripts/package_macos.sh [build-dir]   ->  dist/Z.AMP-macOS.zip
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${1:-build/macos}"
APP="$BUILD/ZackAMP_artefacts/Release/Z.AMP.app"
[ -d "$APP" ] || { echo "Not found: $APP (build first)"; exit 1; }

# Seal the complete bundle with an ad-hoc signature (Apple Silicon requires signed code;
# the linker only signs the executable, not the bundle's resources).
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

STAGE="dist/Z.AMP"
rm -rf dist && mkdir -p "$STAGE"
ditto "$APP" "$STAGE/Z.AMP.app"
cp "Scripts/Install Z.AMP.command" "$STAGE/"
chmod +x "$STAGE/Install Z.AMP.command"
cat > "$STAGE/READ ME FIRST.txt" <<'TXT'
Z.AMP - installing or updating

Easiest (works every time):
  1. Open the Terminal app (Applications > Utilities > Terminal).
  2. Type:  bash   (with a space after it)
  3. Drag "Install Z.AMP.command" from this folder into the Terminal window, press Return.

It quits Z.AMP if it is running, replaces /Applications/Z.AMP.app, clears the macOS
"downloaded from the internet" block and starts the new version. Your presets, IRs, MIDI
mappings and audio settings are kept (they live in ~/Library/Application Support/ZackAMP).

macOS may ask again for microphone access after an update: click Allow
(Z.AMP needs it to hear your audio interface).
TXT
(cd dist && ditto -c -k --sequesterRsrc --keepParent Z.AMP Z.AMP-macOS.zip)
echo "Packaged: dist/Z.AMP-macOS.zip"
