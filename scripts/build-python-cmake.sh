#!/usr/bin/env bash
# One-command CMake build of the vendored CPython 3.13 for the CMake track
# (programming/bigworld/cmake/samples/python).
#
# Same pattern as scripts/build-cstdmf-cmake.sh / build-resmgr-cmake.sh /
# build-network-cmake.sh: the CMake track is bottom-up, and libpython3.13.a is
# the base everything Python-dependent links against (the entitydef sample
# includes this sample as a subdirectory). The make chain
# (build/make/third_party_python.mak) remains the primary build.
#
# Usage:
#   scripts/build-python-cmake.sh [build-dir]
#
# Environment:
#   VCPKG_ROOT  vcpkg checkout (default: ~/vcpkg)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BW_ROOT="$REPO_ROOT/programming/bigworld"
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
BUILD_DIR="${1:-$BW_ROOT/build/cmake-python}"

[ -d "$VCPKG_ROOT" ] || { echo "ERROR: VCPKG_ROOT not found: $VCPKG_ROOT" >&2; exit 1; }
[ -x "$VCPKG_ROOT/vcpkg" ] || "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

echo "== configure: $BUILD_DIR =="
# the toolchain locates VCPKG_ROOT itself via .vcpkg-root; VCPKG_MANIFEST_DIR,
# overlay triplets and the x64-linux-bw triplet are defaulted inside the
# sample's CMakeLists.txt
cmake -S "$BW_ROOT/cmake/samples/python" -B "$BUILD_DIR" \
	-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
	-DCMAKE_BUILD_TYPE=Release

echo "== build =="
cmake --build "$BUILD_DIR" -j"$(nproc)"

LIBPY="$BUILD_DIR/bwpython313/libpython3.13.a"
echo "== verify =="
[ -f "$LIBPY" ] || { echo "ERROR: $LIBPY not produced" >&2; exit 1; }
echo "libpython3.13.a: $(du -h "$LIBPY" | cut -f1)"
ar t "$LIBPY" | grep -qx 'bwhooks.o' \
	|| { echo "ERROR: bwhooks.o not in libpython3.13.a" >&2; exit 1; }
echo "bwhooks.o: present in the archive"
