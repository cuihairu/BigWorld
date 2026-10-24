# Build and Runtime System


## Build entries

BigWorld keeps Make, vcpkg, CMake, batch scripts, and container scripts at
once. In source analysis do not treat these entries as equivalent; they
serve different platforms and targets.

| Entry | Representative path | Main use |
|---|---|---|
| Make | `programming/Makefile`, `programming/bigworld/build/make/Makefile` | Linux server main build chain (modernized to C++23), including embedded CPython 3.13 and the 21-module unit tests |
| vcpkg | The manifest `programming/bigworld/vcpkg.json`; at build time set `VCPKG_ROOT` to an official vcpkg checkout | Third-party dependencies (openssl/curl/jsoncpp/zlib etc.) materialize automatically during make |
| CMake | `programming/bigworld/CMakeLists.txt`, `programming/bigworld/build/cmake/` | Windows/IDE project generation and cross-platform project description |
| Batch scripts | `programming/bigworld/build/*.bat` | Windows build helpers |
| Docker | `programming/bigworld/build/docker/Dockerfile` | Legacy entry — centos:7 went EOL in 2024-06 and its gcc 4.8 cannot carry C++23; clean build environments are now provided by the daily-build workflow |
| GitHub Actions | `.github/workflows/daily-build.yml` + `scripts/package-daily.sh` | ubuntu-24.04 + gcc-14: full build → 21-module unit-test gate → tar/deb/rpm artifacts (see [packaging and daily build](/upgrade-plan/packaging-and-daily-build)) |

The build system itself is not the architectural core, but it decides which
server processes, libraries, and tools get compiled into the same runtime
closed loop.

## Linux server build chain

The Linux build main entry is:

```sh
make -C programming
```

This enters `programming/bigworld/build/make/Makefile`, which organizes the
build by platform configuration, target type, and per-module Makefiles. The
modernized standard is C++23 with a gcc-14-class compiler; a first full
build requires `VCPKG_ROOT` pointing at an official vcpkg checkout, and
manifest mode materializes third-party dependencies automatically during
make.

Key source and configuration:

| File | Role |
|---|---|
| `programming/Makefile` | Top-level forwarding entry |
| `programming/bigworld/build/make/Makefile` | BigWorld Make build master control |
| `programming/bigworld/build/make/platform_*.mak` | Platform differences and compile parameters |
| `programming/bigworld/build/make/third_party_python.mak` | Embedded CPython 3.13 build, stdlib install, and shared-module sentinel |
| `programming/bigworld/server/*/Makefile.rules` | Server process output directories and source-file rules |
| `programming/bigworld/lib/*/Makefile.rules` | Shared-library build rules |

Server binaries are output to `game/bin/server/el7/`. Platform detection
(`platform_info.py`) also maps Debian-family hosts without
`/etc/redhat-release` to the `el7` toolchain configuration — `el7` is a
historical name and does not express a runtime requirement.

The embedded Python build has two easy-to-miss trapdoors:

- `python_install` (installing the CPython stdlib into `game/`) is gated by
  `user_shouldInstallPython=1`, with no default anywhere in the build
  system — unset it and the stdlib is not installed, so embedded-Python
  programs die at startup with `No module named 'encodings'`;
- `bwsentinel` runs existence checks against about 60 Python shared
  extensions one by one: when system dev headers (such as `liblzma-dev`)
  are missing, CPython silently skips modules without erroring, and the
  sentinel provides the backstop assertion.

When analyzing runtime problems from the source, look at build artifact
locations, resource paths, and the configuration-file loading path together.

## CMake project chain

`programming/bigworld/CMakeLists.txt` is the CMake entry and, together with
the configuration scripts under `build/cmake/`, generates projects. It is
valuable for understanding how Windows tools, the client, and some libraries
are organized.

The point is not the CMake version but how the project connects modules:

| Concern | Source or configuration |
|---|---|
| Library and target organization | `programming/bigworld/CMakeLists.txt` |
| Platform configuration | `programming/bigworld/build/cmake/` |
| Unit-test targets | `BW_ADD_TEST` in each module's `unit_test/CMakeLists.txt` |
| Server configuration references | `BWConfiguration_server.cmake` |

If CMake references scripts or paths that do not exist in the repository,
record it as drift between the source tree and project configuration rather
than inferring runtime behavior directly.

## Server process startup model

The server runtime entry is not scattered handwritten `main()` logic but a
unified macro entry:

```
server/<app>/main.cpp
  -> BIGWORLD_MAIN
  -> bwMainT<App>
  -> doBWMainT
  -> ServerApp::runApp
  -> App::init
  -> ServerApp::run
  -> EventDispatcher::processUntilBreak
```

Key files:

| File | Role |
|---|---|
| `lib/server/bwservice.hpp` | Defines `BIGWORLD_MAIN`, `bwMainT`, `doBWMainT` |
| `lib/server/server_app.cpp` | `runApp`, `run`, `advanceTime` |
| `lib/network/event_dispatcher.cpp` | Timer, network-event, and frequent-task dispatch |
| `server/baseapp/main.cpp` | BaseApp/ServiceApp type selection |
| `server/dbapp/main.cpp` etc. | Concrete App type binding |

So when investigating "why didn't the service start", trace
`main.cpp -> bwservice.hpp -> App::init()` instead of only reading the entry
file in the process directory.

## Configuration and resource loading

After `BIGWORLD_MAIN` expands, it initializes the resource and configuration
systems. Each server process then reads its own configuration in `App::init()`,
binds its network interface, registers Watchers, and starts background tasks.

Common chain:

```
BWResource::init
  -> BWConfig::init
  -> bwParseCommandLine
  -> ServerAppConfig::init
  -> App::init
```

When analyzing configuration problems from the source, distinguish three
input classes:

| Input | Representative location | Effect |
|---|---|---|
| Command-line arguments | `bwParseCommandLine` | Process startup parameters and resource paths |
| Global configuration | `BWConfig` | Network, tick, and service parameters |
| Resource tree | `BWResource` | Scripts, EntityDef, standard and tool resources |

EntityDef, scripts, and some third-party runtime content all participate in
loading through resource paths, so "it compiles" does not mean "the server
process can initialize".

## Runtime process categories

The runtime can be divided into four categories:

| Category | Representative processes or tools | Source focus |
|---|---|---|
| MMO server processes | `baseapp`, `cellapp`, `dbapp`, `loginapp` | Mercury interfaces, event loop, entity state |
| Manager processes | `baseappmgr`, `cellappmgr`, `dbappmgr` | Process registration, load, hash, space partitioning |
| Control and observability tools | `bwmachined`, `message_logger`, Watcher tools | Process discovery, logs, runtime control |
| Offline tools and editors | `worldeditor`, `assetprocessor`, `navgen` | Asset production, scene data, client content |

Do not only verify that a single binary starts. BigWorld runtime correctness
depends on the registration and message chains closing among LoginApp,
BaseAppMgr, CellAppMgr, DBAppMgr, BaseApp, CellApp, and DBApp.

## Test entries

Several classes of test entries exist in the source:

| Area | Representative path | Use |
|---|---|---|
| Network tests | `lib/network/unit_test/` | Channel, reliability, fragmentation, windows, TCP/UDP behavior |
| EntityDef tests | `lib/entitydef/unit_test/` | DataType, stream size, property changes, serialization |
| Server tests | `server/baseapp/unit_test/`, `server/cellapp/unit_test/` | Partial BaseApp/CellApp behavior |
| DB tests | `server/dbapp/unit_test/`, `lib/db/unit_test/` | DB interface and some DB behavior |
| General test framework | `lib/unit_test_lib/` | Test macros, multi-process test helpers |

The full-gate entry is the make target (requires `VCPKG_ROOT`):

```sh
env VCPKG_ROOT=<vcpkg> make -C programming bw-run-all-unit-tests
```

The current baseline is **21 modules and 1113 cases**; binaries sit under
`game/bin/server/el7/unit_tests/` and are run by full path from each
module's source `unit_test/` directory; the daily-build workflow executes
the same standard in a clean environment.

When verifying an architectural judgment, look for the corresponding unit
test first. If no test exists, mark it in documentation as "source
inference" rather than "verified behavior".

## Build and runtime troubleshooting order

When investigating runtime problems, use this order:

1. Confirm whether the target is a server process, a tool, the client, or an editor.
2. Find the corresponding `main.cpp` and the App type bound by `BIGWORLD_MAIN`.
3. Enter `App::init()` and check network interface, configuration, EntityDef, DB, and Watcher registration.
4. Check whether the interface calls `registerWithInterface()`.
5. Trace callers with `startMessage()` / `startRequest()`.
6. If entity state is involved, check the EntityDef data domains and serialization streams.
7. If persistence is involved, check from `Base::writeToDB()` to `IDatabase::putEntity()`.

This order avoids mixing build problems, configuration problems, network
registration problems, and business handler problems together.
