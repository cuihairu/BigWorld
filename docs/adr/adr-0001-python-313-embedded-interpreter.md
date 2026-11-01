# ADR-0001: Embedded Interpreter Python 2.7 → 3.13

> **English** | [中文](adr-0001-python-313-embedded-interpreter.md)

- **Status**: Accepted (landed in batches from 2026-09-26; bumped to 3.13.16 on 2026-10-12)
- **Scope**: `programming/bigworld/third_party/python` (vendored CPython),
  `lib/pyscript`, the build chain `third_party_python.mak`, the runtime resource tree

## Context

The engine embeds Python in the server processes as the entity-scripting and
tools layer (`Script::init()` builds its own sys.path and a loading surface
beyond the frozen stdlib); it was originally a heavily modified CPython 2.7 tree:

- 2.7 reached EOL in 2020 and is unmaintainable on modern toolchains
  (gcc 15 / new glibc) and from a security standpoint;
- the engine feeds all CPython heap allocations into BigWorld memory
  statistics (BWHooks) by way of `#define malloc BW_Py_malloc` inside
  `obmalloc.c`/`pymem.h` — an intrusion into the interpreter's
  **private implementation details**, not portable across major versions;
- both the server-side Python ecosystem (twisted/sqlalchemy etc., see R2)
  and the script assets are stuck on 2.x.

## Decision

Replace the vendored interpreter wholesale with **CPython 3.13 (following
the 3.13.x security-patch series)**, keeping it vendored (no distro python),
and rewire BWHooks through the **official embedding APIs**:

1. `PyMem_SetAllocator(PYMEM_DOMAIN_RAW, ...)` forwards all four raw-domain
   entry points to the BW hook layer (with an empty hook table it passes
   straight through to libc, preserving the size==0→1 semantics);
2. `PyObject_SetArenaAllocator` overrides pymalloc arena allocation;
3. both are installed in `Script::init()`, before `Py_Initialize()`;
4. hook symbols get `visibility("default")` and `bwhooks.o` is force-pulled
   with `-Wl,-u` (3.13 no longer references the hooks internally; without
   this, shared modules such as `_hashlib` fail to import).

Accompaniments: the empty `bw_site.py` module is tracked with the source and
installed into the resource tree; `PlatformInfo` gains a Debian→el7 fallback
so that `lib-dynload` directory names match (otherwise every C extension
import fails).

## Trade-offs

| Alternative | Reason for rejection |
|---|---|
| Stay on 2.7 with toolchain patches only | Does not compile under gcc15/C23; no security updates; amounts to freezing the debt |
| Move to an intermediate 3.8/3.10 | Would still require another migration later; 3.13's free-threading/embedding changes are closer to current needs and have a longer maintenance window |
| Use distro/conda Python | Cannot carry the BWHooks patch or control the frozen-stdlib surface; breaks offline reproducible builds |
| Keep the intrusive `#define malloc` hooks | The 3.13 obmalloc layout changed completely, so it is not portable; the official allocator APIs are equivalent and stable |

## Consequences

- **Positive**: `_ssl`/`_hashlib` are restored via vcpkg OpenSSL (ties to
  [ADR-0002](adr-0002-vcpkg-third-party-deps.md)); the 21-module
  unit-test gate is fully green on 3.13; the modern Py3 ecosystem is available.
- **Costs/risks**:
  - R2 — the Py2 ecosystem packages in the resource tree (twisted 11 /
    sqlalchemy 0.6 / zope 2.11 etc.) do not work; each must be upgraded
    before its install rule is re-enabled (deferred; not on the compile
    critical path);
  - R3 — the Windows (PCbuild/VS) side is not migrated; the toolchain break
    is in the Windows build.
- **Follow-ups**: track 3.13.x security patches (precedent: 3.13.15→3.13.16);
  R2/R3 become separate projects.

Process narrative and per-batch evidence:
[full Python 3.13 migration ledger](/python-313-migration).
