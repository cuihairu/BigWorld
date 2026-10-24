# Building from Source


<div class="arch-hero">

This document explains the BigWorld build entries based on the Makefiles,
CMakeLists files, and Dockerfiles currently in the repository. The focus is
on making the real source boundaries explicit, not on rewriting the old
project into a modern generic build flow.

</div>

## Build system overview

BigWorld keeps several build chains:

| Build chain | Entry file | Main targets |
|---|---|---|
| Linux Make | `programming/Makefile` -> `programming/bigworld/build/make/Makefile` | Server components, server tools, unit tests, and some third-party libraries |
| Docker | `programming/bigworld/build/docker/Dockerfile` | A pinned CentOS 7 build environment |
| CMake | `programming/bigworld/CMakeLists.txt` | Windows projects and the remote server generation path |
| Windows scripts | `programming/bigworld/build/*.bat` | Windows project generation helpers |

For Linux server source analysis, look at the Make chain first; for the
client, tools, and Windows projects, then look at the CMake chain.

## Linux server build

### Dependency baseline

The Dockerfile uses `centos:7` and installs these key dependencies:

```text
make
gcc
gcc-c++
rpm-build
mariadb-devel
python-devel
sqlite-devel
readline-devel
gdbm-devel
bzip2-devel
ncurses-devel
binutils-devel
```

This shows the current server build relies on a combination of old-style
system packages and in-tree third-party libraries. Do not assume modern
distributions' package names, compilers, or Python versions are drop-in
replacements.

### Build commands

Run from the repository root:

```sh
make -C programming
```

The example command from the Dockerfile comments:

```sh
make -s -j64 -rR -C programming
```

`-j64` is only an example; tune it to the machine's CPU, memory, and the
stability of third-party library builds.

### Common targets

The top-level Makefile comments define a few main targets:

| Target | Notes |
|---|---|
| `all` | Build the source components |
| `everything` | Mentioned only in the top-level Makefile comment (`build/make/Makefile:5`: "build absolutely everything, even non-source stuff (eg: rpms)"); there is currently **no matching rule** in `build/make/*.mak`, so `make everything` does nothing |
| `clean` | Remove intermediate files |
| `bw-run-all-unit-tests` | Run all unit tests |
| `bw-run-unit-tests` | Run unit tests, stopping early on failure |

To locate a particular server component, continue into that directory's
`Makefile.rules`.

## Output directories

The output paths in the Makefile are assembled from `BW_BUILD_PLATFORM`,
`BW_CONFIG`, and related variables.

| Type | Variable | Path form |
|---|---|---|
| Server binaries | `BW_SERVER_BIN_DIR` | `bin/server/<platform_config>/server` |
| Server tools | `BW_SERVER_TOOLS_BIN_DIR` | `bin/server/<platform_config>/tools` |
| Unit tests | `BW_UNIT_TEST_DIR` | `bin/server/<platform_config>/unit_tests` |
| Third-party artifacts | `BW_THIRD_PARTY_INSTALL_DIR` | `bin/server/<host_platform_config>/third_party` |

Under the default install directory, artifacts land in the `game/` tree.
The Dockerfile comments also note Make output usually goes to
`game/bin/server/el7/`.

## Docker build

The Dockerfile is at:

```text
programming/bigworld/build/docker/Dockerfile
```

Example image build:

```sh
docker buildx build . \
  -f programming/bigworld/build/docker/Dockerfile \
  -t bigworld-build
```

Example of entering a container to build:

```sh
docker run --rm -it \
  -u "$(id -u):$(id -g)" \
  --mount type=bind,source="${PWD}",target=/home/bigworld/host \
  --mount type=tmpfs,target=/home/bigworld/rpmbuild \
  --env HOME=/home/bigworld \
  -w /home/bigworld/host \
  bigworld-build
```

Inside the container:

```sh
make -s -j"$(nproc)" -rR -C programming
```

The top-level Makefile's Known issues comment explicitly warns that the
initial third_party build may not tolerate parallel jobs
(`build/make/Makefile:13`: "initial third party library builds may not like
parallel jobs"). Another Dockerfile comment notes the build can stop early,
confused by Python build-system output, and need a rerun
(`build/docker/Dockerfile:48`). This is a reality of the build chain and
should not be hidden in the documentation.

## CMake project boundaries

Source facts about `programming/bigworld/CMakeLists.txt`:

- `CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12)`.
- `BW_CMAKE_TARGET` must be specified, otherwise it errors out immediately.
- The ordinary path supports only Windows; on non-Windows it reports "Only Windows builds are currently supported."
- Remote server is a separate path, controlled by `BW_IS_REMOTE_ONLY` and `BW_REMOTE_PLATFORM`.
- The supported MSVC tokens are `vc9` through `vc14`.

Therefore this should not be written as:

```sh
cmake -B build
cmake --build build
```

That generic modern CMake flow does not match the current root CMakeLists
behavior. When using CMake you must first fix the target and platform — for
example a Windows project, client, tools, or remote server.

## Unit tests

Test targets are spread across each module's `unit_test/CMakeLists.txt` or
Make build rules.

Example source entries:

| Test area | Path |
|---|---|
| Networking | `programming/bigworld/lib/network/unit_test/` |
| EntityDef | `programming/bigworld/lib/entitydef/unit_test/` |
| BgTaskManager | `programming/bigworld/lib/cstdmf/unit_test/test_bgtasks.cpp` |
| BaseApp | `programming/bigworld/server/baseapp/unit_test/` |
| CellApp | `programming/bigworld/server/cellapp/unit_test/` |
| DBApp | `programming/bigworld/server/dbapp/unit_test/` |

Before running tests, confirm the corresponding target is included in the
current platform's build chain. To find test targets:

```sh
rg "BW_ADD_TEST" programming/bigworld
rg "TEST\\(" programming/bigworld/lib programming/bigworld/server
```

## Build troubleshooting order

When investigating build problems, confirm in this order:

1. Whether the current target is a Linux server, Windows client/tool, or the docs site.
2. Whether the Make chain or CMake chain is being used.
3. Whether `BW_BUILD_PLATFORM`, `BW_CONFIG`, and `BW_INSTALL_DIR` are as expected.
4. Whether third-party libraries have been built and copied to `bin/server/<platform>/third_party`.
5. Whether the target directory's `Makefile.rules` is collected by the top-level Makefile.
6. For CMake, whether `BW_CMAKE_TARGET` and the platform checks pass.
7. For a runtime failure, then check resource paths, `BWResource::init`, `BWConfig::init`, and the App `init()`.

## Related documents

- [Build and runtime system](/analysis/build-and-runtime)
- [Build, platform, and dependency governance](/architecture/build-platform-dependencies)
- [Source code guide](/analysis/source-code-guide)
