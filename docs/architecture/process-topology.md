# Process Topology and Responsibility Split

> **English** | [中文](process-topology.md)

<div class="arch-hero">

The BigWorld server is not a single-process game server. It is a multi-process
MMO runtime. The first step to understanding it is to separate player sessions,
global entities, space simulation, database access, login ingress, the control
plane, and failure recovery.

</div>

## Core Conclusions

The topology follows classic MMO server layering:

- `LoginApp` is the external client login entry point.
- `BaseApp` owns player sessions, Base entities, global logic, and client-connectivity relationships.
- `CellApp` owns in-space entities, AOI, physics/position, and Cell entity logic.
- `DBApp` owns database interaction, entity persistence, and account/entity queries.
- `BaseAppMgr`, `CellAppMgr`, and `DBAppMgr` are part of the control plane and registry.
- `Reviver` monitors critical components and asks `machined` to start replacement processes.

This is not a modern microservice split by business domain. It is a split by
runtime authority domains in an MMO.

## Process Topology

<div class="topology-grid">
  <div class="topology-card">
    <h3>LoginApp</h3>
    <p>Handles external login requests, performs challenge/authentication, and assigns the next connection target.</p>
  </div>
  <div class="topology-card">
    <h3>BaseApp</h3>
    <p>Owns online player sessions, Base entities, cross-Cell coordination, and client communication.</p>
  </div>
  <div class="topology-card">
    <h3>CellApp</h3>
    <p>Owns space simulation, entity position, AOI, ghosts, and in-Cell logic.</p>
  </div>
  <div class="topology-card">
    <h3>DBApp</h3>
    <p>Encapsulates database tasks and isolates blocking database work in the background-task model.</p>
  </div>
  <div class="topology-card">
    <h3>Manager components</h3>
    <p>Own registration, discovery, load information, scaling admission, recovery coordination, and global control.</p>
  </div>
  <div class="topology-card">
    <h3>Reviver</h3>
    <p>Provides process-level recovery through pings, birth/death messages, and machined.</p>
  </div>
</div>

<MermaidDiagram title="BigWorld server process relationships">
flowchart LR
  Client[Client]
  Login[LoginApp]
  Base[BaseApp]
  Cell[CellApp]
  DB[DBApp]
  BAM[BaseAppMgr]
  CAM[CellAppMgr]
  DBM[DBAppMgr]
  Rev[Reviver]
  Machined[machined]

  Client --> Login
  Client <--> Base
  Base <--> Cell
  Base <--> DB
  Cell <--> DB
  Base --> BAM
  Cell --> CAM
  DB --> DBM
  BAM <--> CAM
  DBM <--> BAM
  DBM <--> CAM
  Rev --> Machined
  Rev -. ping/death .-> BAM
  Rev -. ping/death .-> CAM
</MermaidDiagram>

## Startup and Registration Chain

**Summary**: after startup, a BigWorld server process registers with machined
and connects with the Manager components. This underpins discovery and load
balancing.

**Source entry**: baseappmgr.cpp:389

```cpp
// baseappmgr.cpp:389 - BaseAppMgr registration
bool BaseAppMgr::init( bool isReload )
{
    // 1. Register the Mercury interface
    if (!this->BaseAppMgrInterface::registerWithInterface())
    {
        ERROR_MSG( "BaseAppMgr::init: Failed to register interface\n" );
        return false;
    }

    // 2. Register with machined
    if (!this->registerWithMachined())
    {
        ERROR_MSG( "BaseAppMgr::init: Failed to register with machined\n" );
        return false;
    }

    // 3. Listen for birth/death of other components
    this->BaseAppMgrInterface::registerBirthListener( 
        &BaseAppMgr::onBaseAppBirth );
    this->BaseAppMgrInterface::registerDeathListener(
        &BaseAppMgr::onBaseAppDeath );
    
    return true;
}
```

<MermaidDiagram title="Process startup and registration">
sequenceDiagram
    participant Process as Server process
    participant Machined as machined
    participant Mgr as Manager
    participant Other as Other processes

    Process->>Process: initialize
    Process->>Process: registerWithInterface()
    Process->>Machined: register (birth message)
    Machined->>Mgr: broadcast birth
    Mgr->>Mgr: addApp()
    Mgr->>Process: registration success
    Process->>Other: listen for birth/death
</MermaidDiagram>

Detailed interpretation:

1. **Interface registration**: `registerWithInterface()` registers Mercury message handlers so the process can receive and process messages.

2. **machined registration**: `registerWithMachined()` sends a birth message to machined, which broadcasts it to other processes.

3. **Manager admission**: after receiving a birth message, the Manager calls `addApp()`, admitting the process and updating process and load information.

4. **Birth/death listening**: processes listen for other components' birth/death messages for service discovery and failure detection.

### BaseAppMgr startup flow

**Source entry**: baseappmgr.cpp:389

```cpp
// baseappmgr.cpp:389 - BaseAppMgr startup
bool BaseAppMgr::init( bool isReload )
{
    // ... other initialization ...

    // Register the interface
    if (!this->BaseAppMgrInterface::registerWithInterface())
    {
        return false;
    }

    // Register with machined
    if (!this->registerWithMachined())
    {
        return false;
    }

    // Listen for BaseApp birth
    this->BaseAppMgrInterface::registerBirthListener(
        &BaseAppMgr::onBaseAppBirth );

    // Listen for CellAppMgr birth
    this->CellAppMgrInterface::registerBirthListener(
        &BaseAppMgr::onCellAppMgrBirth );

    return true;
}
```

Key details:

- BaseAppMgr must know all BaseApps and CellAppMgr.
- It maintains process lists through birth/death messages.
- Load information is collected through periodic reports.

### CellAppMgr startup flow

**Source entry**: cellappmgr.cpp:238

```cpp
// cellappmgr.cpp:238 - CellAppMgr startup
bool CellAppMgr::init( bool isReload )
{
    // ... other initialization ...

    // Register the interface
    if (!this->CellAppMgrInterface::registerWithInterface())
    {
        return false;
    }

    // Register with machined
    if (!this->registerWithMachined())
    {
        return false;
    }

    // Listen for CellApp birth
    this->CellAppMgrInterface::registerBirthListener(
        &CellAppMgr::onCellAppBirth );

    return true;
}
```

Key details:

- CellAppMgr owns all CellApps, space assignment, and load balancing.
- On CellApp birth it calls `addApp()` for admission.
- On CellApp death it starts the entity-recovery flow.

## Responsibility Boundaries

### LoginApp

`LoginApp` is an entry point, not the long-term session owner. It registers the
external `LoginInterface` and internal `LoginIntInterface`, and listens for
`DBAppMgr` birth: loginapp.cpp.

Trade-off:

- Benefit: isolating login ingress from game sessions simplifies rate limiting and authentication.
- Cost: post-login connection migration, challenge protocol, and database-query chains become more complex.

### BaseApp

`BaseApp` is the core process for online sessions and Base entities. It needs to
know `BaseAppMgr` and `CellAppMgr`, and handles birth of other BaseApps. Source
entry points:

- Internal interface registration: baseapp.cpp
- External interface registration: baseapp.cpp
- CellAppMgr birth handling: baseapp.cpp
- BaseAppMgr birth handling: baseapp.cpp

### CellApp

`CellApp` is the space-simulation process. It registers with `CellAppMgr`, which
coordinates spaces, Cells, load balancing, and recovery. CellApp's gateway
request to CellAppMgr begins in cellappmgr_gateway.cpp.

### Manager components

Managers are not business microservices; they are the control plane:

- `BaseAppMgr` manages the BaseApp set, load, and discovery between BaseApps.
- `CellAppMgr` manages CellApps, spaces, Cell boundaries, load balancing, and recovery.
- `DBAppMgr` manages DBApps, database-component birth/death, and coordination with Base/Cell managers.

`CellAppMgr::addApp()` is the key runtime admission entry point:
cellappmgr.cpp.

## What Dynamic Scaling Actually Means

BigWorld can admit new `BaseApp` / `CellApp` processes at runtime, but the
source path is process registration, Manager admission, load rebalancing, and
state migration—not merely starting stateless replicas.

The more accurate model is:

<div class="flow-strip">
  <span class="flow-node">External process start</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">register with machined / Manager</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Manager admission</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">load rebalance</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">entity/Cell migration</span>
</div>

The design emphasizes runtime migration and control-plane coordination, not
process count alone.

## Source Trade-Offs

The process split serves clear runtime responsibilities:

- Multiple processes isolate crash domains and make failures easier to localize than a single-process, multithreaded design.
- A C++ main loop with embedded Python keeps performance-sensitive paths in the engine layer.
- Managers act as authoritative control planes, preventing every App from maintaining complex global state.
- `machined` and `Reviver` provide process-level discovery, startup, and recovery.
- LoginApp, BaseApp, CellApp, DBApp, and their interfaces are registered separately, supporting responsibility-oriented diagnosis.

The costs are equally clear:

- Components are strongly coupled through Mercury interfaces, birth/death notifications, and Manager state.
- Scaling is effective only if Base/Cell load migrates; starting a process is not sufficient.
- Failures in DBApp, BaseAppMgr, CellAppMgr, or other control-plane components affect many business paths.
- Diagnosis requires machined, Manager, App logs, and watcher state together.

## Source-Code Verification Focus

Topology verification should cover registration, admission, and state propagation:

- Every process must register its corresponding Mercury interface at startup.
- Component type, address, PID, and watcher nub information registered with machined must be correct.
- `CellAppMgr::addApp()` must not admit a new CellApp when BaseApp or Alpha DBApp is unavailable.
- After a new BaseApp/CellApp is admitted, Manager watcher counts, load, and app lists must update.
- Birth/death notifications must propagate to dependents, not remain visible only to the local process.
- Reviver- or machined-level restart must not bypass BigWorld's own state-recovery flow.

## Follow-Up Research Entry Points

Topology is only the shell. Performance and scaling boundaries are determined by:

- The event-loop model inside each process: [Main loop, ticks, and event dispatch](/architecture/event-loop).
- How Mercury turns socket events into messages: [Network I/O model selection](/architecture/network-io-model).
- How EntityDef binds script types, protocol streams, and persistence: [Serialization and EntityDef](/architecture/serialization-entitydef).
- How CellAppMgr partitions Cells, balances load, and migrates entities: [Cell partitioning and load balancing](/architecture/cell-partition-load-balance).
- Process failure recovery: [Scaling and fault tolerance](/architecture/scaling-fault-tolerance).
