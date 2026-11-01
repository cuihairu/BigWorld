# Main Loop, Ticks and Event Dispatch

> **English** | [中文](event-loop.md)

<div class="arch-hero">

BigWorld's main server processes are not "one thread per player": they are driven
by a main Reactor thread that handles networking, timers, statistics and
frequent tasks. Understanding the main loop order is a prerequisite for studying
the threading model, network fairness, tick jitter and hot-path bottlenecks.

</div>

## Core Conclusion

The order of `EventDispatcher::processOnce()` is:

<div class="flow-strip">
  <span class="flow-node">FrequentTasks</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Timers</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Stats</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Network</span>
</div>

The source entry is at
event_dispatcher.cpp.

### The Core EventDispatcher Data Structure

```cpp
// event_dispatcher.hpp - EventDispatcher class definition
class EventDispatcher
{
public:
    EventDispatcher();
    ~EventDispatcher();

    // Main loop entry points
    int processOnce( bool shouldIdle = true );
    int processContinuously();
    int processUntilBreak();

    // Timer management
    int addTimer( int microseconds, TimerHandler * pHandler, 
                  void * pUser, const char * name );
    int addOnceOffTimer( int microseconds, TimerHandler * pHandler,
                         void * pUser, const char * name );

    // FrequentTask management
    void addFrequentTask( FrequentTask * pTask );
    void delFrequentTask( FrequentTask * pTask );

private:
    // Core components
    FrequentTasks * pFrequentTasks_;
    TimeQueue64 * pTimeQueue_;
    EventPoller * pPoller_;
    
    // Wait policy
    double maxWait_;  // default 0.1 seconds (100ms)
    bool breakProcessing_;
    
    // Statistics
    uint64 lastProcessTime_;
    int numTimerCalls_;
    int numFrequentTaskCalls_;
};
```

**Design points:**
- `pFrequentTasks_` is a set of synchronous callbacks executed every round of
  the main loop
- `pTimeQueue_` is a timestamp-based timer queue supporting one-off and
  repeating timers
- `pPoller_` is the epoll/kqueue wrapper, responsible only for FD readiness
  events
- `maxWait_` defaults to 100ms and caps how long the network poll waits
- `breakProcessing_` is the flag used to leave the loop early (for example on
  SIGINT)

### processOnce() Implementation Details

```cpp
// event_dispatcher.cpp:428
int EventDispatcher::processOnce( bool shouldIdle )
{
    // 1. Run the FrequentTasks
    if (pFrequentTasks_)
    {
        pFrequentTasks_->process();
    }
    
    if (breakProcessing_) return 0;
    
    // 2. Process the Timers
    if (pTimeQueue_)
    {
        pTimeQueue_->process( timestamp() );
    }
    
    if (breakProcessing_) return 0;
    
    // 3. Update statistics
    this->processStats();
    
    if (breakProcessing_) return 0;
    
    // 4. Process network events
    if (pPoller_)
    {
        double maxWait = shouldIdle ? this->calculateWait() : 0;
        pPoller_->processPendingEvents( maxWait );
    }
    
    return 0;
}
```

**Key details:**
- FrequentTasks run before Timers, so frequent tasks have higher priority
- Timers are driven by `timestamp()`, not wall-clock
- Network processing comes last and is affected by how long the earlier stages
  took
- `calculateWait()` takes the smaller of `maxWait_` and the nearest timer expiry

This shows network events are not scheduled in isolation: they sit in the second
half of a main loop round. Time spent in any Timer, FrequentTask or main-thread
message handling directly affects how promptly networking runs.

From the server process's perspective the default entry is simpler:
`ServerApp::run()` calls `mainDispatcher_.processUntilBreak()` and only runs
`onRunComplete()` after it returns, see
server_app.cpp.
So the "main thread" of BaseApp, CellApp, LoginApp and Manager is essentially one
`EventDispatcher` cycling around the same loop.

## The Call Chain

<MermaidDiagram title="EventDispatcher main loop">
flowchart TD
  A[processContinuously] --> B[processOnce shouldIdle=true]
  B --> C[processFrequentTasks]
  C --> D{breakProcessing?}
  D -- no --> E[processTimers]
  D -- yes --> H[return 0]
  E --> F[processStats]
  F --> G{breakProcessing?}
  G -- no --> I[processNetwork]
  G -- yes --> H
  I --> J[calculateWait]
  J --> K[pPoller processPendingEvents]
  K --> L[Input/Write Handler]
</MermaidDiagram>

Source anchors:

- `processContinuously()` loops calling `processOnce(true)`, see
  event_dispatcher.cpp.
- The execution order of `processOnce()` is at
  event_dispatcher.cpp.
- `processNetwork()` computes the maximum wait through `calculateWait()`, see
  event_dispatcher.cpp.
- `calculateWait()` takes the smaller of the default `maxWait_` and the nearest
  timer expiry, see
  event_dispatcher.cpp.
- `processUntilBreak()` reports pending exceptions together after the loop ends,
  see
  event_dispatcher.cpp.

## The Wait Policy

The `maxWait` of `processNetwork()` is decided by `shouldIdle`:

- If `shouldIdle=false`, the network poll does not block.
- If `shouldIdle=true`, it waits at most until the nearest timer expiry or the
  default maximum wait.

This is a typical Reactor design: the main thread may sleep when there are no
network events, but it never misses a timer.

The costs:

- Too many dense timers reduce the network wait and raise the loop frequency.
- A handler that runs too long blocks every subsequent event.
- If one network receive drains too many packets, Timers and other tasks can be
  delayed.

The `EventDispatcher` constructor defaults `maxWait_ = 0.1`, i.e. one network
wait is at most 100ms when there is no earlier timer, see
event_dispatcher.cpp.
This is not a fixed tick sleep but a combination of "network poll wait cap +
nearest timer expiry".

## TimerQueue and GameTick

**Overview:** BigWorld's game loop is not a fixed sleep but is driven by timers.
Each App has its own GameTick timer, and executes game logic when it expires.
Timers are driven by `timestamp()`, not wall-clock.

**Source entry:** event_dispatcher.cpp:264

```cpp
// event_dispatcher.cpp:264 - adding a timer
int EventDispatcher::addTimerCommon( int microseconds, 
    TimerHandler * pHandler, void * pUser, const char * name,
    bool isRepeating )
{
    // 1. Convert microseconds into a timestamp interval
    uint64 interval = uint64(microseconds) * 1000;
    
    // 2. Compute the expiry time
    uint64 startTime = timestamp() + interval;
    
    // 3. Insert into the TimeQueue64
    TimeQueue64::iterator iter = pTimeQueue_->add( startTime, 
        pHandler, pUser, interval );
    
    // 4. Return the timer id
    return iter->id();
}
```

**Flowchart:**

<MermaidDiagram title="Timer addition and firing flow">
sequenceDiagram
    participant App as BaseApp/CellApp
    participant Dispatcher as EventDispatcher
    participant TimeQueue as TimeQueue64
    participant Timer as Timer Node
    participant Handler as TimerHandler

    App->>Dispatcher: addTimer(100ms)
    Dispatcher->>TimeQueue: add(timestamp + 100ms)
    TimeQueue->>Timer: create the timer node
    Timer->>Timer: set interval = 100ms
    
    Note over Timer: waiting 100ms...
    
    Timer->>TimeQueue: process(timestamp())
    TimeQueue->>Timer: check whether it has expired
    Timer->>Handler: handleTimeout()
    Handler->>App: tickGameTime()
    Timer->>TimeQueue: re-enqueue (repeating timer)
</MermaidDiagram>

**Detailed walkthrough:**

1. **Time units**: `microseconds` is the input and `timestamp()` returns
   nanoseconds. Hence `interval = microseconds * 1000`.

2. **TimeQueue64 structure**: a timestamp-based priority queue with the
   earliest-expiring timer at the head. `process()` checks whether the head has
   expired.

3. **Repeating timers**: if `interval > 0`, the timer is re-enqueued after
   expiring, which yields periodic firing. The GameTick is a repeating timer.

4. **Timer firing**: `triggerTimer()` calls `TimerHandler::handleTimeout()`
   directly, executing synchronously on the main thread.

### GameTick Startup

**Source entry:** baseapp.cpp:2016

```cpp
// baseapp.cpp:2016 - BaseApp starts the GameTick
void BaseApp::onManagerRebirth( ... )
{
    // ... other initialisation ...
    
    // Start the GameTick timer, 10Hz (100ms)
    gameTimerHandle_ = mainDispatcher_.addTimer(
        1000000 / Config::updateHertz(),  // 1000000 / 10 = 100000us = 100ms
        this,                              // TimerHandler
        NULL,                              // pUser
        "GameTick"                         // name
    );
}
```

**GameTick execution flow:**

<MermaidDiagram title="GameTick execution flow">
sequenceDiagram
    participant Timer as GameTick Timer
    participant BaseApp as BaseApp
    participant ServerApp as ServerApp
    participant EntityApp as EntityApp
    participant Script as Python Scripts

    Timer->>BaseApp: handleTimeout()
    BaseApp->>BaseApp: tickGameTime()
    BaseApp->>ServerApp: advanceTime()
    ServerApp->>ServerApp: onTickPeriod()
    ServerApp->>ServerApp: onEndOfTick()
    ServerApp->>ServerApp: time_++
    ServerApp->>ServerApp: profiler.tick()
    ServerApp->>ServerApp: onStartOfTick()
    ServerApp->>ServerApp: callUpdatables()
    ServerApp->>EntityApp: onTickProcessingComplete()
    EntityApp->>Script: run the script timers
</MermaidDiagram>

**Detailed walkthrough:**

1. **Tick rate**: `Config::updateHertz()` defaults to 10, i.e. 10Hz and one tick
   every 100ms. This is a typical MMO server configuration.

2. **Tick hook order**: `advanceTime()` defines the hook order shared by all
   Apps, guaranteeing a consistent execution order in every tick.

3. **Script timers**: `EntityApp::onTickProcessingComplete()` runs the Python
   script timers; those timers bind to GameTime, not wall-clock.

4. **Load reporting**: at the end of each tick the App reports load information
   to the Mgr, for load balancing decisions.

## Tick Hook Order

`ServerApp::advanceTime()` defines the tick hook order shared by all EntityApp
derived processes:

1. Compute the elapsed tick period and call `onTickPeriod()`.
2. Call `onEndOfTick()`.
3. Increment `time_`.
4. profiler tick.
5. Call `onStartOfTick()`.
6. Call `callUpdatables()`.
7. Call `onTickProcessingComplete()`.

Source: server_app.cpp.
The semantics of these hooks are documented at
server_app.hpp.

`EntityApp::onTickProcessingComplete()` runs the script timer queue after the
ServerApp hooks, see
entity_app.cpp.
This shows script timers are not `EventDispatcher` wall-clock timers but are
bound to `EntityAppTimeQueue` and `GameTime`; `EntityAppTimeQueue::time()`
returns the entity app's current `time_`, see
entity_app.cpp.

`BaseApp::tickGameTime()` runs a batch of BaseApp-specific work around
`advanceTime()`:

- Compute whether the tick is late and the spare time.
- Update the profiler and load.
- Tick pending logins.
- Backup/archive.
- Sync the TimeKeeper.
- Report load, base count and proxy count to BaseAppMgr.
- Tick worker jobs, rate limit filters, send window overflow, idle proxy
  channels, stats and dead CellApp handling.

Source: baseapp.cpp.

The tick work of `CellApp::handleGameTickTimeSlice()` leans more towards spatial
simulation:

- Enter `tickShutdown()` while shutdown is paused.
- Update load and notify CellAppMgr.
- Update the boundaries.
- `advanceTime()`.
- Tick backup.
- Check send window overflow.
- Run offload checks, delete old Spaces, sync time and tick stats.
- Play buffered entity/input messages.
- Tick recordings.

Source: cellapp.cpp.

Key conclusion: the `EventDispatcher` Timer is only the trigger point; the real
game tick is composed of `ServerApp::advanceTime()` plus each App's own tick
body.

## TimeKeeper Calibration

BigWorld's GameTick timer is not a perfectly fixed local beat. `TimeKeeper` keeps
the game time, the target tick rate, the tracked timer handle and the master
address, see
time_keeper.hpp.

When a time reading arrives from the master, `TimeKeeper::inputMasterReading()`
estimates the round trip and computes the offset between the local reading and
the master reading. If the local side is slow it shortens the tracking timer's
interval; if the local side is fast it lengthens it; the adjustment is 1/20 of
the nominal interval, see
time_keeper.cpp.

The sync request goes out through `synchroniseWithMaster()` as a Mercury request
carrying the current `readingNow()`, see
time_keeper.cpp.
`scheduleSyncCheck()` registers a one-off timer near the next tracking timer
delivery, used to check or restore the interval, see
time_keeper.cpp.

Engineering implications:

- Tick sync is achieved by adjusting the timer interval, not by simply assigning
  `time_` the master's value.
- A multi-process MMO needs comparable GameTime across BaseApp/CellApp/Manager,
  otherwise AOI, migration, backup and client sync all get temporal skew.
- `TimeKeeper` itself runs on the main dispatcher, so the sync callbacks are
  still subject to main-thread blocking.

## Network Handling Is Not an Async Completion Model

`EventPoller` is only responsible for dispatching FD readiness to handlers. The
handlers still execute synchronously on the main thread. On Linux,
`EPoller::processPendingEvents()` calls `epoll_wait()` and then processes events
one by one. Source:
event_poller.cpp.

The source boundary is very clear:

- `EventPoller` only dispatches readable/writable events to handlers.
- Handler execution, Mercury packet decoding and Channel state updates still
  occupy the main dispatcher.
- The current code has no submission queue, completion queue or async completion
  callback semantics.

So `EventPoller` cannot be understood as a background I/O completion layer; it is
only the Reactor's FD dispatch layer.

## Tick Fairness

On the receive path, `PacketReceiver::handleInputNotification()` keeps calling
`processSocket()` during a single readability event until it cannot continue or
`maxSocketProcessingTime` is exceeded. Source:
packet_receiver.cpp.

This design shows the developers were already aware that "draining a single
socket" can crush a frame:

- Continuing to drain the socket raises throughput and reduces UDP receive queue
  buildup.
- The processing time cap protects main loop fairness.
- Stopping after the cap leaves kernel queue pressure, which may increase latency
  or packet loss.

This is a very game-server-style compromise: throughput is not the only metric;
tick budget and tail latency matter just as much.

## Child Dispatchers

`NetworkInterface` does not just use one external dispatcher. `EventDispatcher::attach()`
attaches a child dispatcher to the parent dispatcher and registers the child
poller's fd with the parent, see
event_dispatcher.cpp.

`DispatcherCoupling` is itself a `FrequentTask`. It registers with the main
dispatcher at construction, and `doTask()` calls `childDispatcher_.processOnce()`
every round, see
dispatcher_coupling.hpp.

`NetworkInterface::processUntilChannelsEmpty()` alternates between the main
dispatcher and the interface's own dispatcher during the flush phase. Source:
network_interface.cpp.

This shows Mercury does not have a single global dispatcher; there is a
combination of a main dispatcher and a network interface dispatcher. Later
sections keep referring to this chain when analysing Channel closure, reliable
packet flushing and process exit.

## FrequentTask Boundaries

`FrequentTasks::process()` walks the registered `FrequentTask`s every round and
calls `doTask()`, but it is not a task queue scheduler:

- If it is already processing recursively and the set becomes dirty, it returns
  immediately.
- If `FrequentTasks` itself is destroyed during a task, it exits at once via
  `pGotDestroyed_`.
- If a task's add/remove makes the container dirty, the current round breaks to
  avoid iterator invalidation.

Source: frequent_tasks.cpp.

The script layer can register frequent tasks too. `ScriptFrequentTasks::addFrequentTask()`
wraps a Python callable as a `ScriptFrequentTask` and registers it with
`Updatables`; its `update()` calls the Python callback and removes itself when
that returns true, see
script_frequent_tasks.cpp.

This again shows: frequent tasks in the main loop are a set of synchronous
callbacks, not a background thread pool. An expensive callback directly consumes
the main thread budget.

## Trade-offs in the Source

What the source chose is not a general task scheduler but a main dispatcher that
strings together networking, timers, statistics and frequent tasks:

- The logical execution order is fixed by `processOnce()`, which makes it easy
  to reason about the relative order of entity, script and network messages.
- Entity state advances on the main thread by default, which keeps the Base/Cell
  hot paths from being fragmented by lock contention.
- Background threads mainly take over database, file and long-running tasks, then
  hand the results back on the main thread.
- CellApp/BaseApp extension relies more on multi-process sharding, Cell splitting
  and entity migration than on executing entity logic in parallel within one
  process.

The source also exposes clear constraints:

- No handler, timer callback or frequent task may block for long.
- A single hot process is throttled by the main dispatcher's caps.
- `PacketReceiver::maxSocketProcessingTime` only limits the per-socket
  processing time; it cannot remove all main-thread jitter.
- A script frequent task is wrapped from a Python callable, but it still executes
  as a main-thread callback.

## What Source-Verified Tests Should Cover

Verification around the main loop should cover the scheduling order and blocking
boundaries:

- The order of FrequentTasks, Timers, Stats and Network in `processOnce()` must
  not be disturbed.
- When `breakProcessing()` fires inside a FrequentTask, Timers and Network are
  skipped but Stats still runs.
- When `breakProcessing()` fires inside a Timer, Network is skipped but Stats
  still runs.
- A repeating timer should re-enqueue by interval after its callback, and a
  one-off timer should cancel itself.
- The GameTick timer's interval should equal `1000000 / updateHertz` and be able
  to recover after `TimeKeeper` adjusts it.
- With a non-zero `PacketReceiver::maxSocketProcessingTime`, one socket
  readability event must not drain indefinitely.
- Under `DispatcherCoupling` the child dispatcher's `processOnce()` must not
  block the main dispatcher.
- When a script frequent task or updatable throws, it must not break the main
  loop's exception reporting path.

## Follow-up Questions

This chapter only explains "how events are scheduled". The next chapter moves
into the network I/O model itself: how `EventPoller`, `PacketReceiver`, Bundle
send/receive and the Channel reliability layer together form Mercury's network
path.