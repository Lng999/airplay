#!/usr/bin/env bash
# Build the Win32 GUI (app/) inside an MSYS2 UCRT64 shell. Usage: bash scripts/build-app.sh [clean]
set -euo pipefail
cd "$(dirname "$0")/.."
[ "${MSYSTEM:-}" = "UCRT64" ] || echo "warning: MSYSTEM=${MSYSTEM:-unset}, expected UCRT64" >&2
[ "${1:-}" = "clean" ] && rm -rf build-app
cmake -S app -B build-app -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-app -j
echo "==> build-app/airplay-gui.exe"; ls -la build-app/airplay-gui.exe
