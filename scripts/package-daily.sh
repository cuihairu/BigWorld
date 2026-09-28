#!/usr/bin/env bash
# Package the make-installed game/ tree into daily bundles: tar.gz, .deb, .rpm.
#
# Input is the layout produced by a full `make -C programming` run:
#   game/bin/server/el7/{examples,server,tools,unit_tests,third_party}
#   game/res
#
# Usage (local or CI — see .github/workflows/daily-build.yml):
#   scripts/package-daily.sh <out-dir>
#
# All three formats install under /opt/bigworld (tar: `tar -C / -xzf ...`).
# Version source of truth: programming/bigworld/vcpkg.json "version".
# Note: no git tags and no GitHub releases are produced by design (repo rule);
# the daily artifacts are uploaded as workflow artifacts with retention.
set -euo pipefail

OUT_DIR="${1:?usage: package-daily.sh <out-dir>}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_ROOT="$REPO_ROOT/game"
BIN_ROOT="$GAME_ROOT/bin/server/el7"
RES_ROOT="$GAME_ROOT/res"

BASE_VER="$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
	"$REPO_ROOT/programming/bigworld/vcpkg.json" | head -1)"
[ -n "$BASE_VER" ] || { echo "ERROR: cannot read version from vcpkg.json" >&2; exit 1; }

DATE_UTC="$(date -u +%Y%m%d)"
GIT_SHA="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo norev)"
DEB_VER="${BASE_VER}+daily${DATE_UTC}.${GIT_SHA}"
RPM_REL="1.daily${DATE_UTC}.${GIT_SHA}"

[ -d "$BIN_ROOT" ] || { echo "ERROR: $BIN_ROOT missing — run the build first" >&2; exit 1; }
[ -d "$RES_ROOT" ] || { echo "ERROR: $RES_ROOT missing — the res install rules run with the full build" >&2; exit 1; }

mkdir -p "$OUT_DIR"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

# One staging tree serves all formats: /opt/bigworld/{bin,res}.
PKG_STAGE="$STAGE/pkg/opt/bigworld"
mkdir -p "$PKG_STAGE"
cp -a "$GAME_ROOT/bin" "$PKG_STAGE/bin"
cp -a "$RES_ROOT" "$PKG_STAGE/res"
# Unit-test assert dumps are local forensic junk, not package content.
find "$PKG_STAGE" -name '*.log' -delete

echo "== tar.gz =="
tar -C "$STAGE/pkg" -czf "$OUT_DIR/bigworld-${DEB_VER}-linux-x86_64.tar.gz" opt

echo "== deb =="
DEB_DIR="$STAGE/deb/bigworld_${DEB_VER}_amd64"
mkdir -p "$DEB_DIR/DEBIAN"
cp -a "$STAGE/pkg/opt" "$DEB_DIR/opt"
cat > "$DEB_DIR/DEBIAN/control" <<EOF
Package: bigworld
Version: ${DEB_VER}
Architecture: amd64
Maintainer: cuihairu <cuihairu@users.noreply.github.com>
Depends: libc6 (>= 2.17), libstdc++6, libmysqlclient21 | libmariadb3
Section: devel
Priority: optional
Description: BigWorld MMO server engine daily build (Python 3.13 embedded)
 Server binaries, unit tests and the game resource tree under /opt/bigworld.
 Built from source by the daily-build workflow; third-party libraries are
 statically linked (vcpkg), only libc/libstdc++/mysql client stay dynamic.
EOF
dpkg-deb --build --root-owner-group "$DEB_DIR" "$OUT_DIR"

echo "== rpm =="
RPM_TOP="$STAGE/rpmbuild"
mkdir -p "$RPM_TOP"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
cat > "$RPM_TOP/SPECS/bigworld.spec" <<EOF
Name:           bigworld
Version:        ${BASE_VER}
Release:        ${RPM_REL}%{dist}
Summary:        BigWorld MMO server engine daily build (Python 3.13 embedded)
BuildArch:      x86_64
License:        BigWorld Open-Source Edition
AutoReqProv:    no

%description
Server binaries, unit tests and the game resource tree under /opt/bigworld.
Built from source by the daily-build workflow.

%install
mkdir -p %{buildroot}/opt/bigworld
cp -a %{bwsrc}/bin %{buildroot}/opt/bigworld/bin
cp -a %{bwsrc}/res %{buildroot}/opt/bigworld/res

%files
/opt/bigworld
EOF
rpmbuild -bb \
	--define "_topdir $RPM_TOP" \
	--define "dist %{nil}" \
	--define "bwsrc $PKG_STAGE" \
	"$RPM_TOP/SPECS/bigworld.spec"
mv "$RPM_TOP"/RPMS/x86_64/*.rpm "$OUT_DIR/"

echo "== packages in $OUT_DIR =="
ls -lh "$OUT_DIR"
