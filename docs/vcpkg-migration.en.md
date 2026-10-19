# Migrating BigWorld's Third-Party Dependencies to vcpkg

> **English** | [中文](vcpkg-migration.md)
> Task: replace the libraries under `programming/bigworld/third_party` that vcpkg can
> supply with vcpkg manifest entries, and move the make and CMake chains onto the
> vcpkg toolchain/artifacts.
> Branch: dev. Follows on from [python-313-migration.md](python-313-migration.md)
> (the OpenSSL upgrade is the resolution of that document's risk R1).

## 1. Switch scope and decisions

| Library | Previous state | Decision | Notes |
|---|---|---|---|
| OpenSSL | vendored 1.0.0d (does not compile under gcc15) | **vcpkg 3.6.4** | static; restores Python `_ssl`/`_hashlib` (R1). Directory `third_party/openssl` deleted |
| curl | vendored 7.x (configure build) | **vcpkg 8.22** (openssl feature) | link name kept as `bwcurl` |
| jsoncpp | vendored 0.7.0 | **vcpkg 1.9.x** | the consumption surface is only `Reader::parse`/`Value`/`FastWriter` (1.9-compatible, deprecation warnings); directory `third_party/jsoncpp` deleted |
| zlib (directory name `zip`) | vendored 1.2.8 (`MY_ZCALLOC` allocation hook) | **vcpkg zlib 1.3.x** | link name kept as `zip`; zlib allocations no longer counted in BW memory statistics (see §4) |
| libpng | vendored 1.6.2 (source-component build) | **vcpkg 1.6.58** | `lib/moo/png.cpp` drops the private headers and moves to the public API (see §4) |
| sqlite3 | vendored 3.6.23.1 (amalgamation folded into libsqlite) | **vcpkg 3.53.4** | `lib/sqlite` keeps only the wrapper layer; direct consumers set `useSQLite := 1` |
| CPython | vendored 3.13.16 (BWHooks-patched tree) | **stay vendored** | the vcpkg python3 port does not carry the BWHooks memory-tracking patch; `_ssl`/`_hashlib` are nevertheless restored via vcpkg OpenSSL (R1) |
| MongoDB C++ driver | vendored legacy prebuilt | **stay vendored** | vcpkg's mongo-cxx-driver is the 3.x API, incompatible with the legacy driver; only used by message_logger |
| vendored `png`/`zip` source trees | — | **keep for now** | the Windows CMake projects (R3, not migrated) still reference `third_party/png` and `third_party/zip` (sqlite goes through `lib/sqlite`); delete once R3 lands |
| CppUnitLite2 / WTL / putty / nvtt / nedalloc / recast / openautomate etc. | vendored | **stay vendored** | the engine's own test framework, or Windows/client-only, not on the Linux server build path |

### 1.1 Manifest version pinning (builtin-baseline)

`programming/bigworld/vcpkg.json` gained `"builtin-baseline": "10541e317a660f4165ba4ac2851ab54a8d4577b1"`
(the vcpkg repository HEAD as of 2026-09-25). Effects:

- Dependency versions no longer drift with the point in time the vcpkg clone in
  `VCPKG_ROOT` was made — manifest resolution is pinned to that baseline's ports
  tree (measured: it resolves openssl 3.6.4 / curl 8.22.0 / sqlite3 3.53.4 etc.,
  matching the §1 table).
- Without a pinned baseline, vcpkg resolves versions against the current HEAD in
  `VCPKG_ROOT`; "the same vcpkg.json installs different openssl versions on two
  machines" is exactly how that happens.
- **No `vcpkg-configuration.json` is created**: that file is for registry
  overrides / overlay registration (third-party registries, default-registry
  re-routing). This repository uses only the builtin registry and has no override
  needs; the custom triplet (`build/vcpkg-triplets/x64-linux-bw.cmake`) is passed
  per build via `--overlay-triplets` and is not a manifest-layer concern. An empty
  config file would only add parsing surface.
- Upgrading a dependency = change the baseline (together with `git -C ~/vcpkg
  checkout` of the local `VCPKG_ROOT` to the new commit) then `vcpkg upgrade`;
  push once locked.

## 2. Build-system changes (make chain)

- New `vcpkg.json` (BigWorld source root, `programming/bigworld/`): declares
  openssl, curl(openssl), jsoncpp, zlib, libpng, sqlite3, bzip2 (the last solely
  for the CPython `_bz2` extension).
- New custom triplet `build/vcpkg-triplets/x64-linux-bw.cmake`: static libraries,
  Release only (`VCPKG_BUILD_TYPE release`). That directory is deliberately
  **not** under `build/vcpkg/` — `.gitignore:41` excludes
  `programming/bigworld/build/vcpkg/`, while a triplet is a build input that
  belongs in the repository; putting it there would leave a fresh clone both
  without it and unable to use it as a make prerequisite (the
  `--overlay-triplets` directory would fail the same way).
- New `build/make/third_party_vcpkg.mak`:
  - `VCPKG_ROOT` (environment variable or make variable) → `vcpkg install`
    (manifest mode); artifacts land in
    `build/<platform>/third_party/vcpkg_installed/x64-linux-bw/`;
    `vcpkg-install.stamp` serves as the make dependency anchor;
  - each package **stages** the vcpkg static library under its historical
    link-line name: `libbwssl.a`/`libbwcrypto.a`/`libbwcurl.a`/`libjsoncpp.a`/
    `libzip.a`/`libpng.a` (from libpng16)/`libsqlite3.a`; target names are
    unchanged, so the `thirdPartyDependsOn` chains needed no edits;
  - `BW_INCLUDES += -I<vcpkg>/include` (single root for the zlib/png/sqlite3/
    jsoncpp headers).
- `third_party_openssl.mak` / `third_party_curl.mak` / `third_party_json.mak`
  rewritten as vcpkg consumers (external variable names such as
  `OPENSSL_BUILD_DIR`/`BW_SSL_LIB`/`CURL_BUILD_DIR`/`JSON_DIR` kept;
  `OPENSSL_BUILD_DIR` is the vcpkg installed root and at the same time the root
  for Python's `--with-openssl`).
- `third_party/Makefile.rules`: the jsoncpp/png/zip sub-builds removed (only
  CppUnitLite2 remains).
- `common_footer_config.mak`:
  - `useCurl` header path → the single vcpkg root (no more vendored include);
  - new `useSQLite` (set for `lib/sqlite`, baseapp, transfer_db, consolidate_dbs
    and their unit tests: staging dependency + `-lpthread -ldl -lm`);
  - new `usePNG` (set for `lib/moo` and for targets linking libmoo: staging
    dependency + `-lpng -lzip`, because libpng16 depends on zlib). Third-party
    dependencies **do not propagate** (`bwLibraryDepsAsName` is built only from
    the component's own `dependsOn`/`thirdPartyDependsOn`), so every target
    consuming libmoo must declare it itself.
- `common_header.mak`: `useSQLite`/`usePNG` joined `useCurl`/`useOpenSSL` in the
  per-component reset list. Without the reset, the first component that enables
  one leaks the flag to every component included afterwards, conjuring an extra
  third-party link item out of thin air.
- `lib/sqlite/Makefile.rules`: no longer compiles the amalgamation.
- `third_party_python.mak` (R1 restoration, see python-313-migration.md for
  detail): `--with-openssl` + the `_ssl`/`_hashlib` shared modules + the
  whole-archive `-lbwssl -lbwcrypto` + symbol collection restored; plus
  `--with-bz2=$(VCPKG_INSTALLED)` (`_bz2` is in the `pythonSharedMods` list,
  and the host has no system bzip2 development package, so it likewise goes
  through vcpkg).
- Build: `VCPKG_ROOT=~/vcpkg make -C programming` (a clear error is raised when
  VCPKG_ROOT is unset).
- Running the unit tests needs one extra install of the interpreter standard
  library: `make -C programming user_shouldInstallPython=1 python_install`
  (otherwise the modules that use Python fail with `ModuleNotFoundError: No
  module named 'encodings'`).
- The `bw-run-all-unit-tests` recipe in `build/make/Makefile` was fixed in
  passing: the original recipe's `|| FAILED_TESTS=… \` continuation folded the
  status into the same command prefix, `$$?` could not read the module exit
  code, and `[ … == 1 ]` is a bashism — failed modules were neither collected
  completely nor able to stop the run; changed to capture `MODULE_STATUS=$$?`
  first and branch on it, and to replace `==` with the POSIX `=`.
- Several unit-test directories' `Makefile.rules` gained `dependsOn += zip`
  (plus `usePNG`/`moo`/`resmgr` for terrain): the unit_test type sets
  `noDefaultDependencies`, so the `zip` inside `BW_DEPENDS_ON` never reached the
  link line; after the vcpkg switch, libnetwork's zip_stream/compression_stream
  need libzip.a present to link.

### Link closure

The transitive dependencies of the vcpkg static libraries close inside the BW
link lines: curl→ssl/crypto+zlib (`bwssl`/`bwcrypto`/`zip` all staged);
libpng16→zlib (`zip`); sqlite3→pthread/dl/m (injected by `useSQLite`). curl's
brotli/idn2 etc. are optional features, not enabled.

## 3. Build-system changes (CMake chain)

- `vcpkg.json` sits next to the root `CMakeLists.txt`: configuring with
  `-DCMAKE_TOOLCHAIN_FILE=<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake`
  automatically enters manifest mode and installs the dependencies.
- `build/cmake/FindBWOpenSSL.cmake` / `FindBWcurl.cmake`: on non-MSVC, prefer
  `find_package(OpenSSL/CURL)` (results provided by the vcpkg toolchain) and map
  them onto the BW-prefixed variables; the MSVC branch keeps the original
  `third_party/openssl-vsXXXX` and prebuilt-curl paths (Windows migration =
  python-313-migration.md R3, a later batch).
- The Windows client/tools configurations (`BWConfiguration_*.cmake`) still
  reference the `third_party/zip` and `third_party/png` source projects (sqlite
  goes through `lib/sqlite`, see `BWConfiguration_server.cmake:71`); to be
  handled together with R3.

### 3.1 Linux CMake sample (cstdmf, coexisting with the make chain)

`cmake/samples/cstdmf/CMakeLists.txt` is the first independently buildable Linux
component sample (**not** part of the `lib/*/CMakeLists.txt` BWStandardProject
Windows super-project; the two do not reference each other). Sample composition:

- Four targets: `cstdmf` (static library, 57 TUs), `unit_test_lib` (4 TUs),
  `CppUnitLite2` (7 TUs, compiled directly from `third_party/CppUnitLite2/src`),
  `cstdmf_test` (48 TUs). The first two and CppUnitLite2 are also independent
  components in the make chain; the sample compiles them from source in place so
  that the test binary's link surface is identical to make's
  (`-lunit_test_lib -lCppUnitLite2 -lcstdmf -lm -lpthread` + `-rdynamic`).
- Alignment with the make output is guaranteed by construction: compile
  flags/macros (`-std=c++23 -O3 -g -Wall -Wextra -Werror`,
  `MF_SERVER / MF_CONFIG="hybrid" / BW_BUILD_PLATFORM="el7" / CSTDMF_EXPORT|IMPORT /
  CODE_INLINE / MF_USE_ASSERTS / _HYBRID`, plus `-Wno-float-equal` on the test
  side) are taken verbatim from the measured `make -n unit_test_cstdmf` command
  line; the source list is checked entry by entry against `Makefile.rules`
  (excluding Windows/macOS TUs and files outside the make list, e.g. the
  `test_object_pool.cpp` orphan entry — `object_pool.hpp:343` needs
  `LookUpTable::reserve()`, which this tree does not have). When a dependency is
  upgraded, a change to `Makefile.rules` must be synchronized with this list.
- Dependencies go through the shared manifest: `VCPKG_MANIFEST_DIR` defaults to
  `programming/bigworld`; the custom triplet `x64-linux-bw` and the overlay
  directory are defaulted inside the CMakeLists; at first configure, vcpkg
  manifest mode installs the 7 dependencies into the build directory (seconds
  once the binary cache hits).
- One local command: `scripts/build-cstdmf-cmake.sh` (configure → build → ctest;
  default build dir `programming/bigworld/build/cmake-cstdmf/`, already in
  .gitignore).
- Verification record (2026-10-12): cold start from a brand-new build directory
  (dependencies restored from the binary cache), configure/build passed,
  `cstdmf_test` **398 cases all green**, matching the case count of the
  make-produced cstdmf_test; the make-side regression re-run was likewise 398
  all green.
- On the CI side, `.github/workflows/ci.yml` runs the same sample in a clean
  environment on every push/PR (vcpkg cloned per baseline + binary cache +
  ctest), guarding against manifest/sample drift.

### 3.2 resmgr sample (second bottom-up block, 2026-10-13)

`cmake/samples/resmgr/CMakeLists.txt` is the second sample. It reuses 3.1's
`CppUnitLite2` + `unit_test_lib` + `cstdmf` source lists and stacks `math`
(12 TUs), `zip` (vcpkg zlib, staged as `libzip.a` under make), `resmgr`
(25 TUs, `HAVE_ZIP` + `BWENTITY_DLL_EXPORT`), and `resmgr_test` (12 TUs) on top.
Compile flags/macros are taken verbatim from the measured `make -n -B
bw-run-test-resmgr` command line (including `-fPIC` and `-Wfloat-equal`; the
test side does **not** add `-Wno-float-equal` — unlike 3.1, make is
authoritative); `-rdynamic -Wl,--export-dynamic` and the link order
(`unit_test_lib CppUnitLite2 resmgr cstdmf math ZLIB::ZLIB m pthread`) match
the make link line. One local command: `scripts/build-resmgr-cmake.sh`.

Two sample-level points:

- `unit_test_lib`'s `base_resmgr_unit_test_harness.cpp` references `BWResource`
  symbols, but static-library members are pulled in only when the final link
  needs them — which is why 3.1's cstdmf sample links fine without resmgr while
  the resmgr sample must carry resmgr (isomorphic to make's `--start-group`).
- The resmgr test base class (`test_harness.hpp`) concatenates `--root` directly
  into `<repo>/programming/bigworld/lib/resmgr/unit_test/res` and
  `<repo>/game/res/bigworld`, so the sample's `--root` must point exactly at the
  repository root; 3.1's `get_filename_component(BW_REPO_ROOT ...)` originally
  wrote 6 `..` segments (one too many, pointing at `workspaces/`) — the cstdmf
  test base class never reads `--root`, so it went unnoticed; the resmgr sample
  fixed both samples to 5 segments (the repository root), cstdmf included.

Verification (2026-10-13): both samples configure/build; `ctest` all green —
`cstdmf_test` **427** cases and `resmgr_test` **88** cases, same caliber as the
make chain (make-side `bw-run-test-cstdmf` 427 / `bw-run-test-resmgr` 88). The
CI `cmake-samples` job became a matrix (`[cstdmf, resmgr]`, `fail-fast: false`)
with independent steps and logs per component. network/connection/entitydef
stack on later in the same pattern (connection currently has **no** unit_test,
and entitydef depends on 8 libraries; both need separate handling).

### 3.3 network sample (third bottom-up block, 2026-10-14)

`cmake/samples/network/CMakeLists.txt` is the third sample. It reuses the
3.1/3.2 `CppUnitLite2` + `unit_test_lib` + `cstdmf` + `math` + `resmgr` source
lists and adds three blocks: `connection` (26 TUs — first included on the CMake
track; it has **no** unit_test and was previously covered only indirectly
through network's link surface), `network` (78 TUs), and `network_test`
(49 TUs). Compile flags/macros verbatim from the measured `make -n -B
bw-run-test-network` command line; one local command
`scripts/build-network-cmake.sh`.

Three differences from 3.1/3.2 (each backed by make measurements, not
"convenient unification"):

- **`USE_OPENSSL`**: both `lib/connection`'s and `lib/network`'s Makefile.rules
  set `useOpenSSL = 1`, so it is part of the global macro surface (the other
  samples have none). make stages vcpkg OpenSSL as `libbwssl.a`/`libbwcrypto.a`;
  the sample maps this equivalently to `find_package(OpenSSL REQUIRED)` +
  `OpenSSL::SSL`/`OpenSSL::Crypto` — the same vcpkg artifacts, only the link
  names differ.
- **`-Wno-float-equal` also on the library side**: 3.2 recorded that the resmgr
  test side does **not** add it; network adds it **on both the library and test
  sides** — `lib/network/Makefile.rules`'s `cxxFlags_$(binName) +=
  -Wno-float-equal` targets the library itself (`bsd_snprintf.cpp` has
  floating-point `==` comparisons); missing it would fail the gate outright on
  `-Werror=float-equal`. The connection side has no such flag (the 26 TUs other
  than `client_interface.cpp` contain no floating-point comparisons), so it
  stays off.
- **Single-scan static-library link order**: make's link line wraps the whole
  library group in `-Wl,--start-group ... -Wl,--end-group`
  (`unit_test_lib CppUnitLite2 network connection resmgr math cstdmf zip
  bwssl bwcrypto`), because `connection` references `network` symbols
  (`DeferredBundle`, `CompressionIStream`) while `network` references `resmgr`
  symbols (`DataSection::readInt`) — with both directions present, **no linear
  order resolves them**. The sample cannot use `$<LINK_GROUP>` (introduced only
  in CMake 3.24, and it does not accept plain targets), so it writes the raw
  link-flag items `-Wl,--start-group` / `-Wl,--end-group` into
  `target_link_libraries` (CMake passes items starting with `-` through
  literally, in position with the library items), matching make's spelling
  verbatim. `connection` still explicitly depends on `network` in CMake so the
  dependency graph stays correct.

Exclusions (checked entry by entry against the make list): `network` excludes
`net_360.cpp` (Xbox 360) and `pch.cpp`; `connection` excludes
`client_interface.cpp` (Windows/MFC client connectivity) and `pch.cpp`; the test
side excludes `pch.cpp`.

Verification (2026-10-14): configure/build passed, `ctest` all green,
`network_test` **210** cases, same caliber as make (the make-produced
`network_test` is likewise **210** cases, all green). The network cases are
heavy (about 200 s alone, including real sockets and timing behavior); the CI
job's 120-minute timeout has ample headroom. The CI matrix grew to
`[cstdmf, resmgr, network]`. The remaining entitydef depends on 14 libraries;
its Python leg (the vendored CPython has no CMake build system) is paved first
by §3.4 — see §3.5.

### 3.4 vendored CPython 3.13 sample (prerequisite for entitydef, 2026-10-14)

`cmake/samples/python/CMakeLists.txt` is the CMake track's first **non-BW-library**
sample and the common foundation of 3.1–3.3 and §3.5. Upstream CPython ships
only PCbuild (Windows) and autotools (Unix) — no CMake build system — so this
wraps the configure+make that the make chain already runs in an
`ExternalProject`, instead of hand-copying ~250 `.c` files:

- Configure options are taken verbatim from `build/make/third_party_python.mak`'s
  `pythonConfigureOpts` (`--with-openssl=` / `--with-bz2=` pointing at the vcpkg
  manifest install dir, `--with-suffix=.exe`, `--disable-test-modules`,
  `CPPFLAGS`); there is exactly one authoritative source for both — changing
  the make-side options must be synchronized with this list.
- Only `libpython3.13.a EXTRA_CFLAGS="-m64 -fPIC"` is built. Beyond archiving,
  the make chain has a sed post-processing step (the TERNAM undef in
  `pyconfig.h`, the shared extension modules' `LINKFORSHARED` whole-archive +
  `-u,BW_Py_*`) — those affect only `python.exe` and the extension modules,
  neither of which the sample builds, so the whole step is skipped.
- **`INSTALL_COMMAND` copies the stdlib**: `Py_Initialize` resolves the standard
  library against the executable prefix; with only an archive present, an
  embedded interpreter aborts outright at `encodings`. The sample copies the
  vendored `Lib/` to `<build>/bwpython-sample/python3.13/lib/python3.13`,
  equivalent to the tree the make chain's `make install` provides (entitydef_test
  does not dlopen `lib-dynload`, so shared modules are not needed).
- **`BWPython::Python` must be `IMPORTED GLOBAL`**: a CMake IMPORTED target is
  by default visible only in the directory that created it; once pulled in via
  `add_subdirectory` by the entitydef sample it would resolve to an unknown
  target — omitting GLOBAL here is a hard configure-time error.
- `check_symbols.cmake` (`-P` script mode) acts as a build gate verifying that
  the 8 BWHooks symbols
  (`BW_Py_malloc/free/calloc/realloc/getHooks/setHooks/memoryTrackingIgnoreBegin/End`)
  are still defined symbols inside the archive. They come from the archive
  member `bwhooks.o` (`Python/bwhooks.c`) — the single reason the vcpkg
  `python3` port cannot replace the vendored interpreter; once they disappear,
  the entitydef sample loses Python and the build fails.

One local command: `scripts/build-python-cmake.sh` (which additionally checks
that the archive exists and that `bwhooks.o` is inside).

### 3.5 entitydef sample (fourth bottom-up block, 2026-10-14)

`cmake/samples/entitydef/CMakeLists.txt` is the fourth sample. It reuses the
3.1–3.3 verified `CppUnitLite2` + `unit_test_lib` + `cstdmf` + `math` + `resmgr`
+ `network` + `connection` lists, pulls in 3.4 via `add_subdirectory`, and adds
8 new libraries: `physics2` (7 TUs), `moo` (2 TUs, `usePNG=1`), `terrain`
(18 TUs), `scene` (14 TUs, `pch.cpp` is in the make list so it stays), `chunk`
(34 TUs, `usePython=1`), `script` (4 TUs), `pyscript` (18 TUs), `entitydef`
(55 TUs), plus `entitydef_test` (15 TUs). One local command:
`scripts/build-entitydef-cmake.sh`.

The export surface shared by the four samples (not decoration on Linux:
`bwentity_api.hpp` forks between `template class` and `extern template class`
on these macros; getting one wrong yields duplicate instantiations or missing
symbols). The macro surface is derived per library from the `Makefile.rules`'
`exportFor`: `cstdmf` (`exportFor = cstdmf`) uses `CSTDMF_EXPORT`;
`entitydef` / `script` / `pyscript` / `network` / `connection` / `resmgr` /
`math` (`exportFor := bwentity`) use `BWENTITY_DLL_EXPORT` + `CSTDMF_IMPORT`;
`chunk` / `scene` / `terrain` / `moo` / `physics2` / `unit_test_lib` /
`CppUnitLite2` (no `exportFor`) IMPORT on both sides; the test side only
`CSTDMF_IMPORT`.

Further sample-level points:

- **The vendored `png` / `zip` trees are no longer on the build surface**:
  `third_party/Makefile.rules` recurses only into `CppUnitLite2/src`, so the
  `Makefile.rules` of `third_party/png` and `third_party/zip` are dead lists;
  `lib/moo/png.cpp` and `lib/resmgr/zip_*.cpp` actually link the make-chain
  staged `libpng.a`/`libzip.a` (vcpkg artifacts). The sample maps this
  equivalently to `find_package(PNG REQUIRED)` + `find_package(ZLIB REQUIRED)`,
  consistent with 3.2/3.3.
- **The link group spans all 14 BW libraries**: 3.3 recorded why
  `--start-group` exists (`connection` and `network` reference each other).
  entitydef's reference surface is wider — `chunk` references `entitydef`
  vtables, `pyscript` references `resmgr`/`math`, and the test surface touches
  both sides — so the whole group is wrapped in `--start-group`, with
  `BWPython::Python` + `dl`/`m`/`pthread`/`util` outside the group, the same
  shape as the `common_footer_config.mak` link line.
- `-rdynamic -Wl,--export-dynamic` (the `backtrace()` symbol names in
  `callstack.cpp` depend on it) and `-Wno-float-equal` (library and test sides
  of `pyscript` and `network`) follow 3.3's approach — measured against make,
  no "convenient unification".

Verification (2026-10-14): `scripts/build-entitydef-cmake.sh` ran configure /
build / ctest green in one command — `entitydef_test` **318 cases all green**,
same caliber as make (`bw-run-test-entitydef` likewise **318** cases, 0
failures); the make-side full gate was re-run at the same time: 21 modules /
1355 cases / 0 failures. The CI matrix grew to
`[cstdmf, resmgr, network, entitydef]`. At this point the CMake track covers
the four bottom-up chains cstdmf/resmgr/network/entitydef.


## 4. Engine source adaptations

- **libpng** (`lib/moo/png.cpp`): `png/png.h` → `<png.h>`; the private headers
  `pngstruct.h`/`pnginfo.h` deleted; `png_ptr->io_ptr` → `png_get_io_ptr()`;
  `pngInfo->width/height/bit_depth/channels` → `png_get_image_width()`/
  `png_get_image_height()`/`png_get_bit_depth()`/`png_get_channels()`. The
  custom allocation callbacks (`png_create_*_struct_2` + `bw_malloc/bw_free`)
  are public API; memory tracking preserved.
- **zlib**: 5 sites of `#include "zip/zlib.h"` → `<zlib.h>`. `MY_ZCALLOC`/
  `bw_zlib_mem.cpp` were removed together with the vendored component build:
  vcpkg zlib is the stock build, and zlib's internal allocations no longer
  count toward BW memory statistics (no functional impact; the hand-initialized
  `z_stream` sites never went through `zcalloc` anyway).
- **sqlite**: 7 sites of `#include "sqlite/sqlite3.h"` → `<sqlite3.h>`; the API
  surface (open/exec/prepare(_v2)/step/bind/column/backup) all exists in 3.53;
  on-disk format forward compatibility is guaranteed by SQLite upstream.
- **OpenSSL 3** (the low-level APIs are all deprecated-but-present; no legacy
  provider needed — `BF_set_key`/`BF_ecb_encrypt` are self-contained
  implementations, verified against RFC vectors):
  - removed the calls to `ERR_load_crypto_strings`/`ERR_free_strings`, deleted
    in 3.0 (`public_key_cipher.cpp`, `elliptic_curve_checksum_scheme.cpp`; the
    `ping_manager.cpp` site was already inside `#if 0`);
  - `endpoint.cpp` (Win32 cleanup path): a 1.0-only cleanup function gated with
    `OPENSSL_VERSION_NUMBER < 0x10100000L`;
  - `RSA_*`/`ECDSA_*`/`SHA*_Init` etc. kept as-is (deprecation warnings,
    behavior unchanged); the wire protocol unchanged.
  - **`ECDSA_sign_setup` is called only on the private-key side** (a real defect
    fix, see below).
- **jsoncpp**: no source changes (the consumed API surface is compatible).
- **memhook** (gcc15 fix): added sized `operator delete(void*, size_t)`/
  `operator delete[](void*, size_t)`, forwarding to the existing unsized hooks.
- **db_storage_mysql** (gcc15 + MySQL 8 client headers; this directory had
  never compiled on this machine before): `type_traits.hpp`'s `return NULL;`
  (`basic_string(nullptr_t)` is deleted) became `return BW::string();`;
  `database_tool_app.cpp`'s `0`-initialization of two `unique_ptr` members
  became `nullptr` (ambiguity between the `nullptr_t` and `pointer`
  constructors); `wrapper.cpp`'s `my_bool` + `MYSQL_SECURE_AUTH` gated with
  `#ifdef MYSQL_SECURE_AUTH` (MySQL 8 / MariaDB client headers removed both,
  and the new clients never supported the pre-4.1 legacy password protocol
  anyway).
- **Other corners that had never compiled on this machine** (surfaced once the
  `all` target ran end to end): `message_logger/metadata.cpp` assigning a
  `long long` to `Json::Value` is ambiguous among 1.9's several numeric
  constructors — made explicit with `static_cast<Json::Value::Int64>`;
  `sync_db/mysql_upgrade_database.cpp`'s unused definitions of
  anonymous-namespace class member functions (`-Werror=unused-function`)
  marked `[[maybe_unused]]`; `examples/client_integration/python/simple`'s
  leftover `PyString_AsString`/`PyInt_*` replaced with
  `PyUnicode_AsUTF8`/`PyLong_*`.

### 4.1 Real defects fixed along the way

After the migration swapped `third_party`'s build artifacts for vcpkg static
libraries and the unit tests actually ran, defects unrelated to dependency
versions but rendering features outright unusable came to light:

1. **The public-key (verification) path of `EllipticCurveChecksumScheme` was
   completely broken**
   (`lib/network/elliptic_curve_checksum_scheme.cpp`).
   The constructor unconditionally called `ECDSA_sign_setup()` to precompute
   the `(kinv, rp)` used for signing; that function requires the `EC_KEY` to
   carry the private-key component, so it necessarily fails for a public key
   (`missing private key`). The public-key branch therefore returned early,
   `maxSignatureSize_` stayed at 0 → `streamSize()` reported 0 →
   `ChecksumIStream` did not strip the trailing signature → verification
   always failed; `ECDSA_verify(NULL)` would even segfault outright (the crash
   stack in `baseapp_test` was exactly this). Fix: precompute only when
   `isPrivate`; `ECDSA_size()` depends only on the group and is identical for
   private/public keys, so it stays correct as the number of bytes appended/
   stripped on the wire — the wire protocol is unchanged.
2. **`watcherStreamToValue*` read one mode byte too few**
   (`lib/cstdmf/watcher.hpp`, `lib/pyscript/pywatcher.cpp`).
   `watcherValueToStream()` writes `type, mode, len, value`
   (both `WatcherProtocolDecoder::decodeNext()` and
   `watcher_forwarding_collector.cpp` read that format), but the read side
   treated the payload as starting right after `type`, taking `mode` for the
   length prefix. Result: every watcher SET operation failed to decode the
   stream; `putPyObjectOnStream()` (the Python-side writer) and the
   server-side callable-watcher reads were asymmetric. Fix: added
   `watcherStreamSkipMode()`, which consumes and discards the mode byte inside
   every read wrapper.
3. **CPython's `BW_Py_*` hook symbols were hidden inside the interpreter**
   (`third_party/python/Include/bwhooks.h`). CPython compiles its own objects
   with `-fvisibility=hidden`; `-export-dynamic` cannot export hidden symbols;
   3.12+'s `Tools/build/check_extension_modules.py` renames modules whose
   import failed to `_hashlib_failed*.so`, so `lib-dynload` ended up one module
   short and the build's module-existence assertions failed outright. Fix:
   added `__attribute__((visibility("default")))` to the `BW_Py_*` declarations
   and used `-Wl,-u,...` to force `Python/bwhooks.o` into `python.exe`.
4. **`watcherStreamToValue(…, long&)` truncated int64 on LP64**
   (`lib/cstdmf/watcher.hpp`). On LP64 platforms `int64_t` is a typedef of
   `long`; an int64 watcher value binds through overload resolution to the
   `long&` overload (a non-template exact match beats the generic template),
   which unconditionally forwards to the `int32&` extractor — every 64-bit
   watcher value was silently truncated to the low 32 bits (the unit test
   `WatcherValueToStream_int64RoundTrip` measured `-1122334455667788` reading
   back as `628319156`, exactly the low 32 bits). Fix: select the int64/int32
   extractor by `sizeof(long)`.
5. **`snapshot_helper`'s `lvremove` argument was a comma expression**
   (`server/tools/snapshot_helper/snapshot_helper.cpp`).
   `("/dev/" + lvGroup, "/" + lvSnapshot).c_str()` discards `"/dev/"+lvGroup`
   entirely and passes only `"/"+lvSnapshot` to `lvremove` — the snapshot
   volume could never be deleted. gcc 15's `[[nodiscard]]` on `operator+`
   turned this silent discard into a compile error; fixed in passing to
   `"/dev/" + lvGroup + "/" + lvSnapshot`.
6. **`Script::setData(…, int64&)` returned success while leaving an uncaught
   exception behind** (`lib/pyscript/script.cpp`). The 2.7-era fallback branch
   returned 0 unconditionally; on LP64, `long` and `int64` are the same width,
   so the branch either silently accepted out-of-range values (`2**63` read as
   `9223372036854775808`) or returned success with an uncleared `OverflowError`.
   The leaked exception was exactly why `IntegerRangeChecker::findSameRange()`
   reported `findSameRange: PyErr_Occurred` and aborted. Fix: on overflow,
   clear the error and fail as out-of-range, consistent with the `uint64`
   overload's behavior.
7. **`Script::setData(…, int&)` did no range checking at all**
   (same file). The original implementation was `rInt = asLong; if (asLong ==
   rInt) return 0;` — comparing after the assignment is necessarily true, so
   **any** `PyLong` was judged a success; the `-1` sentinel that
   `PyLong_AsLong` returns on overflow also sailed through as "success" with an
   `OverflowError` in tow. The second `PyLong_Check` branch that follows is
   dead code (the first branch always returns), which corroborates this. Fix:
   check `PyErr_Occurred()` first, and require a lossless round trip through
   `int` before accepting.
8. **`ScriptTuple::getItem` used `PyList_GET_SIZE` for the tuple bounds check**
   (`lib/script/py_script_tuple.ipp`). In 2.7 it was a bare `ob_size` field
   read that happened to work on tuples; in 3.13 it became an inline function
   asserting `PyList_Check()`, so it aborted outright
   (`ScriptArgs_createForSingleScriptObjectIsATrap`). `ScriptTuple` is always
   a `PyTuple` (`check()` is exactly `PyTuple_Check`), and the same file's
   `setItem()`/`size()` already used `PyTuple_GET_SIZE` — a slip of the pen.
9. **`MockFileProvider::FEOF()` had its semantics inverted**
   (`server/baseapp/unit_test/test_recording.cpp`). It returned 1 when data
   **remained**, while `ReplayTickLoader::loadFromFile()` asserts `FEOF()`
   nonzero after a short read (already at EOF) — exactly backwards. Fix:
   changed to `remainingLength() <= 0`.
10. **`PythonDataType::isExpression` told pickle from expression by "ends with
    `=`"** (`lib/entitydef/data_types/python_data_type.cpp`). That assumption
    holds for 2.7's cPickle, but 3.x's protocol 2 stores str as
    `BINUNICODE('X')` instead of cPickle's `SHORT_BINSTRING('U')`; the Base64
    length and padding shift accordingly, the pickle was misjudged as an
    expression, and `createFromSection` went off to `eval()` the Base64 text
    (`NameError: name 'gAJdc…' is not defined`). Fix: Base64-decode first,
    then sniff the first byte (`(` = text protocol, `0x80` = binary protocol
    PROTO opcode). A charset check alone is not enough — `"543"` is itself
    valid Base64, yet in the tests its meaning is an expression.
11. **`PyModuleMethodLink::init()` kept its `PyMethodDef` on the stack**
    (`lib/pyscript/script.cpp` + `script.hpp`). The 3.x port replaced the
    removed `Py_InitModule()` with `PyModule_AddFunctions()`, which, true to
    its name, only borrows the caller's `PyMethodDef` array: it builds a
    `builtin_function_or_method` per entry whose `m_ml` points into that
    table. The original implementation wrote `PyMethodDef defs[2] = {...}`
    (automatic storage duration); the table was gone as soon as `init()`
    returned, while those function objects live indefinitely in the module
    dict and are GC-tracked. The consequence: the closing GC in
    `Py_Finalize()` walked them, `meth_traverse` → `PyCFunction_GET_CLASS`
    read `m_ml->ml_flags` dereferencing stale stack memory → segfault, and
    every unit test using Script (`script_test` / `entitydef_test` /
    `pyscript_test`) crashed during teardown. valgrind fingered it directly:
    `Address 0x1ffefff8f1 is on thread 1's stack / 207 bytes below stack
    pointer`. Fix: promoted the table to the `PyModuleMethodLink::defs_[2]`
    member (that class is by convention used as a static/global object anyway,
    so the lifetime suffices).
12. **`PyArrayDataInstance::pyCompare()` /
    `PyFixedDictDataInstance::pyCompare()` asserted outright on operands of a
    different type** (`lib/entitydef/data_instances/array_data_instance.cpp`,
    `fixed_dict_data_instance.cpp`). The original comment claimed "Python
    guarantees both sides are of this type" — wrong: when the left operand's
    (e.g. `list` / `dict`) `__lt__` / `__eq__` returns `NotImplemented`, Python
    calls the **reflected** operation `array.__gt__(list)`, so the incoming
    left operand is not of this type at all. Writing `someArray < 5` in a
    script aborted the whole process. Fix: on a type mismatch return -1
    (ordered and unequal) and **do not set an exception**, preserving the
    existing "never equal across types" convention while letting callers (e.g.
    `ScriptObject::compareTo()`, the unit tests' conversion-fallback paths)
    continue into their own conversion logic.
13. **`ScriptObject::compareTo()` asked `Py_LT` before `Py_EQ`**
    (`lib/script/py_script_object.hpp`). `a < b` is simply undefined for plain
    types like `dict` / `None` / `set` and raises `TypeError`; and 3.13's
    `PyObject_RichCompareBool()` returns **-1** on error, which the old code
    only tested against `== 1`, so the spurious exception was swallowed as
    "greater" — and the exception stayed in place, with the result that
    comparing two equal dicts was also judged a failure (the 24 FIXED_DICT
    failures in `entitydef_test` were exactly this). Fix: ask `Py_EQ` first
    (defined for every type) and only ask `Py_LT` once known unequal; if
    genuinely incomparable, clear the `TypeError` and return "not equal".
14. **Python 2 source embedded in unit tests was never migrated**
    (`lib/pyscript/unit_test/test_py_output_writer.cpp`,
    `lib/entitydef/unit_test/test_stream.cpp`). The earlier Py2-syntax sweep
    covered only `.py` files, not Python code inside C++ string literals:
    `print 'x'` / `print >> sys.stderr, 'x'` are `SyntaxError` in Py3 (34
    failures); the long-integer `L` suffix of `9223372036854775807L` was
    removed in Py3 (12 failures); and the BinaryStream bytes expected by
    `PYTHON_1/4/5/6` are **Python 2 cPickle** protocol-2 encodings (`U` =
    SHORT_BINSTRING, `M` = INT2), whereas Py3 uses `X` = BINUNICODE and `J` =
    BININT — both lengths and content change (8 failures). Fix: rewritten to
    Py3 syntax, `L` dropped, and the expected bytes regenerated per
    `pickle.dumps(obj, protocol=2)` (the generator was reverse-validated
    against the engine using the then-passing `PYTHON_2`/`PYTHON_3`).


## 5. Verification

(For build/test results see "Verification records" at the end of this document.)

## 6. Verification records

| Date | What | Result |
|---|---|---|
| 2026-09-25 | vcpkg manifest install (x64-linux-bw) | openssl 3.6.4 / curl 8.22 / jsoncpp / zlib / libpng 1.6.58 / sqlite3 3.53.4 all succeeded |
| 2026-09-25 | Blowfish KAT (static libcrypto, no legacy provider) | passed (`32 4e d0 fe f4 13 a2 03`) |
| 2026-09-25 | added `bzip2` (needed by `_bz2`; no system bzip2 dev package on the host) | bzip2 1.0.8 installed into the vcpkg tree, CPython `--with-bz2` in effect |
| 2026-09-25 | `make -C programming bw-unit-tests` | all 22 unit-test executables compiled and linked (gcc15 `-Werror`) |
| 2026-09-25 | `make -C programming user_shouldInstallPython=1 python_install` | 59 shared modules + stdlib installed, `_hashlib` no longer renamed |
| 2026-09-25 | `make -C programming bw-run-all-unit-tests` | partially passing; `cstdmf_test` / `test_watcher.cpp` still had pre-existing failures (see §6.1) |
| 2026-09-26 | fixed the 5 real defects of §4.1 items 6–10 | `baseapp_test` went from crash to 17/17 passing; `script_test` 18/18 passing; `entitydef_test`'s PYTHON_1/2/3 turned passing |
| 2026-09-26 | standalone probe validating the embedded CPython itself | bare `Py_Initialize` → import stdlib and hold a bound method → `Py_Finalize` **clean pass** (`/tmp/opencode/pyprobe.c`), proving the §6.2 crashes were on the BigWorld side, not the interpreter side |
| 2026-09-26 | `make -C programming bw-run-all-unit-tests` | 18/21 passing; `script_test` / `entitydef_test` / `pyscript_test` still failing (see §6.2) |
| 2026-09-26 | `make -C programming all` | **first complete verification pass** (exit 0, 0 errors): the server binaries `baseapp` / `serviceapp` / `cellapp` / `process_defs` / `sync_db` etc. all linked, with `-lbwpython3.13 -lbwssl -lbwcrypto` (vcpkg static libs) and `-Wl,-export-dynamic` visible on the link lines |
| 2026-09-26 | fixed §4.1 items 11–14 (the stack-allocated `PyMethodDef` in `PyModuleMethodLink`, the two `pyCompare` cross-type asserts, `compareTo` asking `Py_LT` first, `pickler.cpp`'s `s#`→`y#`, and the Py2 code / Py2 pickle expected bytes embedded in unit tests) | **21/21 modules all green, exit code 0**; `entitydef_test` 285 cases 0 failures, `pyscript_test` 24 cases 0 failures, `script_test` 18 cases 0 failures |

### 6.1 Final state: all green

`make -C programming bw-run-all-unit-tests` — **all 21/21 modules pass, exit
code 0** (the historical failures in `cstdmf_test` / `test_watcher.cpp` are
fixed too). `make -C programming all` likewise exits 0 with 0 errors.

### 6.2 How the three closing-stage crashes were located (process record)

`script_test` / `entitydef_test` / `pyscript_test` once crashed in
`Py_Finalize()` **after the unit tests had already gone all green**:

```
Py_Finalize -> finalize_modules (_PyGC_CollectNoFail at pylifecycle.c:1758)
            -> deduce_unreachable -> subtract_refs (gc.c:464)
            -> meth_traverse (methodobject.c:248) -> segfault reading Py_TYPE(op)
```

The localization process and the directions ruled out:

- **Not the embedded CPython itself.** A standalone probe
  (`/tmp/opencode/pyprobe.c`, linking `libpython3.13.a` directly with
  `-rdynamic --export-dynamic`) ran `Py_Initialize` → import stdlib and hold a
  bound method → `while (PyGC_Collect() > 0);` → `Py_Finalize`, and **exited
  cleanly**.
- **Not the custom allocators.** Probe 2 (`/tmp/opencode/pyprobe2.c`)
  reproduced `Script::init` (`script.cpp:419-456`) verbatim —
  `PyMem_SetAllocator(PYMEM_DOMAIN_RAW)` + `PyObject_SetArenaAllocator` — and
  called the real `BW_Py_*` inside libpython, then **likewise exited cleanly**;
  the control group without allocators installed was also clean. (`Script::init`
  registers only `ignoreAllocs*`, not `mallocHook/freeHook/reallocHook`, so the
  `BW_Py_*` all forward to libc, equivalent to pymalloc's default behavior.)
- **Not `PyObjectPlus` leaking after being GC-tracked.**
  `PyTypeObjectUtil::flags()` returns `Py_TPFLAGS_DEFAULT`, without
  `Py_TPFLAGS_HAVE_GC`.
- **And not any teardown step.** `Script::fini()` was temporarily given a
  `BW_FINI_SKIP` bitmask to skip items one by one (`Pickler::finalise` /
  `runFiniTimeJobs` / `s_pOurInitTimeModules` / `Watcher::fini` / clearing
  `__main__`): **it still crashed with everything skipped**; only skipping
  `Py_Finalize` avoided the crash. The bad object was therefore produced during
  `Script::init` or during test execution — `Py_Finalize`'s GC was merely the
  first traversal to hit it.
- The crash was sensitive to heap layout (adding one `printf` line made it
  "look fixed"), so gdb's stack frames under `-O2` were untrustworthy, and
  `PYTHONMALLOC=debug` / `MALLOC_CHECK_=3` said nothing either. **The decisive
  evidence came from valgrind** (`apt-get install valgrind`): the address read
  by `PyCFunction_GET_CLASS` (`methodobject.h:61`) `is on thread 1's stack /
  207 bytes below stack pointer` — i.e. `PyCFunctionObject::m_ml` pointed at
  **stale stack memory**. Following `m_ml` led straight to §4.1 item 11's
  `PyMethodDef defs[2]`.

One related fact confirmed in passing: the `libbwpython3.13` target of
`third_party_python.mak` **does not depend on the CPython sources**, so make
never notices when the vendored sources change (measured:
`make -C programming libbwpython3.13` plainly said "Nothing to be done"). No
pitfall resulted this time (incremental `make` showed only one stale .o,
`Python/gc.c`, and the behavior was unchanged after rebuilding), but it is a
reproduction hazard beyond a clean clone; adding source dependencies to that
target is worth a follow-up.
