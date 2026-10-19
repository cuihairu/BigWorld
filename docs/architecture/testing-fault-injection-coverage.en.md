# Testing System, Fault Injection, and Coverage Boundaries

> [Chinese](testing-fault-injection-coverage.md) | **English**

<div class="arch-hero">

BigWorld has a substantial test system, including `unit_test` coverage and
network-layer artificial loss and latency. It tests reliable UDP, overflow,
channel switching, ghost buffering, and replay I/O, but these capabilities are
not equivalent to a unified cluster-integration or chaos framework.

</div>

## Conclusion

The test system has five important properties:

1. A shared `BWUnitTest` execution entrypoint exists, and many libraries and
   server components have `unit_test` directories.
2. Network tests are the most mature, especially reliable UDP, retransmission,
   fragmentation, overflow, flood, and channel switching.
3. `MultiProcTestCase` exists, but many distributed-looking scenarios do not use
   separate processes.
4. Fault injection is mainly network loss, latency, timeout, and watchdog
   behavior rather than system-level chaos testing.
5. Many interaction tests simulate several `NetworkInterface` objects inside one
   process and one `EventDispatcher`.

Source entries:

- `BWUnitTest::runTest()`:
  [unit_test.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test.hpp:14)
  and
  [unit_test.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test.cpp:40).
- `MultiProcTestCase`:
  [multi_proc_test_case.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.hpp:60)
  and
  [multi_proc_test_case.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.cpp:33).
- Artificial network loss/latency:
  [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:158).
- LoginApp artificial loss/latency hooks:
  [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:308).
- A reliable retransmission `dropNextSend()` example:
  [test_reliable.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_reliable.cpp:415).

## Test Framework Entry Points

Tests are not scattered as ad-hoc executables. `BWUnitTest::runTest()` executes
registered `CppUnitLite2` suites. Console output is quiet by default, with
`-v`/`--verbose` enabling it, and non-server builds support XML output.

Network, baseapp, and cellapp tests all invoke this common entrypoint. The
project therefore has a real execution mechanism; the limitations are coverage
and environment realism.

## Test Map

Coverage includes:

- `lib/network/unit_test`
- `lib/entitydef/unit_test`
- `lib/cstdmf/unit_test`
- `lib/resmgr/unit_test`
- `lib/server/unit_test`
- `server/baseapp/unit_test`
- `server/cellapp/unit_test`
- `server/baseappmgr/unit_test`
- `server/cellappmgr/unit_test`
- `server/dbapp/unit_test`
- `server/loginapp/unit_test`
- `server/reviver/unit_test`

Coverage depth is uneven. Network tests are the most systematic. EntityDef,
baseapp replay, and cellapp ghost-buffer tests also have substantial scenarios.
Some server directories have a test main but a much thinner suite.

<MermaidDiagram title="BigWorld test layers">
flowchart TD
  A[CppUnitLite2 / BWUnitTest] --> B[Types and serialization]
  A --> C[Network protocol and Channels]
  A --> D[Server-local modules]
  A --> E[Multi-process infrastructure]
  C --> F[Artificial loss / latency]
  C --> G[Timeout / watchdog]
  D --> H[Replay and checksum]
  D --> I[Ghost message permutations]
  E --> J[Fork / waitpid / child status]
</MermaidDiagram>

The emphasis is protocol and component correctness, not a unified black-box
cluster regression.

## Multi-process Infrastructure

`MultiProcTestCase` centralizes parent and child orchestration. It starts
workers with `fork()`, waits with `waitpid()`, records child exit status, and
uses `MultiProcessRunner` for process groups.

However, availability of the infrastructure is not proof of multi-process
coverage. Many tests create in-process peers instead.

## Fault Injection

Artificial loss and latency are implemented directly in the network sender.
Network tests use them to verify reliable UDP and recovery. `dropNextSend()`
forces a retry scenario. Request-failure tests verify that `REASON_TIMER_EXPIRED`
is reported and that a lost channel becomes `REASON_CHANNEL_LOST`.

This is useful but narrowly scoped. It is not equivalent to injecting DBApp
hangs, cross-process partitions, discovery failures, clock drift, GIL starvation,
or worker-thread blocking under one chaos model.

## Key Test Areas

### Network Protocol

Network tests cover Channel, Bundle, reliable, fragment, and request/reply
behavior. `test_reliable.cpp` tests packet loss, reorder, ACK behavior,
transmission queues, fragment recovery, and duplicate handling.

### Request Failure

[test_request_failure.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_request_failure.cpp)
separates a timed-out request from a lost channel.

### Replay Recording

BaseApp replay tests cover file signatures, read/write consistency, checksums,
and recovery. See
[test_recording.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/unit_test/test_recording.cpp:1649).

### Ghost Buffer Permutations

`TestRunner::visitPermutations()` in
[test_buffered_ghost_messages.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/unit_test/test_buffered_ghost_messages.cpp:527)
traverses arrival orders for messages from different addresses during one entity
lifetime. This targets the state-machine failures that often appear in real MMO
operation.

## Coverage Boundaries

### 1. Interaction Tests Are Mostly Single-process

Many tests run multiple `NetworkInterface` objects in one dispatcher. Flood,
BaseApp death, auto-switch, and overflow scenarios are mostly single-process.
They verify Mercury state machines well but do not reproduce OS process
scheduling, crash resource recovery, socket backlog behavior, port reuse, or
cross-machine drift and jitter.

### 2. Fault Injection Is Mainly Network-layer

Existing knobs include artificial loss, latency, dropping the next send,
watchdog timeout, and channel condemnation. Missing unified scenarios include
DBApp hangs, slow queries, cross-process partitions, manager/machined discovery
failures, clock jumps, and GIL or background-thread stalls.

### 3. There Is No Unified Cluster Black-box Regression Framework

The suite proves local modules and protocols, but not that BaseApp, CellApp,
DBApp, LoginApp, machined, and Reviver run stably as one long-lived deployment.
It also does not exercise hot updates, EntityDef changes, DB recovery, and
entity migration together.

### 4. Tests Still Depend Heavily on Real Time

The core timers mainly use real `timestamp()`. Timeout, retry, and watchdog
tests therefore mostly run through a real dispatcher rather than advancing a
controlled clock instantly. See
[testing with controllable time](/architecture/testing-time-control).

## Source Trade-offs

The framework intentionally protects the lowest-level paths most likely to
break runtime correctness. It also centralizes execution, retains server `fork()`
support, and replaces external network simulators with in-code loss and latency.

The costs are clear: single-process simulation differs from distributed
deployment, chaos is mostly network-local, real-time dependence limits timeout
coverage, and there is no equally systematic cluster regression layer.

## Verification Priorities

- `BWUnitTest` and `CppUnitLite2` must discover and run component tests
  stably.
- `MultiProcTestCase` must verify fork, waitpid, child failure, and timeout
  cleanup.
- Loss and latency injection must be active in reliable, request/reply,
  fragment, and channel-switch tests.
- Flood, death, and auto-switch cases must distinguish single-process simulation
  from true multi-process coverage.
- Replay, EntityDef, ghost buffering, and Mercury reliable tests need fixed
  input/output regression sets.
- Timeout, retry, and watchdog scenarios must identify real-time dependencies and
  available test seams.
- Login, entity creation, Cell switch, BaseApp recovery, and DB digest checks
  should form a minimal cluster regression chain.

## Scope

This page defines current tests, fault-injection reach, and coverage limits.
Time-control details are in
[testing with controllable time](/architecture/testing-time-control), and network
implementation details are in
[network backpressure and fault injection](/architecture/network-backpressure-fault-injection).

## Related Pages

- [Testing and controllable time](/architecture/testing-time-control)
- [Network backpressure and fault injection](/architecture/network-backpressure-fault-injection)
- [Mercury reliable UDP](/architecture/mercury-reliable-udp)
- [Dynamic scaling and fault tolerance](/architecture/scaling-fault-tolerance)
