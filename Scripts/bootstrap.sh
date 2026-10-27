#!/usr/bin/env bash
# Fetches the pinned JUCE release into third_party/JUCE (not committed to git).
set -euo pipefail
cd "$(dirname "$0")/.."
JUCE_TAG="8.0.9"
JUCE_COMMIT="f72bad64d29715216226685810c5196bd0d79d77"
if [ -d third_party/JUCE/.git ]; then
  echo "JUCE already present at third_party/JUCE ($(git -C third_party/JUCE rev-parse --short HEAD))"
else
  git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git third_party/JUCE
fi
actual=$(git -C third_party/JUCE rev-parse HEAD)
if [ "$actual" != "$JUCE_COMMIT" ]; then
  echo "WARNING: third_party/JUCE is at $actual, expected $JUCE_COMMIT (JUCE $JUCE_TAG)" >&2
fi
echo "OK: JUCE $JUCE_TAG"
