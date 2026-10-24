# Complete Topic Outline


<div class="arch-hero">

This is the complete map for the subsequent `docs/` rewrite. Every topic
follows the same structure: source facts, call chain, source trade-offs,
verification focus, and chapter boundary.

</div>

## Group 1: Foundation architecture

1. [Research method and evidence grading](/architecture/research-method)
2. [Process topology and responsibility split](/architecture/process-topology)
3. [Main loop, ticks, and event dispatch](/architecture/event-loop)
4. [Network I/O model selection](/architecture/network-io-model)
5. [Mercury protocol and reliable UDP](/architecture/mercury-reliable-udp)
6. [Communication abstraction, interface definitions, and inter-component RPC](/architecture/communication-rpc)
7. [Send/receive paths, backpressure, and fault injection](/architecture/network-backpressure-fault-injection)

## Group 2: Game-state model

8. [Entity model: Base / Cell / Client semantics](/architecture/entity-model)
9. [EntityDef, properties, methods, and protocol generation](/architecture/entitydef-contract-generation)
10. [Serialization and deserialization: BinaryStream, DataType, DataDescription](/architecture/serialization-entitydef)
11. [AOI, Witness, Ghost, and bandwidth scheduling](/architecture/aoi-witness-ghost)
12. [Space partitioning, Cell partitioning, and load balancing](/architecture/cell-partition-load-balance)
13. [Entity migration, Cell retirement, and cross-process state movement](/architecture/entity-migration-offload)
14. [Entity lifecycle state machine](/architecture/entity-lifecycle-state-machine)

## Group 3: Runtime engineering

15. [Login, sessions, and Proxy takeover](/architecture/login-session-proxy-flow)
16. [Threading, background tasks, and the Python GIL](/architecture/threading-background-tasks)
17. [Time system, timers, GameTime, and controllable test time](/architecture/testing-time-control)
18. [Script hot reload, interpreter switching, and entity migration](/architecture/hot-reload-script-migration)
19. [Persistence, consistency, and database threading model](/architecture/persistence-db-model)
20. [Dynamic scaling, control plane, and capacity governance](/architecture/scaling-fault-tolerance)
21. [Reviver, machined, and failure-recovery boundaries](/architecture/machined-control-plane)

## Group 4: Engineering quality and source boundaries

22. [Testing, fault injection, and coverage boundaries](/architecture/testing-fault-injection-coverage)
23. [Security, rate limiting, encryption, and attack surface](/architecture/security-rate-limit)
24. [Watcher, Profiler, logging, and observability](/architecture/observability-watcher-profiler-logs)
25. [Memory, object lifetime, and resource management](/architecture/memory-lifecycle)
26. [Build, platforms, and dependency governance](/architecture/build-platform-dependencies)
27. [MMO architecture boundary comparison](/architecture/modern-mmo-comparison)

## Questions explicitly covered

<div class="decision-grid">
  <div class="decision-card">
    <h3>Network model</h3>
    <p>select, poll, LT epoll, ET epoll, io_uring, recvmmsg/sendmmsg, SO_REUSEPORT, and AF_XDP/DPDK.</p>
  </div>
  <div class="decision-card">
    <h3>Threading model</h3>
    <p>Single-reactor main thread, background tasks, BaseApp WorkerThread, Python GIL, actor-like mailboxes, and entity scheduling boundaries.</p>
  </div>
  <div class="decision-card">
    <h3>Hot updates</h3>
    <p>New-interpreter loading, old-interpreter migration, EntityType/UDO/Mailbox/entity migration, and production boundaries.</p>
  </div>
  <div class="decision-card">
    <h3>Testing and time</h3>
    <p>unit_test, network fault injection, timer driving, GameTime, virtual-clock gaps, and repeatable-test boundaries.</p>
  </div>
  <div class="decision-card">
    <h3>Login and sessions</h3>
    <p>LoginApp, DBApp, BaseAppMgr, BaseApp, Proxy, SessionKey, PendingLogins, and NAT/firewall handling.</p>
  </div>
  <div class="decision-card">
    <h3>Entity lifecycle</h3>
    <p>Base-only, pending cell, real, ghost, offload, onload, destroy, restore, and zombie-ghost states.</p>
  </div>
  <div class="decision-card">
    <h3>Communication and serialization</h3>
    <p>Mercury Interface, Bundle, BinaryStream, DataType, EntityDescription, protocol compatibility, and the EntityDef contract.</p>
  </div>
  <div class="decision-card">
    <h3>Dynamic scaling</h3>
    <p>Manager admission of new apps, Cell load balancing, entity migration, Reviver recovery, and state-migration boundaries.</p>
  </div>
</div>

## Fixed output for every chapter

Every chapter must contain:

- Source entry points
- Key call chains
- Thread/process ownership
- Data structures or state machines
- Source trade-offs
- Source boundaries
- Verification focus
- Chapter boundary

When an item does not exist in the source, the document must explicitly say
that it “does not exist or is unconfirmed” and explain plausible reasons,
rather than waiting for readers to identify the omission.
