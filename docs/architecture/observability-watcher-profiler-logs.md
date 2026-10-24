# Watcher, Profiler and Logs


<div class="arch-hero">

BigWorld's observability is built around the Watcher tree, the Profiler, DogWatch,
EntityProfiler, the Message Logger and the management tooling. It is not plain log
aggregation: runtime objects, the process control plane, performance sampling and
centralised logging all live in one engine toolchain.

</div>

## Conclusion First

BigWorld's observability is genuinely worth studying, because it is not log
instrumentation bolted on afterwards. It is embedded deeply in the engine object
model:

- Watcher provides a tree of paths that can read and write runtime variables.
- The Manager process exposes App, Space, Cell, load and DB state through Watcher.
- `ForwardingWatcher` forwards watcher requests to a set of BaseApp/CellApp targets.
- `PacketReceiver` exposes network stats and the socket processing time budget.
- The Profiler supports C++/GPU/Python categorisation, hierarchical statistics, hitch
  detection and JSON/CSV output.
- EntityProfiler turns entity execution time directly into load, which feeds
  load-balancing decisions.
- The Message Logger is a separate toolchain, not just each process writing stdout.

Key source code:

- The Watcher type system is at
  watcher.hpp.
- `PacketReceiver::pWatcher()` is at
  packet_receiver.cpp.
- `BaseAppMgr::addWatchers()` is at
  baseappmgr.cpp.
- `Space::pWatcher()` is at
  space.cpp.
- `ForwardingWatcher` is at
  watcher_forwarding.cpp.
- The Profiler macros and event model are at
  profiler.hpp.
- EntityProfiler is at
  entity_profiler.hpp.
- The combined Guard/Profiler/MemTracker macros are at
  guard.hpp.

## What a Watcher Is

A Watcher can be understood as BigWorld's built-in runtime object tree.

It is not a plain metrics system. It also provides:

- Read: query variables, statistics and object state.
- Write: change writable configuration or trigger commands.
- Paths: organise objects with directory-like paths.
- Types: int, uint, float, bool, string, tuple and type are supported.
- Remote access: read other components through the watcher network protocol.

The Watcher type definitions are at
watcher.hpp.

`WATCHER_SEPARATOR` is `/`, which means the Watcher is organised as a path tree by
nature. See
watcher.hpp.

## The Watcher Tree Model

<MermaidDiagram title="Watcher observation model">
flowchart TD
  Root[Watcher root] --> ServerApp[ServerApp common watchers]
  Root --> Mgr[BaseAppMgr / CellAppMgr]
  Mgr --> Apps[baseApps / cellApps / dbApps]
  Mgr --> Loads[load min avg max]
  Mgr --> Forward[forwardTo]
  Forward --> Remote[remote component watcher]
  Root --> Network[NetworkInterface / PacketReceiver]
  Network --> Stats[packet stats]
  Network --> Budget[maxSocketProcessingTime]
  Root --> Space[Space]
  Space --> Cells[cells / bsp / geometry / load]
</MermaidDiagram>

Design implications:

- Observability is organised by runtime object, not by file or log keyword.
- The Manager can see the key data of the cluster control plane.
- Load balancing, space partitioning and the network processing budget can all be
  observed at runtime.
- Watcher is both a debugging tool and part of the operations control plane.

## The Watcher Network Protocol

A Watcher is not just the local `Watcher::rootWatcher()`. A service process can
expose its watcher tree to external tools through `WatcherNub`.

Key facts from the source:

- `WatcherNub::init()` creates both a UDP socket and a TCP socket and tries to bind
  them to the same port, see
  watcher_nub.cpp
  and
  watcher_nub.cpp.
- `WatcherNub::attachTo()` registers the UDP and TCP file descriptors with
  `EventDispatcher`, named `WatcherUDP` and `WatcherTCP` respectively, see
  watcher_nub.cpp.
- `WatcherNub::notifyMachineGuard()` registers the watcher nub's port, process ID,
  short name and version with the local machined, see
  watcher_nub.cpp.

The Watcher protocol itself supports two generations of GET/SET/TELL messages:

- v1: `WATCHER_MSG_GET`, `WATCHER_MSG_SET`, `WATCHER_MSG_TELL`.
- v2: `WATCHER_MSG_GET2`, `WATCHER_MSG_SET2`, `WATCHER_MSG_TELL2`,
  `WATCHER_MSG_SET2_TELL2`.

The definitions are at
watcher_nub.hpp.

`WatcherPacketHandler` batches several path requests into one reply packet and handles
the UDP packet-size limit:

- A v1 UDP reply writes `<Err>` and `Exceeded maximum packet size` when the limit is
  exceeded.
- v2 uses the larger `WN_PACKET_SIZE_TCP` for TCP, while UDP remains limited by
  `WN_PACKET_SIZE`.

Source:
watcher_packet_handler.cpp
and
watcher_packet_handler.cpp.

This explains several engineering properties of the Watcher:

- It is a runtime remote-introspection protocol, not a plain metrics-pulling protocol.
- It allows SET, so it carries control-plane risk by nature.
- It relies on machined discovery, so not every tool needs each port configured
  statically.
- Querying a large watcher subtree can hit the UDP packet size and needs TCP or narrower
  paths.

## Manager Watchers

`BaseAppMgr::addWatchers()` exposes:

- `numBaseApps`
- `numServiceApps`
- `numBases`
- `numProxies`
- `config/shouldShutDownOthers`
- `baseAppLoad/min`
- `baseAppLoad/average`
- `baseAppLoad/max`
- `serviceAppLoad/min`
- `serviceAppLoad/average`
- `serviceAppLoad/max`

Source:
baseappmgr.cpp.

These watchers map directly onto operational questions:

- How many BaseApps are there right now.
- How many proxies are online right now.
- Whether the BaseApp load is uneven.
- Whether a Base needs to be migrated or processes need to be scaled out.

The CellAppMgr Space watcher exposes:

- `id`
- `cells`
- `areaNotLoaded`
- `numRetiringCells`
- `loadMin`
- `loadMax`
- `loadAvg`
- `artificialMinLoad`
- `numCells`
- `geometry`
- `bsp`

Source:
space.cpp.

BigWorld's Cell space partitioning is therefore not a black box. The space, the BSP, the
load and retiring Cells can all be observed.

## ForwardingWatcher

`ForwardingWatcher` forwards watcher requests to a set of target components. It defines
several targets:

- `cellApps`
- `baseApps`
- `serviceApps`
- `baseServiceApps`
- `leastLoaded`

Source:
watcher_forwarding.cpp.

When `setFromStream()` parses a path, it supports:

- `all/command/addBot`
- `leastLoaded/command/addBot`
- `1,2,45,87/command/addBot`

Source comment:
watcher_forwarding.cpp.

This design is powerful, and it also carries risk:

- Powerful: the Manager can run one watcher operation across many components.
- Risk: if write access is exposed on an untrusted network, it becomes a remote
  administration entry point.

A security review must split Watcher access explicitly into read-only observation and
writable control.

## Watcher Encoding Boundaries

v2 Watcher data is not JSON; it is a binary type stream.

`WatcherProtocolDecoder::decodeNext()` reads `type` and `mode` on each iteration, then
dispatches to the matching handler by watcher type, see
watcher_protocol.cpp.

The types supported today are:

- `WATCHER_TYPE_INT`
- `WATCHER_TYPE_UINT`
- `WATCHER_TYPE_FLOAT`
- `WATCHER_TYPE_BOOL`
- `WATCHER_TYPE_STRING`
- `WATCHER_TYPE_TUPLE`

Source:
watcher_protocol.cpp.

`defaultHandler()` reads the length and validates the remaining stream, so out-of-range
reads are not allowed, see
watcher_protocol.cpp.

The Watcher security boundary is therefore not just "is there authentication". It also
covers:

- Whether a path may be written.
- Whether the type and the mode match.
- Whether the length field can be trusted.
- Whether one request can be amplified into a large number of forwarded watcher
  requests.
- Whether the reply packet can be so large that it is dropped or truncated.

## Network Observation

`PacketReceiver::pWatcher()` exposes:

- `stats`
- `maxSocketProcessingTimeStamps`

Source:
packet_receiver.cpp.

The comment on `maxSocketProcessingTime()` states explicitly:

- When it is non-zero, it limits the time a single socket input notification is allowed
  to spend processing.
- When it is zero, there is no limit and it processes until the receive queue is empty.

Source:
packet_receiver.cpp.

This matters for reading the network model. BigWorld does not merely wait for events with
epoll; it also bounds how long one round of network processing occupies the main thread,
through a parameter that is both observable and tunable.

## Profiler

The Profiler is not a simple timer. `profiler.hpp` exposes several modes:

- `HIERARCHICAL`
- `SORT_BY_TIME`
- `SORT_BY_NUMCALLS`
- `SORT_BY_NAME`
- `CPU_GPU`
- `GRAPHS`
- `CORES`

Source:
profiler.hpp.

The event categories are:

- `CATEGORY_CPP`
- `CATEGORY_GPU`
- `CATEGORY_PYTHON`

Source:
profiler.hpp.

The macro layer includes:

- `PROFILER_SCOPED`
- `PROFILER_COUNTER`
- `PROFILE_FILE_SCOPED`
- `PROFILER_IDLE_SCOPED`
- `PROFILER_LEVEL_COUNTER`

Source:
profiler.hpp.

BigWorld is therefore not asking only "where is it slow". It also tracks:

- Call hierarchy.
- Call count.
- Idle time.
- File I/O.
- Python and C++ categorisation.
- Hitch detection.

## EntityProfiler and Load

`EntityProfiler` is one of BigWorld's most characteristic designs: it turns entity
execution time into load, which Cell load balancing then consumes.

In the source, `EntityProfiler` exposes:

- `load()`
- `rawLoad()`
- `maxRawLoad()`
- `artificialMinLoad()`
- `tick()`

and it computes load with exponential smoothing, see
entity_profiler.hpp.

`AUTO_SCOPED_ENTITY_PROFILE` uses RAII to start on scope entry and stop on scope exit,
see
entity_profiler.hpp.

Design implications:

- Load balancing does not look at entity count alone.
- A single expensive entity shows up in the load.
- The artificial minimum load exists for testing and tuning.
- Observation data feeds the scheduling policy directly.

## Guard, Stack Tracker and MemTracker

`guard.hpp` defines combined macros:

- `BW_GUARD`
- `BW_GUARD_PROFILER`
- `BW_GUARD_MEMTRACKER`
- `BW_GUARD_PROFILER_MEMTRACKER`

Source:
guard.hpp.

Macros like these are common in older C++ game engines, and they serve to:

- Provide function-level stack tracking.
- Open a profiling scope.
- Open a memory-tracking scope.
- Provide context at crash or assertion time.

The advantage is that this is intrusive but low-cost to add. The cost is macro spread and
a complicated set of build switches.

## The Logging System

The repository contains a standalone toolchain at `server/tools/message_logger`. BigWorld
logging is therefore not just every process writing to stdout; there is a central message
logger.

From an architecture standpoint, the value of the Message Logger is:

- A multi-process cluster needs log collection in one place.
- MMO incidents usually cross BaseApp, CellApp, DBApp and Manager.
- Single-process logs alone cannot reconstruct entity migration, Channel death or the
  login path.

The end-to-end chain is:

<MermaidDiagram title="Log forwarding chain">
flowchart TD
  A[TRACE/DEBUG/INFO/WARNING/ERROR macros] --> B[DebugFilter]
  B --> C[DebugMessageCallback]
  C --> D[LoggerMessageForwarder]
  D --> E[LoggerEndpoint TCP/UDP]
  E --> F[message_logger Logger]
  F --> G[LogStorage]
  G --> H[MLDB file backend or MongoDB backend]
</MermaidDiagram>

Source evidence:

- `DebugFilter::handleMessage()` hands the message to the registered
  `DebugMessageCallback`, see
  debug_filter.cpp.
- The `LoggerMessageForwarder` constructor calls
  `DebugFilter::instance().addMessageCallback(this)`, hooking itself into the log
  callback chain, see
  logger_message_forwarder.cpp.
- `FindLoggerHandler` finds the MessageLogger address through machined's
  `ProcessStatsMessage` and adds the logger endpoint, see
  logger_message_forwarder.cpp.
- `LoggerEndpoint` connects to the remote MessageLogger and maintains TCP/UDP, reconnect,
  the buffer queue and the dropped-message count, see
  logger_endpoint.hpp.
- `Logger` is the body of the `message_logger` process, responsible for receiving
  component logs, registering components, handling disconnects and writing to storage,
  see
  logger.hpp.

This chain reflects several trade-offs:

- The game processes do not write to the central log file directly; logs are forwarded as
  network messages.
- The MessageLogger can filter by UID, component name, LoggerID and priority, see
  logger.cpp.
- The Logger process exposes watchers of its own, for example `size`, `reattachAll` and
  `filter/TRACE` through `filter/CRITICAL`, see
  logger.cpp.

## MessageLogger Storage Model

`message_logger` is not a single text-file writer. It has an abstract storage layer:

- `LogStorage` defines interfaces such as `addLogMessage()`, `writeLogToDB()`, `roll()`,
  `validateNextHostname()` and `setAppInstanceID()`, see
  log_storage.hpp.
- `LogStorage::addLogMessage()` reads the network header, the message source, the priority
  and the format string, then resolves the host and category, and finally hands the
  message to the backend for writing, see
  log_storage.cpp.
- `mldb` is the local file-based storage backend, in
  mldb/log_storage.hpp.
- The MongoDB backend is implemented by `LogStorageMongoDB`, with buffering, a background
  TaskManager, reconnect, roll, expiry cleanup and BSON construction, see
  mongodb/log_storage.hpp.

The MongoDB backend has one more important runtime boundary:

- If the connection drops, `LogStorageMongoDB::addLogMessage()` pauses logging and queues
  a `ReconnectTask`, then resumes logging once the connection is back, see
  mongodb/log_storage.cpp.

MessageLogger is therefore a standalone log-database service, not "each process printfing
to a file". It can be queried centrally, but it brings its own reliability problems:

- LoggerEndpoint drops messages when its buffer is full.
- A MessageLogger process failure affects central logging.
- Writes pause when the MongoDB connection fails.
- The log format string and the metadata protocol need front/back-end version
  compatibility.

## Log Fields and Gaps

BigWorld's logs already have some structured shape:

- `LoggerMessageHeader` carries the component priority, the message priority, the message
  source and the category, see
  logger_message_forwarder.cpp.
- `LoggerComponentMessage` carries the version, loggerID, uid, pid and componentName, see
  logger_message_forwarder.cpp.
- `LogStorage::resolveUID()` resolves a uid to a username through machined, see
  log_storage.cpp.

But it is not yet fully structured logging in the modern sense:

- Structured log fields.
- A trace/correlation id.
- A unified convention for key context such as EntityID, SpaceID, CellAppID and Channel
  addr.
- Log sampling and dynamic level control.
- Correlation with metrics and profiler events.

So two things have to be kept separate:

- BigWorld already has central logging and partly structured metadata.
- BigWorld has no unified cross-process trace/span model, and no requirement that every
  key log carries EntityID/DBID/SpaceID/SessionKey.

## Source-Code Responsibility Split

This chapter must not lump Watcher, Profiler and Message Logger into one "monitoring
system". The responsibility boundaries in the source are finer:

- Watcher is a runtime object tree and a management protocol; paths can be read, and may
  also be written or executed via TELL.
- `ForwardingWatcher` is a cross-process forwarder that can send watcher commands to
  all, leastLoaded or an explicit set of processes.
- `Profiler` / `DogWatch` is in-process timing sampling and section statistics, feeding
  performance analysis.
- `EntityProfiler` serves entity load analysis directly and overlaps with the CellApp
  load-balancing input.
- `LoggerEndpoint` and the Message Logger form the central logging chain, owning the
  format string, process metadata, priority, category and the storage backend.

When analysing the source, decide first which chain a question belongs to:

- To see a runtime variable, check the watcher path and the setter/TELL permissions.
- To see time spent inside a tick, check profiler scopes and `DogWatch` sections.
- To see the CellApp load caused by entities, check EntityProfiler and the load reports.
- To see a cross-process fault, check whether the Message Logger received complete logs
  and whether the backend paused writes.

## Source Boundaries

A few boundaries directly affect production troubleshooting:

- Watcher is not a read-only metrics protocol; SET/TELL paths must be treated as
  control-plane entry points.
- Watcher UDP replies are limited by packet size, so a large path tree or a long response
  cannot be assumed complete.
- ForwardingWatcher changes a command's scope, so troubleshooting must distinguish local
  watchers from forwarded watchers.
- `LoggerEndpoint` can lose logs when the Message Logger is unavailable or its buffer is
  full, so central logging must not be treated as a strongly reliable audit log.
- The MongoDB backend pauses writes and queues a reconnect task on disconnect, so log
  visibility drops until it recovers.
- Logs already carry metadata such as component, priority, source and category, but the
  source has no unified trace/span model.

## Source-Code Verification Focus

Tests for an observability change must not stop at "metrics are visible". They must cover
at least:

- Watcher v1/v2 GET/SET/TELL compatibility.
- Behaviour when a UDP watcher reply packet exceeds the limit.
- ForwardingWatcher results for all, leastLoaded and an explicit ID list.
- Permissions, auditing and failure rollback for writable watchers.
- Message loss in LoggerEndpoint when the MessageLogger is unavailable, during reconnect
  and when the buffer is full.
- Roll, expiry cleanup and query compatibility for the MLDB/MongoDB backends.
- Whether DebugFilter category suppression takes effect dynamically.
- Whether EntityProfiler load matches the Cell load-balancing input.

## Chapter Boundary

This chapter explains the observability facilities. The next chapter analyses memory and
lifetime: BigWorld makes heavy use of custom SmartPointer, ReferenceCount, PyObjectPlus,
Packet/Bundle and entity state transitions, so lifetime itself is a source of
architectural complexity.
