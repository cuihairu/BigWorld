# Overall Picture


## One-sentence verdict

Open-source BigWorld is a large MMO engine source repository built from a
C++ server, the Mercury network protocol, the EntityDef contract, and an
embedded scripting layer. Its core is not any single language or toolchain,
but the closed loop among multi-process state sharding, entity lifecycle,
client synchronization, and database persistence.

## Source main lines

From the source relationships, BigWorld has at least six main lines:

| Main line | Representative source | Question it answers |
|---|---|---|
| Process framework | `programming/bigworld/lib/server/` | How each server process starts, registers its interface, and enters the event loop |
| Mercury networking | `programming/bigworld/lib/network/` | How messages are packed, sent, received, dispatched, and retried |
| Entity contract | `programming/bigworld/lib/entitydef/` | How a `.def` determines property domains, methods, serialization order, and digest |
| Server state | `programming/bigworld/server/` | How BaseApp, CellApp, DBApp, LoginApp and the other processes cooperate |
| Client connection | `programming/bigworld/lib/connection/`, `programming/bigworld/client/` | How the client logs in, takes over a Proxy, and receives AOI and property updates |
| Persistence mapping | `programming/bigworld/lib/db_storage*`, `programming/bigworld/server/dbapp/` | How EntityDef properties map to the database and get written |

When reading the source, connect these six lines first rather than reading
files one by one in directory order.

## Runtime closed loop

The minimal server closed loop can be understood as:

```
BIGWORLD_MAIN
  -> bwMainT / doBWMainT
  -> ServerApp::runApp
  -> concrete App::init
  -> Interface::registerWithInterface
  -> EventDispatcher::processOnce
  -> Mercury handler
  -> Entity / Manager / DB state change
```

The entity loop is more complex:

```
entities.xml / .def
  -> EntityDescriptionMap
  -> Base / Cell / Client / Persistent data domains
  -> Bundle / MethodDescription / DataDescription serialization
  -> BaseApp / CellApp / Proxy / DBApp
  -> AOI, Ghost, Offload, writeToDB
```

This explains the main difficulty of the BigWorld source: a single `.def`
property can simultaneously affect client visibility, Cell ghost
synchronization, Base backup, DB persistence, hot update, and migration
streams.

## Repository tiers

Viewed from the root, the main-line source is concentrated in these areas:

| Directory | Role |
|---|---|
| `programming/bigworld` | BigWorld main source: client, server, tools, libraries, third-party dependencies, and the build system |
| `programming/fantasydemo` | Sample content and demo assets; helps understand how the business layer uses the engine |
| `docs/pdf` | Original documentation; usable as background, but source conclusions should be based on the current repository |

`programming/bigworld/lib` is the shared-mechanism layer;
`programming/bigworld/server` is the MMO server-process layer. The former
defines "messages, types, resources, scripts, DB abstractions"; the latter
assembles those mechanisms into a concrete runtime.

## Project scale characteristics

Directory scans give these facts:

| Area | Number of first-level directories | Meaning |
|---|---:|---|
| `programming/bigworld/lib` | 57 | Many shared libraries with significant cross-module dependencies |
| `programming/bigworld/server` | 12 | Server processes and extension directories coexist |
| `programming/bigworld/server/tools` | 10 | Operations, logging, and data-processing tools are part of the server ecosystem |
| `programming/bigworld/third_party` | 22 | The repository uses a bundled third-party dependency strategy (another 7 dependencies come through vcpkg; see `vcpkg.json`) |

These numbers indicate scale only, not architecture. Architectural judgments
must return to source call chains: where an interface is registered, where a
message is sent, what state a handler mutates, and whether that state
ultimately reaches a client or the database.

## Architectural layering

By responsibility the source can be split into five layers:

| Layer | Contents | Key boundary |
|---|---|---|
| Infrastructure | `cstdmf`, `network`, `resmgr`, `math` | Time, memory, resources, networking primitives |
| Contract and scripting | `entitydef`, `script`, `pyscript`, `entitydef_script` | `.def`, DataType, script objects, and the C++ boundary |
| Server processes | `baseapp`, `cellapp`, `dbapp`, `loginapp`, managers | Process responsibilities, RPC, state sharding, fault recovery |
| Client and tools | `client`, `tools`, editor libraries | Client protocol, editors, the asset production chain |
| Persistence and operations | `db_storage*`, `server/tools`, Watcher | DB mapping, background tasks, observability and control |

These five layers are not strictly one-way dependencies. For example
EntityDef is used by networking, scripting, DB, and the client at once;
Watcher is used for both observation and some control; Base/Cell entity
state flows across processes.

## Goals of the current documentation

The source analysis on this site aims to:

- explain BigWorld's multi-process responsibilities and message chains;
- explain how EntityDef affects networking, DB, the client, and migration;
- explain the state boundaries among Base, Cell, Client, Proxy, Witness, and Ghost;
- treat the build, runtime, tests, Watcher, and logs as source-verification entry points rather than mere usage instructions.

For continued reading, start with the [source code guide](/analysis/source-code-guide),
then move to the [architecture research method](/architecture/research-method)
and the topic chapters.
