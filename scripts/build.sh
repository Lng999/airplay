#!/usr/bin/env bash
#
# Builds the pinned UxPlay submodule into ./build/uxplay.exe.
#
# MUST run inside the MSYS2 UCRT64 environment, e.g. from Windows:
#   C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/pc/Desktop/airplay && ./scripts/build.sh"
# with MSYSTEM=UCRT64 exported first (or simply use C:\msys64\ucrt64.exe -c "...").
# MSYSTEM must be set BEFORE the login shell starts so /ucrt64/bin is on PATH
# (docs/research/gstreamer-msys2-windows.md section 2).
#
# Optional environment:
#   USE_DNS_SD=1   build against Apple Bonjour dns_sd.h instead of the bundled
#                  lib/mdnsd responder (fallback for upstream issue #546).
#                  Requires the Bonjour SDK for Windows. UxPlay/CMakeLists.txt:51
#
# Usage: ./scripts/build.sh [clean]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/third_party/UxPlay"
BUILD_DIR="${REPO_ROOT}/build"
EXE="${BUILD_DIR}/uxplay.exe"

step() { printf '\n==> %s\n' "$*"; }
die()  { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# --- clean ------------------------------------------------------------------
if [ "${1:-}" = "clean" ]; then
    step "Removing ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
    echo "clean done"
    exit 0
elif [ -n "${1:-}" ]; then
    die "unknown argument '${1}' (only 'clean' is supported)"
fi

# --- apply local patches to the submodule (idempotent) -----------------------
# patches/*.patch are kept out of the submodule; see patches/README.md.
# Already-applied patches are detected via `git apply --reverse --check`.
apply_patches() {
    local p name
    for p in "${REPO_ROOT}"/patches/*.patch; do
        [ -e "$p" ] || continue
        name="$(basename "$p")"
        if git -C "${SRC_DIR}" apply --reverse --check "$p" >/dev/null 2>&1; then
            echo "patch ${name}: already applied"
        elif git -C "${SRC_DIR}" apply --check "$p" >/dev/null 2>&1; then
            git -C "${SRC_DIR}" apply "$p" && echo "patch ${name}: applied"
        else
            die "patch ${name} does not apply cleanly to third_party/UxPlay (submodule pin changed?)"
        fi
    done
}

# --- environment sanity -----------------------------------------------------
if [ "${MSYSTEM:-}" != "UCRT64" ]; then
    # MINGW64 was deprecated on 2026-03-15 (research doc 1.3); UCRT64 is the decision.
    printf 'WARNING: MSYSTEM is "%s", expected "UCRT64". Build may pick the wrong toolchain.\n' \
        "${MSYSTEM:-unset}" >&2
fi

for tool in cmake ninja gcc pkg-config; do
    command -v "${tool}" >/dev/null 2>&1 || \
        die "'${tool}' not found on PATH. Run scripts/setup-msys2.ps1 first."
done

# The submodule is checked out by the caller: `git submodule update --init`.
[ -f "${SRC_DIR}/CMakeLists.txt" ] || die \
"UxPlay sources missing: ${SRC_DIR}/CMakeLists.txt not found.
       The submodule is not checked out. From the repo root run:
           git submodule update --init"

step "Applying local patches (patches/*.patch)"
apply_patches

# --- configure --------------------------------------------------------------
CMAKE_ARGS=(
    -S "${SRC_DIR}"
    -B "${BUILD_DIR}"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    # NO_MARCH_NATIVE spelling verified at UxPlay lib/CMakeLists.txt:6; without it the
    # build injects -O3 -march=native, which pins the binary to this exact CPU.
    -DNO_MARCH_NATIVE=ON
)

if [ "${USE_DNS_SD:-0}" = "1" ]; then
    # UxPlay/CMakeLists.txt:51 - selects lib/dns_sd (Apple Bonjour) over lib/mdnsd.
    step "USE_DNS_SD=1: building against Apple Bonjour dns_sd.h (needs the Bonjour SDK)"
    CMAKE_ARGS+=( -DUSE_DNS_SD=1 )
else
    # Default on Windows: the bundled minimal mDNSResponder, no flag needed
    # (UxPlay/CMakeLists.txt:63-68, SPEC.md 2b).
    step "Using the bundled lib/mdnsd responder (default)"
fi

step "Configuring"
cmake "${CMAKE_ARGS[@]}"

# --- build ------------------------------------------------------------------
step "Building"
# -j without a value: CMake lets the native build tool choose its own default.
cmake --build "${BUILD_DIR}" -j

[ -f "${EXE}" ] || die "build finished but ${EXE} was not produced"

# --- report -----------------------------------------------------------------
step "Result"
printf 'executable : %s\n' "${EXE}"
printf 'size       : %s bytes\n' "$(stat -c %s "${EXE}" 2>/dev/null || echo '?')"

# ntldd reads PE imports properly; ldd is the MSYS2 fallback
# (docs/research/gstreamer-msys2-windows.md section 4.3).
if command -v ntldd >/dev/null 2>&1; then
    dll_all=$(ntldd -R "${EXE}" 2>/dev/null | grep -c '=>' || true)
    dll_ucrt=$(ntldd -R "${EXE}" 2>/dev/null | grep -ci 'ucrt64' || true)
    printf 'DLL deps   : %s total (transitive), %s from ucrt64  [ntldd -R]\n' \
        "${dll_all:-0}" "${dll_ucrt:-0}"
elif command -v ldd >/dev/null 2>&1; then
    dll_all=$(ldd "${EXE}" 2>/dev/null | grep -c '=>' || true)
    printf 'DLL deps   : %s direct  [ldd]\n' "${dll_all:-0}"
else
    printf 'DLL deps   : ntldd/ldd unavailable, skipped\n'
fi

printf '\nRun it with: pwsh -File scripts/run-uxplay.ps1\n'
