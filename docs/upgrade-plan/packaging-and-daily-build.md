# Packaging and Daily-Build Analysis

> **English** | [中文](packaging-and-daily-build.md)

<div class="arch-hero">

Three questions: is packaging RPM-only? Can Windows be supported? How should a
daily build work? Conclusions first — **packaging owes no build work, only
formats** (a full make build already produces the complete install tree),
**Windows is usable today via WSL2 while native support is a layered
project**, and **the daily build has landed as a GitHub Actions workflow**
(build + 21-module unit-test gate + tar/deb/rpm artifacts in three formats).

</div>

## Packaging status: RPM is legacy, not the ceiling

The three RPMs in the repository root (`bigworld-devel` /
`bigworld-bwmachined` / `bigworld-web-integration`, all
`14.4.1.el7.x86_64`) are upstream distribution legacy; the historical
packaging chain is `build/bw_internal/scripts/makeRPM.py` → `make rpm`, with
the accompanying `build/docker/Dockerfile` (centos:7). This chain has two
hard problems:

- CentOS 7 went EOL in 2024-06, and its gcc 4.8 cannot carry the modernized
  C++23 build standard;
- `makeRPM.py` points at the historical `src/server` layout, which no longer
  matches this repository's `game/` install tree and was not revalidated
  through the vcpkg / Python 3.13 migration.

The key fact is: **a full make build already does most of packaging** — the
install rules lay artifacts out into `game/bin/server/el7/` (about 1.4 GB:
examples / server / tools / unit_tests / third_party) and `game/res/`
(about 90 MB). The packaging layer only needs "putting it into a format":

<div class="decision-table">

| Format | How produced | Positioning |
|------|----------|------|
| tar.gz | `tar` directly archives the install tree | The floor format; extractable in any environment; `tar -C / -xzf` lands it at `/opt/bigworld` |
| .deb | `dpkg-deb` (shipped with ubuntu/debian) | Debian-family installation standard; declares libc/libstdc++/mysql client dependencies; third-party libraries are vcpkg static links |
| .rpm | `rpmbuild` (`apt install rpm` suffices; no CentOS needed) | Preserves the RPM delivery standard, aligned with historical el7 artifacts |

</div>

All three formats share the same `/opt/bigworld/{bin,res}` layout, with a
unified version standard `14.4.1+daily<UTC date>.<short git sha>` (sourced
from the `version` field of `vcpkg.json`).

## Layered Windows-support analysis

"Daily development on Windows" maps to different near- and long-term paths:

<div class="decision-table">

| Layer | Feasibility | Basis | Recommendation |
|------|--------|------|------|
| WSL2 (immediate) | ✅ usable today | During the 3.13 migration `platform_info.py` already maps Debian-family hosts without `/etc/redhat-release` to the el7 toolchain configuration; WSL2 is Ubuntu user space, and the full vcpkg dependency list (openssl/curl/jsoncpp/zlib/libpng/sqlite3/bzip2) is available | Run the complete build + 21-module unit tests directly on a Windows dev machine, same standard as CI |
| MSVC smoke (medium term) | ⚠️ needs engineering | The CMake chain and `build/*.bat` are the historical Windows project-generation surface (client / tools); the existing `.vcproj` files are VS2003-era formats; vcpkg itself is cross-platform and in place, what is missing is project-file modernization | First get the platform-independent libraries (cstdmf / math) through the MSVC compile gate, then expand outward |
| Native Windows server (long term) | ⚠️ large project | Platform mak files have no win32 configuration; `event_poller` already abstracts select/poll/epoll (Windows select has a ready landing point), but I/O, paths, signals and other platform surfaces are broad | Keep it out of the daily-build scope |

</div>

## Daily-build design (landed)

`.github/workflows/daily-build.yml` + `scripts/package-daily.sh`:

<div class="decision-table">

| Stage | Design | Key points |
|------|------|--------|
| Trigger | `schedule: 23 21 * * *` (UTC = 05:23 Beijing time daily) + `workflow_dispatch` | GitHub only runs schedule on the **default branch (main)** — it activates automatically after merging to main; the dev branch uses manual triggers |
| 48-hour rule | On schedule trigger, `git rev-list --count --since='48 hours ago' HEAD`; if 0, skip all build steps (checkout needs `fetch-depth: 0`); `workflow_dispatch` builds unconditionally | Idle days with no commits produce no packages and burn no runner; the "daily build = ship a binary update only when something changed in 48 hours" semantics live in this step |
| Environment | Pinned ubuntu-24.04 + gcc-14 (update-alternatives points g++ to the default slot) | Platform detection's Debian fallback lands it on the el7 toolchain configuration automatically; no override needed |
| System dependencies | dev packages for readline/ncurses/bz2/sqlite3/gdbm/mysqlclient/ffi etc. | `libmysqlclient-dev` is mandatory — `third_party_mysql.mak` is in the compile chain; the vendored CPython 3.13 build needs this header set |
| UDP buffers | `sysctl net.core.rmem_max=16777216` | The ConfigTest gate threshold in network_test; below it the whole module goes red |
| Third party | Clone official vcpkg and checkout the manifest's `builtin-baseline` (extracted from vcpkg.json, single source of truth) + bootstrap; the vcpkg binary cache is reused across runs via `actions/cache` (`VCPKG_DEFAULT_BINARY_CACHE`, cache key = baseline + manifest hash) | Dependency versions do not drift with upstream vcpkg (pinning mechanism in [third-party dependency switch to vcpkg](/vcpkg-migration) §1.1) |
| Gate | Full build → `bw-run-all-unit-tests` (21 modules) | Packaging runs only after both upstream steps are green |
| Artifacts | Three formats tar.gz / deb / rpm; artifact name `bigworld-daily-<version>+daily<UTC date>.<short git sha>-linux-x86_64` (read from `out/pkg-version.txt`), retained 14 days | Package filenames use the same standard (below); **no tag, no GitHub Release** (repository rule); daily artifacts expire automatically with the retention period |

</div>

The three package files are produced in one pass by
`scripts/package-daily.sh` from the make install tree, naming rules:

- tar.gz: `bigworld-14.4.1+daily20261012.<sha>-linux-x86_64.tar.gz`
- deb: `bigworld_14.4.1+daily20261012.<sha>_amd64.deb`
- rpm: `bigworld-14.4.1-1.daily20261012.<sha>.x86_64.rpm`

Local one-line equivalent: `bash scripts/package-daily.sh <out-dir>` (requires
a completed full make first; the script takes the version baseline from the
`version` field of `vcpkg.json`). **The exe gap**: native Windows build is not
migrated (R3, see the layered table above), so the daily build currently has
no exe artifact; this is registered as a blocker in the workflow and on this
page, not an omission.

### Validation standard (full chain opened on dev branch 2026-09-28)

After **five rounds of live `workflow_dispatch` iteration** the workflow's
full chain is connected (final run `36420988341`, 39m29s, every step ✓,
1113-case CI gate fully green):

<div class="decision-table">

| Round | Failure point | Root cause | Fix |
|------|--------|------|------|
| 1 | Build | The runner lacked `liblzma-dev`; `_lzma` fell out of the CPython module set, breaking `bwsentinel`'s per-module existence checks | Add `liblzma-dev` / `libgdbm-compat-dev` / `tk-dev` |
| 2 | Unit tests | `python_install` is gated by `user_shouldInstallPython=1` with no default anywhere in the build system — the stdlib was never installed, and embedded-Python tests died with `No module named 'encodings'` | The workflow injects the environment variable |
| 3 | 1 unit-test case | `test_py_traceback` hardcoded `/tmp/opencode` (the local session temp directory); on a clean machine `mkfifo ENOENT` (now `/tmp/pyscript_py_traceback_fifo.py`, `test_py_traceback.cpp:350`) | Move the test path to the `/tmp` root (`876561a4`) |
| 4 | Package.rpm | The packaging script's rpm section addressed a staging-copy level incorrectly (without rpmbuild locally that line had simply never executed) | The `bwsrc` macro points directly at the staging tree (`f74985bb`) |

</div>

Final artifacts total 2.5 GB: **tar.gz 1.4G / deb 905M / rpm 169M** (version
`14.4.1+daily20260928.f74985bb`), delivered as workflow artifacts (retained
14 days). The automatic daily schedule still requires a merge to main to take
effect; dev keeps manual dispatch.

### Relationship to the coverage work

The daily build does not change the local gate standard (full green under
`bw-run-all-unit-tests` remains the rule); it upgrades "green in a clean
environment" from a local fact to a **daily external fact**. Coverage status
is in [migration and coverage status](/upgrade-plan/migration-status), and
the batch-by-batch process is in the [full ledger](/python-313-migration).
