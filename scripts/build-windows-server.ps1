# scripts/build-windows-server.ps1
#
# Configure the root superproject for the server target and build the
# Windows server executables, one MSBuild invocation per target.
#
# Shared by two callers so the daily delivery lane and the probe lane build
# exactly the same thing:
#   * .github/workflows/daily-build.yml  - windows job (delivery)
#   * .github/workflows/ci-windows.yml   - root-build-probe (probe-within-probe)
#
# Prerequisites on the caller's environment (both workflows set these):
#   $env:DXSDK_DIR            - D3DX 9.29.952.8 headers + libs; lib/math's
#                               Windows vector types are D3DX typedefs and
#                               server modules pull them through math headers.
#   $env:VCPKG_JSONCPP_INCLUDE - jsoncpp include dir (message_logger's
#                               metadata backend; the make chain takes the same
#                               port from vcpkg and nothing vendors it).
#
# Usage:
#   pwsh -File scripts/build-windows-server.ps1 `
#        -Repo programming/bigworld -BuildDir build/win_root_build
#
# Exit code 0 only when every target on the list builds. Nothing here is a
# release: no tags, no releases, no publishing - build the tree only.

param(
	[Parameter(Mandatory)]
	[string] $Repo,

	# The server target builds the executables; -G picks the runner's VS
	# generator (VS 18 2026 on windows-latest; there is no VS2022 image).
	[string] $BuildDir = "build/win_root_build",

	# The root FORCE-sets CMAKE_CONFIGURATION_TYPES to Debug;Hybrid
	# (root CMakeLists.txt:22), so the generated projects carry no Release
	# configuration and --config Release fails every target with MSB8013
	# (observed on the first probe dispatch, run 38745028827).
	[string] $Config = "Hybrid",

	# One invocation per target: a single msbuild invocation stops at its
	# first failing project, which hid the other 10 executables behind one
	# failure (run 39013952226: cellappmgr's two C2131s).
	[string[]] $Targets = @(
		"cellapp", "cellappmgr",
		"baseapp", "baseappmgr",
		"dbapp", "dbappmgr",
		"loginapp", "reviver",
		"bots", "transfer_db",
		"message_logger"
	),

	[int] $Parallel = 4,

	# The probe job configures on its own (its log feeds the configure
	# artifact) and interleaves a stage-1 ALL build between the configure and
	# the exe lane, so it skips this script's configure.
	[switch] $SkipConfigure
)

$ErrorActionPreference = "Stop"
$env:CMAKE_POLICY_VERSION_MINIMUM = "3.5"
# The root's BWCompilerAndLinkerOptions.cmake appends /WX (a VS2008-era
# setting) and _CL_ appends AFTER the command line, so /WX- wins. Keeps this
# a compile gate rather than a warning gate; the size_t-conversion warnings in
# legacy server code (e.g. server_super_model.hpp:74 C4267) are a follow-up.
$env:_CL_ = "/WX-"

$RepoPath = (Resolve-Path $Repo).Path -replace '\\', '/'
$BuildPath = (Resolve-Path $BuildDir -ErrorAction SilentlyContinue).Path -replace '\\', '/'
if (-not $BuildPath) { $BuildPath = $BuildDir -replace '\\', '/' }

# The vcpkg toolchain, resolved the way the workflows provision it: an
# explicit VCPKG_ROOT (the daily lane clones the pinned manifest baseline)
# wins, then the runner's preinstalled vcpkg. With a toolchain the manifest
# (vcpkg.json) supplies openssl/curl/zlib/libpng/sqlite3 at configure time;
# without one the legacy vendored branches in the FindBW* modules still
# resolve, so a local run keeps working. The triplet is static-md: static
# libraries against the dynamic CRT the root's BWCompilerAndLinkerOptions
# mandates (/MD), so no vcpkg DLLs have to ship next to the exes.
$VcpkgToolchain = $null
if ($env:VCPKG_ROOT) {
	$VcpkgToolchain = ($env:VCPKG_ROOT -replace '\\', '/') + '/scripts/buildsystems/vcpkg.cmake'
} elseif ($env:VCPKG_INSTALLATION_ROOT) {
	$VcpkgToolchain = ($env:VCPKG_INSTALLATION_ROOT -replace '\\', '/') + '/scripts/buildsystems/vcpkg.cmake'
}
$ToolchainArgs = @()
if ($VcpkgToolchain -and (Test-Path $VcpkgToolchain)) {
	$ToolchainArgs += @(
		"-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain",
		"-DVCPKG_TARGET_TRIPLET=x64-windows-static-md"
	)
	Write-Host "=== vcpkg toolchain: $VcpkgToolchain ==="
}

if (-not $SkipConfigure) {
	Write-Host "=== configure root superproject (server target) ==="
	cmake -S $RepoPath -B $BuildPath `
	      -G "Visual Studio 18 2026" -A x64 `
	      -DBW_CMAKE_TARGET=server `
	      -DBW_JSONCPP_INCLUDE_DIR="$env:VCPKG_JSONCPP_INCLUDE" `
	      @ToolchainArgs 2>&1 |
	    Tee-Object -FilePath cmake-configure-root.log
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$failures = 0
foreach ($target in $Targets) {
	Write-Host "=== exe target: $target ==="
	cmake --build $BuildPath --config $Config --target $target `
	      --parallel $Parallel 2>&1 |
	    Tee-Object -FilePath cmake-build-root-exes.log -Append
	if ($LASTEXITCODE -ne 0) {
		Write-Host "exe target $target FAILED (exit $LASTEXITCODE)"
		$failures++
	}
}

Write-Host "exe lane: $failures target(s) failed"
if ($failures -gt 0) { exit 1 }
exit 0
