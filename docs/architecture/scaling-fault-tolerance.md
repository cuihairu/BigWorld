# Dynamic Scaling and Fault Tolerance


<div class="arch-hero">

BigWorld scaling is not "spin up stateless replicas". It is runtime admission,
load balancing, Cell/Entity migration and process-level recovery under MMO state
sharding. Understanding it requires separating three layers: resource
scheduling, state migration, and fault recovery.

</div>

## Key Takeaways

BigWorld supports adding new `BaseApp` / `CellApp` processes at runtime, and it
has a process-level recovery mechanism built on `Reviver + machined`. It is not
an automatic cloud-native elasticity system.

Key sources:

- `CellAppMgr` initialisation registers the load balance, meta load balance and
  overload check timers, see cellappmgr.cpp.
- `CellAppMgr::addApp()` is the entry point for admitting a new CellApp, see
  cellappmgr.cpp.
- A new CellApp requires BaseApp and Alpha DBApp to be known, otherwise it is
  not admitted yet, see
  cellappmgr.cpp.
- `CellAppMgr::startTimer()` starts `GameTick` and creates the `TimeKeeper`,
  see cellappmgr.cpp.
- `CellApps::sendGameTime()` sends the current game time to all CellApps, see
  cellapps.cpp.
- `ComponentReviver` registers birth/death listeners and pings components
  periodically, see component_reviver.cpp.
- `Reviver::revive()` asks machined to start a process through `CreateMessage`,
  see reviver.cpp.

## Three Layers of the Scaling Model

BigWorld's dynamic scaling splits into three layers:

<div class="decision-grid">
  <div class="decision-card">
    <h3>Resource layer</h3>
    <p>Processes are started from outside; each process registers with machined and the Manager.</p>
  </div>
  <div class="decision-card">
    <h3>Control layer</h3>
    <p>BaseAppMgr/CellAppMgr admit processes, allocate IDs, synchronise shared data and broadcast time.</p>
  </div>
  <div class="decision-card">
    <h3>State layer</h3>
    <p>Cell load balancing, entity offload, Ghost maintenance, Base migration and recovery.</p>
  </div>
</div>

Modern cloud platforms solve the resource layer. BigWorld's difficulty lies
mostly in the state layer.

## CellApp Admission Flow

**Overview:** after a new CellApp starts it must register with CellAppMgr.
CellAppMgr checks the preconditions (BaseApp known, DBApp known); if they hold,
it allocates an ID, synchronises data and admits the new CellApp.

**Source entry:** cellappmgr.cpp:1245

```cpp
// cellappmgr.cpp:1245 - CellAppMgr admits a new CellApp
bool CellAppMgr::addApp( const Mercury::Address & addr,
    const Mercury::UnpackedMessageHeader & header,
    BinaryIStream & data )
{
    // 1. Check the preconditions
    if (baseAppAddr_.isNone())
    {
        ERROR_MSG( "CellAppMgr::addApp: No BaseApp known yet\n" );
        return false;
    }
    
    if (dbAlphaAddr_.isNone())
    {
        ERROR_MSG( "CellAppMgr::addApp: No DBApp Alpha known yet\n" );
        return false;
    }
    
    if (!allowNewCellApps_)
    {
        ERROR_MSG( "CellAppMgr::addApp: New cell apps not allowed\n" );
        return false;
    }
    
    if (isRecovery_)
    {
        ERROR_MSG( "CellAppMgr::addApp: Currently in recovery\n" );
        return false;
    }
    
    // 2. Allocate an ID and initialise the data
    CellAppInitData initData;
    initData.id = ++lastCellAppID_;
    initData.time = this->time();
    initData.baseAppAddr = baseAppAddr_;
    initData.dbAlphaAddr = dbAlphaAddr_;
    initData.isReady = true;
    initData.timeoutPeriod = Config::timeoutPeriod();
    
    // 3. Add it to the CellApp collection
    CellApp * pApp = new CellApp( addr, initData.id );
    cellApps_.add( pApp );
    
    // 4. Put it in the pending list
    pendingApps_.push_back( pApp );
    
    // 5. Reply with the initialisation data
    header.reply( initData );
    
    // 6. Send down the shared data
    this->sendSharedData( addr );
    this->sendGlobalData( addr );
    
    return true;
}
```

**Flowchart:**

<MermaidDiagram title="CellApp admission flow">
sequenceDiagram
    participant CellApp as New CellApp
    participant CellAppMgr as CellAppMgr
    participant BaseApp as BaseApp
    participant DBApp as DBApp Alpha

    CellApp->>CellAppMgr: addApp request
    
    alt Preconditions not met
        CellAppMgr->>CellApp: reject
    else Preconditions met
        CellAppMgr->>CellAppMgr: allocate ID
        CellAppMgr->>CellAppMgr: initialise data
        CellAppMgr->>CellAppMgr: cellApps_.add()
        CellAppMgr->>CellAppMgr: pendingApps_.push_back()
        CellAppMgr->>CellApp: reply CellAppInitData
        CellAppMgr->>CellApp: sendSharedData()
        CellAppMgr->>CellApp: sendGlobalData()
    end
</MermaidDiagram>

**Detailed walkthrough:**

1. **Precondition checks**:
   - `baseAppAddr_.isNone()`: the BaseApp address must be known, otherwise no
     Base Entity can be created
   - `dbAlphaAddr_.isNone()`: DBApp Alpha must be known, otherwise nothing can
     be persisted
   - `allowNewCellApps_`: a runtime switch that can forbid new CellApps from
     joining
   - `isRecovery_`: no new CellApps while recovery is in progress, to avoid
     state conflicts

2. **ID allocation**: `lastCellAppID_` increments monotonically, so every
   CellApp has a unique ID used for message routing and state management.

3. **Initialisation data**: `CellAppInitData` carries all the initial
   information a CellApp needs:
   - the current game time
   - the BaseApp address (for Base-Cell communication)
   - the DBApp Alpha address (for persistence)
   - the timeout period (for heartbeat detection)

4. **pending list**: a new CellApp goes into `pendingApps_` first and only
   takes part in load balancing for real once it is ready.

5. **Shared data distribution**: `sendSharedData()` and `sendGlobalData()`
   synchronise global configuration so that all CellApps hold a consistent
   configuration.

### CellAppMgr Initialisation

**Source entry:** cellappmgr.cpp:190

```cpp
// cellappmgr.cpp:190 - CellAppMgr initialisation
bool CellAppMgr::init( bool isReload )
{
    // ... other initialisation ...
    
    // Register the load balance timer
    mainDispatcher_.addTimer( Config::loadBalancePeriod() * 1000000,
        this, (void*)LOAD_BALANCE_TIMER, "LoadBalance" );
    
    // Register the meta load balance timer
    mainDispatcher_.addTimer( Config::metaLoadBalancePeriod() * 1000000,
        this, (void*)META_LOAD_BALANCE_TIMER, "MetaLoadBalance" );
    
    // Register the overload check timer
    mainDispatcher_.addTimer( Config::overloadCheckPeriod() * 1000000,
        this, (void*)OVERLOAD_CHECK_TIMER, "OverloadCheck" );
    
    // Create the TimeKeeper
    pTimeKeeper_ = new TimeKeeper( this, this->time() );
    
    return true;
}
```

**Key details:**

- The load balance timer fires periodically, checks CellApp load and decides
  whether to migrate Cells
- The meta load balance timer handles the more complex global load-balancing
  decisions
- The overload check timer monitors whether a CellApp is overloaded and
  triggers protection
- TimeKeeper is used for cross-process time synchronisation

A CellApp cannot host entities just by starting up. It must be admitted by the
control plane and synchronised with its initial state.

## Load Balance and GameTick

`CellAppMgr::init()` registers three timers:

- `LoadBalance`
- `MetaLoadBalance`
- `OverloadCheck`

The source is at cellappmgr.cpp.

`CellAppMgr::startTimer()` uses `Config::updateHertz()` to start `GameTick` and
creates the `TimeKeeper`. The source is at
cellappmgr.cpp.

CellAppMgr broadcasts the game time to all CellApps, see
cellapps.cpp.

Scaling is not just putting a process into a list. The process must also be
brought under the unified tick and load control.

## Entity Offload

Cell load balancing ultimately comes down to entity migration and Ghost
maintenance.

Source entries:

- `OffloadChecker::run()` performs the offload and ghost checks, see
  offload_checker.cpp.
- `Cell::offloadEntity()` calls `Entity::offload()`, see
  cell.cpp.
- `Entity::offload()` is one of the real entry points for entity migration, see
  entity.cpp.
- `RealEntity` notifies the ghosts before it is offloaded, see
  real_entity.cpp.

The real difficulty of dynamic scaling is not "start one more process", but:

- when to move a Cell boundary.
- which entities have to migrate.
- whether the Ghosts are ready.
- how messages are buffered during migration.
- how Channel version detects stale packets.
- how Base/Cell/Client three-party state stays consistent.

The later entity migration and AOI chapters expand on these.

## Reviver Fault-Tolerance Model

`Reviver` monitors critical components and asks machined to restart them.

`ComponentReviver::init()` does three things:

1. Reads the ping period and timeout.
2. Looks up the target interface address through machined.
3. Registers birth/death listeners.

The source is at component_reviver.cpp.

`ComponentReviver::handleTimeout()` runs on every timer expiry:

- If misses are still allowed, it sends a ping request.
- On `REVIVER_PING_YES` it resets the miss count and marks the component
  attached.
- If there are too many misses it calls `revive()`.

The source is at component_reviver.cpp.

`handleMessage()` also triggers revive on a death message when the address
matches the current component, see
component_reviver.cpp.

<MermaidDiagram title="Reviver recovery flow">
flowchart TD
  A[ComponentReviver] --> B[register birth/death listener]
  A --> C[start ping timer]
  C --> D{ping response?}
  D -- yes --> E[reset missed count attached]
  D -- no --> F[decrement pingsToMiss]
  F --> G{miss too many?}
  G -- no --> C
  G -- yes --> H[Reviver revive]
  B --> I{death message}
  I -- matching addr --> H
  H --> J[CreateMessage to machined]
  J --> K[start process with recover flag]
</MermaidDiagram>

## machined's Boundaries

`Reviver::revive()` asks the local machined to create a process via
`CreateMessage`:

- `cm.uid_ = getUserId()`
- `cm.recover_ = 1`
- `cm.name_ = createComponent`
- `cm.config_ = BW_COMPILE_TIME_CONFIG`
- sent to `127.0.0.1`

The source is at reviver.cpp.

There is also a TODO in the source: it would be better to know whether machined
really started the process successfully, and one possible cause of failure is
that `machined.conf` is not configured correctly. See
reviver.cpp.

Reviver is process-level recovery, not a full orchestration system. It can
request a restart, but it is not equivalent to the scheduling, health checks,
resource constraints, rolling upgrades and event auditing of a modern
orchestrator.

## Entity Recovery Call Chain After a CellApp Crash

When a CellApp dies unexpectedly, BigWorld restores entities through the
following call chain:

### Phase 1: CellAppMgr Detects the Death

CellAppMgr detects the CellApp death through a Channel disconnect and calls
handleCellAppDeath():

Source entry: cellappmgr.cpp:1806

Call chain:

<div class="flow-strip">
  <span class="flow-node">CellApp Channel disconnected</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">CellAppMgr::handleCellAppDeath()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">send SIGQUIT to the dead CellApp</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">create CellAppDeathHandler</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">pDeadApp->handleUnexpectedDeath()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">wait for all CellApps to reply</span>
</div>

### Phase 2: CellApps Collect Entity State

Surviving CellApps receive the death notification and collect the state of the
ghost entities they hold:

Source entry: cellapp.cpp

Call chain:

<div class="flow-strip">
  <span class="flow-node">CellApp receives handleCellAppDeath</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">iterate all ghost entities</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">check whether the ghost belongs to the dead CellApp</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">collect the list of entities to restore</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">reply to CellAppMgr</span>
</div>

### Phase 3: CellAppMgr Notifies BaseAppMgr

After all CellApps have replied, CellAppMgr notifies BaseAppMgr to start
recovery:

Call chain:

<div class="flow-strip">
  <span class="flow-node">CellAppDeathHandler receives all replies</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">CellAppMgr notifies BaseAppMgr</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BaseAppMgr broadcasts to all BaseApps</span>
</div>

### Phase 4: BaseApp Restores Cell Entities

After receiving the notification, BaseApp iterates over all Base Entities and
calls restoreTo() to restore the Cell Entity:

Source entry: baseapp.cpp:1942

Call chain:

<div class="flow-strip">
  <span class="flow-node">BaseApp::handleCellAppDeath()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">pDeadCellApps_->addApp()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">pDeadCellApps_->tick()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">iterate the entities in bases_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Base::restoreTo()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">restore from cellBackupData_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">send to the new CellApp</span>
</div>

### Phase 5: The New CellApp Rebuilds Entities

After the new CellApp receives the recovery data it rebuilds the Cell Entity:

Call chain:

<div class="flow-strip">
  <span class="flow-node">new CellApp receives the restoreTo request</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">create the Entity object</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">restore properties from the backup data</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">rebuild the real/ghost relationship</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">notify BaseApp that recovery is complete</span>
</div>

### Recovery Failure Scenarios

Recovery can fail in these scenarios:

- The Base Entity has no cellBackupData_ (never backed up)
- The target CellApp is unavailable
- The database is unavailable (persistent data cannot be read)
- The entity is in an offload intermediate state
- Protocol versions are incompatible

The source is at base.cpp:3810.

## Recovery Boundaries

BigWorld has multiple layers of recovery:

- Reviver/machined: restart after a process dies.
- Manager birth/death: component discovery and state updates.
- BaseApp/CellApp death handlers: entity and Channel recovery.
- DB backups and entity persistence: the source of recovered state.
- Channel version and recently-dead channels: identifying stale packets and
  residual messages from dead components.

But recovery is not magic:

- If in-memory state was never backed up, recovery may lose it.
- If an Entity is in an offload intermediate state, the recovery logic is more
  complex.
- If the database is unavailable, restarting the process still cannot complete
  business recovery.
- If the control plane itself splits, the old machined model provides no modern
  consensus guarantees.
- If protocol versions are incompatible, a restarted process may still fail to
  join.

## Source Trade-Offs

BigWorld's scaling and fault tolerance are built around MMO state migration,
not ordinary request distribution:

- Process roles are fixed and managers coordinate centrally.
- machined provides machine-level process discovery and startup.
- Reviver does lightweight process monitoring and restart.
- Load balancing revolves around Cells, Entities and Ghosts, not around HTTP
  requests.
- Multiple processes isolate crash domains, avoiding a whole-server crash from a
  single multithreaded process.

Costs in the source:

- Whether a process is alive and whether game state is migratable are two
  separate things.
- CellApp admission, Cell splitting, Entity offload and Ghost preparation must
  complete in order.
- Reviver can restart a process, but it cannot recreate in-memory state that was
  never backed up or written to the database.
- When the control plane itself lacks strong consensus semantics, birth/death
  and stale-message handling must rely on Manager state and channel version
  protection.

## Source-Code Verification Focus

Scaling and fault-tolerance tests should cover state migration, not only
process restarts:

- A new CellApp must be rejected when BaseApp and Alpha DBApp are unknown.
- After a CellApp joins, CellAppMgr must update load, space, Cell boundaries and
  watcher state.
- Cell splitting, Cell deletion and Entity offload must keep real/ghost/Witness
  consistent.
- BaseApp death, CellApp death and DBApp unavailability must each trigger their
  corresponding recovery or failure path.
- recently-dead channels and channel version must stop stale packets from
  polluting new components.
- Recovery paths must cover missing in-memory backups, DB unavailability,
  offload intermediate state and protocol incompatibility.

## Chapter Boundary

This chapter only sets up the framework for dynamic scaling and fault
tolerance. The entity model, AOI/Ghost, Cell partitioning, entity migration and
DB persistence need to be explored further before BigWorld's state governance
can be explained completely.
