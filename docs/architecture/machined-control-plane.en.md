# machined Control Plane and Process Discovery

> **English** | [中文](machined-control-plane.md)

<div class="arch-hero">

BigWorld's control plane is not a general-purpose orchestration system or a
strongly consistent metadata cluster. It relies on bwmachined for machine-level
process registration, interface discovery, birth/death notifications, process
startup, and signal delivery. Understanding machined explains how BaseAppMgr,
CellAppMgr, DBAppMgr, LoginApp, and Reviver form a dynamic cluster.

</div>

## Key Takeaways

machined is the core infrastructure of BigWorld's legacy cluster control plane:

- A process registers its interface name, port, version, and ID with machined after startup.
- Other components call `findInterface()` to locate an interface address.
- Managers register birth/death listeners and receive component startup/death notifications.
- Reviver sends a `CreateMessage` asking machined to restart a component.
- Tools can use machined to query processes, send signals, and retrieve machine information.

Key source code:

- MachineGuard message types: [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:121).
- `ListenerMessage` implements birth/death listeners: [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:600).
- `CreateMessage` asks machined to start a process: [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:634).
- `registerWithMachined()`: [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:47).
- `registerBirthListener()` / `registerDeathListener()`: [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:138).
- `findInterface()`: [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:339).
- BaseAppMgr initialization registers death and birth listeners: [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:378).
- Reviver uses `CreateMessage`: [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:440).

## What machined Provides

The `MachineGuardMessage::Message` enum defines its capability boundary:

- `WHOLE_MACHINE_MESSAGE`
- `PROCESS_MESSAGE`
- `PROCESS_STATS_MESSAGE`
- `LISTENER_MESSAGE`
- `CREATE_MESSAGE`
- `SIGNAL_MESSAGE`
- `TAGS_MESSAGE`
- `USER_MESSAGE`
- `PID_MESSAGE`
- `RESET_MESSAGE`
- `QUERY_INTERFACE_MESSAGE`
- `CREATE_WITH_ARGS_MESSAGE`
- `MACHINE_PLATFORM_MESSAGE`

Source: [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:124).

Together these capabilities form a lightweight control plane:

- Service discovery.
- Process status queries.
- Process birth/death notifications.
- Remote startup.
- Signal delivery.
- Machine and user information queries.

It is stronger than plain DNS or address-oriented service discovery, but weaker
than a modern orchestrator.

## Registration Flow

Process registration uses `registerWithMachined()`:

- An empty name returns success immediately.
- Construct a `ProcessMessage`.
- Set `REGISTER` or `DEREGISTER`.
- Set the category to `SERVER_COMPONENT`.
- Fill in port, name, ID, and version.
- Send it to the local machined and wait for a reply.

Source: [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:50).

<MermaidDiagram title="Process registration with machined">
sequenceDiagram
  participant App
  participant Machined
  App->>Machined: ProcessMessage REGISTER(name, id, port, version)
  Machined-->>App: ProcessMessage reply
  App->>App: interface visible to other components
</MermaidDiagram>

Important detail: once registered, other processes can find the interface and
send messages. The source therefore forbids calling a blocking reply handler
after registration. See [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:58).

## Interface Discovery

`findInterface()` constructs a `ProcessStatsMessage` and searches by:

- category.
- uid.
- name.
- id.

Source: [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:339).

After receiving `ProcessStatsMessage`, it checks:

- whether the interface version matches `MERCURY_INTERFACE_VERSION`;
- whether the username matches the current user;
- and assembles a `Mercury::Address`.

Source: [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:249).

BigWorld service discovery is therefore not bare address discovery. It also
enforces version and user-isolation checks.

## Birth / Death Listeners

`ListenerMessage` registers birth/death listeners. The source comment states
that listeners receive network-wide process birth/death notifications:
[machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:600).

`registerListener()`:

- serializes the Mercury callback message into a `UDPBundle`;
- saves the binary fragments before and after the address placeholder as `preAddr_` / `postAddr_`;
- constructs a `ListenerMessage`;
- sets `ADD_BIRTH_LISTENER` or `ADD_DEATH_LISTENER`;
- fills in uid, pid, port, and interface name;
- sends the request to the local machined.

Source: [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:104).

The design is notable: when machined observes a process event, it can insert the
target address into the saved Mercury message fragments and send the message back
to the listener.

## How BaseAppMgr Uses It

During initialization BaseAppMgr:

- initializes `ReviverSubject`;
- checks the `-recover` flag;
- registers BaseApp and ServiceApp death listeners;
- registers its own interface with machined;
- registers a CellAppMgr birth listener;
- attempts `findInterface("CellAppMgrInterface")`;
- registers a BaseAppMgr birth listener.

Source: [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:365).

<MermaidDiagram title="BaseAppMgr control-plane initialization">
flowchart TD
  A[BaseAppMgr init] --> B[ReviverSubject init]
  B --> C[registerDeathListener BaseAppIntInterface]
  C --> D[registerDeathListener ServiceAppInterface]
  D --> E[registerWithMachined BaseAppMgrInterface]
  E --> F[registerBirthListener CellAppMgrInterface]
  F --> G[findInterface CellAppMgrInterface]
  G --> H[registerBirthListener BaseAppMgrInterface]
</MermaidDiagram>

Managers do not learn about one another from a static configuration. They build
control-plane connections through machined discovery and event notifications.

## Reviver and machined

Reviver's recovery action ultimately sends a `CreateMessage` to machined:

- `cm.uid_ = getUserId()`
- `cm.recover_ = 1`
- `cm.name_ = createComponent`
- `cm.config_ = BW_COMPILE_TIME_CONFIG`
- destination is `127.0.0.1`

Source: [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:440).

`CreateMessage` fields include:

- executable name.
- configuration.
- user ID.
- recovery flag.
- output-forwarding IP and port.

Source: [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:638).

Reviver is therefore not the process executor. It makes monitoring and recovery
decisions; machined actually starts the process.

## Source Boundaries of machined

machined addresses BigWorld process discovery and management:

- It understands BigWorld interfaces.
- It can find processes by user, version, and interface name.
- It can start BigWorld components.
- It can notify components about birth and death.

It deliberately does not solve:

- Cell partitioning and load balancing.
- BaseApp/CellApp entity-state recovery.
- DBApp data consistency.
- Watcher permission isolation.
- Strongly consistent leader election.

BigWorld's state control plane still needs to understand BaseApp, CellApp,
DBApp, Space, Entity, load, and offload. machined only supplies the lower-level
discovery, startup, and notification mechanics.

## Source Trade-Offs

machined, Managers, and Reviver divide responsibility as follows:

- machined owns local process registration, query, startup, signaling, and birth/death propagation.
- Managers consume birth/death events and maintain BigWorld runtime state.
- Reviver tracks component liveness and decides when to ask machined to start a replacement.

Source-level costs:

- State is distributed across machined, Managers, and each App, so diagnosis requires logs and watchers from several sources.
- The system depends on the local machined being healthy.
- Its security model assumes a trusted internal network.
- There is no strongly consistent control plane; stale processes, duplicate birth/death events, and network partitions require upper-layer protection.

## Source-Code Verification Focus

Control-plane verification should cover discovery, startup, and notification:

- After `ProcessMessage` registration, `findInterface()` should find the target by interface name, version, and user.
- Watcher nub registration should expose port, process ID, short name, and version.
- Birth/death listeners should receive component creation and exit events.
- `CreateMessage` should start a process using component, configuration, user, recovery, and output-forwarding parameters.
- A Reviver-triggered recovery should only decide; execution should pass through machined.
- If machined is unavailable, dependent registration, discovery, and startup paths should report an explicit failure state.

## Chapter Boundary

This chapter explains the machined control plane. It complements
[Scaling and fault tolerance](/architecture/scaling-fault-tolerance): that
chapter covers app admission, Cell load, and Reviver recovery semantics, while
this one covers the lower-level process-discovery and startup mechanism.

## Related Documentation

- Scaling and fault tolerance: [Scaling and fault tolerance](/architecture/scaling-fault-tolerance).
- Process topology: [Process topology and responsibility split](/architecture/process-topology).
- Thread architecture: [Thread architecture and background tasks](/architecture/threading-background-tasks).
