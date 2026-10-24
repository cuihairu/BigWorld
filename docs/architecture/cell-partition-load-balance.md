# Cell Partitioning and Load Balancing



<div class="arch-hero">

BigWorld load balancing does not route requests between backend servers. It
changes the geometric Cell boundaries of a Space so an entity's authoritative
CellApp changes. It is a spatial partition control plane, not L4/L7 load
balancing.

</div>

## Conclusion

CellAppMgr owns global spatial partitioning; each CellApp owns local runtime
space state. The two `Space` classes have different responsibilities:

- `server/cellappmgr/Space`: control-plane Space with `CellData`, BSP tree,
  load, geometry bounds, and Cell addition/removal.
- `server/cellapp/Space`: runtime Space with local entities, CellInfo tree,
  RangeList, SpaceData, physics, and recovery state.

Key sources:

- CellAppMgr `Space`:
  space.hpp.
- CellAppMgr `CellData`:
  cell_data.hpp.
- Load balance:
  space.cpp.
- CellApp runtime Space:
  space.hpp.
- Runtime offload checking:
  cell.cpp.

## Control-plane Space

CellAppMgr's `Space` decides how a world is partitioned. Its `cells_` collection
holds `CellData`, `pRoot_` holds the BSP root, `preferredIP_` biases machine
selection, `spaceBounds_` bounds the geometry, and `isBalancing_` prevents
recursive balance calls.

<MermaidDiagram title="Control-plane Space structure">
flowchart TD
    A[Space] --> B[BSP Root]
    B --> C[BSP Node 1]
    B --> D[BSP Node 2]
    C --> E[CellData A]
    C --> F[CellData B]
    D --> G[CellData C]
    D --> H[CellData D]
    E --> I[CellApp 1]
    F --> J[CellApp 2]
    G --> I
    H --> K[CellApp 3]
</MermaidDiagram>

The leaves represent Cells. Each `CellData` links to a CellApp and participates
in smoothed-load computation, boundary adjustment, and serialized geometry
updates.

## CellData and BSP

`CellData` extends `CM::BSPNode` and stores smoothed load, area not loaded, and
other balance inputs. `Space::loadBalance()` updates BSP load, computes a safety
bound, invokes balance, and then notifies CellApps of geometry updates.

The safety bound prevents moving nodes whose load would create a new, more
severe hotspot. Area with unloaded chunks is accounted separately. The balancer
therefore avoids splitting hotspots and avoids moving entities into areas whose
chunk resources are not ready.

## Geometry Distribution

After a BSP change, CellAppMgr serializes the new tree and sends it to the
CellApps. The runtime `Space::readTree()` reconstructs the local CellInfo tree.
Leaf metadata includes `hasBeenCreated`, so runtime code can distinguish a leaf
that is merely defined from one that is ready to receive offloads.

## Runtime Offload and Ghost Decisions

Balance itself does not move entities. It changes which Cell a coordinate
belongs to. Actual migration is triggered later by CellApp offload checks:

- `CellApp::checkOffloads()` invokes periodic Cell checks.
- `Cell::checkOffloadsAndGhosts()` runs an `OffloadChecker`.
- `OffloadChecker::run()` checks real entities, then sends offloads together.
- `EntityGhostMaintainer::checkEntityForOffload()` uses `pCellAt(position)` to
  find the home Cell.
- Migration is suppressed if the target is the same Cell, has not been created,
  is pending deletion, or has no usable channel.
- Ghost creation uses `ghostDistance + appealRadius` and only marks required
  haunts for the destination area.

<MermaidDiagram title="Partition update to entity migration">
sequenceDiagram
  participant M as CellAppMgr Space
  participant A as CellApp Space
  participant C as Cell
  participant E as EntityGhostMaintainer
  M->>M: BSP balance
  M->>A: update geometry tree
  A->>A: rebuild CellInfo tree
  A->>C: check offloads and ghosts
  C->>E: check real entities
  E->>A: pCellAt(position)
  E->>C: add to offloads if target is valid
  C->>A: offloadEntity over CellAppChannel
</MermaidDiagram>

## Offload, Retire, and Removal

`Cell::shouldOffload()` combines a local Cell flag with the global CellApp flag.
This lets a control-plane update pause offloads during recovery, shutdown, or
space retirement.

Cell removal is asynchronous. Retire sets a retiring state; remove waits for
acknowledgements; deletion readiness requires empty buffered messages, empty real
entities, empty space entities, and no pending acknowledgements. Objects are not
released while messages or entities remain.

## Group-level Scaling

Single-Space balancing only moves existing Cell boundaries. Adding or retiring
CellApps requires `metaLoadBalance()`:

- `CellAppGroups` groups CellApps that can exchange load through ordinary
  spatial partitioning.
- Overloaded groups call `addCell()` in descending load order.
- If a CellApp is already retiring, retire is cancelled and an existing Cell is
  reused.
- Underloaded groups call `retireAllCells()` only when the remaining average
  load stays below the configured lower bound.

Thus scale-up adds a Cell to a Space; scale-down retires Cells and waits for
entity migration. Neither is a process-level traffic switch or kill command.

## Trade-offs

Benefits:

- Hot regions can split into smaller Cells while sparse regions keep large ones.
- BSP, ghost, offload, and chunk loading share one spatial semantic model.
- No separate routing layer must mirror Cell boundaries.

Costs:

- CellApp load projected onto CellData is an approximation.
- Boundaries must respect chunk bounds and ghost distances.
- Addition, deletion, retirement, `hasBeenCreated`, `isDeletePending`, and
  `pendingAcks` form a multi-phase state machine that cannot be collapsed.

## Verification Priorities

1. Confirm load reporting precedes boundary update in the CellApp game-tick
   slice.
2. Inspect CellAppMgr watchers for BSP tree, unloaded area, load min/max/average,
   and retiring Cell count.
3. Verify serialized leaves include `hasBeenCreated`.
4. Raise load on one CellApp and confirm balancing refuses moves that would
   exceed the safety bound.
5. Exercise unloaded chunks and verify balance avoids or stops unsafe moves.
6. Retire a Cell and verify it shrinks until no area and no entities remain.
7. Compare runtime CellInfo state with the control-plane BSP tree.
8. Move an entity across a boundary and verify target-cell, deletion-state, and
   Channel checks.

## Scope

This page explains Cell partitioning and balancing. Entity migration and
offload state sequencing are covered in
[entity migration and offload](/architecture/entity-migration-offload).
