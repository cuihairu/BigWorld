# BigWorld Embedded Python 2.7.3 → 3.13.16 Migration Ledger


> **No English translation of this document exists yet.** The complete ledger is
> maintained in Chinese in [python-313-migration.md](python-313-migration.md)
> (997 lines). This page exists only so the language switcher has a working
> target and readers land somewhere explicit rather than on a dead link.
>
> Why it is not translated here: the document is a long-form engineering ledger
> (source-tree swap, patch re-port, C-API and script migration, then 35
> numbered coverage-hardening batches). A single-pass rendering would be
> machine-translation-grade prose on material where exact identifiers, file:line
> references, and measured numbers are load-bearing — an honest pointer is
> worth more than a fabricated rendering.

## Task statement (verbatim from the Chinese original, translated)

- Task: upgrade the Python integrated in BigWorld from 2.7.3 to 3.13.15
  (full source-tree replacement + patch re-port + whole C-API/script migration).
- Branch: dev (all commits land on dev only).

## Document outline (section titles only)

- §1 Baseline facts
- §2 Patch disposition table (2.7 → 3.13)
- §3 Build-system changes; §3.1 runtime Python initialization fixes (the precondition for a green unit-test suite)
- §4 BWHooks official-interface approach (replacing `#define` redirection)
- §5 Remaining risks (R1–R4)
- §6 Batches and commits
- §7 Unit-test and coverage baseline (2026-09-26), with §7.1–§7.35 the 35 coverage-hardening batches and the hot-reload supersede-semantics batch

## What the Chinese document records, in brief

The migration was carried out in 10 numbered batches on the dev branch:
replacing the vendored CPython 2.7.3 tree with 3.13.15, re-porting the
BWHooks memory-tracking patch set onto the official
`PyMem_SetAllocator`/`PyObject_SetArenaAllocator` embedding interfaces,
migrating the whole C++ binding surface (62 files including `Python.h`; 428
`PyString_*` sites, 197 `PyInt_*` sites) and 204 server/tool Python scripts
via 2to3, upgrading the vendor tree in `third_party/python_modules` to
Python 3 compatible packages (Twisted 24.7 / SQLAlchemy 1.4.54 / zope.interface 7
/ Pympler 1.0 / pika 1.3.2 / msgpack 1.1.0 plus their dependencies), then a
security-bump 3.13.15 → 3.13.16 (tag v3.13.16, no C-API changes upstream).

The OpenSSL 1.0.0d risk (R1: below the 3.13 minimum of 1.1.1, and uncompilable
under gcc 15's default C++23) is resolved in
[vcpkg-migration.md](vcpkg-migration.md), which also restored `_ssl`/`_hashlib`,
the shared-module list, symbol harvesting, and the libpython link dependencies.
R2 (the Py2-only `third_party/python_modules` set) is likewise closed except
for the `oursql` Py2 C extension. R3 (Windows PCbuild/VS migration) and R4
(client and large asset resource verification) remain open.

§7 then records a unit-test and coverage baseline (21 modules, 856 cases,
0 failures at the time) and 35 coverage-hardening batches taking
`lib/pyscript`, `lib/connection`, `lib/entitydef`, `lib/cstdmf`, `lib/server`
and the entity/DB layer from the baseline toward the module-level figures
recorded there — each batch entry naming the exact test file, the
percentage movement, the gate case count, and the engine defects found while
doing it.

For any of those details, read the Chinese original; this page deliberately
does not paraphrase them.
