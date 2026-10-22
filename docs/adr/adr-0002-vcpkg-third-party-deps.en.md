# ADR-0002: Third-Party Dependencies Move from Vendored Source Trees to a vcpkg Manifest

> **English** | [中文](adr-0002-vcpkg-third-party-deps.md)

- **Status**: Accepted (landed 2026-10-12; switch in `2bdfc5aa`, baseline pinned in `1180ece6`)
- **Scope**: `programming/bigworld/third_party/`, `third_party_*.mak`,
  `build/cmake/FindBW*.cmake`, CI/daily-build workflows

## Context

The 20+ libraries under `third_party/` are all vendored source trees from
2008–2014 (OpenSSL 1.0.0d, curl 7.x, jsoncpp 0.7.0, zlib 1.2.8,
libpng 1.6.2, sqlite 3.6.23 …), each shipping its own configure/visualc
build scripts:

- OpenSSL 1.0.0d is below Python 3.13's minimum requirement (1.1.1) and
  does not compile under gcc15's default C23 (`bool` keyword clash) —
  directly blocking the interpreter's `_ssl`/`_hashlib`;
- every library has its own private build scripts; versions have no
  lockfile, upgrades have no reference point, and the audit surface is large.

## Decision

Libraries with an equivalent vcpkg port uniformly move to **vcpkg manifest
consumption** (`programming/bigworld/vcpkg.json` pins `builtin-baseline`
for reproducible resolution; custom triplet `x64-linux-bw`); both the make
and CMake chains consume the vcpkg toolchain artifacts: **OpenSSL 3.6 /
curl 8.x / jsoncpp 1.9 / zlib / libpng / sqlite3 / bzip2**. Link names keep
their old names (`zip`, `bwcurl`) to minimize consumer-surface changes
(png drops the private headers for the public API; sqlite keeps only the
wrapper layer).

Three categories are **explicitly kept vendored** (justified item by item in
the switch-scope table): CPython (the BWHooks-patched tree, see
[ADR-0001](adr-0001-python-313-embedded-interpreter.en.md)), the MongoDB
legacy C++ driver (vcpkg ships the 3.x API, incompatible with the legacy call
surface), and the png/zip source trees (still referenced by the Windows
CMake projects; deleted after R3 lands).

## Trade-offs

| Alternative | Reason for rejection |
|---|---|
| Upgrade each vendored tree in place | Every library still needs private build scripts and patch maintenance; 7 libs × forever = continuous debt, with no lockfile |
| Consume system packages (apt) directly | CI/user-machine versions are uncontrollable and offline reproducibility is violated; fragmentation is even worse on macOS/Windows |
| Move everything to vcpkg (incl. CPython/mongo) | The CPython port lacks BWHooks; the mongo port is a different API generation, so a hard switch amounts to rewriting the consumer surface |
| CMake FetchContent | The make main chain cannot consume it; two dependency resolutions (FetchContent + make) are worse than a single manifest |

## Consequences

- **Positive**: OpenSSL 3.6 unblocks `_ssl`/`_hashlib` (R1 closed); a
  dependency upgrade becomes one manifest line plus a baseline bump; the
  binary cache makes CI/local builds seconds-fast; `builtin-baseline` makes
  any point-in-time build reproducible.
- **Costs/risks**: zlib allocations no longer go through the `MY_ZCALLOC`
  hook → **zlib allocations are absent from BW memory statistics** (a
  registered change in observability semantics); the jsoncpp consumer
  surface uses deprecated APIs (warning noise, tolerated for now).
- **Follow-ups**: the residual `third_party/png`, `third_party/zip`, and
  `third_party/sqlite` trees are deleted; vcpkg upgrades follow the
  baseline-bump process.

Evidence: [third-party dependency switch to vcpkg](/vcpkg-migration).
