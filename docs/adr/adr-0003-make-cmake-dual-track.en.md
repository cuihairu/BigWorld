# ADR-0003: Dual Track — make Main Chain plus Per-Component CMake Samples

> **English** | [中文](adr-0003-make-cmake-dual-track.md)

- **Status**: Accepted (cstdmf sample landed 2026-10-12; resmgr + network + CI matrix on 2026-10-14)
- **Scope**: `programming/bigworld/build/make/`, `cmake/samples/`,
  `scripts/build-*-cmake.sh`, `.github/workflows/ci.yml`,
  `lib/*/CMakeLists.txt` (the Windows superproject)

## Context

The repository's native build is the recursive make set under
`build/make/` (the sole validated Linux server path: the 21-module
unit-test gate runs there), alongside a Windows CMake superproject from the
2008 era (`BWConfiguration_*.cmake` + `lib/*/CMakeLists.txt`, the
MSVC/editor surface, not migrated). Modern build requirements (vcpkg
toolchain, reproducibility, incrementality, IDE support) conflict with the
migration risk carried by decades of accumulated make rules.

## Decision

**No big-bang migration.** Linux keeps make as the single authoritative
chain (compile flags, TU lists, and the test gate are all defined by it);
a separate bottom-up dual track of per-component samples lives at
`cmake/samples/<component>/CMakeLists.txt`:

- sample compile flags/macros are taken verbatim from measured `make -n`
  command lines; source lists are checked entry by entry against
  `Makefile.rules` (Windows/macOS TUs and off-list files are explicitly
  excluded with comments); dependencies go through the shared vcpkg
  manifest (see [ADR-0002](adr-0002-vcpkg-third-party-deps.en.md));
- one command per component (`scripts/build-<component>-cmake.sh`:
  configure → build → ctest), accepted only when case counts match the
  make chain (cstdmf 427, resmgr 88, network 210);
- CI (`ci.yml`) runs all samples in a matrix to prevent manifest/sample drift;
- the Windows superproject stays untouched and is handled wholesale under R3.

## Trade-offs

| Alternative | Reason for rejection |
|---|---|
| Translate the make chain into a CMake superproject in one pass | 30+ components × platform-macro matrix; any semantic drift directly threatens the 21-module gate; it cannot be accepted incrementally |
| Rewrite with a new system such as Bazel/Meson | Even greater people/tooling cost, and nothing from the existing Windows CMake ecosystem would be reusable |
| Keep make only, no CMake track | No path to IDE/incremental modernization; a thin validation surface for vcpkg toolchain integration and the CI matrix |
| Fix the Windows superproject to run on Linux directly | It is deeply bound to MSVC/MFC/the editor surface; mixing in a Linux server profile would pull R3 risk into the main line |

## Consequences

- **Positive**: the two tracks cross-check each other (sample case counts
  are a continuous assertion against the make chain); components can
  modernize on demand; the onboarding template is replicable for new
  components (cstdmf → resmgr → network already validated).
- **Costs/risks**: source-list changes in `Makefile.rules` must be mirrored
  in the sample lists (sample headers already carry a "keep in sync" note);
  two build semantics coexist long-term and need the CI matrix to catch
  drift; each sample's ~300-line explicit list is deliberate "translation
  transparency", not debt.
- **Follow-ups**: connection/entitydef samples stack on in the same pattern
  (connection has no unit_test and needs a test surface first; entitydef
  depends on 8 libraries and is assessed separately); at R3 time a new ADR
  decides whether the Windows superproject and Linux samples merge or
  divide responsibilities.

Evidence: [vcpkg-migration §3](/vcpkg-migration),
[build system modernization](/upgrade-plan/build-system-modernization).
