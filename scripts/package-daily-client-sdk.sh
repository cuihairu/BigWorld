#!/usr/bin/env bash
# Package the client SDK lanes into a daily bundle (CL-4): one nupkg (C#
# protocol core), one npm tarball (TypeScript plugin), one wheel (bwgen), plus
# a MANIFEST.txt with per-file SHA256 + byte counts and a zip named
# `bigworld-client-sdk-dailyYYYYMMDD.<sha>.zip`.
#
# Input: the client_sdk/ tree at HEAD. The three test suites are expected to
# already be green (the calling workflow runs them before invoking this
# script); packaging is artifact-only — no git tags, no releases (repo rule).
#
# Usage (CI — see .github/workflows/daily-build.yml):
#   scripts/package-daily-client-sdk.sh <out-dir>
#
# The version is the TS package version (single source of truth for the SDK
# version), and the build tag follows the daily scheme used by
# scripts/package-daily.sh: dailyYYYYMMDD.<short-sha>.
set -euo pipefail

OUT_DIR="${1:?usage: package-daily-client-sdk.sh <out-dir>}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

SDK_VER="$(python3 -c 'import json; print(json.load(open("client_sdk/typescript/package.json"))["version"])')"
DATE_UTC="$(date -u +%Y%m%d)"
GIT_SHA="$(git rev-parse --short HEAD 2>/dev/null || echo norev)"
TAG="daily${DATE_UTC}.${GIT_SHA}"

mkdir -p "$OUT_DIR"

echo "== nuget (C# protocol core) =="
dotnet pack client_sdk/unity/src/BigWorld.ClientSdk/BigWorld.ClientSdk.csproj \
  -c Release -o "$OUT_DIR" -p:Version="$SDK_VER" --nologo

echo "== npm tarball (TypeScript plugin) =="
(
  cd client_sdk/typescript
  # pack-destination needs an absolute path.
  pnpm pack --pack-destination "$(cd "$OUT_DIR" && pwd)"
)

echo "== wheel (bwgen) =="
python3 -m pip wheel --no-deps -w "$OUT_DIR" client_sdk/generator >/dev/null

echo "== manifest =="
MANIFEST="$OUT_DIR/MANIFEST.txt"
{
  echo "BigWorld client SDK products"
  echo "version:      $SDK_VER"
  echo "build:        $TAG"
  echo "commit:       $GIT_SHA"
  echo "built:        $(date -u +"%Y-%m-%d %H:%M:%S")Z"
  echo "toolchain:    dotnet SDK + node 22 + pnpm (corepack, packageManager) + python3"
  echo ""
  find "$OUT_DIR" -maxdepth 1 -type f ! -name 'MANIFEST.txt' ! -name 'pkg-version.txt' \
    ! -name '*.zip' -printf '%f\n' | sort | while read -r f; do
    p="$OUT_DIR/$f"
    printf '%s  %12d  %s\n' "$(sha256sum "$p" | cut -d' ' -f1)" "$(wc -c < "$p")" "$f"
  done
} > "$MANIFEST"
cat "$MANIFEST"

echo "== zip =="
ZIP_NAME="bigworld-client-sdk-${TAG}.zip"
python3 - "$OUT_DIR" "$ZIP_NAME" <<'PY'
import os, sys, zipfile
out_dir, zip_name = sys.argv[1], sys.argv[2]
zip_path = os.path.join(out_dir, zip_name)
if os.path.exists(zip_path):
    os.remove(zip_path)
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, _dirs, files in os.walk(out_dir):
        for name in files:
            path = os.path.join(root, name)
            if os.path.abspath(path) == os.path.abspath(zip_path):
                continue
            zf.write(path, os.path.relpath(path, out_dir))
print(zip_name)
PY

echo "== bundle =="
ls -l "$OUT_DIR"

# Same contract as scripts/package-daily.sh: consumed by the workflow for the
# artifact name (steps.pkg.outputs.ver).
printf '%s\n' "$TAG" > "$OUT_DIR/pkg-version.txt"
