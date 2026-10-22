# Source Code Guide

> **English** | [中文](source-code-guide.md)

<div class="arch-hero">

This chapter gives a reading map for the BigWorld source. Rather than just
listing directories, it connects process entry points, message registration,
EntityDef, the database, client communication, and Cell migration into
verifiable source chains.

</div>

## Reading boundaries

The main source directory is `programming/bigworld/`:

```
programming/bigworld/
├── server/           # Server processes: BaseApp, CellApp, DBApp, LoginApp, etc.
├── lib/              # Shared libraries: Mercury, EntityDef, DB, Script, Watcher, etc.
├── client/           # Client runtime
├── tools/            # Editors, operations and offline tools
├── common/           # Shared definitions
├── build/            # Build scripts
└── third_party/      # Third-party dependencies
```

Do not start with a complete scan of `lib/` or `third_party/`. BigWorld's
key relationships are not directory-tree relationships but the runtime chain
"interface registration -> network message -> handler -> entity/database
state change".

Start with these stable anchors:

| Topic | Read first | Key functions or types |
|---|---|---|
| Process entry | `lib/server/bwservice.hpp` | `BIGWORLD_MAIN`, `bwMainT`, `doBWMainT` |
| Main loop | `lib/server/server_app.cpp`, `lib/network/event_dispatcher.cpp` | `ServerApp::runApp`, `ServerApp::run`, `ServerApp::advanceTime`, `EventDispatcher::processOnce` |
| Service initialization | `server/baseapp/baseapp.cpp`, `server/cellapp/cellapp.cpp`, `server/dbapp/dbapp.cpp`, `server/loginapp/loginapp.cpp` | `BaseApp::init`, `CellApp::init`, `DBApp::init`, `LoginApp::init` |
| Network interfaces | `lib/network/interface_macros.hpp`, `lib/network/network_interface.hpp` | `BEGIN_MERCURY_INTERFACE`, `registerWithInterface`, `NetworkInterface` |
| Entity definitions | `lib/entitydef/entity_description_map.cpp` | `EntityDescriptionMap::parse`, `parseInternal`, `parseServices` |
| Persistence | `server/baseapp/base.cpp`, `server/dbapp/dbapp.cpp`, `lib/db_storage_mysql/` | `Base::writeToDB`, `DBApp::writeEntity`, `IDatabase::putEntity`, `PropertyMapping::create` |

## Process startup chain

`main.cpp` exists in the server directories, but most logic is not written in
`main()`; it plugs into the unified service framework through the
`BIGWORLD_MAIN` macro.

Read the real startup chain in this order:

1. `server/<app>/main.cpp` uses `BIGWORLD_MAIN` to call `bwMainT<App>()`.
2. In `lib/server/bwservice.hpp`, `BIGWORLD_MAIN` expands to the real `main()`, initializing `BWResource`, `BWConfig`, and command-line arguments.
3. `bwMainT` creates a `Mercury::EventDispatcher` and a `Mercury::NetworkInterface`.
4. `doBWMainT` initializes `ServerAppConfig`, constructs the concrete `SERVER_APP`, and calls `serverApp.runApp()`.
5. `ServerApp::runApp()` first calls the derived class's `init()`, and on success enters `ServerApp::run()`.
6. `ServerApp::run()` calls `mainDispatcher_.processUntilBreak()`, and the event loop then enters `EventDispatcher::processOnce()`.

This chain explains why you cannot read only the `main.cpp` in each process
directory: `main.cpp` is only the process-type selection point; real
initialization, timers, and network-event dispatch happen in the shared
service framework.

## Service registration chain

Mercury interfaces are not runtime-reflection auto-discovery; the
interface-definition macros generate message descriptors, which are then
registered on the `NetworkInterface` during process initialization.

| Process | Registration point | Interface |
|---|---|---|
| BaseApp | `server/baseapp/baseapp.cpp` | `BaseAppIntInterface`, `BaseAppExtInterface` |
| CellApp | `server/cellapp/cellapp.cpp` | `CellAppInterface` |
| DBApp | `server/dbapp/dbapp.cpp` | `DBAppInterface` |
| LoginApp | `server/loginapp/loginapp.cpp` | `LoginInterface`, `LoginIntInterface` |
| BaseAppMgr | `server/baseappmgr/baseappmgr.cpp` | `BaseAppMgrInterface` |
| CellAppMgr | `server/cellappmgr/cellappmgr.cpp` | `CellAppMgrInterface` |
| DBAppMgr | `server/dbappmgr/dbappmgr.cpp` | `DBAppMgrInterface` |
| Reviver | `server/reviver/reviver.cpp` | `ReviverInterface` |

Verification: look up `BEGIN_MERCURY_INTERFACE` in the interface header, then
the corresponding process's `registerWithInterface()`, and finally follow
`bundle.startMessage()` or `bundle.startRequest()` to callers.

## Main loop and tick

The main loop is driven by `EventDispatcher`; game-time advancement is
triggered by the derived process in a timer handler.

Core chain:

```
ServerApp::run()
  -> EventDispatcher::processUntilBreak()
  -> EventDispatcher::processOnce()
  -> processFrequentTasks()
  -> processTimers()
  -> processNetwork()
```

BaseApp's game tick entry is `BaseApp::tickGameTime()`; CellApp's game tick
entry is `CellApp::handleGameTickTimeSlice()`. Both call or depend on
`ServerApp::advanceTime()` in their logic, which then triggers the unified
phases `onEndOfTick()`, `onStartOfTick()`, `callUpdatables()`, and so on.

Keep two layers distinct while reading:

| Layer | Code | Role |
|---|---|---|
| Event dispatch | `EventDispatcher::processOnce()` | Network, timers, statistics, frequent tasks |
| Game time | `ServerApp::advanceTime()` | Advances `GameTime` and runs the tick lifecycle |
| Process logic | `BaseApp::tickGameTime()`, `CellApp::handleGameTickTimeSlice()` | Base/Cell-specific backup, load, AOI, migration, etc. |

## Network message chain

For Mercury, a good reading order attacks from both ends — "how a message is
sent" and "how a message reaches a handler":

```
Send side:
InterfaceElement
  -> Bundle::startMessage/startRequest
  -> Channel / UDPChannel
  -> PacketSender

Receive side:
PacketReceiver
  -> NetworkInterface
  -> UDPBundleProcessor
  -> InterfaceElement handler
  -> concrete App / Entity method
```

Key files:

| File | Focus |
|---|---|
| `lib/network/interface_macros.hpp` | How the interface macros generate `Args::start()`, `startRequest()`, `registerWithInterface()` |
| `lib/network/bundle.hpp` | How messages are written into a bundle |
| `lib/network/udp_bundle.cpp` | How a UDP bundle starts a message/request |
| `lib/network/network_interface.hpp` | The network interface holding channel, sender, receiver, request manager |
| `lib/network/packet_receiver.hpp` | How socket input enters Mercury |
| `lib/network/channel.hpp`, `lib/network/udp_channel.hpp` | Core state for reliable, ordered, retransmitted, and indexed channels |

To trace an RPC, do not guess the protocol format first: run
`rg "startRequest( SomeInterface::someMessage"` to find the sender, inspect
the message type in the interface definition, and finally look at the
handler's owning class for state changes.

## EntityDef chain

The `.def` file is BigWorld's entity-contract entry, not just a script
declaration. It determines property domains, exposed methods, serialization
order, protocol digest, and the database-mapping basis.

Reading chain:

```
examples/client_integration/python/simple/res/scripts/entities.xml
  -> examples/client_integration/python/simple/res/scripts/entity_defs/<Entity>.def
  -> EntityDescriptionMap::parse()
  -> EntityDescriptionMap::parseInternal()
  -> EntityDescription
  -> DataDescription / MethodDescription
  -> DataType::addToStream/createFromStream
```

Key tag meanings:

| `.def` tag | Source carrier | Effect |
|---|---|---|
| `Persistent` | `DataDescription::isPersistent()` | Whether it enters the persistent data domain |
| `Identifier` | `EntityDescription::pIdentifier()` | Whether a business identifier can be used to look up a DBID |
| `Indexed` | `DataDescription::isIndexed()` | Whether MySQL mapping creates an indexed column |
| `DatabaseLength` | `DataDescription::databaseLength()` | Database length limits for strings, blobs, user types, etc. |
| Client/Base/Cell domains | `EntityDescription` data domains | How a property is serialized among base, cell, and client |

When verifying a `.def` effect, do not look only at the XML. Check all of:

| Verification point | Source |
|---|---|
| Whether a property persists | `DataDescription::parse()` reads the field; `DataDescription::isPersistent()` returns the verdict |
| Writing only persistent properties | `EntityDescription::ONLY_PERSISTENT_DATA`, `Base::addToStream()` |
| Protocol consistency | `EntityDescriptionMap::addPersistentPropertiesToMD5()` |
| Database field mapping | `PropertyMappingsPerType`, `PropertyMapping::create()` |
| Method-argument serialization | `MethodDescription::addToStream()`, `MethodDescription::createArgsFromStream()` |

See [Serialization and EntityDef](/architecture/serialization-entitydef) and
[EntityDef contract and protocol generation](/architecture/entitydef-contract-generation).

## Property changes and "dirty data"

In BigWorld "property change" has two chains with different purposes; do not
merge them into one ORM-style dirty-field tracker.

The first is the runtime sync chain:

```
A property is modified by a script or container
  -> PropertyOwner / PropertyChange
  -> Entity::onOwnedPropertyChanged()
  -> HistoryEvent / ghostedDataUpdate / Witness::sendToClient()
```

This chain pushes changes to clients, ghosts, or replay. It cares about
network visibility, AOI, nested property changes, and message size; it is not
equivalent to "this field will be written to the database later".

The second is the explicit persistence chain:

```
Base.writeToDB()
  -> Base::writeToDB()
  -> Base::addToStream()
  -> EntityDescription::ONLY_PERSISTENT_DATA
  -> DBAppInterface::writeEntity
  -> DBApp::writeEntity()
  -> WriteEntityHandler
  -> IDatabase::putEntity()
```

This chain is triggered by `writeToDB`, selecting and serializing base/cell
data according to the `.def`'s `Persistent` domain. The source focus is
`Base::writeToDB()`, `Base::requestCellDBData()`, `Base::addToStream()`, and
`DBApp::writeEntity()`.

So when reading source, understand "dirty data" as two classes of fact:

| Scenario | Representative source | Semantics |
|---|---|---|
| A property changed and needs sync | `PropertyChange`, `Entity::onOwnedPropertyChanged()` | Incremental network event |
| Persistence is needed | `Base::writeToDB()`, `WRITE_BASE_CELL_DATA`, `ONLY_PERSISTENT_DATA` | Explicit persistence snapshot |

## Database mapping chain

BigWorld's database layer is "EntityDef-driven persistence mapping", not a
typical ORM.

The persistence chain:

```
Base::writeToDB(flags)
  -> If there is Cell data, first request a cell snapshot via CellAppInterface::writeToDBRequest
  -> Base::addToStream(flags, stream, pCellData)
  -> DBAppInterface::writeEntity
  -> DBApp::writeEntity()
  -> WriteEntityHandler::writeEntity()
  -> DBApp::putEntity()
  -> IDatabase::putEntity()
  -> MySqlDatabase::putEntity() / XMLDatabase::putEntity()
```

The mapping chain:

```
EntityDescriptionMap
  -> EntityDescription
  -> DataDescription
  -> PropertyMappingsPerType
  -> PropertyMapping::create()
  -> NumMapping<T> / StringMapping / SequenceMapping / ClassMapping / BlobMapping / StringLikeMapping / UserTypeMapping ...
```

Differences from an ORM:

| Dimension | BigWorld source | Typical ORM |
|---|---|---|
| Model source | `.def` + `EntityDescription` | Language object class / annotation / schema |
| Save trigger | Explicit `writeToDB` and login/logout/backup flows | Unit of Work, transaction commit, or object-state flush |
| Field selection | `Persistent` data domain + `ONLY_PERSISTENT_DATA` | Mapped columns or tracked fields |
| Relation expression | mailbox, DBID, Identifier, Base/Cell data flow | Object relation, foreign key, lazy loading |
| Runtime target | Persisting distributed MMO entity state | Mapping application objects to relational tables |

When reading database-related source, first confirm whether a property is
`Persistent` in the `.def`, then see how `PropertyMapping::create()` chooses
the concrete mapping based on `DataType`. Do not reverse-engineer the entity
model from SQL table structure, or you will miss Base/Cell partitioning, the
Identifier cache, the secondary DB, and auto-load flags.

See [Persistence and the DB thread model](/architecture/persistence-db-model).

## Client communication chain

"Client communication" in BigWorld primarily means the protocol chain among
the client runtime and LoginApp/BaseApp/CellApp. The entry is not an HTTP API
but Mercury interfaces and `ServerConnection`.

Login and takeover chain:

```
Client LoginInterface::login
  -> LoginApp::login()
  -> DBAppInterface::logOn
  -> BaseAppMgr / BaseApp creates or restores a Base
  -> BaseAppExtInterface::baseAppLogin
  -> Proxy takes over the client channel
```

Runtime upstream/downstream chains:

| Direction | Interface | Representative source |
|---|---|---|
| Client to LoginApp | `LoginInterface` | `lib/connection/login_interface.hpp`, `server/loginapp/loginapp.cpp` |
| Client to BaseApp | `BaseAppExtInterface` | `lib/connection/baseapp_ext_interface.hpp`, `server/baseapp/baseapp.cpp`, `server/baseapp/proxy.cpp` |
| Server to client | `ClientInterface` | `lib/connection/client_interface.hpp`, `server/baseapp/proxy.cpp`, `server/cellapp/entity.cpp` |
| Client-local protocol handling | `ServerConnection` | `lib/connection/server_connection.cpp` |

The `Proxy` is the server-side representative of a client connection: it owns
the post-authentication channel, switches BaseApps, sends `ClientInterface`
messages to the client, and forwards client input to Base/Cell entity
methods. `ServerConnection` is the client-side encapsulation of these
protocols.

See [Login, sessions, and proxy takeover](/architecture/login-session-proxy-flow)
and [Communication abstraction and RPC](/architecture/communication-rpc).

## Actor similarities and boundaries

BigWorld entity communication has actor-like traits, but it is not a generic
Actor runtime.

Similarities:

| Similarity | Source expression |
|---|---|
| Addressed communication | mailbox, EntityID, Mercury address |
| Message driven | `bundle.startMessage()` / `startRequest()` |
| Single entity encapsulating state | Base/Cell entity methods execute around an entity instance |
| Part of distributed location transparency | Base mailbox, Cell mailbox, Proxy forwarding |

Boundaries:

| Difference | BigWorld source expression |
|---|---|
| No generic mailbox runtime | A mailbox is an MMO entity and Mercury protocol wrapper, not a unified actor scheduler |
| The scheduling core is not an actor scheduler | The main loop is `EventDispatcher` + timers + network handlers |
| State is domain-partitioned by EntityDef | Base/Cell/Client/Persistent domains determine data flow |
| Location migration is a Cell/App mechanism | Offload, Ghost, Witness are driven by space partitioning and AOI |

So BigWorld can be understood as "a messaged distributed object system for
MMO entities", but source analysis cannot apply the Actor model wholesale to
explain every behavior.

## AOI, Ghost, and Offload

Read the Cell-side source through space management, not just from the entity
classes themselves.

Core chain:

```
CellApp::handleGameTickTimeSlice()
  -> CellApp::updateBoundary()
  -> Cell / OffloadChecker
  -> EntityGhostMaintainer::check()
  -> Entity::createGhost()
  -> Entity::offload()
  -> RealEntity::writeOffloadData()
  -> Space::createGhost()
  -> RealEntity::readOffloadData()
```

AOI and client-visibility chain:

```
Witness::update()
  -> EntityCache / HistoryEvent
  -> ClientInterface::enterAoI / leaveAoI / property update / method call
  -> ServerConnection handler
```

The key to Offload is not "moving object memory" but serializing the real
entity's necessary state to the target Cell while maintaining ordering
constraints — ghost, haunt, next real address — on the original and adjacent
Cells. The relevant source is concentrated in `server/cellapp/entity.cpp`,
`real_entity.cpp`, `entity_ghost_maintainer.cpp`, `offload_checker.cpp`, and
`witness.cpp`.

See [AOI, Witness, and Ghost](/architecture/aoi-witness-ghost),
[Cell partitioning and load balancing](/architecture/cell-partition-load-balance),
and [Entity migration and offload](/architecture/entity-migration-offload).

## Observation and test entries

Source verification should not rely on static reading alone. BigWorld ships
with Watcher, Profiler, unit-test, and network-test entries.

| Goal | Source entry |
|---|---|
| Runtime observation | `lib/network/watcher_nub.hpp`, `lib/server/watcher_forwarding_collector.cpp` |
| Service metrics | Each App's `addWatchers()` |
| Tick performance | `ServerApp::advanceTime()`, `AUTO_SCOPED_PROFILE` |
| Network behavior tests | `lib/network/unit_test/` |
| EntityDef serialization tests | `lib/entitydef/unit_test/` |
| Cell ghost/offload tests | `server/cellapp/unit_test/` |

To find tests, prefer `rg "TEST"` and `rg "BW_ADD_TEST"`. To find runtime
observation, prefer `rg "addWatchers"`, then see how the Watcher path mounts
on the root node.

## Cross-verification method

When reading BigWorld source, keep these search entries fixed:

| Question to verify | Search term |
|---|---|
| Whether an interface is actually registered | `registerWithInterface(` |
| Who sends a message | `startMessage( Interface::message` |
| Whether an RPC waits for a reply | `startRequest( Interface::message` |
| How a property is serialized | `addToStream(`, `createFromStream(` |
| Whether a field is persistence-only | `ONLY_PERSISTENT_DATA`, `isPersistent()` |
| Whether Cell data participates in persistence | `WRITE_BASE_CELL_DATA`, `writeToDBRequest` |
| Where the client protocol is handled | `ClientInterface::`, `BaseAppExtInterface::`, `ServerConnection::` |
| How Offload ordering is guaranteed | `ghostSetNextReal`, `writeOffloadData`, `readOffloadData` |
| Whether a handler permits callbacks | `callbacksPermitted` |

A source-analysis conclusion must answer at least three questions together:
where a message or data comes from, which handler catches it, and which
entity or database state it ultimately changes.

## Next steps

- Read the [architecture research method](/architecture/research-method) for a unified evidence-grading and source-citation standard.
- Read [process topology and responsibility split](/architecture/process-topology) to fix App roles and process relationships first.
- Read [main loop, tick, and event dispatch](/architecture/event-loop), then return to this chapter to inspect source along call chains.
- Read [persistence and the DB thread model](/architecture/persistence-db-model) to complete the persistence path across `.def`, DBApp, and MySQL mapping.
