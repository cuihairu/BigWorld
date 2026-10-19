# Thread Architecture and Background Tasks

> **English** | [中文](threading-background-tasks.md)

<div class="arch-hero">

The BigWorld server does not use one thread per player, nor does it split entity
logic into fully parallel jobs. The source code shows a single Reactor main
thread, a background task pool, and a small number of dedicated threads. Threads
primarily isolate blocking I/O, delayed work, and background preparation; they
do not concurrently mutate entity state.

</div>

## Key Takeaways

The BigWorld server thread model has four categories:

| Category | Representative source | Responsibility |
|---|---|---|
| Main Reactor thread | `EventDispatcher`, `ServerApp` | Network, timers, frequent tasks, handlers, and entity logic |
| General background task pool | `BgTaskManager`, `TaskManager` | Executes blocking or expensive work, optionally completing on the main thread |
| File I/O thread | `FileIOTaskManager` | Dedicated background work for resources and files |
| BaseApp delayed thread | `BaseApp::WorkerThread` | Executes `WorkerJob` objects in time order |

Key source code:

- Main-loop ordering: [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:428).
- `ServerApp::advanceTime()` advances the tick lifecycle: [server_app.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/server_app.cpp:311).
- `TaskManager::addBackgroundTask()` queues background work: [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:693).
- `TaskManager::addMainThreadTask()` queues main-thread completion: [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:709).
- `TaskManager::tick()` consumes foreground tasks on the owning thread: [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:744).
- `BackgroundTaskThread::run()` pulls and executes background tasks: [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:137).
- `WorkerThread::run()` schedules `WorkerJob` objects by time: [worker_thread.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/worker_thread.cpp:66).
- `Script::initThread()` initializes the script thread state for a background thread: [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:815).

## Main-Thread Boundary

`EventDispatcher` drives the server main thread:

<div class="flow-strip">
  <span class="flow-node">FrequentTasks</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Timers</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Stats</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Network</span>
</div>

This sequence has four consequences:

- Mercury handlers normally execute synchronously on the main thread.
- BaseApp and CellApp tick logic advances on the main thread.
- Core objects such as entities, AOI, ghosts, witnesses, and proxies are inferred
  to have main-thread ownership by default.
- A blocking handler or script callback directly delays the process tick and
  network processing.

The main thread exists not because the model is simple, but because entity state
flows across the Base, Cell, Client, and DB domains. Uncontrolled concurrent
mutation would break message ordering, lifecycle rules, and callback boundaries.

## The `BgTaskManager` Chain

`BgTaskManager` is the background task pool. A task object has two phases:

| Phase | Method | Executing thread | Purpose |
|---|---|---|---|
| Background phase | `doBackgroundTask()` | Background thread | Performs blocking I/O or expensive preparation |
| Foreground phase | `doMainThreadTask()` | Owning/main thread | Publishes results and returns control to main-thread state |

The basic call chain is:

```text
TaskManager::addBackgroundTask()
  -> bgTaskList_ enqueue
  -> bgTaskSemaphore_ wake a background thread
  -> BackgroundTaskThread::run()
  -> BackgroundTask::doBackgroundTask()
  -> optionally TaskManager::addMainThreadTask()
  -> TaskManager::tick()
  -> BackgroundTask::doMainThreadTask()
```

The source establishes these boundaries:

| Boundary | Representative source |
|---|---|
| Background tasks can be queued from any thread | `TaskManager::addBackgroundTask()` |
| Main-thread tasks can be posted from a background thread | `TaskManager::addMainThreadTask()` |
| Foreground tasks are consumed by the owning thread | `TaskManager::tick()` |
| Background-thread exit returns a `ThreadFinisher` to the main thread | `BackgroundTaskThread::run()` |
| Long-task cancellation relies on cooperative checks | `TaskManager::shouldAbortTask()` |

Background work is therefore not concurrent entity logic. It is a two-phase
model: prepare in the background, publish on the main thread.

## How `EntityApp` Consumes Background Tasks

`EntityApp::onTickProcessingComplete()` handles main-thread callbacks from
background tasks at the end of a tick: [entity_app.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/entity_app.cpp:136).

This creates the following rhythm:

```text
App tick / network handler
  -> background task performs blocking work
  -> addMainThreadTask()
  -> end of tick
  -> doMainThreadTask()
  -> update main-thread-visible state
```

A background result therefore does not immediately mutate game state. It waits
until the main thread consumes it at a safe point. When analyzing asynchronous
database, file-loading, or resource paths, identify whether the final publication
point is `doMainThreadTask()`.

## Database Background Tasks

The MySQL backend makes extensive use of background tasks:

| Source | Behavior |
|---|---|
| `lib/db_storage_mysql/mysql_database.cpp` | Posts get, put, delete, and look-up operations to `bgTaskManager_` |
| `lib/db_storage_mysql/tasks/background_task.cpp` | Returns a completed database task to the main thread for finalization |
| `lib/db_storage_mysql/buffered_entity_tasks.cpp` | Buffers and sequences tasks related to the same entity |
| `server/dbapp/write_entity_handler.cpp` | Continues writing after `DBApp::writeEntity()` |

This chain means DBApp should not execute blocking SQL on the main thread. The
main thread receives Mercury requests and starts database tasks; background
threads perform database I/O; handlers then restore main-thread semantics when
the work completes.

## `FileIOTaskManager`

CellApp initialization starts a file I/O thread:

```text
fileIOTaskManager_.initWatchers("FileIO")
fileIOTaskManager_.startThreads("FileIO", 1)
```

Source: [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:574).

The CellApp tick also services the file I/O task manager:
[cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:958).

CellApp therefore moves some resource loading and file access off the main
thread, but still finalizes it through the tick safe point. This is not a general
job graph and does not change the thread ownership of core entity logic.

## BaseApp `WorkerThread`

`server/baseapp/worker_thread.cpp` is another BaseApp-specific thread mechanism.
It is not a wrapper around `BgTaskManager`; it is a time-ordered `WorkerJob`
scheduler.

The core behavior of `WorkerThread::run()` is:

1. Wait, at most for a bounded interval, when the queue is empty.
2. Keep the queue ordered by `nextTime_`.
3. Pop and execute a `WorkerJob` when it is due.
4. Let the job return its next delay in seconds.
5. Requeue when the returned value is non-negative.
6. Stop scheduling when the returned value is negative, deleting the job when
   ownership requires it.

This is closer to a delayed background polling mechanism for specific BaseApp
work than a parallel scheduler for entity logic.

## Scripting Thread Boundary

The scripting layer runs in an embedded interpreter. A thread must explicitly
manage thread state and the interpreter lock before entering script code.

The key chain is:

| Source | Purpose |
|---|---|
| `Script::init()` | Initializes the interpreter, saves the main-thread state, and installs main-thread idle lock release/restore callbacks |
| `Script::initThread()` | Creates or binds the thread state for a new thread |
| `Script::releaseLock()` | Releases the script lock through the interpreter API |
| `Script::acquireLock()` | Restores the script thread state and re-enters the script execution context |

Source: [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:411), [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:815), and [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:935).

A background thread may enter script-related code, but script logic is not
unconstrained parallel execution. Every thread entering the scripting layer must
honor the interpreter lock and thread-state rules.

## Why Background Tasks Return to the Main Thread

Background threads can prepare work, but cannot freely publish game state. The
source boundaries explain why:

| State | Reason for main-thread publication |
|---|---|
| Entity / Base / Cell | Lifecycle, script callbacks, mailboxes, and migration depend strongly on execution order |
| Mercury Channel / Bundle | Many paths assume main-thread network handlers and send queues |
| Witness / AOI / Ghost | Client visibility and ghost synchronization must remain consistent within a tick |
| Watcher-exposed state | Read/write observation and control need predictable object lifetimes |
| Script objects | They require the interpreter lock and thread state; cross-thread destruction is risky |

BigWorld background tasks are therefore best understood as an asynchronous
I/O and preparation layer, not a parallel business-state layer.

## Source Trade-Offs

The source model makes these trade-offs:

| Benefit | Cost |
|---|---|
| Clear ordering for main-thread state | One BaseApp or CellApp can become a main-thread bottleneck |
| Entity/AOI/ghost/proxy lifecycles are easier to reason about | Background tasks cannot solve entity-script hot spots |
| Blocking database and file I/O can be isolated | Accumulated foreground tasks still slow the tick |
| Cross-thread publication concentrates in `doMainThreadTask()` | Cancellation depends on cooperative checks |
| Matches the multi-process Base/Cell sharding model | Debugging still spans background and main-thread logs |

This is not a comparison of general-purpose thread models. It is the concrete
trade-off shaped by entity-state complexity, the distributed process model, and
the scripting boundary.

## Source-Code Verification Focus

When verifying thread-related conclusions, start with:

| Question | Search entry point |
|---|---|
| Where are background tasks created? | `addBackgroundTask(` |
| Where do they return to the main thread? | `addMainThreadTask(` |
| Who consumes foreground tasks? | `TaskManager::tick()` |
| Which apps service tasks during a tick? | `onTickProcessingComplete`, `fileIOTaskManager_.tick()` |
| Do database paths use background tasks? | `bgTaskManager_.addBackgroundTask`, `BufferedEntityTasks` |
| Does file I/O use a dedicated thread? | `FileIOTaskManager` |
| Can long tasks be cancelled? | `shouldAbortTask()` |
| Can background threads enter scripts? | `Script::initThread`, `releaseLock`, `acquireLock` |

Test entry points:

| Test directory | Coverage focus |
|---|---|
| `lib/cstdmf/unit_test/test_bgtasks.cpp` | `BgTaskManager` startup/shutdown, task execution, and queue behavior |
| `lib/cstdmf/unit_test/test_background_file_writer.cpp` | Background file writing |
| `lib/terrain/unit_test/` | Resource tasks and `BgTaskManager::tick()` usage |
| `server/baseapp/unit_test/` | Local verification of BaseApp asynchronous paths |

## Chapter Boundary

This chapter explains the source-level boundaries for threads, background tasks,
and main-thread publication. Script hot reload, EntityDef migration, entity
offload, and database persistence reuse these boundaries, but their state
machines are covered by later topics.
