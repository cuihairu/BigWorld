# Testing System and Controlled Time

> **English** | [中文](testing-time-control.md)

<div class="arch-hero">

BigWorld has unit tests, network protocol tests, and multi-process test
helpers, but its time system is built primarily around real `timestamp()` calls
and `EventDispatcher` timers. For engine-architecture study, that is an
important boundary: many MMO-critical behaviors are difficult to replay with
complete determinism.

</div>

## Key Takeaways

The BigWorld test system is not empty; the network layer in particular covers
reliable UDP, fragmentation, reordering, floods, WebSocket behavior, and channel
versioning. Source evidence also shows, however, that there is no unified
virtual-clock injection layer.

Key facts:

- Unit-test entry uses `BWUnitTest::runTest()`: unit_test.hpp.
- `MultiProcTestCase` can fork multiple child processes: multi_proc_test_case.hpp.
- `EventDispatcher::addTimerCommon()` registers a timer with `timestamp() + interval`: event_dispatcher.cpp.
- `EventDispatcher::processTimers()` is driven by the real `timestamp()`: event_dispatcher.cpp.
- On Linux, `timestamp()` may use `RDTSC`, `gettimeofday`, or `clock_gettime`: timestamp.hpp.
- BaseApp starts `GameTick` through a dispatcher timer: baseapp.cpp.
- CellAppMgr starts its game-tick timer and creates `TimeKeeper`: cellappmgr.cpp.

## Test-Coverage Profile

The project contains many `unit_test` directories, including:

- `lib/network/unit_test`
- `lib/entitydef/unit_test`
- `server/baseapp/unit_test`
- `server/cellapp/unit_test`
- `server/cellappmgr/unit_test`
- `server/reviver/unit_test`
- `lib/unit_test_lib/unit_test`

The network layer is particularly valuable to study:

- `test_reliable.cpp`
- `test_receive_window.cpp`
- `test_fragment.cpp`
- `test_channel_version.cpp`
- `test_flood.cpp`
- `test_piggybacks_longer_than_normal.cpp`
- `test_baseapp_death.cpp`

These tests show that the authors treated Mercury's reliability layer and
abnormal network conditions as important concerns.

## Multi-Process Testing

`MultiProcTestCase` is useful infrastructure. It provides:

- a `ChildProcess` abstraction;
- `runChildren()` to start child processes;
- `waitForAll()` to wait for them;
- `killChildren()` for failure cleanup;
- `checkAllChildrenPass()` to aggregate results.

Source: multi_proc_test_case.hpp.

This matters for an MMO server because many behaviors cannot be expressed as a
single-process function call:

- Mercury requests and replies.
- Channel death.
- BaseApp/CellApp death.
- Reviver pings.
- machined birth/death messages.
- Inter-process registration and reconnection.

## Real-Time Dependency of Timers

**Summary**: BigWorld's timer system depends on real time, not a virtual clock.
This is the main obstacle to testing and deterministic replay.

**Source entry**: event_dispatcher.cpp:264

```cpp
// event_dispatcher.cpp:264 - add a timer
TimerHandle EventDispatcher::addTimerCommon( int64 microseconds,
    TimerHandler * handler, void * arg, bool recurrent,
    const char * name )
{
    MF_ASSERT( handler );

    if (microseconds <= 0)
        return TimerHandle();

    // 1. Convert microseconds to a timestamp interval via TimeStamp::fromSeconds
    uint64 interval = TimeStamp::fromSeconds(
            ((double)microseconds) / 1000000.0 );

    // 2. Compute the deadline from real time and insert it into the time queue
    TimerHandle handle = pTimeQueue_->add( timestamp() + interval,
            recurrent ? interval : 0,
            handler, arg, name );

    return handle;
}
```

`processTimers()` directly calls `timestamp()`:

```cpp
// event_dispatcher.cpp:340 - process timers
pTimeQueue_->process( timestamp() );
```

<MermaidDiagram title="Timer dependency on real time">
sequenceDiagram
    participant App as Application
    participant Dispatcher as EventDispatcher
    participant TimeQueue as TimeQueue64
    participant System as System time

    App->>Dispatcher: addTimer(100ms)
    Dispatcher->>System: timestamp()
    System->>Dispatcher: current time
    Dispatcher->>TimeQueue: add(now + 100ms)

    Note over System: wait 100ms...

    App->>Dispatcher: processTimers()
    Dispatcher->>System: timestamp()
    System->>Dispatcher: current time
    Dispatcher->>TimeQueue: process(now)
    TimeQueue->>TimeQueue: inspect due timers
    TimeQueue->>App: handleTimeout()
</MermaidDiagram>

Detailed interpretation:

1. **Real-time dependency**: timers are driven by `timestamp()`, not a virtual clock. Tests must let real time pass; they cannot accelerate or slow it.

2. **Testing obstacle**: timers, resends, timeouts, load balancing, and Reviver pings all use real time. Testing them requires actual delays, is slower, and cannot be replayed deterministically.

3. **Why no virtual clock**: the game server needed real-time behavior, a virtual clock would have added complexity, and no unified clock-injection layer existed at the time.

## Platform Issues in `timestamp`

`timestamp.hpp` states that RDTSC is fast but can misbehave when CPU frequency
changes. The Linux path also supports `gettimeofday` and `clock_gettime`:
timestamp.hpp.

When timing results are anomalous, BaseApp's `tickGameTime()` reports possible
multi-core TSC desynchronization:
baseapp.cpp.

This reflects the practical constraints of legacy server clocks:

- RDTSC is fast but carries cross-core and frequency-scaling risk.
- `gettimeofday` can be affected by system-clock adjustments.
- `clock_gettime(CLOCK_MONOTONIC)` is better suited to modern servers.
- Tests that depend on real time can become flaky.

## `GameTime` versus Real Time

`GameTime` is a tick count, not wall-clock time. `updateHertz` drives it.

BaseApp starts the game-tick timer with:

```cpp
mainDispatcher_.addTimer(1000000 / Config::updateHertz(), ...)
```

Source: baseapp.cpp.

CellAppMgr starts its game tick in the same way and creates a `TimeKeeper`:
cellappmgr.cpp.

CellAppMgr sends the current game time to every CellApp:
cellapps.cpp.

BigWorld therefore has two time systems:

- `timestamp()`: real time for timers, performance statistics, timeouts, and resends.
- `GameTime`: logical game ticks for entities, AOI, backups, replay, and synchronization.

## The Controlled-Time Gap

Current source evidence shows no unified `IClock` or `TimeProvider` injection
interface for core timers. `timestamp()` is a global inline function scattered
through networking, tasks, load logic, resends, Reviver, statistics, and ticks.

Consequences:

- Unit tests cannot instantly advance a five-second timeout.
- Reliable-UDP resend tests tend to rely on sleep or a real dispatcher loop.
- Reviver missed-ping tests are slow and potentially flaky.
- Load balancing and overload detection lack deterministic boundary tests.
- Python timers, entity timers, and GameTick interleaving cannot be replayed exhaustively.

This does not mean the engine cannot be tested. It means adjustable time is not
a first-class architectural abstraction.

## Current Fault Injection

Existing network tests show deliberate attention to abnormal networks:

- out-of-order receive windows;
- reliable resends;
- fragmentation;
- floods;
- channel versions;
- piggyback boundaries.

Modern server-test expectations additionally require systematic fault injection:

- packet loss, delay, jitter, and duplication;
- lost and delayed ACKs;
- handler blocking;
- stalled background tasks;
- CellApp death;
- BaseApp death restore;
- DBApp timeouts;
- incompatible EntityDef versions;
- late GameTicks and clock jumps.

## Source Trade-Offs

The absence of a unified virtual clock creates concrete engineering boundaries:

- The inline global `timestamp()` prioritizes performance; an abstraction adds call cost and invasiveness.
- Full deterministic simulation is expensive once multiple processes, networking, Python, and databases interact.
- EventDispatcher, TimeQueue, Mercury request timeouts, Reviver pings, and GameTicks all advance with real time.
- Tests must distinguish wall-clock time, GameTime, dispatcher timers, and TimeKeeper calibration.

This is not an attempt to rationalize the gap. It identifies which source paths
cannot be safely asserted through mere sleeps.

## Source-Code Verification Focus

Time-related tests should be split by source of time:

- Timer expiry order in `EventDispatcher::processOnce()` should remain stable.
- `TimeQueueT::process()` should correctly handle one-shot timers, recurrent timers, cancellation, and cancellation from a callback.
- Mercury request timeouts, reliable resends, and Reviver pings should cover normal expiry, delay, and cancellation.
- `GameTime` advancement must not be mistaken for wall-clock advancement.
- After `TimeKeeper` adjusts a tracking interval, it should return to the nominal interval.
- Handler blocking, stalled background tasks, CellApp death, and DBApp timeout should separately verify effects on dispatcher time and GameTime.
- EntityDef incompatibility, login timeout, and BaseApp restore tests should avoid brittle uncontrollable sleeps.

## Chapter Boundary

This chapter explains testing and time control. The next chapter covers dynamic
scaling and fault tolerance: how BigWorld admits new Apps, broadcasts time,
balances load, and restores processes through Reviver and machined.
