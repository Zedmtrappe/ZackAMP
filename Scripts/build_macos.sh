#!/usr/bin/env bash
# Builds AMP FORGE on macOS (universal arm64 + x86_64 by default) and runs the tests.
#   Scripts/build_macos.sh            Release build + tests
#   Scripts/build_macos.sh --no-tests skip tests
#   ARCHS="arm64" Scripts/build_macos.sh   Apple Silicon only (faster build)
# Requirements: Xcode command line tools (xcode-select --install), CMake >= 3.22.
# Ninja is used if installed (brew install ninja), otherwise Unix Makefiles.
set -euo pipefail
cd "$(dirname "$0")/.."

command -v cmake >/dev/null || { echo "CMake not found. Install with: brew install cmake  (or from cmake.org)"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode command line tools missing. Run: xcode-select --install"; exit 1; }

Scripts/bootstrap.sh
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"
ARCHS="${ARCHS:-arm64;x86_64}"
JOBS=$(sysctl -n hw.ncpu)

cmake -S . -B build/macos -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCHS"
cmake --build build/macos --config Release -j "$JOBS"

if [ "${1:-}" != "--no-tests" ]; then
  (cd build/macos && ctest --output-on-failure)
fi

APP="build/macos/AmpForge_artefacts/Release/AMP FORGE.app"
echo
echo "Built: $APP"
echo "Run:   open \"$APP\""
echo "Note:  the build is ad-hoc signed only (not notarised). See README 'Gatekeeper' if macOS blocks it."
