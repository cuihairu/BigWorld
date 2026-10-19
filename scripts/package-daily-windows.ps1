# scripts/package-daily-windows.ps1
#
# Package the root superproject's Hybrid server build into one deployable
# Windows bundle (zip) and write a SHA256 manifest for it.
#
# Shared by two callers so the daily delivery lane and the probe lane ship
# exactly the same layout:
#   * .github/workflows/daily-build.yml  - windows job (delivery)
#   * .github/workflows/ci-windows.yml   - root-build-probe (evidence)
#
# Versioning mirrors scripts/package-daily.sh exactly so the two lanes'
# artifacts line up: the base version comes from programming/bigworld/
# vcpkg.json "version", then "+daily<UTC date>.<short sha>". Artifact name
# pattern: bigworld-<ver>-windows-x64 (Linux: bigworld-<ver>-linux-x86_64).
#
# Layout is FLAT on purpose, not bin/ + python/ split. Nothing in the
# embedder calls Py_SetPythonHome/Py_SetPath, so the interpreter resolves its
# home from python313.dll's own directory and expects Lib/ next to it -
# PCbuild's own layout. Splitting the two would leave every import failing.
#
# What ships, and why:
#   <ver>/            11 Hybrid executables (the server process set)
#   <ver>/*.dll       python313.dll, the PCbuild extension modules, and the
#                     BigWorld Python modules (_bwlog.dll, bwengine_xml.dll,
#                     _bw_profile.dll) - they resolve through the interpreter
#                     so they belong next to it, not next to the executables
#   <ver>/Lib/        the CPython stdlib, 2338 files; in-repo and copied in
#                     so the bundle is self-contained
# The BigWorld core libraries (cstdmf, network, server, ...) are static on
# this track - no cstdmf.dll or network.dll ships, and no such file exists to
# copy. Recorded in the manifest so the bundle's contents are auditable.
#
# Usage:
#   pwsh -File scripts/package-daily-windows.ps1 `
#        -Repo . -BuildDir build/win_root_build -OutDir out
#
# Nothing here is a release: no git tags, no GitHub releases - the bundle is a
# workflow artifact with a retention window (repo rule).

param(
	# Repository root; vcpkg.json's version and the python tree both live here.
	[Parameter(Mandatory)]
	[string] $Repo,

	[string] $BuildDir = "build/win_root_build",
	[string] $Config = "Hybrid",
	[string] $OutDir = "out"
)

$ErrorActionPreference = "Stop"

$RepoPath = (Resolve-Path $Repo).Path
$BaseDir  = Join-Path $RepoPath "programming/bigworld"

# Version source of truth, same as package-daily.sh.
$json = Get-Content (Join-Path $BaseDir "vcpkg.json") -Raw
$baseVer = [regex]::Match($json, '"version"\s*:\s*"([^"]*)"').Groups[1].Value
if (-not $baseVer) { throw "cannot read version from vcpkg.json" }
$date = (Get-Date).ToUniversalTime().ToString("yyyyMMdd")
$sha = git -C $RepoPath rev-parse --short HEAD 2>$null
if (-not $sha) { $sha = "norev" }
$ver = "${baseVer}+daily${date}.${sha}"

$stageRoot = Join-Path $OutDir "stage"
$stage = Join-Path $stageRoot "bigworld-windows-server-$ver"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
# Absolute from here on: the manifest slices file paths with $stage.Length,
# and Get-ChildItem only ever hands back absolute paths.
$stage = (Get-Item $stage).FullName

# The build tree carries one directory per configuration; only $Config was
# built, so take exactly that stratum.
$configRe = "\\$Config\\"

Get-ChildItem -Recurse -Filter *.exe $BuildDir |
  Where-Object { $_.FullName -match $configRe } |
  Copy-Item -Destination $stage -Force
Get-ChildItem -Recurse -Filter *.dll $BuildDir |
  Where-Object { $_.FullName -match $configRe } |
  Copy-Item -Destination $stage -Force

# Vendored CPython: python.props maps x64 to ArchName=amd64 and pythoncore
# writes to $(PySourcePath)PCbuild\$(ArchName)\.
$pyBinPath = Join-Path $BaseDir "third_party/python/PCbuild/amd64"
if (Test-Path (Join-Path $pyBinPath "python313.dll")) {
	Copy-Item (Join-Path $pyBinPath "python313.dll") $stage -Force
	Get-ChildItem $pyBinPath -Filter *.pyd |
	  Copy-Item -Destination $stage -Force
} else {
	Write-Warning "python313.dll not found in $pyBinPath - interpreter absent"
}

$libDir = Join-Path $BaseDir "third_party/python/Lib"
if (Test-Path $libDir) {
	Copy-Item $libDir (Join-Path $stage "Lib") -Recurse -Force
	$libFiles = (Get-ChildItem -Recurse -File $libDir | Measure-Object).Count
} else {
	$libFiles = 0
	Write-Warning "python stdlib missing: $libDir"
}

# Manifest: hash + size + name per file, then the facts that shape the bundle
# - including what is NOT in it. Hashes make a bundle diffable across lanes
# and give the daily consumer something to pin.
$manifest = @()
$manifest += "BigWorld Windows server products"
$manifest += "version:      $ver"
$manifest += "commit:       $($sha)"
$manifest += "built:        $((Get-Date).ToUniversalTime().ToString("yyyy-MM-dd HH:mm:ss"))Z"
$manifest += "toolset:      MSVC 19.51 / Visual Studio 18 2026, x64"
$manifest += "configuration: $Config"
$manifest += "runtime crt:  UCRT (ships with Windows 10+; not bundled here)"
$manifest += "python stdlib: in-repo third_party/python/Lib ($libFiles files), copied in"
$manifest += "not built:    examples/cellapp_extension - its unresolved externals are"
$manifest += "              cellapp-exe-owned symbols (Entity::*, Controller::*, ...) that"
$manifest += "              a Windows DLL link must resolve, unlike a Unix .so load. Needs an"
$manifest += "              export decision (.def / dllexport / hosted interface)."
$manifest += ""
$manifest += Get-ChildItem $stage -Recurse -File | Sort-Object FullName |
  ForEach-Object {
	$rel = $_.FullName.Substring($stage.Length).TrimStart('\')
	$sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
	"{0}  {1,12:N0}  {2}" -f $sha256, $_.Length, $rel
}
$manifest | Out-File -FilePath (Join-Path $stage "MANIFEST.txt") -Encoding utf8
# Also at the out root, where the caller's artifact glob can grab it without
# opening the bundle.
$manifest | Out-File -FilePath (Join-Path $OutDir "products-manifest.txt") -Encoding utf8

Get-Content (Join-Path $OutDir "products-manifest.txt")

$zip = Join-Path $OutDir "bigworld-windows-server-$ver.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -Force

Write-Host "== bundle in $OutDir =="
Get-ChildItem $OutDir | Format-Table Name, Length -Auto
# Consumed by daily-build.yml for the artifact name, the same contract
# package-daily.sh has with out/pkg-version.txt.
Set-Content -Path (Join-Path $OutDir "pkg-version.txt") -Value $ver
Write-Host $ver
