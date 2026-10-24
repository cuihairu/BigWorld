# AOI, Witness and Ghost


<div class="arch-hero">

BigWorld's AOI is not a simple "broadcast to everything within a radius". The
Witness maintains the client's view, bandwidth budgets, entity aliases, reliable
position updates and SpaceData sync; the Ghost maintains a copy across Cell
boundaries, providing continuity for AOI and entity migration.

</div>

## Conclusion First

How AOI, Witness and Ghost relate:

- A `Witness` attaches to a real entity that has a client, and manages that
  client's Area of Interest.
- `AoITrigger` converts RangeList enter/leave events into
  `Witness::addToAoI()` / `removeFromAoI()`.
- `EntityCache` records the per-client send state of one entity inside that
  Witness's view; it is not a plain set element.
- A client does not see a new entity in one create - it is a two-stage
  handshake: `enterAoI -> requestEntityUpdate -> createEntity`.
- The Witness update queue is scheduled by priority heap, AoIUpdateScheme,
  packet budget and bandwidth deficit.
- A `Ghost` is a copy of an entity on a neighbouring CellApp, used for
  cross-Cell AOI, offload and recovery.
- `EntityGhostMaintainer` periodically maintains ghosts and decides whether a
  real entity should be offloaded to another Cell.

Key sources:

- The `Witness` comment says it is created when a client attaches to an entity,
  and that its main activity is managing the AoI list, see
  witness.hpp.
- The `Entity` comment says the AoI uses an equal-length range on x and z plus
  hysteresis, with different radii for entering and leaving the AoI, see
  entity.hpp.
- `Witness` maintains `aoiMap_`, `entityQueue_`, `aoiRadius_`, `aoiHyst_`,
  `pAoIRoot_`, `freeAliases_` and the reliable position history, see
  witness.hpp.
- The `EntityCache` flags include `ENTER_PENDING`, `REQUEST_PENDING`,
  `CREATE_PENDING`, `GONE`, `WITHHELD`, `REFRESH`, see
  entity_cache.hpp.
- `AoITrigger::triggerEnter()` / `triggerLeave()` are at
  witness.cpp.
- The send-queue main loop of `Witness::update()` is at
  witness.cpp.
- `EntityGhostMaintainer::check()` checks offload first, then marks, creates and
  deletes haunts, see
  entity_ghost_maintainer.cpp.

## What a Witness Is

A Witness is not the client connection itself. The client connection mostly
lives in Proxy/BaseApp; the Witness lives on CellApp and represents "what this
player entity can see".

The responsibilities of `Witness` include:

- Maintaining the set of entities inside the AoI.
- Deciding the entity enter/create/leave messages.
- Managing entity ID aliases to shrink client packets.
- Sending SpaceData changes.
- Sending its own detailed position and other entities' volatile updates.
- Scheduling updates against the bandwidth budget.
- Handling script-driven control: manual AoI, withhold, position detailed.

Source: witness.hpp.

The `Witness` constructor also performs a few key initialisations:

- Uses `defaultAoIRadius` as the initial AoI radius.
- Sets `pAoIRoot_` by default to this entity's own `RangeListNode`.
- Initialises `freeAliases_`, keeping `NO_ID_ALIAS` in reserve.
- Registers the witness with `CellApp`.
- Sends the first `tickSync` to Proxy.
- If this is the initial real creation, sends `createCellPlayer` to the client.

Source: witness.cpp.

## AoI Hysteresis

**Overview:** the AoI uses hysteresis to avoid flapping at the boundary.
Entities enter the AoI on the inner radius and leave it on the outer radius.
This stops entities that hover on the boundary from repeatedly triggering
enter/leave messages.

**Source entry:** entity.hpp:106

```cpp
// entity.hpp:106 - AoI comment
/*
 *  Area of Interest, or "AoI" is an important concept for all BigWorld
 *  entities which belong to clients. The AoI of an entity is the area around
 *  it which the entity's client (if it has one) is aware of. This is used for
 *  culling the amount of data that the client is sent. The actual shape of an
 *  AoI is defined by a range of equal length on both the x and z axis, with a
 *  hysteresis area of similar shape extending out further. An Entity enters
 *  another Entity's AoI when it enters the AoI area, but doesn't leave it
 *  until it has moved outside the hysteresis area.
 */
```

**Flowchart:**

<MermaidDiagram title="AoI hysteresis mechanism">
flowchart TD
    A[Entity moves] --> B{Inside inner AoI?}
    B -- yes --> C[Trigger enterAoI]
    C --> D[Send enter/create message]
    D --> E[Add to Witness AoI list]
    E --> F{Moved outside outer AoI?}
    F -- no --> E
    F -- yes --> G[Trigger leaveAoI]
    G --> H[Send leave/delete message]
    H --> I[Remove from Witness AoI list]
</MermaidDiagram>

**Detailed walkthrough:**

1. **Inner radius**: `aoiRadius_` defines the inner AoI range. When an entity
   enters it, `enterAoI` is triggered.

2. **Outer radius**: `aoiRadius_ + aoiHyst_` defines the outer AoI range. Only
   when an entity leaves the outer range is `leaveAoI` triggered.

3. **Hysteresis effect**: entering and leaving use different radii, which
   prevents boundary flapping. For example:
   - inner radius 50m, outer radius 60m
   - entity A enters the AoI at 49m
   - entity A moves to 55m (still inside the AoI)
   - entity A moves to 61m (leaves the AoI)
   - entity A moves to 59m (still outside the AoI, because it is inside the
     outer range)
   - entity A moves to 51m (re-enters the AoI)

4. **Network optimisation**: fewer enter/leave messages, which lowers network
   overhead and client-side object churn.

### AoI Triggering

**Source entry:** witness.cpp:3586

```cpp
// witness.cpp:3586 - AoI trigger
void AoITrigger::triggerEnter( RangeListNode * pOther )
{
    // 1. Get the target entity
    Entity * pEntity = static_cast<Entity *>(pOther);
    
    // 2. Check whether it is already in the AoI
    if (pWitness_->isInAoI( pEntity ))
    {
        return;
    }
    
    // 3. Add to the AoI
    pWitness_->addToAoI( pEntity );
}

void AoITrigger::triggerLeave( RangeListNode * pOther )
{
    // 1. Get the target entity
    Entity * pEntity = static_cast<Entity *>(pOther);
    
    // 2. Check whether it is in the AoI
    if (!pWitness_->isInAoI( pEntity ))
    {
        return;
    }
    
    // 3. Remove from the AoI
    pWitness_->removeFromAoI( pEntity );
}
```

**Key details:**

- `RangeListNode` is a range tree node used for efficient spatial queries.
- `triggerEnter()` and `triggerLeave()` are called by the range tree when an
  entity enters or leaves the range.
- `addToAoI()` and `removeFromAoI()` update the Witness's AoI list and the
  EntityCache.

What actually triggers enter/leave is not a full scan every tick - it is
`AoITrigger` attached to a `RangeList`:

- `AoITrigger` derives from `RangeTrigger`; its constructor first visits the
  large entities already covered, then inserts itself into the RangeList, see
  witness.cpp.
- `triggerEnter()` ignores itself and manual AoI entities, then calls
  `Witness::addToAoI()`, see
  witness.cpp.
- `triggerLeave()` likewise ignores itself and manual AoI entities, then calls
  `Witness::removeFromAoI()`, see
  witness.cpp.

`setAoIRadius()` first clamps the radius to `maxAoIRadius`, then calls
`pAoITrigger_->setRange()`, and updates the trigger on the entity or on the
moving node depending on the AoI root type, see
witness.cpp.
The related configuration is at
cellapp_config.cpp.

This means the AoI radius, max radius and ghostDistance are not documentation
notions but part of the CellApp configuration constraints.
`CellAppConfig::sanityCheckSettings()` also checks the relations between
`maxAoIRadius` and `ghostDistance`, and between `defaultAoIRadius` and
`maxAoIRadius`, see
cellapp_config.cpp.

## EntityCache and the Send Queue

Inside a Witness:

- `entityQueue_`: send queue of known entities.
- `aoiMap_`: map of entity caches inside the AoI.
- `bandwidthDeficit_`: bandwidth deficit.
- `maxPacketSize_`: per-update packet size constraint.
- `freeAliases_[256]`: pool of client entity aliases.

Source: witness.hpp.

This shows that BigWorld's AoI update is not "walk every entity once and send
it to the client", but caches entity state and schedules against a budget.

`EntityCache` is the state machine of that queue. Its flag definitions include:

- `ENTER_PENDING`: waiting to send `enterAoI` to the client.
- `REQUEST_PENDING`: enter sent, waiting for the client's `requestEntityUpdate`.
- `CREATE_PENDING`: request received, waiting to send `createEntity`.
- `GONE`: waiting to be removed from the priority queue.
- `WITHHELD`: the entity is in the AoI but is not sent to the client for now.
- `REFRESH`: the client view has to be deleted and re-entered.
- `PRIORITISED`: the next `Witness::update()` should send this first.
- `MANUALLY_ADDED` / `ADDED_BY_TRIGGER`: distinguishes a script-driven manual
  AoI from a RangeList automatic trigger.

Source: entity_cache.hpp.

On entering the AoI, `Witness::addToAoI()` will:

1. Ignore destroyed entities and entities that cannot appear on a client.
2. If the cache already exists and is `GONE`, call `EntityCache::reuse()`.
3. If there is no cache, add it to `aoiMap_`, set `ENTER_PENDING`, then add it
   to `entityQueue_`.
4. Record whether the source is a manual add or a trigger add.
5. Outside the offload case, call the entity script's `onEnteredAoI`.

Source: witness.cpp.

On leaving the AoI, `Witness::removeFromAoI()` does not delete the cache
immediately. It clears the manual/trigger source markers; if another source is
still present, it returns right away. When the entity really leaves, it only
sets `GONE` and calls the script's `onLeftAoI`, see
witness.cpp.

This deferred deletion matters: the leave message has to stay ordered against
the following bundle, event numbers and alias recycling, so it cannot simply
vanish from every structure at the moment the RangeList fires.

## The enter/request/create/leave Handshake

The first time a client sees an AoI entity it does not receive the full entity
data outright. In the source it is a small state machine:

<MermaidDiagram title="State machine for an AoI entity reaching the client">
stateDiagram-v2
  [*] --> ENTER_PENDING: addToAoI
  ENTER_PENDING --> REQUEST_PENDING: sendEnter
  REQUEST_PENDING --> CREATE_PENDING: requestEntityUpdate
  CREATE_PENDING --> UPDATABLE: sendCreate
  UPDATABLE --> GONE: removeFromAoI
  GONE --> [*]: leaveAoI + delete cache
  GONE --> REFRESH: reuse but event history trimmed
  REFRESH --> ENTER_PENDING: delete + re-enter
</MermaidDiagram>

`Witness::sendEnter()` allocates an ID alias, changes the cache from
`ENTER_PENDING` to `REQUEST_PENDING`, and then sends `enterAoIOnVehicle` or
`enterAoI` depending on whether the target is on a vehicle, see
witness.cpp.

The client then comes back through `requestEntityUpdate`.
`Witness::requestEntityUpdate()` will:

- Confirm the entity is still in the AoI.
- Reject the abnormal cases of a request for itself or for a non-pending
  entity.
- Change `REQUEST_PENDING` to `CREATE_PENDING`.
- Validate that the number of LoD event stamps from the client does not exceed
  the cache's LoD count.
- Record the LoD event numbers, then rejoin the send queue.

Source: witness.cpp.

`Witness::sendCreate()` clears `CREATE_PENDING`, writes vehicle changes, picks
`createEntity` or `createEntityDetailed` depending on whether the entity is
volatile, then writes the entity id, client type id, position, direction and
outer detail level properties, see
witness.cpp.

On the way out, `EntityCache::addLeaveAoIMessage()` sends `leaveAoI` and writes
the LoD event numbers back into the bundle, see
entity_cache.cpp.
Then `Witness::onLeaveAoI()` recycles the ID alias, see
witness.cpp.

What this handshake design buys:

- enter is cheap: it first tells the client "there is such an entity".
- create can send only the delta, based on the event stamps and LoD state the
  client already has.
- leave carries event stamps, which gives the basis for reuse or refresh when
  an entity leaves and re-enters quickly.
- aliases are only allocated for volatile entities, shrinking the frequent
  position update packets.

`Witness::allocateIDAlias()` explicitly allocates an alias only for entities
that have volatile data; with no free alias or a non-volatile entity it returns
`NO_ID_ALIAS`, see
witness.cpp.

## Witness Send Budget and Priority

`Witness::update()` is the core hot path of AOI sending. It first computes the
desired packet size for this tick:

```cpp
const float throttle = CellApp::instance().emergencyThrottle();
const int desiredPacketSize = int(maxPacketSize_ * throttle) -
        bandwidthDeficit_ + bundle.size();
```

Source: witness.cpp.

Then it sends in a fixed order:

1. SpaceData changes.
2. Reference position, or the player's detailed position.
3. Entities in the vehicle stack, forced to the front.
4. The highest-priority entity in the `entityQueue_` heap.
5. Stop sending for this tick based on bundle size, `desiredPacketSize` and
   `witnessUpdateMaxPriorityDelta`.
6. Update `bandwidthDeficit_`, capping it when necessary.
7. Re-maintain the heap.
8. Flush to Proxy and write the next tick's `tickSync`.

Source: witness.cpp.

`handleStateChange()` is responsible for turning pending flags into client
messages:

- `GONE`: send leave and remove from the AoI.
- `WITHHELD`: if the client already knows the entity, delete it first, then set
  `ENTER_PENDING` again.
- `ENTER_PENDING`: send enter.
- `CREATE_PENDING`: send create and update priority.
- `REFRESH`: delete, then enter again.

Source: witness.cpp.

An ordinary updatable entity goes through `sendQueueElement()`. It computes the
LoD priority first, then calls the entity's `writeClientUpdateDataToBundle()` to
write property/position updates, see
witness.cpp.

Priority is not plain distance ordering. `EntityCache::updatePriority()` uses
`AoIUpdateSchemes::apply(updateSchemeID_, distSQ)` to compute a delta, limits the
delta growth with `witnessUpdateDeltaGrowthThrottle`, and finally accumulates
it into the cache priority, see
entity_cache.ipp.

The default formula of an `AoIUpdateScheme` is
`(distance * distanceWeighting + 1) * weighting`. If both min and max delta are
0, the scheme is treated as coincident and the LoD priority is handled as 0, see
aoi_update_schemes.hpp
and
aoi_update_schemes.cpp.

The real effect of this mechanism:

- Nearby entities are naturally updated more often.
- Distant entities do not eat the whole budget just by being in the queue every
  tick.
- The vehicle stack goes first, which avoids jitter in the player's position
  relative to their vehicle.
- Emergency throttle and bandwidth deficit feed network pressure back into this
  tick's send volume.

## Reliable Position Updates

`Witness::addDetailedPlayerPosition()` decides whether to send the player's own
detailed position, and whether unchanged positions can be skipped, see
witness.cpp.

Key logic:

- Non-volatile or witness-controlled entities are not sent.
- Records `lastSentReliableGameTime_`, `lastSentReliablePosition_`,
  `lastSentReliableDirection_`.
- If position and direction are unchanged, and the configured time window
  allows it, the send can be skipped.
- In some cases the next message is marked reliable.

After sending a reliable position, `onSendReliablePosition()` records the last
reliable position, see
witness.cpp.

This relates directly to Mercury's reliable UDP design: positions are normally
volatile, but certain reference positions or detailed positions must be
reliable, otherwise later relative positions lose their basis.

## tickSync

At the end of the Witness update it sends `tickSync` to Proxy, telling Proxy
that the following messages belong to the next tick:

Source: witness.cpp.

This shows that client sync is not a pure real-time stream, but batched around
GameTime/Tick boundaries.

## What a Ghost Is For

A Ghost is a cross-Cell copy of a real entity. It serves three goals:

- Let a neighbouring CellApp provide the view of a remote entity to local
  Witnesses.
- Let a real entity offload to a neighbouring Cell with the target Cell already
  holding a base state.
- Provide a basis for recovery and buffering when a CellApp dies or messages
  arrive out of order.

`check()` of `EntityGhostMaintainer` will:

1. If the current Cell allows offload, check whether the entity should migrate
   to its home cell.
2. Mark all haunts.
3. Create or unmark the haunts that are still needed.
4. Delete invalid haunts.

Source: entity_ghost_maintainer.cpp.

## The Full AOI Call Chains

### Entity Enters the AOI

The full call chain for an entity entering the player's view:

Source entry: witness.cpp:3586

<div class="flow-strip">
  <span class="flow-node">AoITrigger::triggerEnter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">ignore itself and manual AoI</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Witness::addToAoI()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">create EntityCache</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">set ENTER_PENDING</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">join entityQueue_</span>
</div>

### Entity Leaves the AOI

The full call chain for an entity leaving the player's view:

Source entry: witness.cpp:2096

<div class="flow-strip">
  <span class="flow-node">AoITrigger::triggerLeave()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">EntityCache::addLeaveAoIMessage()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">send leaveAoI</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">write LoD event numbers</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Witness::onLeaveAoI()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">recycle ID alias</span>
</div>

### Witness Update Send

The full call chain for the per-tick Witness update that sends entity data:

Source entry: witness.cpp:1088

<div class="flow-strip">
  <span class="flow-node">Witness::update()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">compute desiredPacketSize</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">send SpaceData changes</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">send reference position</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">send vehicle stack entities</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">send entityQueue_ by priority</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">update bandwidthDeficit_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">flush to Proxy</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">write tickSync</span>
</div>

### Ghost Creation and Maintenance

The call chain for ghost entity creation and maintenance:

Source entry: entity_ghost_maintainer.cpp:46

<div class="flow-strip">
  <span class="flow-node">EntityGhostMaintainer::check()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">check offload state</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">mark all haunts</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">create/unmark haunts</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">delete invalid haunts</span>
</div>

## Ghost Preparation Before Offload

`EntityGhostMaintainer::checkEntityForOffload()` looks up
`Space::pCellAt(x,z)` from the entity's position:

- If the home cell is the current cell, do not offload.
- If the target cell is being deleted, do not offload.
- If the target CellApp channel does not exist or is unhealthy, do not offload.
- Otherwise add the target to the offload list.

Source: entity_ghost_maintainer.cpp.

This reflects BigWorld's safety boundary: coordinates crossing a boundary are
not a reason to migrate blindly - the target Cell and Channel have to be
confirmed available.

## Ghost Message Ordering

When an entity offloads, the old real tells the ghosts the next real address:

- `RealEntity::destroy()` walks the haunts.
- Sends `ghostSetNextReal` to every non-target haunt.
- Then resets/destroys the current real channel.

Source: real_entity.cpp.

`Entity::ghostSetNextReal()` sets `nextRealAddr_` on receipt and plays back the
buffered ghost messages, see
entity.cpp.

That is the practical meaning of channel versioning and buffered ghost
messages: offload reorders messages, so the Ghost must know when to start
accepting messages from the new real.

## Trade-offs in the Source

The AoI/Ghost code is not a single "spatial query result" but several pieces of
state that together maintain client visibility:

- `AoITrigger` only converts RangeList enter/leave events into Witness
  operations.
- `EntityCache` holds the client-side pending/created/gone/withheld/refresh
  state plus the LoD event numbers.
- `Witness::update()` writes bundles in batches according to packet budget,
  bandwidth deficit, priority heap and vehicle stack.
- `EntityGhostMaintainer` maintains haunts and confirms the target Cell and
  Channel are available before offload.
- `ghostSetNextReal` and buffered ghost messages on `RealEntity` / `Entity`
  handle the real switch and message reordering during migration.

This design binds visibility, bandwidth, reliable positions, cross-Cell copies
and migration ordering together. The benefit is that clients do not lose
context because of a Cell boundary or a brief AoI exit; the cost is that
debugging has to look at Witness, EntityCache, RealEntity, Haunt, Channel and
event history at the same time.

## Source Boundaries

A few boundaries that are easy to misread must be handled the way the source
does:

- `requestEntityUpdate()` logs an error and returns when asked for a
  non-pending cache; when the LoD stamps exceed the count it logs `CHEAT` and
  truncates, rather than disconnecting the client.
- `EntityCache::reuse()` can only be reused safely while the cache is still
  `GONE` and its event history has not been trimmed; if `lastEventNumber` is
  older than `lastTrimmedEventNumber`, `REFRESH` is set.
- `handleStateChange()` on `REFRESH` calls `deleteFromClient()` first and then
  `setEnterPending()`, so the client walks enter/request/create again.
- `removeFromAoI()` distinguishes manual and trigger sources; as long as either
  source remains, leave must not be sent.
- `allocateIDAlias()` only allocates an alias for entities that have volatile
  data and only when the alias pool has a free slot.

## What Source-Verified Tests Should Cover

Tests for AOI/Witness/Ghost should cover the state ordering, not just whether
the client eventually has the entity:

- After an entity enters the AoI it must first be `ENTER_PENDING`, and only
  after enter is sent become `REQUEST_PENDING`.
- A client `requestEntityUpdate` may only act on a pending cache; a request for
  a non-pending entity should be rejected.
- When the LoD event stamp count exceeds the cache's LoD count, it should be
  truncated and a `CHEAT` log written - never an out-of-range LoD event number
  write.
- `removeFromAoI()` only clears its own source; while either manual or trigger
  remains, leave must not be sent.
- A `GONE` cache should have leave sent in `Witness::update()`, with the alias
  recycled.
- `EntityCache::reuse()` must trigger a refresh when the event history has been
  trimmed, instead of reusing the stale cache.
- `setAoIRadius()` must clamp to `maxAoIRadius`.
- `witnessUpdateMaxPriorityDelta` and the packet budget should limit the number
  of entities sent in one tick.
- Entities in the vehicle stack should be sent ahead of the ordinary heap
  queue.
- Replacing the AoI root during offload must not trigger `onEnteredAoI` twice.

## Scope of This Chapter

This chapter explains client visibility and cross-Cell copies. The next
chapter analyses Cell partitioning and load balancing, which is why these
ghosts and offloads happen at all.