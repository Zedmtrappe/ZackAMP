#!/bin/bash
# Installs or updates ZackAMP from the folder this script is in.
set -e
cd "$(dirname "$0")"
if [ ! -d "ZackAMP.app" ]; then
  echo "ZackAMP.app was not found next to this script. Unzip the download first."
  exit 1
fi
if pgrep -xq ZackAMP; then
  echo "Quitting the running ZackAMP..."
  osascript -e 'quit app "ZackAMP"' || true
  for _ in 1 2 3 4 5 6 7 8 9 10; do pgrep -xq ZackAMP || break; sleep 1; done
fi
echo "Installing to /Applications/ZackAMP.app ..."
rm -rf "/Applications/ZackAMP.app"
ditto "ZackAMP.app" "/Applications/ZackAMP.app"
xattr -dr com.apple.quarantine "/Applications/ZackAMP.app" 2>/dev/null || true
echo "Done. Starting ZackAMP."
open "/Applications/ZackAMP.app"
