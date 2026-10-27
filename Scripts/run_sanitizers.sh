#!/usr/bin/env bash
# Runs the automated tests under AddressSanitizer+UBSan and ThreadSanitizer.
set -euo pipefail
cd "$(dirname "$0")/.."
for preset in asan tsan; do
  cmake --preset "$preset"
  cmake --build --preset "$preset"
  ctest --preset "$preset"
done
