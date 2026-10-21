# MMO Architecture Boundary Comparison

> **English** | [中文](modern-mmo-comparison.md)

<div class="arch-hero">

This chapter does not impose an external architecture template on BigWorld.
Instead, it compares the boundaries established by the preceding source
analysis: which capabilities belong to BigWorld's core MMO semantics, and
which are merely resource-layer, tool-layer, or peripheral engineering
concerns.

</div>

## Conclusion first

BigWorld's most valuable part is not one isolated technique but several
closed loops:

- BaseApp, CellApp, DBApp, LoginApp, Manager, machined, and Reviver form the process-level control plane. See [process topology and responsibility split](/architecture/process-topology).
- Base/Cell/Client split entity authority domains, while Base coordinates database persistence. See [entity model Base/Cell/Client](/architecture/entity-model).
- AOI, Witness, Ghost, Haunt, and offload form the cross-Cell visibility and migration loop. See [AOI, Witness, and Ghost](/architecture/aoi-witness-ghost).
- Mercury handles high-frequency communication through UDP channels, reliable messages, ACKs, retransmission, piggybacking, and version handling. See [Mercury reliable UDP](/architecture/mercury-reliable-udp).
- EntityDef simultaneously defines properties, methods, data domains, persistence, client visibility, and the protocol digest. See [EntityDef contract and protocol generation](/architecture/entitydef-contract-generation).
- Watcher, Profiler, EntityProfiler, and Message Logger are embedded directly in the runtime diagnosis path. See [Watcher, Profiler, and logging](/architecture/observability-watcher-profiler-logs).

A single external concept cannot directly replace these loops. During source
analysis, first determine which loop a problem belongs to before discussing
optimization or replacement.

## Network boundary

BigWorld's network model is not an ordinary “one TCP connection per player”
server model:

- There are few UDP sockets; `UDPChannel` is the logical connection. See [udp_channel.hpp:57](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:57).
- `EventPoller` only dispatches readiness; handlers still execute synchronously on the main reactor. See [event_poller.cpp:1115](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:1115).
- `PacketReceiver::maxSocketProcessingTime` protects the budget for each socket-processing pass. See [packet_receiver.cpp:83](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:83).
- Reliability is expressed jointly by Channel, Bundle, ACK, retransmission, and piggybacking. See [bundle.hpp:27](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:27).
- Channel version participates in entity migration and component recovery, so packet send/receive alone is insufficient. See [udp_channel.hpp:383](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:383).

Therefore, the network I/O backend is only one boundary. Real correctness also
depends on Mercury's reliable layer, entity message ordering, request/reply
timeouts, and main-thread handler budgets.

## Concurrency boundary

BigWorld's concurrency model is a main reactor, background tasks, and
multiprocess sharding:

- The main thread advances network handlers, timers, frequent tasks, entity logic, and script callbacks. See [event_dispatcher.cpp:428](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:428).
- Background tasks offload blocking paths such as database work, files, and resource loading. See [bgtask_manager.cpp:137](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:137).
- Completed tasks commit results back to the main thread through foreground tasks. See [bgtask_manager.cpp:744](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:744).
- BaseApp/CellApp horizontal scaling depends on process sharding, Cell splitting, and entity migration. See [cellappmgr.cpp:1245](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1245).

This differs from “one independently scheduled unit per entity.” BigWorld has
actor-like mailboxes, but entities are split into Base, Cell, Client, and
Ghost views; property synchronization, AOI, database writes, and migration
streams are first-class mechanisms.

## State boundary

Base/Cell/Client is not simple object layering; it is a split of authority:

- Base stores long-term identity, Proxy, DBID, the Cell mailbox, and persistence-coordination authority. See [base.hpp:58](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:58).
- Cell stores spatial authority, real/ghost state, AOI, physics, position, and offload state. See [entity.hpp:89](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:89).
- Client receives only the entity projection filtered by Witness. See [entity.cpp:2475](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2475).
- Database stores only the EntityDef persistent view and engine runtime metadata. See idatabase.hpp.

This explains why debugging one entity problem often requires joining logs
from BaseApp, CellApp, DBApp, Proxy, Witness, Mailbox, Channel, and the
client.

## Control-plane boundary

machined, Manager, and Reviver address BigWorld component discovery, process
admission, birth/death notification, and recovery decisions:

- machined does not understand Cell partitions, entity migration, or database consistency. See [machined control plane](/architecture/machined-control-plane).
- Manager maintains BaseApp, CellApp, DBApp, Space, Cell, and load state. See [cellappmgr.cpp:190](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:190).
- Reviver only monitors and requests restarts; it cannot recover unbacked in-memory state. See [reviver.cpp:440](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:440).
- A successful new-process launch does not mean game-state migration has completed. See [scaling and fault tolerance](/architecture/scaling-fault-tolerance).

Consequently, scale-out and disaster-recovery tests must cover app admission,
Cell-boundary changes, entity offload, Ghost readiness, and death recovery —
not merely whether a process remains alive.

## Protocol boundary

EntityDef plus BinaryStream is not an ordinary wire schema:

- `entities.xml` order affects EntityTypeID. See [EntityDef contract and protocol generation](/architecture/entitydef-contract-generation).
- `.def` property flags determine Base, Cell, Client, Persistent, and Ghost data domains. See data_description.hpp.
- The exposed-method range determines which methods the client can call. See exposed_message_range.hpp.
- Digests determine compatibility among client, server, and database persistent views. See entity_description.hpp.
- The database mapping layer consumes the EntityDef persistent stream, not Python objects themselves. See [persistence and the database model](/architecture/persistence-db-model).

Therefore, a `.def` change must be evaluated together with protocol numbers,
client visibility, database schema, hot updates, migration streams, and the
persistence digest.

## Observability boundary

BigWorld observability is more than logging:

- Watcher is a runtime object tree supporting GET/SET/TELL. See watcher.hpp.
- ForwardingWatcher forwards watcher commands to a set of processes. See watcher_nub.cpp.
- Profiler and DogWatch record in-process time consumption. See profiler.hpp.
- EntityProfiler participates in entity-load analysis. See entity_profiler.hpp.
- Message Logger is a separate centralized logging path with MLDB/MongoDB backends and reconnection boundaries. See message_logger.hpp.

The most important boundary is that Watcher can both observe and control, so
it must be analyzed as a management-plane attack surface. Message Logger
supports centralized queries but must not be treated as a strongly reliable
audit log.

## Verification focus

Cross-chapter verification should follow BigWorld's actual loops rather than
splitting work by external technology names:

- Login: LoginApp, DBApp, BaseAppMgr, BaseApp, Proxy, SessionKey, and successful-login reply caching.
- Communication: InterfaceElement, Bundle, Channel, request/reply, exposed-method range, and complete stream consumption.
- Entities: Base/Cell creation, real/ghost switching, offload/onload, destroy, restore, and Witness flush.
- Database: Base coordination, Cell persistent-data requests, DBApp writeEntity, PropertyMapping, and secondary databases.
- Control plane: machined registration, Mgr admission, birth/death, Reviver recovery, and recently-dead channels.
- Network: ACK, retransmission, fragmentation, piggybacking, channel version, full send queues, and artificial packet loss/latency.
- Testing: real dispatcher time, GameTime, TimeKeeper, timeout/retry/watchdog, and multiprocess fault injection.

## Chapter boundary

This chapter only summarizes architectural boundaries. Specific source entry
points, call chains, and verification details belong in the corresponding
topic chapters rather than being expanded here.
