---
layout: doc
---

# Getting Started

<div class="arch-hero">

This chapter helps you quickly locate the entry points for reading the source
and running builds. The open-source BigWorld build chain is old, so the
content below describes the current repository source rather than recasting
it as a modern generic CMake project.

</div>

## What to read first

If your goal is source analysis, enter in this order:

1. [Source code guide](/analysis/source-code-guide): a reading map covering process entry points, networking, EntityDef, DB, client-facing communication, and offload.
2. [Overall picture](/analysis/overview): the source main line and the runtime closed loop.
3. [Architecture research method](/architecture/research-method): distinguishing source facts, inferences, and open questions.
4. [Process topology and responsibility split](/architecture/process-topology): pinning down BaseApp, CellApp, DBApp, LoginApp, and Manager responsibilities.

Do not start with `third_party`, the old PDFs, or the upgrade plans — it is
easy to mistake peripheral dependency issues for the architectural main line.

## Environment baseline

The Linux build path in the current source clearly takes the CentOS/RHEL
family as its main baseline; the Dockerfile also uses `centos:7`.

| Type | Source evidence | Notes |
|---|---|---|
| Linux server | `programming/Makefile`, `programming/bigworld/build/make/Makefile` | Make is the main server entry |
| Docker build environment | `programming/bigworld/build/docker/Dockerfile` | Based on `centos:7`; installs `gcc-c++`, `mariadb-devel`, `python-devel`, etc. |
| Windows projects | `programming/bigworld/CMakeLists.txt` | The CMake entry requires `BW_CMAKE_TARGET`; the ordinary path supports only Windows or remote server |
| CMake version | `CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12)` | Not a CMake 3.x-style project |

You can read the source and generate these docs on a newer machine, but
validating the old server build should prefer the repository's
Docker/CentOS path.

## Getting the source

```sh
git clone <repo-url> BigWorld
cd BigWorld
```

If the repository contains submodules, also run:

```sh
git submodule update --init --recursive
```

## Building the docs site

This site is VitePress documentation and is separate from building the
BigWorld engine binaries.

```sh
npm install
npm run docs:build
```

A successful docs build only proves the site can be generated; it does not
mean the BigWorld server or client binaries build.

## Linux server build entry

Source entry:

```sh
make -C programming
```

The recommended form from the Dockerfile comments:

```sh
make -s -j64 -rR -C programming
```

Unit-test entry:

```sh
make -s -j64 -rR -C programming bw-run-all-unit-tests
```

Adjust `-j` to the actual machine's resources. The Dockerfile also notes
that the first third-party library build and the Python build output can
make the process need a rerun; this is a build reality of the current source tree.

## Build artifact locations

The server output paths in the Makefile are assembled from the platform and
configuration:

```text
game/bin/server/<platform_config>/server
game/bin/server/<platform_config>/tools
game/bin/server/<platform_config>/unit_tests
```

The Dockerfile comments explicitly state that server output usually lands in:

```text
game/bin/server/el7/
```

Intermediate artifacts live at:

```text
programming/bigworld/build/<platform>
```

When analyzing runtime problems, check binary locations, resource paths, and
the configuration loading path together.

## Server startup order

Bringing up a full cluster depends on registration and discovery across
machined, the Managers, DBApp, BaseApp, CellApp, and LoginApp. You cannot
judge architectural behavior by starting a single process.

Source reading order:

1. `server/<app>/main.cpp`.
2. `BIGWORLD_MAIN`, `bwMainT`, and `doBWMainT` in `lib/server/bwservice.hpp`.
3. The concrete `App::init()`.
4. `Interface::registerWithInterface()`.
5. `EventDispatcher::processOnce()`.

Runtime-chain detail is in [Build and runtime system](/analysis/build-and-runtime)
and [machined control plane and process discovery](/architecture/machined-control-plane).

## Next steps

- [Project structure](/getting-started/project-structure): directories and module responsibilities.
- [Building from source](/getting-started/build-from-source): the real boundaries of Make, CMake, and Docker.
- [Source code guide](/analysis/source-code-guide): reading the source along call chains.
- [Architecture research method](/architecture/research-method): a shared evidence standard.
