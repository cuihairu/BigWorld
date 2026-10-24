# Entity Model: Base / Cell / Client


<div class="arch-hero">

A BigWorld entity is not a single-object model, but is split into three
authoritative domains. Base carries the session, the database and cross-Cell
coordination; Cell carries space, position, AOI, physics and real/ghost; Client
only receives the view filtered by the Witness and the bandwidth scheduler.

</div>

## Conclusion First

BigWorld's core abstraction is "one game entity has different parts in
different processes":

- `Base`: long-lived identity, database id, Proxy/client connection, Cell
  mailbox, creating/destroying Cell entities, DB writes, backup and archiving.
- `Cell Entity`: the authoritative object inside the space, maintaining position,
  direction, controllers, AOI, real/ghost, event history and client sync.
- `Client Entity`: the client-visible projection, produced by sync through the
  Witness, EntityCache, Bundle and EntityDef.

Key sources:

- The `Base` comment says a Base entity lives on BaseApp, can create the
  associated Cell entity, and communicates with it through a `CellEntityMailBox`,
  see
  base.hpp.
- `Base` provides `createCellEntity()`, `restoreTo()`, `cellBundle()`,
  `sendToCell()`, `writeToDB()`, `backupTo()`, `offload()` and more, see
  base.hpp.
- The `Entity` comment says a Cell entity can be real or ghost, that each entity
  has exactly one authoritative real, and that it may have 0 or more ghosts, see
  entity.hpp.
- `Entity` can reach the base and client mailboxes, and remote calls are declared
  by the `.def` file, see
  entity.hpp.

## Three Layers of Responsibility

<div class="decision-grid">
  <div class="decision-card">
    <h3>Base</h3>
    <p>Identity, session, database, Proxy, global logic, Cell references and cross-Cell coordination.</p>
  </div>
  <div class="decision-card">
    <h3>Cell</h3>
    <p>Spatial authority, position, physics, AOI, real/ghost, controllers and movement.</p>
  </div>
  <div class="decision-card">
    <h3>Client</h3>
    <p>Only receives visible entities and property projections; owns no server-side authoritative state.</p>
  </div>
</div>

This layering is neither MVC nor an ordinary ECS. It is an MMO distributed state
ownership model.

## The Responsibility of Base

`Base` derives from `PyObjectPlus` and is visible to Python scripts. It stores:

- `EntityID`
- `DatabaseID`
- `EntityType`
- The Cell mailbox
- The current `SpaceID`
- The UDP channel
- Timers
- The backup/archive policy
- destroy/create cell pending state

Source: base.hpp.

### The Core Base Data Structure

```cpp
// base.hpp:58 - Base class definition
class Base: public PyObjectPlus
{
public:
    Base( EntityID id, DatabaseID dbID, EntityTypePtr pType );

    EntityID id() const                    { return id_; }
    DatabaseID databaseID() const          { return databaseID_; }
    CellEntityMailBox * pCellEntityMailBox() const { return pCellEntityMailBox_; }
    SpaceID spaceID() const                { return spaceID_; }
    Mercury::UDPChannel & channel()        { return *pChannel_; }
    bool isProxy() const                   { return isProxy_; }
    bool isDestroyed() const               { return isDestroyed_; }

    // Key state flags
    bool isCreateCellPending() const       { return isCreateCellPending_; }
    bool isGetCellPending() const          { return isGetCellPending_; }
    bool isDestroyCellPending() const      { return isDestroyCellPending_; }
    bool hasBeenBackedUp() const           { return hasBeenBackedUp_; }
};
```

**Design points:**
- `id_` is the runtime EntityID, allocated by BaseApp, used for fast in-process
  lookup
- `databaseID_` is the persistence id, initially 0 and assigned by DBApp after
  the first `writeToDB()`
- `pCellEntityMailBox_` points at the Cell Entity; Base sends messages to
  CellApp through it
- `pChannel_` is the UDP channel to CellApp, carrying all Base-Cell traffic
- `isProxy_` marks whether this is a player proxy (with a client connection);
  ordinary Bases have no client

### Important Base Methods

- `writeToDB()`: write to the database.
- `requestCellDBData()`: request persistent data from the Cell.
- `backupTo()` / `writeBackupData()`: back up to another BaseApp.
- `offload()`: migrate out when a BaseApp retires or load shifts.
- `createCellEntity()`: create the matching Cell entity.
- `setCurrentCell()`: update the CellApp address the Cell entity lives on.
- `callBaseMethod()` / `callCellMethod()`: cross-domain method calls.

This shows Base is the centre of an entity's long-term identity and cross-domain
coordination.

## The Responsibility of a Cell Entity

`Entity` also derives from `PyObjectPlus`, but it lives on CellApp and owns the
authoritative logic inside the space.

The source comment lists the key semantics:

- real / ghost.
- Position and direction.
- Controller-driven position changes.
- AOI.
- Client visibility.
- Base mailbox and Client mailbox.

See entity.hpp.

### The Core Entity Data Structure

```cpp
// entity.hpp:138 - Entity class definition
class Entity : public PyObjectPlus
{
public:
    // Position validation: prevents NaN coordinates
    static bool isValidPosition( const Position3D &c )
    {
        const float MAX_ENTITY_POS = 1000000000.f;
        return (-MAX_ENTITY_POS < c.x && c.x < MAX_ENTITY_POS &&
            -MAX_ENTITY_POS < c.y && c.y < MAX_ENTITY_POS &&
            -MAX_ENTITY_POS < c.z && c.z < MAX_ENTITY_POS);
    }

    // Entity initialisation: real and ghost take different paths
    bool initReal( BinaryIStream & data, const ScriptDict & properties,
        bool isRestore,
        Mercury::ChannelVersion channelVersion,
        EntityPtr pNearbyEntity );

    void initGhost( BinaryIStream & data );

    // offload migration
    void offload( CellAppChannel * pChannel, bool isTeleport );
    void onload( const Mercury::Address & srcAddr,
        const Mercury::UnpackedMessageHeader & header,
        BinaryIStream & data );

    // ghost creation
    void createGhost( Mercury::Bundle & bundle );
};
```

**Design points:**
- `initReal()` and `initGhost()` are two entirely different initialisation paths
- A real entity restores its full state from the binary stream (properties,
  controllers, AOI)
- A ghost entity only restores position and the necessary state; it owns no
  authoritative logic
- `isValidPosition()` stops NaN coordinates from polluting the space system
- offload/onload are the core of entity migration: a real becomes a ghost, a
  ghost becomes a real

### What a Cell Entity Stores and Exposes

- `position()` / `direction()`.
- `volatileInfo()`.
- `isReal()` / `pReal()`.
- `realAddr()` / `nextRealAddr()`.
- `Space` / `Cell`.
- `EventHistory`.
- `writeClientUpdateDataToBundle()`.
- `addHistoryEventLocally()`.
- `offload()` / `onload()` / `createGhost()`.

See entity.hpp.

## Real and Ghost

Every Cell Entity has exactly one authoritative real, and may have several
ghosts.

What ghosts are for:

- Letting neighbouring CellApps hold a copy of the entity ahead of time.
- Supporting AOI across Cell boundaries.
- Supporting uninterrupted messages around an entity offload.
- Supporting state decisions when a CellApp dies and during recovery.

What the real is responsible for:

- Owning the authoritative script logic.
- Writing real-only properties.
- Driving position, controllers and event history.
- Maintaining haunts, i.e. the CellApps the ghosts live on.

This is where BigWorld differs from an ordinary sharded server: it does not
merely let an entity exist only on whichever shard its coordinates fall into, but
actively maintains ghosts near boundaries.

## Mailboxes Are Cross-domain References

Base can talk to a Cell entity through `CellEntityMailBox`; a Cell entity can
call remote methods through its Base mailbox and Client mailbox.

This relates directly to
[Communication abstractions and RPC](/architecture/communication-rpc):

- A mailbox is not an object pointer.
- It encapsulates the EntityID, address, component ownership and interface
  methods.
- Method arguments are serialised by EntityDef's `MethodArgs`.
- The transport is carried by a Mercury Bundle / Channel.

So BigWorld's "object method call" is essentially a distributed message.

## Lifecycle Callbacks

The Base-side callbacks include:

- `onDestroy`
- `onOnload`
- `onRestore`
- `onGetCell`
- `onLoseCell`
- `onCreateCellFailure`
- `onWriteToDB`
- `onPreArchive`
- `onTimer`

Source: base.cpp.

The Cell-side callbacks include:

- `onRestore`
- `onGhostCreated`
- `onGhostDestroyed`
- `onWriteToDB`
- `onNoise`
- `onSpaceGone`
- `onGetWitness`
- `onLoseWitness`
- `onWitnessed`
- `onEnteringCell`
- `onEnteredCell`
- `onLeavingCell`
- `onLeftCell`

Source: entity.cpp.

These callbacks are script-level extension points, but they are also risk points:
a callback may destroy, migrate or offload the entity, so the C++ code often has
to guard against object lifetime changes.

## The Cross-domain Method Call Chain

Method calls between Base and Cell go through mailboxes and Mercury messages.
This is the core of BigWorld's distributed object model: one entity's method
call may cross a process boundary.

### Cell -> Base Call Chain

**Overview:** when a Cell Entity needs to call a Base method (such as telling
Base to write the database, or asking Base to create a new Cell), it serialises
the call as a Mercury message through `callBaseMethod()` and sends it to BaseApp.

**Source entry:** entity.cpp:5255

```cpp
// entity.cpp:5255 - Cell Entity calls a Base method
bool Entity::callBaseMethod( int methodIndex, BinaryIStream & data,
    bool isForGhost )
{
    // 1. Look up the method definition
    const MethodDescription * pMethod = 
        pType_->description().base().internalMethod( methodIndex );
    
    // 2. Create the Base Entity mailbox
    BaseEntityMailBox mailbox( pType_, id_, baseAddr_ );
    
    // 3. Get the output stream and write the method call
    Mercury::Bundle & bundle = mailbox.startMessage( 
        BaseAppIntInterface::callBaseMethod );
    bundle << methodIndex;
    bundle.transfer( data, data.remainingLength() );
    
    return true;
}
```

**Flowchart:**

<MermaidDiagram title="Cell -> Base method call flow">
sequenceDiagram
    participant Cell as Cell Entity
    participant Mailbox as BaseEntityMailBox
    participant Bundle as Mercury Bundle
    participant Channel as UDP Channel
    participant BaseApp as BaseApp
    participant Base as Base Entity

    Cell->>Mailbox: callBaseMethod(index, data)
    Mailbox->>Bundle: startMessage(callBaseMethod)
    Bundle->>Bundle: write methodIndex
    Bundle->>Bundle: write argument data
    Bundle->>Channel: send message
    Channel->>BaseApp: UDP packet
    BaseApp->>Base: dispatch callBaseMethod
    Base->>Base: execute Python method
</MermaidDiagram>

**Detailed walkthrough:**

1. **Method index lookup**: `methodIndex` is the internal index of the method in
   EntityDef, generated when the `.def` file is compiled. Cell and Base share one
   EntityDef, so the index agrees.

2. **Mailbox encapsulation**: `BaseEntityMailBox` is not a simple pointer; it
   encapsulates the `EntityID`, the target address (the BaseApp address) and the
   entity type. It knows how to route messages.

3. **Bundle serialisation**: the arguments are transferred straight from
   `BinaryIStream` into the bundle, avoiding an extra copy. The Bundle manages
   reliable/unreliable semantics internally.

4. **Asynchronous semantics**: this is a one-way message; the Cell does not wait
   for a Base reply. If a reply is needed, Base calls back via
   `callCellMethod()`.

### Base -> Cell Call Chain

**Overview:** when Base needs to call a Cell method (such as telling the Cell to
move an entity, or to create a child entity), it serialises the call through
`callCellMethod()` and sends it to CellApp.

**Source entry:** base.cpp:1391

```cpp
// base.cpp:1391 - Base calls a Cell method
bool Base::callCellMethod( int methodIndex, BinaryIStream & data )
{
    // 1. Check whether the Cell mailbox exists
    if (!pCellEntityMailBox_)
    {
        ERROR_MSG( "Base::callCellMethod: No cell mailbox\n" );
        return false;
    }
    
    // 2. Send through the Cell mailbox
    Mercury::Bundle & bundle = pCellEntityMailBox_->startMessage(
        CellAppIntInterface::callCellMethod );
    bundle << id_;
    bundle << methodIndex;
    bundle.transfer( data, data.remainingLength() );
    
    return true;
}
```

**Flowchart:**

<MermaidDiagram title="Base -> Cell method call flow">
sequenceDiagram
    participant Base as Base Entity
    participant Mailbox as CellEntityMailBox
    participant Bundle as Mercury Bundle
    participant Channel as UDP Channel
    participant CellApp as CellApp
    participant Entity as Cell Entity

    Base->>Mailbox: callCellMethod(index, data)
    Mailbox->>Bundle: startMessage(callCellMethod)
    Bundle->>Bundle: write EntityID
    Bundle->>Bundle: write methodIndex
    Bundle->>Bundle: write argument data
    Bundle->>Channel: send message
    Channel->>CellApp: UDP packet
    CellApp->>Entity: dispatch callCellMethod
    Entity->>Entity: execute Python method
</MermaidDiagram>

**Detailed walkthrough:**

1. **Cell mailbox check**: Base must have the corresponding Cell Entity before it
   can call a Cell method. If the Cell is destroyed or was never created,
   `pCellEntityMailBox_` is null.

2. **EntityID transport**: a CellApp holds many entities, so the message has to
   carry the `EntityID` for CellApp to route it to the right Entity.

3. **Address routing**: `pCellEntityMailBox_` knows the target CellApp address,
   which may change as entities migrate. Base updates the address through
   `setCurrentCell()`.

4. **Failure handling**: if the CellApp is dead or the entity has migrated, the
   message is lost. BigWorld handles this with CellApp death detection and Base
   recovery.

### Property Sync to the Client

**Overview:** property changes on a Cell Entity have to be synced to the client.
This is not a real-time full sync: after Witness and AOI filtering, only visible
and changed properties are sent.

**Source entry:** entity.cpp:2475

```cpp
// entity.cpp:2475 - Cell Entity writes client update data
void Entity::writeClientUpdateDataToBundle( Mercury::Bundle & bundle,
    bool isFullUpdate )
{
    // 1. Write vehicle changes (if any)
    this->writeVehicleChangeToBundle( bundle );
    
    // 2. Walk the event history and write property changes
    EventHistory::iterator iter = eventHistory_.begin();
    while (iter != eventHistory_.end())
    {
        if (iter->isForClient())
        {
            iter->writeToBundle( bundle );
        }
        ++iter;
    }
    
    // 3. Write volatile data (position, direction and other high-frequency data)
    if (volatileInfo_.hasVolatile())
    {
        this->writeVolatileDetailedDataToBundle( bundle );
    }
}
```

**Flowchart:**

<MermaidDiagram title="Cell -> Client property sync flow">
sequenceDiagram
    participant Cell as Cell Entity
    participant History as EventHistory
    participant Bundle as Mercury Bundle
    participant Witness as Witness
    participant Cache as EntityCache
    participant Client as Client

    Cell->>History: record property change
    Cell->>Cell: writeClientUpdateDataToBundle()
    Cell->>Bundle: write vehicle change
    Cell->>Bundle: write event history
    Cell->>Bundle: write volatile data
    Bundle->>Witness: send to the Witness
    Witness->>Cache: update the EntityCache
    Cache->>Client: sync to the client
</MermaidDiagram>

**Detailed walkthrough:**

1. **EventHistory mechanism**: property changes are not sent immediately but
   recorded in `EventHistory`. Each event is marked for whether it has to sync
   to the client and whether it is reliable.

2. **Volatile data**: high-frequency data such as position and direction uses
   `volatileInfo_` to control the sync rate. Different entity types can have
   different sync policies.

3. **Witness filtering**: only entities inside the client's AOI are synced. The
   Witness manages the visible entity set of each client.

4. **EntityCache caching**: the client keeps an `EntityCache` of known entities to
   avoid repeated syncs. Only property changes are sent.

5. **Bandwidth control**: the Bundle has internal bandwidth limits; the amount
   sent within a single tick is capped to prevent congestion.

## The Entity Recovery Call Chain

After a CellApp crashes, Base recovers the Cell Entity through `restoreTo()`.
This is the core of BigWorld's high availability: even if a CellApp dies, the
player's Base Entity survives and can be restored onto a new CellApp.

**Overview:** when a CellApp dies, CellAppMgr detects the death, notifies
BaseAppMgr, which notifies all BaseApps. Each BaseApp checks whether one of its
entities has its Cell on the dead CellApp and, if so, restores it onto a new
CellApp from backup data.

**Source entry:** base.cpp:3810

```cpp
// base.cpp:3810 - Base restores the Cell Entity
void Base::restoreTo( const Mercury::Address & addr,
    const Mercury::UnpackedMessageHeader & header,
    BinaryIStream & data )
{
    // 1. Read the backup data
    this->readBackupData( data );
    
    // 2. Check whether there is a Cell Entity
    if (!this->hasCellEntity())
    {
        return;
    }
    
    // 3. Create the new Cell Entity
    this->createCellEntityOn( addr );
    
    // 4. Update the Cell address
    this->setCurrentCell( spaceID_, addr );
}
```

**Flowchart:**

<MermaidDiagram title="Entity recovery after a CellApp crash">
sequenceDiagram
    participant CellApp as CellApp (dead)
    participant CellAppMgr as CellAppMgr
    participant BaseAppMgr as BaseAppMgr
    participant BaseApp as BaseApp
    participant Base as Base Entity
    participant NewCellApp as new CellApp

    CellApp->>CellAppMgr: death detection (heartbeat timeout)
    CellAppMgr->>BaseAppMgr: notify CellApp death
    BaseAppMgr->>BaseApp: broadcast CellApp death
    BaseApp->>Base: check whether the Cell is on the dead CellApp
    Base->>Base: read cellBackupData_
    Base->>NewCellApp: createCellEntityOn()
    NewCellApp->>NewCellApp: restore the Cell Entity
    Base->>Base: setCurrentCell(new address)
</MermaidDiagram>

**Detailed walkthrough:**

1. **Death detection**: CellAppMgr detects a CellApp death through heartbeats. If
   a CellApp sends no heartbeat for a while, CellAppMgr marks it dead.

2. **Backup data**: Base periodically uses `backupTo()` to back up the Cell
   Entity's state to other BaseApps. The backup data lives in `cellBackupData_`.

3. **Recovery timing**: once Base receives the CellApp death notice it
   immediately checks whether its Cell is on the dead CellApp, and if so recovers
   from the backup data.

4. **Choosing a new CellApp**: BaseApp picks a live CellApp to restore the Cell
   Entity onto. The selection algorithm considers load, spatial position and
   similar factors.

5. **State restoration**: the backup data holds the entity's properties,
   position, AOI state and so on. After restoration the Entity keeps running on
   the new CellApp, and the client may notice a brief hitch.

**Why it is designed this way:**

- **Players do not disconnect**: the Base Entity always stays on a BaseApp, so
  even if a CellApp crashes the player connection is not dropped.
- **Fast recovery**: recovering from backup data is far faster than loading from
  the database, which reduces player waiting time.
- **Transparent migration**: as far as the client is concerned it just feels like
  a brief hitch, with no need to log in again.

## Why It Is Split This Way

This split solves several MMO problems:

- The player connection and the space simulation need not live in the same
  process.
- Spatial load can be split by Cell and migrated.
- Base is long-lived, while Cells can be created, destroyed and migrated with
  position.
- Database writes are coordinated centrally by Base, which requests Cell data
  when needed.
- The Client only sees the projection filtered by the Witness and does not need
  to know the complete server-side state.

This is one of the core design values of BigWorld.

## The Costs

The costs are just as clear:

- An entity's state is scattered across Base, Cell, Client, DB and Ghost.
- The lifecycle is complex; creation, destruction and migration all require
  cross-process coordination.
- Mailboxes can go stale, and channel versioning has to handle old packets.
- EntityDef changes affect several domains.
- Script callbacks can change object state in the middle of a migration.
- Debugging one entity problem means reading BaseApp, CellApp, DBApp and client
  logs across the board.

## Trade-offs in the Source

The source trade-offs of the three-layer Base/Cell/Client model are about
splitting different authoritative domains:

- Base carries long-term identity, database coordination, Proxy and cross-Cell
  lifecycle management.
- Cell carries space, position, AOI, physics and real/ghost state.
- Client only receives the entity projection filtered by the Witness plus the
  exposed methods it may call.
- The DB only persists the EntityDef persistent view; it does not store the
  complete runtime object graph.

The source cost is that one entity problem usually propagates across several
domains: mailboxes, channels, EntityDef, DBID, Cell real/ghost and the client
entity cache may all take part in the same failure chain.

## What Source-Verified Tests Should Cover

Entity model tests should cover the domain boundaries:

- Base-only, Cell-only, Client-visible and Persistent properties should enter the
  corresponding streams according to the EntityDef data domains.
- When creating a Cell entity, the Base mailbox, the Cell mailbox and the client
  player relationship should be consistent.
- After a Cell is lost, offloaded or restored, the Cell mailbox held by Base must
  not point at the old address.
- Client methods may only be called through the exposed method range and cannot
  bypass the Proxy to connect straight to a CellApp.
- DB writes should be coordinated by Base, requesting Cell persistent data when
  necessary.
- A Ghost should only serve as a cross-Cell copy and migration aid, and must not
  be mistaken for a client entity connection.

## Scope of This Chapter

This chapter explains the three-layer entity model. The next chapter continues
with AOI, Witness and Ghost, which is the core of client visibility and
cross-Cell boundary consistency.