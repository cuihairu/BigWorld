# BigWorld client SDK code generator (CL-3)

`bwgen.py` reads a BigWorld `res/scripts` tree (`entities.xml` plus
`entity_defs/*.def`) and emits per-entity base classes for the two client SDK
runtimes delivered in CL-1 / CL-2:

| Output | Runtime | Consumed by |
| --- | --- | --- |
| `cs/<Entity>Base.cs` | `client_sdk/unity` (netstandard2.1, no UnityEngine ref) | Unity / any .NET host |
| `ts/<Entity>Base.ts` | `client_sdk/typescript` (ES2022 module) | Web / Node / any JS engine |

Stdlib-only Python 3 (no dependencies), so it runs anywhere the engine tools
do.

## Usage

```sh
python3 client_sdk/generator/bwgen.py \
    programming/bigworld/examples/client_integration/python/simple/res/scripts \
    --out /tmp/bwgen_out --lang both
```

`--lang` accepts `cs`, `ts` or `both` (default). The in-repo golden tree
(`programming/bigworld/examples/client_integration/python/simple/res/scripts`)
is the acceptance corpus: five entities (Account, ClientAvatar, ClientOnly,
Guard, Space) covering strings, fixed-width ints, an unsupported USER_TYPE
property and exposed base methods.

The same CLI is available as a wheel (`pip install client_sdk/generator`, then
`bwgen ...`, or `python3 -m bwgen ...`); the wheel is one of the daily client
SDK bundles built by `.github/workflows/daily-build.yml`. The repo-root
`bwgen.py` and the package CLI are the same `bwgen.cli.main`.

## What one generated file contains

`<Entity>Base` (the class game code subclasses, kbcmd naming):

* `PropertyIdXxx` / `MethodIdXxx` constants — the wire-facing ids in the
  server's size-sorted order (see TYPEMAP.md §ids).
* typed client-visible properties (CLIENT-flagged only: `BASE_AND_CLIENT`,
  `CELL_AND_CLIENT`, `ALL_CLIENTS`, `OWN_CLIENT`, `OTHER_CLIENTS`).
* `ReadClientData(reader)` — decodes the createEntity initial snapshot.
* `OnXxx(...)` hooks + `InvokeClientMethod(id, reader)` — server-to-client
  method dispatch; unknown ids go to `OnUnknownClientMethod` (C#) / are
  ignored (TS).

`EntityCall<Entity>Base` (remote-call handle):

* `SendXxx(...)` stubs for the exposed **base/cell** methods, writing
  `[entityId: int32 LE][exposed method id: uint8][args...]` through the
  runtime's send path (C#: `ClientConnection.SendMessage`; TS:
  `EntityCall.newCall`/`sendCall`).

## Repository layout

```
client_sdk/generator/
  pyproject.toml        wheel build (console script: `bwgen`)
  bwgen.py              CLI entry point (script invocation)
  bwgen/
    cli.py              the shared CLI (generate / main)
    __main__.py         `python3 -m bwgen` entry
    model.py            def model (EntityDef / MethodDef / PropertyDef)
    defparse.py         entities.xml + *.def parser
    typemap.py          def type -> wire size / C# / TS mapping
    ids.py              the wire id numbering (server sort replication)
    naming.py           case conversion helpers
    emit_cs.py          C# emitter
    emit_ts.py          TypeScript emitter
  tests/
    test_generator.py   unittest suite (see below)
    golden/{cs,ts}/     byte-exact reference output for the golden tree
```

The generated files that actually ship inside the runtimes live next to their
consumers (`client_sdk/unity/src/BigWorld.ClientSdk/Generated/`,
`client_sdk/typescript/src/generated/`) — currently Account and ClientAvatar,
the entities the runtime smoke tests drive. The consistency test asserts
those copies equal fresh generator output, so they cannot drift.

## Tests

```sh
python3 -m unittest discover -s client_sdk/generator/tests -v
```

Three layers:

1. **Golden** — regenerate every golden-tree entity and require byte-identical
   output to `tests/golden/`. The generator is deterministic (no timestamps,
   no absolute paths in the output), so any diff is a real behavioural change.
2. **Runtime consistency** — the shipped `Generated/` copies in the unity and
   typescript trees equal fresh output.
3. **Synthetic** — a small entity exercising ARRAY / VECTOR / BLOB branches
   plus the unsupported-type boundary (id slot reserved, TODO emitted, decode
   stops) that the golden tree does not hit.

Joint smoke (the CL-3 acceptance line "generated code runs through the CL-1 /
CL-2 runtimes"): `client_sdk/unity/tests/.../Generated/GeneratedEntityTests.cs`
and `client_sdk/typescript/test/generated.test.ts` drive mock login payloads
and assert the outgoing wire bytes for an exposed method call.

## Batch-one boundary

Deliberately out of scope (the generator emits a TODO comment and reserves the
id slot so numbering still matches the server):

* `USER_TYPE`, `FIXED_DICT`, `PY_TUPLE`, `PY_DICT`, `MAILBOX*`, `UDO_REF`,
  `TWO_VECTOR2` — no C#/TS mapping emitted; properties get no field and the
  initial-snapshot decode stops at that index; methods fall back to an
  `OnRawXxx(reader)` raw-bytes hook (C#) or skip decode.
* Property-update streams (delta updates) — only the full initial snapshot is
  decoded.
* Nested `ARRAY<ARRAY<...>>` and `ARRAY` of vectors — flattened types are
  supported, deeper nesting is not validated.

See TYPEMAP.md for the full type table.
