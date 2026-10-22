---
layout: home

hero:
  name: "BigWorld Engine Architecture Research"
  text: "Learning MMO server design from source-evidence chains"
  tagline: "Focused analysis of threading, networking, ticks, communication, serialization, hot updates, scaling and failure recovery — with source evidence, trade-offs, and verification boundaries."
  actions:
    - theme: brand
      text: Start the research
      link: /architecture/research-method
    - theme: alt
      text: Network model
      link: /architecture/network-io-model

features:
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><rect x="3" y="4" width="18" height="5" rx="1.5"/><rect x="3" y="15" width="8" height="5" rx="1.5"/><rect x="13" y="15" width="8" height="5" rx="1.5"/><path d="M12 9v3"/><path d="M7 15v-3h10v3"/></svg>'
    title: Not a directory tour
    details: The docs are organized around architecture decisions; each chapter covers design goals, call chains, thread ownership, source trade-offs, and verification boundaries.
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><circle cx="10.5" cy="10.5" r="6.5"/><path d="m21 21-5.7-5.7"/><path d="m8 10.5 1.8 1.8 3.4-3.6"/></svg>'
    title: Source evidence first
    details: Key conclusions are annotated with source entry points, and source facts, high-confidence inference, and unconfirmable open questions are kept clearly apart.
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><circle cx="12" cy="12" r="5.5"/><ellipse cx="12" cy="12" rx="10" ry="4" transform="rotate(-24 12 12)"/></svg>'
    title: Clear boundaries
    details: Understand the engine architecture first, then judge which parts are core source constraints and which are merely peripheral dependencies or toolchain issues.
---

> **English** | [中文](index.md)

<div class="arch-hero">

**Main research thread**: BigWorld is not an ordinary Python project — it is a distributed MMO server engine led by C/C++ with Python embedded in it, and the embedded interpreter has already completed its [Python 2.7 → 3.13 migration](/upgrade-plan/migration-status). The value of studying it lies not in "copying old code" but in understanding how a mature commercial MMO engine handled networking, entities, space, load, scripting, and operations under the platform constraints of its time.

</div>

## Current sample chapters

<div class="topology-grid">
  <div class="topology-card">
    <h3>Research method</h3>
    <p>Establish the evidence grading first, so source facts, era-based inference, and subjective guesswork are never mixed together.</p>
  </div>
  <div class="topology-card">
    <h3>Process topology</h3>
    <p>Understand the distributed architecture from the responsibility boundaries of BaseApp, CellApp, DBApp, Mgr, LoginApp, and Reviver.</p>
  </div>
  <div class="topology-card">
    <h3>Event loop</h3>
    <p>Analyze the execution order and tick fairness of the main Reactor, Timer, FrequentTask, and Network.</p>
  </div>
  <div class="topology-card">
    <h3>Network model</h3>
    <p>Analyze the boundaries of select, poll, epoll, batched UDP, SO_REUSEPORT, io_uring, and the Mercury execution model.</p>
  </div>
  <div class="topology-card">
    <h3>Reliable UDP</h3>
    <p>Analyze Mercury Channel ACK, retransmission, windows, piggyback, indexed channels, and version semantics.</p>
  </div>
  <div class="topology-card">
    <h3>Communication and serialization</h3>
    <p>Explain InterfaceElement, Bundle, RPC, the EntityDef contract, BinaryStream, DataType, and DataDescription.</p>
  </div>
  <div class="topology-card">
    <h3>Runtime engineering</h3>
    <p>Cover the threading architecture, background tasks, the Python GIL, hot updates, controllable test time, dynamic scaling, and fault tolerance.</p>
  </div>
  <div class="topology-card">
    <h3>Game-state governance</h3>
    <p>Analyze Base/Cell/Client, AOI/Witness/Ghost, cell partitioning, entity migration, and DB persistence.</p>
  </div>
  <div class="topology-card">
    <h3>Engineering quality</h3>
    <p>Fill in security rate limiting, encryption, Watcher/Profiler/logging, memory lifecycle, build dependencies, and MMO architecture boundaries.</p>
  </div>
</div>

## Reading order

1. [Research method and evidence grading](/architecture/research-method)
2. [Process topology and responsibility split](/architecture/process-topology)
3. [Main loop, tick, and event dispatch](/architecture/event-loop)
4. [Network I/O model choice](/architecture/network-io-model)
5. [Mercury reliable UDP](/architecture/mercury-reliable-udp)
6. [Communication abstraction and RPC](/architecture/communication-rpc)
7. [Serialization and EntityDef](/architecture/serialization-entitydef)
8. [Network backpressure and fault injection](/architecture/network-backpressure-fault-injection)
9. [Entity model: Base / Cell / Client](/architecture/entity-model)
10. [EntityDef contract and protocol generation](/architecture/entitydef-contract-generation)
11. [AOI, Witness, and Ghost](/architecture/aoi-witness-ghost)
12. [Cell partitioning and load balancing](/architecture/cell-partition-load-balance)
13. [Entity migration and offload](/architecture/entity-migration-offload)
14. [Entity lifecycle state machine](/architecture/entity-lifecycle-state-machine)
15. [Persistence and the DB thread model](/architecture/persistence-db-model)
16. [Login, sessions, and proxy takeover](/architecture/login-session-proxy-flow)
17. [Threading architecture and background tasks](/architecture/threading-background-tasks)
18. [Script hot update and migration](/architecture/hot-reload-script-migration)
19. [Testing and controllable time](/architecture/testing-time-control)
20. [Dynamic scaling and fault tolerance](/architecture/scaling-fault-tolerance)
21. [machined control plane and process discovery](/architecture/machined-control-plane)
22. [Testing, fault injection, and coverage boundaries](/architecture/testing-fault-injection-coverage)
23. [Security, rate limiting, and encryption](/architecture/security-rate-limit)
24. [Watcher, Profiler, and logging](/architecture/observability-watcher-profiler-logs)
25. [Memory, object lifecycle, and resource management](/architecture/memory-lifecycle)
26. [Build, platforms, and dependency governance](/architecture/build-platform-dependencies)
27. [MMO architecture boundary comparison](/architecture/modern-mmo-comparison)
28. [Full topic outline](/architecture/outline)

## Core judgments

- BigWorld's server core is a distributed multi-process system, not a monolithic server.
- Inside each major process the design leans toward a single Reactor main thread, with background threads for blocking I/O, asset loading, database work, and so on — it is not full-logic multithreading.
- The Linux network backend uses `epoll` but keeps `select`-like level-triggered semantics; it is not an ET or `io_uring` architecture.
- A Mercury UDP Channel is a logical connection, not one socket per player, so network optimization cannot simply apply a "million FDs" mental model.
- The Mercury reliability layer expresses reliability per message and per Bundle, supporting ACK, retransmission, out-of-order windows, piggyback, indexed channels, and entity-migration version numbers.
- EntityDef serialization is not mere network encoding — it is the type contract that scripting, entity attributes, method parameters, persistence, migration, and hot updates all depend on.
- The EntityDef contract is generated jointly from `entities.xml`, `.def` files, components, interfaces, script distribution, message scope, and digests; entity order and exposed numbering carry protocol meaning.
- Network backpressure is composed of receive budgets, send-queue-full waits, manual packet loss/latency, and the Mercury reliability layer — it is not something a single epoll parameter can explain.
- BigWorld's entity model is a three-tier authority split of Base/Cell/Client, with real and ghost distinguished inside a cell.
- AOI is managed by Witness, including hysteresis, bandwidth budgets, entity aliases, reliable positions, and SpaceData synchronization.
- Cell load balancing adjusts spatial BSP boundaries and entity ownership, not ordinary request distribution.
- The entity lifecycle is essentially a state machine over Base, Cell real, Ghost, Offload, Onload, Destroy, and Restore.
- Persistence is coordinated by the BaseApp over Base/Cell data; the DBApp isolates blocking database work behind IDatabase and background tasks.
- The login chain completes through LoginApp, DBApp, BaseAppMgr, BaseApp, and a second Proxy handshake; success replies are cached, while failure replies are deliberately unreliable to reduce DoS exposure.
- BigWorld's threading model is mainly a main Reactor plus background tasks that commit back to the main thread, not full-logic multithreading.
- `reloadScript` is a development-debugging-oriented, high-risk migration mechanism; the source explicitly warns against using it in production.
- The current time system relies mainly on the real `timestamp()`; controllable virtual time is an important gap for repeatable testing.
- The current test system is not weak on Mercury, EntityDef, Replay, or Ghost buffering, but cluster-level black-box regression and unified chaos injection are still insufficient.
- The core of dynamic scaling is the Manager admitting processes and driving Cell/Entity state migration, not simply launching stateless replicas.
- machined provides process registration, interface discovery, birth/death listeners, and startup components — the foundation of BigWorld's legacy-style control plane.
- BigWorld has explicit ingress rate limiting, message budgets, and a Channel encryption abstraction, but it is not a modern end-to-end security system.
- Watcher/Profiler/EntityProfiler are the core facilities for understanding runtime state and load governance, and they also need permission boundaries and metric export.
- Object lifecycles depend on intrusive reference counting, Python objects, Mercury Channels, and entity real/ghost transitions — mechanical replacement with standard smart pointers is not appropriate.
- Build governance has to handle CMake/Makefile, OpenSSL, embedded Python, third-party libraries, and the multi-target server/client/tools build, not just bumping a single dependency version.
