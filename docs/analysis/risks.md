# Source Risk Boundaries

> **English** | [中文](risks.md)

## Risk is not a single-point problem

BigWorld's risks arise mainly from the superposition of cross-process state,
protocol contracts, and the script/C++ boundary. In source analysis do not
reduce risk to "some library is old" or "some directory is complex"; trace
whether the concrete closed loop can be verified.

## High risk 1: interface macros hide the real call boundary

Mercury interfaces define messages through macros, which
`registerWithInterface()` then registers on a `NetworkInterface`. This makes
the call boundary less direct than an ordinary function call.

Risk symptoms:

| Symptom | Source to verify |
|---|---|
| You see the handler but not the sender | `bundle.startMessage()` / `bundle.startRequest()` |
| You see the interface definition but not the registering process | `registerWithInterface()` |
| Unclear whether it waits for a reply | `startRequest()` and the reply handler |
| Unclear about message reliability | Interface macros and Bundle/Channel parameters |

Handling: every cross-process conclusion must cite the interface definition,
the registration point, and at least one send point.

## High risk 2: EntityDef affects several chains at once

A `.def` is not plain entity configuration. It affects:

| Affected surface | Representative source |
|---|---|
| Property domains | `DataDescription`, `EntityDescription` |
| Method arguments | `MethodDescription`, `MethodArgs` |
| Network serialization | `addToStream()`, `createFromStream()` |
| Client synchronization | `ClientInterface::entityProperty`, `nestedEntityProperty`, `sliceEntityProperty` |
| DB persistence | `ONLY_PERSISTENT_DATA`, `PropertyMapping::create()` |
| Digest consistency | `EntityDescriptionMap::addPersistentPropertiesToMD5()` |

The risk is that a field change looks local but can actually break the
client protocol, DB schema, entity migration streams, or hot-reload paths.

## High risk 3: property sync and persistence are not the same mechanism

The property-change chain and database-write chain differ:

| Chain | Representative source | Risk |
|---|---|---|
| Runtime sync | `PropertyChange`, `Entity::onOwnedPropertyChanged()` | Mistaking "sent to the client" for "persisted" |
| Explicit persistence | `Base::writeToDB()`, `DBApp::writeEntity()`, `IDatabase::putEntity()` | Mistaking property changes for an automatic flush to DB |

Treating BigWorld as an ORM leads to wrong save-semantics judgments. Database
writes depend on `writeToDB`, the `Persistent` data domain, Base/Cell data
snapshots, and DBApp backend tasks — not automatic dirty checking of object
fields.

## High risk 4: Cell migration depends on message order and state transitions

Offload/Ghost/Witness are not ordinary object migration.

Key source:

| Source | Risk point |
|---|---|
| `EntityGhostMaintainer::check()` | Decides whether to create a ghost or offload |
| `Entity::offload()` | Real-entity transfer entry |
| `RealEntity::writeOffloadData()` | Migration stream write order |
| `Space::createGhost()` | One of the entries for creating ghost/real on the target Cell |
| `Entity::ghostSetNextReal()` | Message-ordering constraints during the real switch |
| `Witness::writeOffloadData()` | Client view-state migration |

The risk is that a local change can break real/ghost lifecycle, buffered
ghost messages, AOI continuity, or client position synchronization.

## High risk 5: the main loop is synchronous-handler driven

`EventDispatcher::processOnce()` handles timers, networking, and frequent
tasks in the same event loop. The concrete App's handlers execute directly
on this chain.

Risk symptoms:

| Risk | Representative source |
|---|---|
| Handler cost affects the tick | `EventDispatcher::processOnce()`, `ServerApp::advanceTime()` |
| Base/Cell tick logic interleaves with network callbacks | `BaseApp::tickGameTime()`, `CellApp::handleGameTickTimeSlice()` |
| Script callbacks can change object lifecycle | `callbacksPermitted(false/true)`, entity migration code |
| Background-task completion still needs main-loop finalization | DBApp, MySQL tasks, the background task manager |

When reading source, confirm whether code runs in the main-thread event
loop, a timer callback, a network handler, or a background task.

## High risk 6: Watcher is both an observability and a control plane

Watcher is not just metric export. Some Watchers support writes, and the
Managers also aggregate state across processes through forwarding watchers.

Key source:

| Source | Focus |
|---|---|
| `lib/network/watcher_nub.hpp` | Watcher network entry |
| `lib/server/watcher_forwarding_collector.cpp` | Cross-process Watcher aggregation |
| Each App's `addWatchers()` | Which runtime state and controls are exposed |

The risk is treating Watcher as read-only monitoring and overlooking its
effect on runtime state and the management plane. Documentation conclusions
involving Watcher must state the read/write boundary.

## High risk 7: build configuration and source tree can drift

Build scripts, project files, PDF documents, and the current source tree are
not necessarily fully consistent.

Verification points:

| Question | How to check |
|---|---|
| Whether paths referenced by build scripts exist | `rg` script and target names in configuration |
| Whether CMake/Make cover the same targets | Compare `CMakeLists.txt` against `Makefile.rules` |
| Whether documentation still matches source | Look back by function name, interface name, file path |
| Whether test targets build | Check `BW_ADD_TEST` and the corresponding source |

If a conclusion appears only in old documents and the corresponding file
cannot be found in the current source, mark it as documentation drift rather
than writing it as a source fact.

## Risk verification checklist

Each risk conclusion answers at least these questions:

| Question | Search entry |
|---|---|
| Who defines the message | `BEGIN_MERCURY_INTERFACE` |
| Where the interface is registered | `registerWithInterface(` |
| Who sends the message | `startMessage(`, `startRequest(` |
| How data is encoded | `addToStream(`, `createFromStream(` |
| Where state changes | The concrete App, Entity, Manager, or DB handler |
| Whether tests cover it | `BW_ADD_TEST`, `TEST(` |
| Whether runtime observation exists | `addWatchers()` |

This checklist is more useful than vague "technical debt" descriptions,
because it pins risk to a re-checkable source evidence chain.
