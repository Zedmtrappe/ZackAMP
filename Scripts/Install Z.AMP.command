#!/bin/bash
# Installs or updates Z.AMP from the folder this script is in.
set -e
cd "$(dirname "$0")"
if [ ! -d "Z.AMP.app" ]; then
  echo "Z.AMP.app was not found next to this script. Unzip the download first."
  exit 1
fi
# Quit a running copy (also the earlier "ZackAMP" build).
for name in "Z.AMP" "ZackAMP"; do
  if pgrep -xq "$name"; then
    echo "Quitting the running $name..."
    osascript -e "quit app \"$name\"" || true
    for _ in 1 2 3 4 5 6 7 8 9 10; do pgrep -xq "$name" || break; sleep 1; done
  fi
done
# The app used to be called ZackAMP: remove the old copy (your presets and settings are kept).
rm -rf "/Applications/ZackAMP.app"
echo "Installing to /Applications/Z.AMP.app ..."
rm -rf "/Applications/Z.AMP.app"
ditto "Z.AMP.app" "/Applications/Z.AMP.app"
xattr -dr com.apple.quarantine "/Applications/Z.AMP.app" 2>/dev/null || true
echo "Done. Starting Z.AMP."
open "/Applications/Z.AMP.app"
