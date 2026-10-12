#!/usr/bin/env bash
# One-command CMake build + unit test for the cstdmf CMake sample
# (programming/bigworld/cmake/samples/cstdmf).
#
# This is the local entry point for the Linux CMake track (see
# docs/vcpkg-migration.md §3.1); the make chain remains the primary build.
#
# Usage:
#   scripts/build-cstdmf-cmake.sh [build-dir]
#
# Environment:
#   VCPKG_ROOT  vcpkg checkout (default: ~/vcpkg)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BW_ROOT="$REPO_ROOT/programming/bigworld"
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
BUILD_DIR="${1:-$BW_ROOT/build/cmake-cstdmf}"

[ -d "$VCPKG_ROOT" ] || { echo "ERROR: VCPKG_ROOT not found: $VCPKG_ROOT" >&2; exit 1; }
[ -x "$VCPKG_ROOT/vcpkg" ] || "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

# gcc-14 for C++23, same compiler choice as the make chain / CI
CC_BIN="$(command -v gcc || true)"
if [ -n "$CC_BIN" ] && ! gcc -dumpfullversion -dumpversion | grep -q '^1[4-9]\.'; then
	GCC14="$(command -v gcc-14 || true)"
	[ -n "$GCC14" ] || { echo "ERROR: gcc >= 14 required (C++23)" >&2; exit 1; }
	export CC="$GCC14" CXX="${GCC14%gcc}g++"
fi

echo "== configure: $BUILD_DIR =="
# the toolchain locates VCPKG_ROOT itself via .vcpkg-root; VCPKG_MANIFEST_DIR,
# overlay triplets and the x64-linux-bw triplet are defaulted inside the
# sample's CMakeLists.txt
cmake -S "$BW_ROOT/cmake/samples/cstdmf" -B "$BUILD_DIR" \
	-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
	-DCMAKE_BUILD_TYPE=Release

echo "== build =="
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "== test =="
ctest --test-dir "$BUILD_DIR" --output-on-failure
