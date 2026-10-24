# Entity Migration and Offload


<div class="arch-hero">

BigWorld's entity migration is not a memcpy of an object into another process. It
is a state machine completed jointly by Real/Ghost state transitions, script
callbacks, property serialisation, channel migration, ghost sign-off, buffered
messages and Cell boundary control.

</div>

## Conclusion First

The core path of a Cell entity offload:

1. The Cell periodically runs `checkOffloadsAndGhosts()`.
2. `OffloadChecker` walks the real entities.
3. `EntityGhostMaintainer` decides which home cell the entity's position belongs
   to.
4. The target CellApp channel is confirmed healthy and the ghost prepared.
5. `Cell::offloadEntity()` calls `onLeavingCell`.
6. `Entity::offload()` writes the `onload` message to the target CellApp.
7. `Entity::convertRealToGhost()` writes the real data, sets `nextRealAddr_` and
   destroys the real part.
8. The target CellApp reads the real data and turns the ghost into a real.
9. The old real tells the other ghosts the next real address.
10. The ghost plays back buffered messages.

Key sources:

- `Cell::checkOffloadsAndGhosts()` creates the `OffloadChecker`, see
  cell.cpp.
- `OffloadChecker::run()` walks the real entities and sends the offloads, see
  offload_checker.cpp.
- `EntityGhostMaintainer::checkEntityForOffload()` decides whether to migrate
  based on `Space::pCellAt()` and the target channel's state, see
  entity_ghost_maintainer.cpp.
- `Cell::offloadEntity()` calls `onLeavingCell` and turns the real into a ghost,
  see
  cell.cpp.
- `Entity::offload()` writes `CellAppInterface::onload` and calls
  `convertRealToGhost()`, see
  entity.cpp.
- `Entity::readRealDataFromStreamForOnloadInternal()` reads the real data in the
  same order the offload writes it, see
  entity.cpp.

## Offload Trigger Conditions

An entity does not migrate the moment it crosses a boundary.
`EntityGhostMaintainer::checkEntityForOffload()` checks:

- `Space::pCellAt(position.x, position.z)` finds the home cell.
- The home cell must not be the current cell.
- The home cell must not be delete-pending.
- The target `CellAppChannel` must exist.
- The target channel must be `isGood()`.

Source: entity_ghost_maintainer.cpp.

This avoids migrating entities to an unavailable or deleting Cell.

## Ghost Maintenance Order

`OffloadChecker::run()` does more than check a boundary crossing. While the
Space is not shutting down it walks this Cell's `realEntities()`, hands each
real entity to `EntityGhostMaintainer::check()`, and finally calls
`sendOffloads()` collectively, see
offload_checker.cpp.

The order inside `EntityGhostMaintainer::check()` matters:

1. If `Cell::shouldOffload()` is true, run `checkEntityForOffload()` first,
   which may fill `pOffloadDestination_` and add it to the offload list.
2. `markHaunts()` marks all haunts of the current real entity as due for
   checking, while recording whether the target offload Cell already has a
   ghost.
3. `createOrUnmarkRequiredHaunts()` builds an interest area from
   `ghostDistance + appealRadius` and walks the Cells hit by
   `Space::visitRect()`.
4. `visit()` clears the mark on existing haunts; for newly needed Cells it calls
   `addHaunt()` and `Entity::createGhost()`.
5. While offloading, ghosts may only be created on the target Cell, which avoids
   creating extra channels for non-target Cells.
6. `deleteMarkedHaunts()` deletes expired ghosts that are still marked, bounded
   by `maxGhostsToDelete()`, the new-real retention time and
   `minGhostLifespanInTicks()`.

Source: entity_ghost_maintainer.cpp,
entity_ghost_maintainer.cpp
and
entity_ghost_maintainer.cpp.

So "the target CellApp already has a ghost" is not a verbal constraint: the
source asserts it with
`doesOffloadDestinationHaveGhost || numGhostsCreated_ == 1`, i.e. the target
ghost exists or was just created this round, see
entity_ghost_maintainer.cpp.

## The Offload State Machine

<MermaidDiagram title="Entity offload state machine">
stateDiagram-v2
  [*] --> RealOnSource
  RealOnSource --> GhostPreparedOnDest: create/unmark haunt
  GhostPreparedOnDest --> OffloadQueued: addToOffloads
  OffloadQueued --> LeavingSource: onLeavingCell
  LeavingSource --> StreamingToDest: write onload + real data
  StreamingToDest --> GhostOnSource: convertRealToGhost
  StreamingToDest --> RealOnDest: onload + read real data
  GhostOnSource --> AwaitGhostSetReal
  AwaitGhostSetReal --> GhostFollowingNewReal: ghostSetNextReal
  RealOnDest --> [*]
</MermaidDiagram>

This is not a single-step operation but a cross-process, multi-message,
multi-object state transition.

## Write Order and Read Order

Inside `Entity::convertRealToGhost()`, if there is a target channel:

- Writes the real data to the stream.
- Sets `pRealChannel_`.
- Sets `nextRealAddr_`.
- Calls `offloadReal()` to delete the real part.
- Deletes the real-only properties, leaving only the ghost + real shared
  properties.

Source: entity.cpp.

The actual stream write happens in `Entity::writeRealDataToStream()` and
`Entity::writeRealDataToStreamInternal()`:

1. Write the `EntityID` first.
2. Then write `teleportFailure = false`.
3. Wrap a compression stream with `internalNetworkCompressionType()`.
4. Write the real-only properties, i.e. the properties between
   `propCountGhost()` and `propCountGhostPlusReal()`.
5. Write the `RealProps` token.
6. Call `RealEntity::writeOffloadData()` to write the RealEntity state.
7. Write the exposed-for-replay base properties.

Source: entity.cpp
and entity.cpp.

`RealEntity::writeOffloadData()` continues writing:

- Channel state.
- velocity, topSpeed, topSpeedY, physicsCorrections.
- `shouldAutoBackup_`.
- The current `spaceID` and `isTeleport`.
- The haunt address list; and if the current CellApp address is not among the
  haunts, the current address is force-appended, because the source becomes a
  ghost after offloading.
- `controlledBy_`, profiler, real controllers, `periodsWithoutWitness_`,
  `recordingSpaceEntryID_`.
- If there is a Witness, write the marker `'W'` and the witness offload data;
  otherwise write `'-'`.

Source: real_entity.cpp.

On the receiving side, the comment of `readRealDataFromStreamForOnloadInternal()`
explicitly requires reading in the same order as the offload writes:

- Entity first.
- Then the script-related data.
- Then the real data.

Source: entity.cpp.

The concrete order on the target side:

1. `Entity::onload()` reads `teleportFailure` from the `GHOST_ONLY` raw varlen
   message, fires `onEnteringCell` first, then calls `convertGhostToReal()`, see
   entity.cpp.
2. `convertGhostToReal()` disables ordinary callbacks, clears the `pRealChannel_`
   pointing at the old real, then calls `readRealDataFromStreamForOnload()`, see
   entity.cpp.
3. `readRealDataFromStreamForOnloadInternal()` first restores the real-only
   properties, validates the `RealProps` token, then `createReal()` and calls
   `RealEntity::init(..., CREATE_REAL_FROM_OFFLOAD, ...)`, see
   entity.cpp.
4. After reading the exposed-for-replay base properties, it starts the real
   controllers, see
   entity.cpp.
5. `convertGhostToReal()` adds the entity to the Cell's real list, calls
   `relocated()`, then fires `onEnteredCell` through the high-priority callback
   buffer, see
   entity.cpp.

This shows the wire format is strongly coupled to the state transitions; the
field order cannot be rearranged freely.

## Script Callback Order

A Cell offload fires script callbacks:

- Source Cell: `onLeavingCell`.
- After the source Cell becomes a ghost: `onLeftCell`.
- Target Cell: `onEnteringCell`.
- After the target ghost becomes a real: `onEnteredCell`.

Source comment: entity.cpp.

The documentation must stress the reminder in the source comment: these callbacks
are "Think twice before using", and in many cases a better data type should be
used instead.

The reason is that a callback can run script, and the script may destroy,
teleport, offload again or change state.

## Channel and nextRealAddr

`Entity::convertRealToGhost()` sets `nextRealAddr_` to the target CellApp address,
see entity.cpp.

`RealEntity::destroy()` notifies all non-target ghosts on offload:

- Sends `ghostSetNextReal`.
- Tells the ghost the address of the new real.
- Resets/destroys the current real channel.

Source: real_entity.cpp.

`Entity::ghostSetNextReal()` sets `nextRealAddr_` and then plays the buffered
messages, see
entity.cpp.

On the target side, after `RealEntity::readOffloadData()` reads the haunt address
list, it sends `ghostSetReal` to every valid ghost, telling it the new owner
address and `numTimesRealOffloaded`, see
real_entity.cpp.

The handshake order on the source side and with the other ghosts is:

1. The source real sets `nextRealAddr_` in `convertRealToGhost()`, then calls
   `offloadReal()`.
2. `RealEntity::destroy( pNextRealAddr )` sends `ghostSetNextReal` to every
   non-target haunt - this is the last message the current real sends to those
   ghosts.
3. After the target real is created, `readOffloadData()` sends `ghostSetReal` to
   the retained ghosts.
4. After a ghost receives `ghostSetNextReal`, it accepts only the following
   GHOST_ONLY messages from `nextRealAddr_`, and plays the buffered subsequence
   belonging to that address.
5. When a ghost receives `ghostSetReal`, if `numTimesRealOffloaded` is not the
   expected value, it delays this message as a new subsequence until the previous
   offload completes.

Source: real_entity.cpp,
entity.cpp
and entity.cpp.

This is exactly the business background of Mercury's indexed channel versioning:
after an offload the old real, the new real and buffered packets may arrive
interleaved, so message origins must be identified by owner address, offload
count and subsequence boundaries.

## Buffered Ghost Messages

`BufferedGhostMessages` buffers by `EntityID`, then queues by source
`Mercury::Address`:

- `BufferedGhostMessages::add()` puts the message into a queue for a given
  entity and source, see
  buffered_ghost_messages.cpp.
- `playSubsequenceFor(entityID, srcAddr)` only plays the queue of the source
  address that should currently receive, see
  buffered_ghost_messages.cpp.
- `BufferedGhostMessagesForEntity::playSubsequence()` keeps the other source
  queues when the current source queue is missing, instead of playing them by
  mistake, see
  buffered_ghost_messages_for_entity.cpp.
- `BufferedGhostMessageQueue::playSubsequence()` plays up to the next subsequence
  end; reaching a `ghostSetNextReal` may recursively trigger the next source
  queue, see
  buffered_ghost_message_queue.cpp.
- `delaySubsequence()` requires the first message to be a subsequence start, and
  delays the old unexpected subsequence along with it, see
  buffered_ghost_message_queue.cpp.

`BufferedGhostMessages::isSubsequenceStart()` treats `ghostSetReal` as the start of
a subsequence, and `isSubsequenceEnd()` treats `ghostSetNextReal` and `delGhost`
as subsequence ends, see
buffered_ghost_messages.hpp.

`Space::createGhost()` takes part in the reordering handling too: if an entity
with the same id already exists, or the entity already has buffered messages
from that source, `createGhost` is wrapped as a `BufferedCreateGhostMessage`
rather than creating or overwriting directly, see
space.cpp.

There is another layer of protection at the message entry point:

- `EntityMessageHandler::handleMessage()` creates a buffered message for a
  `GHOST_ONLY` message when the entity does not exist,
  `shouldBufferMessagesFrom(srcAddr)` is true, or that source is currently being
  delayed, see
  message_handlers.cpp.
- If a `GHOST_ONLY` message is clearly addressed to an already-destroyed ghost -
  for example the current entity is already real, or it is offloading to that
  source but the message is not a subsequence start - it is dropped and a failure
  returned, see
  message_handlers.cpp.
- When a `REAL_ONLY` message arrives and this process has no real, it looks up
  the real channel cached on the ghost or the population's real channel and
  forwards it, see
  message_handlers.cpp.
- The rule in `Entity::shouldBufferMessagesFrom()` is direct: a real and a
  zombie ghost do not buffer; if `nextRealAddr_` is set, only the next real is
  accepted; otherwise only the current real, see
  entity.cpp.

Together these layers guarantee that the old real's tail packets, the new real's
first packets, ghost creation packets and ghost deletion packets may arrive out
of order without breaking the entity lifecycle in arrival order.

## The Relationship Between Offload and Ghosts

The comment of `OffloadChecker::sendOffload()` says a single offload is bounded
by the ghosting capacity and the channel state, see
offload_checker.cpp.

Although the current implementation merely calls `cell_.offloadEntity()`, the
preceding `EntityGhostMaintainer` has already guaranteed the target conditions.

In other words:

- Ghosts are not an accessory of offload.
- Ghosts are one of the preconditions for offloading safely.

## The Cell Deletion Boundary

Cell deletion also depends on offloads completing.

`Cell::isReadyForDeletion()` requires:

- No buffered entity messages.
- No buffered input messages.
- `isRemoved_` is true.
- No real entities.
- No space entities.
- No pending ACKs.

Source: cell.cpp.

This shows BigWorld does not brutally delete a Cell while migration messages are
still outstanding.

## The Teleport Branch

An ordinary offload is an authoritative migration between neighbouring Cells;
a teleport allows crossing spaces or distant CellApps, so the source takes a
separate message `CellAppInterface::onloadTeleportedEntity`, see
cellapp_interface.hpp.

The key differences in `RealEntity::teleport()` on the source side:

- If the mailbox id is 0, only the local position changes and
  `onTeleportSuccess(NULL)` is called, without an offload.
- A remote teleport first calls `onLeavingCell`, then writes
  `onloadTeleportedEntity` to the target CellApp.
- That message first carries the nearby entity id, then the createGhost data
  length and the ghost data.
- The source calls `Cell::offloadEntity(..., isTeleport=true)`, and at that point
  `Cell::offloadEntity()` no longer calls `onLeavingCell` again.
- The source temporarily changes the local position/direction to write into the
  offload data and restores the old position afterwards, so that the source
  ghost state is not polluted by a wrong position.

Source: real_entity.cpp
and real_entity.cpp.

`CellApp::onloadTeleportedEntity()` on the target side splits one composite
message into two internal messages:

1. Complete the createGhost stream with the nearby entity's space id.
2. Call the `CellAppInterface::createGhost` handler to create the ghost first.
3. Call the `CellAppInterface::onload` handler to turn the ghost into a real.
4. Once the new entity is found, call `onTeleportSuccess( nearbyEntity )`.

If the nearby entity does not exist, the target side drops the createGhost data
and sends the remaining onload data back to the source with
`teleportFailure=true`. After seeing the teleport failure, the source's
`Entity::onload()` removes the failed destination haunt and calls
`onTeleportFailure`, see
cellapp.cpp
and entity.cpp.

## Differences from Base Offload

This chapter is mostly about Cell entity offload. BaseApp also has Base offload,
used for BaseApp retiring, backup and recovery.

The Base side has:

- `Base::backupTo()`
- `Base::writeBackupData()`
- `Base::offload()`
- `Base::readBackupData()`

See base.hpp.

The differences:

- Cell offload is a spatial authoritative migration.
- Base offload migrates the session/long-lived part of an entity between BaseApps.
- Both rely on Mercury, BinaryStream, EntityDef and Mailbox.

## Trade-offs in the Source

BigWorld's offload mechanism is complex, but the complexity in the source comes
mainly from these constraints:

- Real/Ghost are different authoritative states of the same Entity on different
  CellApps, so they cannot simply be memcpy'd.
- The target must have a ghost before it can turn a ghost into a real; otherwise
  real-only properties, controllers, witnesses, history and channels all lack
  context.
- The wire format is strongly coupled to the read/write order; the `RealProps`
  token is only a development-time check, not a schema evolution mechanism.
- Script callbacks are inserted at critical migration points, so the source must
  use `callbacksPermitted(false)` and high-priority callback buffering to shrink
  the window for damage.
- Ghost messages can only be guaranteed ordered within a single CellApp channel;
  messages spanning the old and new reals have to be stitched by subsequences.
- Cell deletion must wait until buffered messages, real entities, space entities
  and removal ACKs are all empty.

What this design buys is online boundary migration, AOI continuity and CellApp
load balancing; what it costs is a state machine that is hard to reason about
locally. When reading or modifying the source, protect the message order, the
callback order and the Real/Ghost lifecycle boundaries first, instead of
abstracting it as "object migration".

## What Source-Verified Tests Should Cover

1. In an ordinary offload scenario confirm the source-side order:
   `onLeavingCell` -> `Cell::realEntities_.remove()` -> `Entity::offload()` ->
   `offloadReal()` -> `onLeftCell`.
2. Check that the target-side `onload` message must land on a ghost:
   `CellAppInterface::onload` is `GHOST_ONLY`, not an ordinary CellApp RPC.
3. Add token/golden-stream tests for `writeRealDataToStreamInternal()` and
   `readRealDataFromStreamForOnloadInternal()`, so the real-only property order
   cannot drift.
4. Construct the case where the target ghost does not exist, and confirm
   `EntityGhostMaintainer::check()` creates the target ghost before the offload,
   or fires an assertion.
5. Construct `ghostSetReal` arriving before `ghostSetNextReal` and confirm
   `Entity::ghostSetReal()` delays the subsequence instead of switching the real
   channel immediately.
6. Construct `createGhost` arriving while the entity still exists and confirm
   `Space::createGhost()` goes into `BufferedCreateGhostMessage` rather than
   overwriting the entity.
7. Construct a teleport whose nearby entity does not exist and confirm the target
   returns `teleportFailure=true` and the source fires `onTeleportFailure`.
8. When deleting a Cell confirm `isReadyForDeletion()` has empty buffered entity
   messages, buffered input messages, real entities, space entities and pending
   ACKs.

## Scope of This Chapter

This chapter explains Cell entity offload. The next chapter analyses the
persistence and DB thread model, explaining how entity state is stored,
backed up and restored.