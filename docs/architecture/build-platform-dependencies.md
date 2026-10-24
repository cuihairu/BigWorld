# Build, Platform, and Dependency Governance


<div class="arch-hero">

BigWorld's build constraints are not confined to one dependency version. They
span the build system, platform macros, third-party dependencies, Python
embedding, the CMake/Makefile split, coexistence of server/client/tool chains,
and optional middleware switches inherited from a commercial engine.

</div>

## Key Takeaways

The build system reflects a large historical cross-platform game engine:

- The top-level CMake still uses the `CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )` style.
- Many `Makefile.rules` files remain, so this is not a single modern-CMake system.
- `BW_PYTHON_AS_SOURCE` is on by default, allowing Python to be built from source into the project.
- Numerous feature switches exist: PCH, FMOD, Scaleform, SpeedTree, Umbra, and Awesomium.
- The network library requires OpenSSL through `FIND_PACKAGE( BWOpenSSL REQUIRED )`.
- Unit tests are registered with `BW_ADD_TEST(...)`.
- Server, client, tools, and third-party libraries are orchestrated in one large project.

Key source code:

- Top-level feature switches: CMakeLists.txt.
- Library and executable traversal: CMakeLists.txt.
- Remote-only `BUILD_SERVER` target: CMakeLists.txt.
- Network encryption source list: lib/network/CMakeLists.txt.
- Network OpenSSL dependency: lib/network/CMakeLists.txt.
- Conditional `pyscript` dependency on `libpython`: lib/pyscript/CMakeLists.txt.
- `network_unit_test` registration: lib/network/unit_test/CMakeLists.txt.

## Build Layers

<MermaidDiagram title="BigWorld build layers">
flowchart TD
  A[programming/bigworld/CMakeLists.txt] --> B[BWConfiguration target]
  B --> C[Feature flags]
  C --> D[Libraries]
  C --> E[Executables]
  D --> F[cstdmf network entitydef pyscript server]
  E --> G[BaseApp CellApp DBApp LoginApp Tools Client]
  D --> H[Third party libs]
  H --> I[OpenSSL zip sqlite libpython]
  A --> J[Unit Tests]
  A --> K[Makefile.rules legacy path]
</MermaidDiagram>

The key point is that BigWorld is not a Python project. It is a large C++
project with Python embedded in it.

Replacing or upgrading the embedded Python runtime must therefore address:

- the C++ compiler and ABI;
- Python source/library construction;
- the `pyscript` and `script` libraries;
- third-party Python packages;
- server and tool binaries;
- resource paths and runtime deployment.

## Feature Switches

Top-level CMake defaults include:

- `BW_PYTHON_AS_SOURCE ON`
- `BW_PCH_SUPPORT ON`
- `USE_MEMHOOK OFF`
- `BW_FMOD_SUPPORT OFF`
- `BW_SCALEFORM_SUPPORT OFF`
- `BW_SPEEDTREE_SUPPORT OFF`
- `BW_UMBRA_SUPPORT OFF`
- `BW_AWESOMIUM_SUPPORT OFF`

Source: CMakeLists.txt.

This reflects a typical commercial game-engine layout:

- one source tree served different products and licensed components;
- some middleware was not necessarily open source or available;
- build output depended on target configuration;
- documentation must state the actually buildable scope of this repository rather than assume every feature can be enabled.

## CMake and Makefile Dual Track

The repository contains many `Makefile.rules` files:

- `programming/bigworld/Makefile.rules`
- `programming/bigworld/lib/Makefile.rules`
- `programming/bigworld/server/Makefile.rules`
- `programming/bigworld/lib/network/Makefile.rules`
- `programming/bigworld/server/baseapp/Makefile.rules`
- plus rules under third-party and tool directories.

The build history is not a single modern CMake line. Makefiles, CMake, platform
configuration, and generated IDE projects coexist.

Build risks:

- modifying only CMake can miss a Makefile path;
- a successful Linux server build does not prove the tool chain or client works;
- build macros affect conditional compilation, so analysis must state the target platform;
- CI should first choose a minimal target rather than try to cover every historical platform at once.

## Python Embedding

`BW_PYTHON_AS_SOURCE` is enabled by default. Under that switch, `pyscript` links
`libpython`:

```cmake
IF(BW_PYTHON_AS_SOURCE)
    BW_TARGET_LINK_LIBRARIES( pyscript INTERFACE libpython )
ENDIF()
```

Source: lib/pyscript/CMakeLists.txt.

Design implications:

- Python is not an external script-interpreter process.
- The Python runtime is part of the C++ process.
- Upgrading Python affects compilation, linking, initialization, thread state, module loading, object lifetime, and ABI.

The embedded runtime is not an isolated dependency. It cuts through build,
scripting, hot reload, threading, and EntityDef concerns, and cannot be treated
as an ordinary interpreter replacement.

## OpenSSL and Network Dependencies

The network library source list includes:

- `elliptic_curve_checksum_scheme.cpp`
- `encryption_filter.cpp`
- `encryption_stream_filter.cpp`

Source: lib/network/CMakeLists.txt.

The network library also requires:

```cmake
FIND_PACKAGE( BWOpenSSL REQUIRED )
ADD_DEFINITIONS( -DUSE_OPENSSL )
BW_TARGET_LINK_LIBRARIES( network INTERFACE ${BWOPENSSL_LIBRARIES} )
```

Source: lib/network/CMakeLists.txt.

Source boundaries:

- An OpenSSL version upgrade may affect API, ABI, and algorithm availability.
- Encryption code cannot be judged secure merely because it compiles.
- Compatibility between system OpenSSL and legacy code requires separate validation.
- Retaining an old OpenSSL creates security-maintenance debt.

## Unit-Test Integration

The CMake definition for `network_unit_test` shows the integration pattern:

- `BW_ADD_EXECUTABLE( network_unit_test ... )`
- link `cstdmf`, `network`, and `unit_test_lib`
- `BW_ADD_TEST( network_unit_test )`

Source: lib/network/unit_test/CMakeLists.txt.

The BaseApp unit test is registered similarly with
`BW_ADD_TEST( baseapp_unit_test )`:
server/baseapp/unit_test/CMakeLists.txt.

Consequences:

- test infrastructure already exists;
- modern CI can begin with existing `BW_ADD_TEST` targets;
- the test framework should not be rewritten first;
- legacy tests should be made runnable in the modern build before new tests are added.

## Remote Only and the Server Target

When `BW_IS_REMOTE_ONLY` is set, top-level CMake creates a `BUILD_SERVER`
target and uses `_NON_EXISTANT_FILE.cpp` as a generated-source placeholder:
CMakeLists.txt.

This style shows that the old build system served:

- Visual Studio solution generation;
- remote server builds;
- IDE project organization;
- command-line server targets.

The system cannot be understood as a pure Linux CMake project. First decide:

- whether Windows clients/tools remain supported;
- whether Linux server is the initial support target;
- whether IDE solution generation remains required;
- whether server construction should be separated from the large project.

## Source Trade-Offs

The trade-offs come from one project covering server, client, tools, and third-party code:

- A commercial engine needed one source tree spanning client, server, tools, and licensed middleware.
- Bundled Python source improved reproducibility and avoided dependence on host system Python.
- Retained `Makefile.rules` likely preserved historical Linux and internal-build compatibility.
- Feature switches enabled licensed/platform-specific tailoring.
- CMake, Makefiles, third-party code, and platform macros jointly determine the real build graph.

Source-level costs:

- dependency and macro conditions are complex;
- multiple build paths make documentation drift likely;
- third-party security upgrades are difficult;
- compiler, CMake, Python, OpenSSL, and other foundation changes expose compatibility issues together.

## Source-Code Verification Focus

Build and dependency verification should center on reproducibility:

- Record OS, compiler, CMake, Python, OpenSSL, and third-party versions in the minimal server baseline.
- Repeatedly build key server libraries and existing unit tests under the same configuration.
- Classify dependencies as vendored, system-provided, deprecated, or security-sensitive.
- If CMake and `Makefile.rules` paths both remain, document output differences.
- Optional dependencies such as embedded Python, OpenSSL, MySQL, MongoDB, and Scaleform should expose clear switches and failures.
- A server-only build should not be unintentionally blocked by legacy client/tool dependencies.

## Explicit Risks

- Upgrading CMake without running legacy tests.
- Deleting all `Makefile.rules` in one change.
- Directly linking system Python without validating embedded-runtime ABI.
- Upgrading OpenSSL without auditing encryption code.
- Introducing a large package manager without pinning a lockfile.
- Upgrading compiler, CMake, Python, OpenSSL, the network model, and the thread model simultaneously.

Each of these stacks changes and makes the source of regressions difficult to
isolate.

## Chapter Boundary

This chapter explains build and dependency governance. Later source analysis
should return to specific dependencies, macro switches, platform paths, and test
baselines rather than generalize the issue as “upgrade the toolchain.”

## Related Documentation

- Build entry points and runtime order: [Build and runtime system](/analysis/build-and-runtime).
- Directory and module responsibilities: [Directories and modules](/analysis/modules).
- Source-risk verification checklist: [Source risk boundaries](/analysis/risks).
- Process discovery and control plane: [machined control plane and process discovery](/architecture/machined-control-plane).
