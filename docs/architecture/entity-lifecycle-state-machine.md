# Entity Lifecycle State Machine

> **English** | [中文](entity-lifecycle-state-machine.md)

<div class="arch-hero">

A BigWorld Entity is not an ordinary business object. It may be a Base, a Cell
real, a Cell ghost, a Proxy, a client-owned entity with a Witness, or an entity
migrating from real to ghost during offload before becoming real again on the
target CellApp. Its lifecycle state machine is central to the MMO architecture.

</div>

## Key Takeaways

The lifecycle has at least these key states:

- Base-only: only the long-lived Base entity exists on BaseApp.
- Base + pending Cell: Base is requesting creation of a Cell entity.
- Base + Cell real: an authoritative real entity exists on CellApp.
- Ghost: a non-authoritative copy used for AOI and cross-Cell visibility.
- Offloading: the real entity is migrating to another CellApp.
- Destroying: entity destruction has begun, but the object cannot be deleted immediately.
- Zombie ghost: a leftover ghost possible in recovery scenarios.
- Restored real: a real restored from backup or a migration stream.

Key source code:

- `Base::createCellEntity()` requests creation of the linked Cell entity: base.cpp.
- `Base::sendCreateCellEntity()` sends `CellAppInterface::createEntityNearEntity`: base.cpp.
- `Cell::createEntityInternal()` creates a real entity: cell.cpp.
- `Entity::offload()` sends `CellAppInterface::onload` and calls `convertRealToGhost()`: entity.cpp.
- `Entity::convertRealToGhost()` writes migration data and flushes the Witness: entity.cpp.
- `Entity::readRealDataFromStreamForOnload()` reads real data on the target CellApp: entity.cpp.
- `Entity::destroy()` enters the destroyed state and handles real/ghost differences: entity.cpp.
- `Cell::~Cell()` / `Cell::shutDown()` destroy every real entity: cell.cpp.

## State-Machine Overview

<MermaidDiagram title="Entity lifecycle state machine">
stateDiagram-v2
  [*] --> BaseOnly: createBase / login
  BaseOnly --> PendingCell: Base.createCellEntity
  PendingCell --> CellReal: Cell.createEntityInternal
  CellReal --> Offloading: load balance / teleport
  Offloading --> SourceGhost: convertRealToGhost
  Offloading --> TargetReal: onload + readRealData
  SourceGhost --> Ghost: haunts / AOI copy
  Ghost --> Destroying: ghost destroyed
  CellReal --> Destroying: Entity.destroy
  Destroying --> Destroyed: setDestroyed
  Ghost --> ZombieGhost: restore edge case
  ZombieGhost --> Destroying: destroyZombie
  TargetReal --> CellReal: start controllers + backup
  Destroyed --> [*]
</MermaidDiagram>

This state machine is not a single source-level enum. It is an architectural
state model reconstructed from multiple classes and call chains.

## Base Creates a Cell Entity

`Base::createCellEntity()` is one script API entry point.

It first checks whether a linked Cell entity already exists. If a nearby entity
mailbox was supplied, it calls `sendCreateCellEntityToMailBox()`:
base.cpp.

`Base::sendCreateCellEntity()`:

- creates a separate `UDPBundle` rather than using the channel's own bundle;
- calls `startRequest( CellAppInterface::createEntityNearEntity, pHandler )`;
- writes the nearby entity ID;
- writes the current entity channel version;
- writes `isRestore = false`;
- calls `addCellCreationData()` to write creation data;
- sends the request to the target CellApp.

Source: base.cpp.

A key comment explains the separate bundle: streaming may fail and must be
abortable partway through. Entity creation is therefore not an ordinary RPC; it
places strict requirements on stream integrity.

## Cell Creates a Real

`Cell::createEntityInternal()` is the core real-creation path on CellApp.

Key steps:

1. Read `EntityID` and `EntityTypeID` from the stream.
2. If the ID is zero, allocate a new one through `idClient()`.
3. Check whether the population already contains a non-destroyed entity.
4. In a restore scenario, an existing ghost may be treated as a zombie ghost and destroyed.
5. `space_.newEntity(id, entityTypeID)` creates the Entity.
6. Temporarily disallow callbacks.
7. Call `initReal()` to initialize the real.
8. Insert it into the real-entity list.
9. Notify population observers.
10. Restore callback permission.
11. If replay data exists, write initial state.
12. Immediately call `pReal()->backup()` to send a backup to BaseApp.

Source: cell.cpp.

Important design points:

- Callbacks are disabled during initialization so a half-initialized object cannot run script.
- The currentCell message is delayed into the same bundle as the backup, eliminating a window where Base lacks cellData before the first backup.
- Restore explicitly handles zombie ghosts.

## Difference Between Real and Ghost

In BigWorld:

- A real is authoritative and executes position, controller, script, and persistence-related state.
- A ghost is a copy serving AOI and cross-Cell visibility.
- An Entity object can change from real to ghost, and can become real again on the target CellApp from a ghost or new object through onload.

This is more than copying an object:

- Real-only properties differ from ghost properties.
- Witnesses exist only on certain real Proxy entities.
- Controllers, channels, haunts, and ghosts must all be rebound.
- Migration streams must preserve EntityDef serialization order.

## Offload Migration

`Entity::offload()` may only be called on a real entity. It:

- asserts `isReal()`;
- obtains the target `CellAppChannel` bundle;
- starts a `CellAppInterface::onload` message when not teleporting;
- calls `convertRealToGhost(&bundle, pChannel, isTeleport)`.

Source: entity.cpp.

`convertRealToGhost()`:

- asserts that the entity is currently real;
- disables callbacks;
- flushes the Witness first when one exists;
- writes migration data through `writeRealDataToStream()` when a target channel exists;
- destroys real-only state and leaves the entity as a ghost.

Source: entity.cpp.

The essence of migration:

- source CellApp: real -> ghost;
- target CellApp: read migration stream -> real;
- ghosts and haunts must learn the next real address;
- client Witness state must flush before the switch.

## Onload Restores a Real

The target CellApp reads the migration stream in
`Entity::readRealDataFromStreamForOnload()`. The internal order must match the
offload write order:

1. Read real properties.
2. `TOKEN_CHECK( data, "RealProps" )`.
3. `createReal()`.
4. `pReal_->init( data, CREATE_REAL_FROM_OFFLOAD, ... )`.
5. Read exposed base properties for replay.
6. `startRealControllers()`.

Source: entity.cpp.

Order matters: incompatible EntityDef, property serialization, controller data,
or replay data causes entity migration to fail.

## Destroy Flow

The comment on `Entity::destroy()` states that immediate deletion is impossible
because other objects may still reference the entity:
entity.cpp.

For a real:

- set `inDestroy_` to prevent reentry;
- keep the object alive with `EntityPtr pThis = this`;
- invoke script `onDestroy`;
- if a Base exists, notify it that the Cell entity was lost;
- call `setDestroyed()`;
- delete ghosts;
- notify Cell through `entityDestroyed()`;
- call `convertRealToGhost()`;
- if no Base exists, mark the ID for return;
- clean up from the population's real channel.

Source: entity.cpp.

For a ghost:

- invoke `onGhostDestroyed`;
- retain the real address to process messages that may arrive later.

Source: entity.cpp.

Destruction is not destruction of the C++ object; it is a protocol-state change.

## Zombie Ghost

During restore, `Cell::createEntityInternal()` may treat an existing ghost in
the population as a zombie ghost. The comment notes that critical channels
should prevent restoring an entity that is still alive, but a stale ghost can
still appear during recovery:
cell.cpp.

It repeatedly calls `destroyZombie()` until the entity is destroyed:
cell.cpp.

Recovery is therefore not simple object reconstruction; it must also remove old
state remnants.

## Cell Shutdown

Both `Cell::~Cell()` and `Cell::shutDown()` repeatedly destroy `realEntities_`:
cell.cpp.

Cell is thus one ownership/management boundary for real entities:

- a Cell must handle real entities before deletion;
- entity destruction may trigger callbacks, Base notifications, and ghost deletion;
- deleting a Cell is not merely releasing a container.

## Lifecycle Risks

High-risk points:

- Script callbacks during initialization expose a half-initialized object.
- Offload stream write/read order diverges.
- EntityDef changes make migration streams incompatible.
- Script callbacks during destroy trigger destroy again.
- Base/Cell notification failure creates inconsistent state.
- A stale ghost conflicts with restore.
- An unflushed Witness leaves the client with stale state.
- Channel-version mismatch lets old messages pollute the new real.

These risks explain BigWorld's extensive state assertions,
`callbacksPermitted`, channel versions, backup logic, and ghost machinery.

## Source Trade-Offs

BigWorld chose real/ghost/offload because entity lifecycles must jointly satisfy
spatial authority, visibility, and migration:

- MMO worlds are too large for one process.
- AOI crossing Cell boundaries must still see nearby entities.
- Load balancing must move entity authority from one CellApp to another.
- Client connection and long-term identity remain on BaseApp while spatial logic runs on CellApp.
- Migration must preserve the script object and EntityID continuity.

Costs:

- lifecycle state is complex;
- serialization is strongly coupled to hot reload;
- testing is difficult;
- cross-process tracing is essential;
- script-object and C++ Entity lifetimes are tightly coupled, so destruction, callback permission, and migration streams must be verified together;
- channel versions, buffered messages, and zombie ghosts add message-ordering complexity.

## Source-Code Verification Focus

Lifecycle tests should target state transitions and cross-process sequences:

- Base creation, Cell creation, real creation, and client player creation must maintain consistent ordering.
- No script lifecycle callback may run while `callbacksPermitted` is false.
- Reentrant destroy or a script callback during destroy must not corrupt the state machine.
- Offload/onload field order must remain compatible with EntityDef, backup, and ghost data.
- During real/ghost switching, `ghostSetReal`, `ghostSetNextReal`, and buffered messages must filter stale messages by channel version.
- Zombie-ghost and restore scenarios should cover source CellApp death, missing target real, and failed BaseApp notification.
- Unflushed Witness, pending Cell destruction, and failed Base mailbox updates need regression cases.

## Chapter Boundary

This chapter organizes entity lifecycle as a state machine. AOI, Cell load
balancing, and the offload data stream are expanded in
[AOI, Witness, and Ghost](/architecture/aoi-witness-ghost),
[Cell partitioning and load balancing](/architecture/cell-partition-load-balance),
and [Entity migration and offload](/architecture/entity-migration-offload).

## Related Documentation

- Entity-model foundation: [Entity model: Base/Cell/Client](/architecture/entity-model).
- EntityDef contract: [EntityDef contract and protocol generation](/architecture/entitydef-contract-generation).
- Persistence: [Persistence and DB thread model](/architecture/persistence-db-model).
- Process failure recovery: [Scaling and fault tolerance](/architecture/scaling-fault-tolerance).
