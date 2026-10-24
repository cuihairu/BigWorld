# Python 3.12 Roadmap


::: tip Status update (2026-09-28)
This roadmap was written before the migration started and is kept as a
decision record. The actual migration has since been completed and merged
into dev: the embedded interpreter switched from Python 2.7 to **Python
3.13** (the version was raised from the planned 3.12 along the dependency
chain). The batch-by-batch process and acceptance are in the [full Python
3.13 migration ledger](/python-313-migration), and a current-state overview
is in [migration and coverage status](/upgrade-plan/migration-status).
2026-10-13 audit addendum: in the end **no** unified compatibility wrapper
layer was built; point-by-point direct replacement was used instead (see the
[python-c-api-guide status note](/upgrade-plan/python-c-api-guide)).
:::

## Definition of the goal

In the BigWorld context, "switching to a more modern Python 3.12" comprises
at least four things:

1. Migrating the embedded interpreter from Python 2.7 to Python 3.12
2. Completing the Python 3 API adaptation of the C/C++ ↔ Python bridge layer
3. Replacing or rebuilding the project's own shared modules and third-party Python packages
4. Synchronizing adjustments to build, packaging, deployment, and the runtime resource layout

Completing only item 1 does not count as a successful migration.

## Conclusion first

Switching straight to Python 3.12 on the current main line in one shot is not
recommended. A "four-phase migration strategy" is recommended.

## Phase 0: establish a trustworthy baseline

The goal is to answer one question: whether the current repository can
actually build and run stably.

### Required work

- Verify whether the Linux server builds completely under the current documentation assumptions.
- Verify the minimal startup chain of the key server processes.
- Verify minimal compilability of the client/tool chains.
- Fill in missing scripts and drifted paths, for example the real source of `control_cluster.py`.
- Fix a "current-state build manual" and a "current-state dependency list".

### Outputs

- Reproducible build steps
- An executable smoke test
- A list of current Python coupling points

Without this step, the later migration is uncontrollable.

## Phase 1: first upgrade to a Python 3-compatible abstraction layer

The goal is not to plug in 3.12 directly, but first to detach the code from
Python-2-specific APIs.

### Core actions

- Build a unified compatibility wrapper layer in `lib/pyscript` and `lib/script`.
- Funnel the scattered `PyString_*`, `PyInt_*`, and `Py_InitModule` calls into local adaptation interfaces.
- Handle uniformly:
  - strings
  - bytes
  - integers
  - module initialization
  - exception formatting
  - `pickle/cPickle` behavior

### Principles

- `KISS`
  Funnel the APIs first; do not start by rewriting the whole script system.
- `DRY`
  Do not write an independent Python 2/3 branch in every submodule.
- `YAGNI`
  Abstract only the APIs actually used today; do not pre-build a "comprehensive" binding framework.

### Expected result

- The business layer no longer depends directly on a large surface of Python-2-specific symbols
- Local replacement conditions are created for the later interpreter-version switch

## Phase 2: migrate the embedded runtime and stdlib layout

### Core actions

- Replace the sourcing strategy for `third_party/python`
- Redesign the build logic of `third_party_python.mak`
- Reconfirm the following directory conventions:
  - `Lib`
  - `lib-dynload-*`
  - `site-packages`
- Adjust how dynamic modules are packaged

### Key blockers

- Python 3.12's build process differs significantly from Python 2.7
- Old OpenSSL/system dependencies may no longer fit
- The old `configure + make` custom patches must be re-evaluated

### Recommendation

Evaluate a platform upgrade in parallel in this phase, prioritizing:

- New Rocky / Alma / Ubuntu LTS build environments
- An updated CMake baseline
- An updated compiler baseline

If you stay locked to CentOS 7, migration costs are amplified significantly.

## Phase 3: migrate Python business scripts and third-party packages

### The Python scripts themselves

The number of first-party `.py` files is not large, so in theory this is a
lower-cost layer, but it still needs checking for:

- `print` syntax
- `except Exception, e`
- `iteritems`
- `xrange`
- `unicode/str/bytes`
- `cPickle`

### Third-party packages

The currently packaged old dependencies cannot be carried directly into
Python 3.12; handle them with "replace first":

- `oursql`
  Prefer replacing with a modern MySQL driver rather than continuing to patch.
- `SQLAlchemy 0.6.6`
  If it must be kept, assess the API-change cost; more realistically, isolate the old calls and upgrade to a modern version.
- `Pympler`, `pika`
  Upgrade or remove depending on the actual usage surface.

### Principles

- First prove "which dependencies are still really used"
- Then decide "upgrade, replace, or delete"

This follows `YAGNI` and avoids paying migration costs for dead code.

## Phase 4: end-to-end validation and dual-track switch

### Validation scope

- Server-process startup
- Entity-script loading
- Database interaction
- Logging and messaging systems
- Editor script functionality
- Client script calls

### Recommended strategy

- Keep the Python 2.7 baseline branch
- Create a new Python 3.12 migration branch
- Build a minimal regression list
- Use dual-track validation rather than overwriting in place

## Migration priority recommendations

### P0

- Build the Python C API compatibility layer
- Survey `lib/pyscript` / `lib/script`
- Identify missing scripts and broken build paths

### P1

- Redesign the embedded Python build chain
- Replace expired third-party Python packages
- Add automated smoke tests

### P2

- Platform-baseline upgrade
- Toolchain upgrade
- Clean up historical compatibility residue

## Approaches not recommended

- Global `PyString_` → `PyUnicode_` replacement
- Introducing Python 3.12 without baseline validation
- Forcing every old third-party package onto 3.12 by patching
- Switching the server side first while ignoring the editors and client

These look fast short-term but are guaranteed rework long-term.

## Recommended near-term actions

If the migration is actually to start next, do three things first:

1. Build an analysis list outside a `python-modernization` branch, but do not change business logic immediately.
2. Run a dedicated interface survey of `lib/pyscript` and `lib/script`, producing a list of Python-2-specific APIs.
3. Add a minimal build-and-startup validation script set inside the current repository, making the current state testable first.

## Final judgment

Switching BigWorld to Python 3.12 is feasible, but in essence it is an
"embedded scripting runtime modernization project", not a simple version
upgrade.

If advanced in the correct order:

- baseline first
- then abstraction
- then replace the runtime
- finally migrate scripts and dependencies

the work is controllable.

Skipping the first two steps and upgrading the interpreter directly has a
high failure probability.

---

## Important update: Python 3.13 free-threaded mode (no-GIL)

### Overview

Python 3.13 (released October 2024) introduced **free-threaded mode**
(PEP 703), also known as no-GIL. This is an experimental feature that could
fundamentally change BigWorld's threading model.

### Key features

| Feature | Notes |
|------|------|
| Build option | `--disable-gil`, or use `python3.13t` |
| Status | Experimental, not enabled by default |
| Performance cost | ~5-10% single-threaded |
| C-extension compatibility | Requires recompilation and testing |
| Memory management | Thread-safe reference counting |
| Internal data structures | Thread-safe |

### Impact on BigWorld

#### Threading-model transformation

BigWorld's current threading model:
```
Main thread (Reactor + all script logic)
├── Background thread 1 (database)
├── Background thread 2 (file I/O)
└── Background thread 3 (scheduled tasks)
```

A possible model after Python 3.13 no-GIL:
```
Main thread (Reactor + networking)
├── Script thread pool (parallel entity logic)
├── Background thread 1 (database)
├── Background thread 2 (file I/O)
└── Background thread 3 (scheduled tasks)
```

#### Performance-gain potential

- **BaseApp**: could process multiple players' script logic in parallel
- **CellApp**: could process entity updates for different Cells in parallel
- **Throughput**: theoretically N-fold (N = number of CPU cores)

### Migration path recommendations

#### Phase 1: compatibility verification (current)

```bash
# 1. Test the standard Python 3.13 build
python3.13 --version

# 2. Test the free-threaded build
python3.13t --version

# 3. Run the existing tests
python3.13t -m pytest tests/
```

#### Phase 2: C-extension adaptation

```cpp
// Check C-extension compatibility
// Python 3.13 free-threaded requires:
// 1. Use of the new reference-counting APIs
// 2. Avoidance of global state
// 3. Thread-safe data structures
```

#### Phase 3: enable parallelism step by step

```cpp
// 1. First test on non-critical paths
// 2. Gradually enable parallel script execution
// 3. Monitor performance and stability
```

### Risks and mitigations

| Risk | Mitigation |
|------|----------|
| Incompatible C extensions | Test step by step, prioritize |
| Single-threaded performance drop | Profile-driven optimization |
| Increased debugging complexity | Keep single-threaded mode as fallback |
| Incomplete third-party library support | Enable selectively; stay conservative on critical paths |

### Recommended strategy

```
Current state: Python 2.7 + GIL
    ↓
Phase 1: Python 3.12 + GIL (stability first)
    ↓
Phase 2: Python 3.13 + GIL (verify compatibility)
    ↓
Phase 3: Python 3.13t + no-GIL (test on non-critical paths)
    ↓
Phase 4: fully enable no-GIL (performance optimization)
```

### References

- [PEP 703: Making the Global Interpreter Lock Optional](https://peps.python.org/pep-0703/)
- [Python 3.13 Release Notes](https://docs.python.org/3.13/whatsnew/3.13.html)
- [CPython free-threaded documentation](https://docs.python.org/3.13/howto/free-threading-python.html)
