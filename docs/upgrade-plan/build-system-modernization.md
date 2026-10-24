# Build System Modernization


<div class="arch-hero">

BigWorld's build system mixes Make, CMake, and platform scripts, all at old
versions. This document plans the modernization path for the build system.

</div>

## Current state

### Build entries

| Entry | Platform | Status |
|------|------|------|
| `programming/Makefile` | Linux | Primary |
| `programming/bigworld/CMakeLists.txt` | Windows | Primary |
| `programming/bigworld/build/docker/Dockerfile` | Docker | Auxiliary |
| `programming/bigworld/build/*.bat` | Windows | Auxiliary |

### Version baseline

| Tool | Current version | Recommended version |
|------|----------|----------|
| CMake | 2.8.12 | 3.20+ |
| GCC | 14 (CI samples) / 15 (el7 image), C++23 in effect | Keep |
| Clang | - | 14+ (optional) |
| Make | GNU Make | Keep |
| Docker | CentOS 7 | Ubuntu 22.04 |

> C++23 is already in effect on the make main chain
> (`programming/bigworld/build/make/platform_el7.mak:6`); see the
> [C++23 migration](/cpp23-migration).

## Upgrade path

### Phase 1: CMake modernization

**Goal**: upgrade from CMake 2.8 to 3.20+

**Main changes**:

1. **Minimum version requirement**
```cmake
# Old
cmake_minimum_required(VERSION 2.8.12)

# New
cmake_minimum_required(VERSION 3.20)
```

2. **Target properties**
```cmake
# Old
include_directories(${CMAKE_SOURCE_DIR}/lib)
add_executable(bigworld main.cpp)
target_link_libraries(bigworld network)

# New
add_executable(bigworld main.cpp)
target_include_directories(bigworld PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(bigworld PRIVATE network)
```

3. **Generator expressions**
```cmake
# Old
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

# New
target_compile_definitions(bigworld PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)
```

### Phase 2: platform baseline update

**Goal**: upgrade from CentOS 7 to modern Linux

**New Dockerfile**:

```dockerfile
# Old
FROM centos:7

# New
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    python3-dev \
    libssl-dev \
    libmysqlclient-dev \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*
```

**Dependency changes**:

| CentOS 7 package | Ubuntu 22.04 package |
|-------------|-----------------|
| python-devel | python3-dev |
| mariadb-devel | libmysqlclient-dev |
| sqlite-devel | libsqlite3-dev |
| openssl-devel | libssl-dev |

### Phase 3: compiler upgrade

**Goal**: support C++17/20 (this phase's goal has been met and exceeded:
the make main chain has switched to C++23, see `platform_el7.mak:6` and the
[C++23 migration](/cpp23-migration); the configuration below is kept as an
archive of the upgrade path)

**CMake configuration**:

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

**Compiler flags**:

```cmake
# Warning level
add_compile_options(-Wall -Wextra -Wpedantic)

# Security checks
add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
```

## CI/CD integration

### GitHub Actions (landed 2026-10-12)

What actually takes effect is two build workflows (plus the
`deploy-docs.yml` documentation deployment):

- `.github/workflows/ci.yml` — the **push/PR gate**: clone of the vcpkg
  manifest's pinned baseline + binary cache (key = baseline + manifest
  hash) → configure/build the CMake sample matrix
  (`[cstdmf, resmgr, network, entitydef]`, `fail-fast: false`;
  [vcpkg-migration §3.1–§3.5](/vcpkg-migration)) → ctest. Steps are named,
  and on failure an artifact with the configure/build/ctest logs is
  uploaded, so failures can be located by step.
- `.github/workflows/daily-build.yml` — the **daily build**: 48-hour
  freshness check → full make + the 21-module unit-test gate →
  tar.gz/deb/rpm packaging in three formats, delivered as artifacts. See
  [packaging and daily build](/upgrade-plan/packaging-and-daily-build).

Local equivalents: one command per CMake sample
(`scripts/build-cstdmf-cmake.sh` / `scripts/build-resmgr-cmake.sh` /
`scripts/build-network-cmake.sh` / `scripts/build-entitydef-cmake.sh`; the
vendored CPython leg is built separately by
`scripts/build-python-cmake.sh` and pulled in automatically by the entitydef
sample); packaging is one command, `bash scripts/package-daily.sh <out-dir>`.

### Docker build

```bash
# Build the image (path relative to the repository root; the Dockerfile
# header comment uses the same form)
docker buildx build -t bigworld-build \
  -f programming/bigworld/build/docker/Dockerfile .

# Run the build
docker run --rm -v $(pwd):/src bigworld-build \
    cmake -B build -DCMAKE_BUILD_TYPE=Release
docker run --rm -v $(pwd):/src bigworld-build \
    cmake --build build -j$(nproc)
```

## Migration steps

### Step 1: CMake upgrade

1. Update `cmake_minimum_required`
2. Replace deprecated commands
3. Add modern target properties
4. Test all platforms

### Step 2: platform upgrade

1. Create a new Dockerfile
2. Update dependency package names
3. Test the build
4. Update documentation

### Step 3: compiler upgrade

1. Set the C++ standard
2. Fix compile warnings
3. Enable security checks
4. Performance testing

## Risks and mitigations

| Risk | Impact | Mitigation |
|------|------|----------|
| Deprecated CMake commands | Build failure | Replace step by step, keep compatibility |
| Dependency package-name changes | Installation failure | Documentation updates, script adaptation |
| More compiler warnings | Build noise | Fix gradually, enable in phases |
| Performance regression | Slower runtime | Benchmarking, rollback mechanism |

## Validation checklist

- [ ] Linux build passes
- [ ] Windows build passes
- [ ] Docker build passes
- [ ] Unit tests pass
- [ ] Integration tests pass
- [ ] No performance regression
- [ ] Documentation updated

## References

- [CMake documentation](https://cmake.org/cmake/help/latest/)
- [GCC documentation](https://gcc.gnu.org/onlinedocs/)
- [Build and runtime system](/getting-started/build-from-source)
