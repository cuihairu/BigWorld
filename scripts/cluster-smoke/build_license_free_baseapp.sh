#!/usr/bin/env bash
# Build a baseapp without the bigworld.key license check, for internal smokes.
#
# server/bigworld.key is not shipped in this repo and cannot be fabricated
# (repo rule). The license heartbeat lives in exactly one translation unit:
# baseapp/ping_manager.cpp is the only baseapp file that references
# USE_OPENSSL, and it gates the whole PingManager on that define. So a
# license-free baseapp is just ping_manager.o compiled without USE_OPENSSL.
#
# Two make invocations are needed because baseapp itself still links OpenSSL
# (openssl.c does TLS), so a whole-binary useOpenSSL=0 fails at link time:
#   1. make useOpenSSL=0  recompiles ping_manager.o (the license check is
#      compiled out) and then fails to link; the .o is produced first.
#   2. make (normal flags) relinks baseapp with the OpenSSL libraries.
#
# This deliberately leaves ping_manager.o compiled without the license check,
# so the shared build tree now produces a license-free baseapp until that
# object is recompiled with the default flags. Restore a licensed build with:
#   rm bigworld/build/<os>/obj/.../server/baseapp/ping_manager.o
#   make -C bigworld/server/baseapp
#
# Usage: build_license_free_baseapp.sh [--relink-only]
set -eu

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT/programming"

PROG="${BASH_SOURCE[0]##*/}"
OBJREL="bigworld/build/el7/obj/programming/bigworld/server/baseapp/ping_manager.o"
BIN="$REPO_ROOT/game/bin/server/el7/server/baseapp"

export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export user_shouldInstallPython="${user_shouldInstallPython:-1}"
JOBS="${JOBS:-16}"

# A license-free build cannot be verified by the absence of a compiler error:
# the object only proves it if the string the check prints is gone.
assert_no_license_check() {
	if strings "$OBJREL" 2>/dev/null | grep -q "bigworld.key"; then
		echo "[$PROG] FAIL: ping_manager.o still references bigworld.key" >&2
		exit 1
	fi
}

if [ "${1:-}" = "--relink-only" ]; then
	echo "[$PROG] relinking baseapp against the existing license-free object"
	exec make -s -j"$JOBS" -rR -C bigworld/server/baseapp
fi

# make decides on timestamps, not on the -D flags, so an existing
# ping_manager.o would be reused and the license check would survive.
rm -f "$OBJREL"

echo "[$PROG] recompiling ping_manager.o without USE_OPENSSL"
# The link is expected to fail (baseapp needs the OpenSSL libraries); only the
# object is wanted from this invocation.
if ! make -s -j"$JOBS" -rR -C bigworld/server/baseapp useOpenSSL=0; then
	if [ ! -f "$OBJREL" ]; then
		echo "[$PROG] FAIL: ping_manager.o was not rebuilt" >&2
		exit 1
	fi
	echo "[$PROG] object rebuilt (link failed as expected)"
else
	echo "[$PROG] NOTE: unexpected - useOpenSSL=0 linked cleanly"
fi
assert_no_license_check

echo "[$PROG] relinking baseapp with the normal flags"
make -s -j"$JOBS" -rR -C bigworld/server/baseapp

echo "[$PROG] license-free baseapp: $BIN"
echo "[$PROG] ping_manager.o is compiled without the license check; see the"
echo "[$PROG] header comment to restore a licensed build."