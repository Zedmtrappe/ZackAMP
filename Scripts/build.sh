#!/usr/bin/env bash
# Generic build + test (macOS or Linux). On Linux the standalone app also builds
# (JUCE ALSA/X11 backend) which is how the UI is exercised in CI.
set -euo pipefail
cd "$(dirname "$0")/.."
Scripts/bootstrap.sh
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"
cmake -S . -B build/release -G "$GEN" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build/release --config Release -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
(cd build/release && ctest --output-on-failure)
