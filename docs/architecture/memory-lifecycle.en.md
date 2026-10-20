# Memory, Object Lifecycle, and Resource Management

> **English** | [中文](memory-lifecycle.md)

<div class="arch-hero">

BigWorld is a typical legacy C++ game engine: object lifetime is composed
jointly by custom reference counting, SmartPointer, the Python object model,
Mercury Channel, Packet/Bundle, entity real/ghost conversion, and background
tasks. Studying it means looking past "does it have a smart pointer" to who
owns an object, when it is destroyed, whether it crosses threads, whether it
crosses interpreters, and whether it migrates across processes.

</div>

## Key Takeaways

BigWorld's lifetime management has a clear engineering system:

- `ReferenceCount`: a non-thread-safe reference count, suited to objects owned
  by the main thread.
- `SafeReferenceCount`: an atomic reference count, suited to cross-thread
  scenarios or to things like filters and tasks.
- `SmartPointer` / `ConstSmartPointer`: intrusive reference-counting smart
  pointers.
- `PacketFilter` derives from `SafeReferenceCount`, which means filters can be
  shared across asynchronous paths.
- `Channel` derives from `ReferenceCount`, leaning toward management inside the
  main thread / network thread.
- `BackgroundTask` derives from `SafeReferenceCount`; a SmartPointer keeps it
  alive during handover between the background and main threads.
- `MemoryOStream` is a readable/writable memory stream and a core structure for
  networking, serialization, and buffered messages.
- Entity lifetime is not ordinary object lifetime: it also includes real/ghost
  conversion, Witness cleanup, Channel handover, and migration-stream writes.

Key sources:

- `ReferenceCount` is at
  [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:50).
- `SafeReferenceCount` is at
  [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:176).
- `ConstSmartPointer` is at
  [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:343).
- `Channel` derives from `ReferenceCount`, see
  [channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:35).
- `PacketFilter` derives from `SafeReferenceCount`, see
  [packet_filter.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_filter.hpp:21).
- `BackgroundTask` derives from `SafeReferenceCount`, see
  [bgtask_manager.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.hpp:25).
- `TaskManager::tick()` handles foreground tasks on the main thread, see
  [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:738).
- `MemoryOStream` is defined at
  [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:20).
- `RealEntity::destroy()` is at
  [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:246).
- `Entity::convertRealToGhost()` is at
  [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2005).

## Reference Counting Model

**Overview:** BigWorld uses intrusive reference counting: the count lives inside
the referenced object rather than in an external control block the way
`std::shared_ptr` has one. This is the typical practice of an old-school C++
game engine; the benefit is low overhead, the drawback is no `weak_ptr`
semantics.

**Source entry:**
[smartpointer.hpp:31](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:31)

```cpp
// smartpointer.hpp:31 - ReferenceCount definition
class ReferenceCount
{
public:
    ReferenceCount() : refCount_(0) {}

    // Increment the reference count
    void incRef() const { ++refCount_; }

    // Decrement the reference count
    void decRef() const
    {
        if (--refCount_ == 0)
        {
            delete this;  // Delete itself when the count reaches zero
        }
    }

    // Get the current count
    int refCount() const { return refCount_; }

protected:
    virtual ~ReferenceCount() {}

private:
    mutable int refCount_;
};
```

```cpp
// smartpointer.hpp:157 - SafeReferenceCount definition
class SafeReferenceCount
{
public:
    SafeReferenceCount() : refCount_(0) {}

    // Atomically increment the reference count
    void incRef() const
    {
        AtomicInt::add( refCount_, 1 );
    }

    // Atomically decrement the reference count
    void decRef() const
    {
        if (AtomicInt::add( refCount_, -1 ) == 1)
        {
            this->destroy();  // Call destroy() when the count reaches zero
        }
    }

    // Get the current count
    int refCount() const { return AtomicInt::get( refCount_ ); }

protected:
    virtual ~SafeReferenceCount() {}
    virtual void destroy() const { delete this; }

private:
    mutable AtomicInt refCount_;
};
```

**Flowchart:**

<MermaidDiagram title="Reference counting lifetime">
sequenceDiagram
    participant Obj as ReferenceCount object
    participant Ptr1 as SmartPointer 1
    participant Ptr2 as SmartPointer 2
    participant GC as Garbage collection

    Obj->>Obj: refCount_ = 0

    Ptr1->>Obj: incRef()
    Obj->>Obj: refCount_ = 1

    Ptr2->>Obj: incRef()
    Obj->>Obj: refCount_ = 2

    Ptr1->>Obj: decRef()
    Obj->>Obj: refCount_ = 1

    Ptr2->>Obj: decRef()
    Obj->>Obj: refCount_ = 0
    Obj->>GC: delete this
</MermaidDiagram>

**Detailed walkthrough:**

1. **Intrusive design**: the reference count is stored inside the object, so no
   extra control block is needed. This reduces memory allocation and
   indirection.

2. **Not thread-safe**: `ReferenceCount`'s `incRef()` / `decRef()` are not
   atomic operations and can only be used on a single thread.

3. **Thread-safe**: `SafeReferenceCount` uses atomic operations and can be used
   safely from multiple threads.

4. **Destruction timing**:
   - `ReferenceCount::decRef()` directly `delete this`es when the count
     reaches zero.
   - `SafeReferenceCount::decRef()` calls `destroy()` when the count reaches
     zero, so the destruction logic can be overridden.

5. **Virtual destructor**: both `~ReferenceCount()` and
   `~SafeReferenceCount()` are virtual, which guarantees the correct
   destruction order.

**Why not use std::shared_ptr:**

- Intrusive reference counting avoids the extra overhead of a control block.
- It can be embedded directly in engine objects without a separate allocation.
- It supports construction from a raw pointer (`STEAL_REFERENCE` and
  `NEW_REFERENCE`).
- The drawback is no `weak_ptr`; reference cycles must be broken by hand.

### ReferenceCount Lifecycle Call Chain

**Overview:** the complete call chain of the non-thread-safe reference count,
from object creation to destruction.

**Source entry:**
[smartpointer.hpp:610](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:610)

```cpp
// smartpointer.hpp:610 - SmartPointer construction and destruction
template<class T>
class SmartPointer
{
public:
    // Construct from a raw pointer (STEAL_REFERENCE)
    SmartPointer( T * pObject, int /*refType*/ )
        : pObject_( pObject )
    {
        // Do not increment the reference count; take over ownership
    }

    // Copy constructor
    SmartPointer( const SmartPointer & other )
        : pObject_( other.pObject_ )
    {
        if (pObject_)
        {
            pObject_->incRef();  // Increment the reference count
        }
    }

    // Destructor
    ~SmartPointer()
    {
        if (pObject_)
        {
            pObject_->decRef();  // Decrement the reference count
        }
    }

    // Assignment operator
    SmartPointer & operator=( const SmartPointer & other )
    {
        if (this != &other)
        {
            if (pObject_)
            {
                pObject_->decRef();  // Decrement the old object's count
            }
            pObject_ = other.pObject_;
            if (pObject_)
            {
                pObject_->incRef();  // Increment the new object's count
            }
        }
        return *this;
    }

private:
    T * pObject_;
};
```

**Key details:**

- `STEAL_REFERENCE`: construct from a raw pointer without incrementing the
  reference count, taking over ownership
- `NEW_REFERENCE`: construct from a raw pointer and increment the reference
  count
- The copy constructor and assignment operator manage the reference count
  correctly
- The destructor decrements the reference count, which may trigger object
  destruction

Source entry:
[smartpointer.hpp:117](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:117)

<div class="flow-strip">
  <span class="flow-node">SmartPointer construction</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">incRef() atomic add</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">count_++</span>
</div>

<div class="flow-strip">
  <span class="flow-node">SmartPointer destruction</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decRef() atomic decrement</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">count_--</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">count == 0 ?</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">delete this</span>
</div>

### SafeReferenceCount Lifecycle

The complete call chain of the thread-safe reference count:

Source entry:
[smartpointer.hpp:191](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:191)

<div class="flow-strip">
  <span class="flow-node">SmartPointer construction</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">incRef() atomic add</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BW_ATOMIC32_INC_AND_FETCH</span>
</div>

<div class="flow-strip">
  <span class="flow-node">SmartPointer destruction</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decRef() atomic decrement</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BW_ATOMIC32_DEC_AND_FETCH</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">count == 0 ?</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">destroy()</span>
</div>

### Channel Lifecycle

The complete lifecycle call chain of a Channel object:

Source entry: [channel.hpp:35](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:35)

<div class="flow-strip">
  <span class="flow-node">NetworkInterface creates Channel</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">incRef() reference count +1</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">registered into channelMap_</span>
</div>

<div class="flow-strip">
  <span class="flow-node">Channel disconnect</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">removed from channelMap_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decRef() reference count -1</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">check condemnedChannels_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">deferred destruction</span>
</div>

### BackgroundTask Lifecycle

The complete lifecycle call chain of a background task:

Source entry: [bgtask_manager.hpp:25](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.hpp:25)

<div class="flow-strip">
  <span class="flow-node">Create BackgroundTask</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">addBackgroundTask() enqueue</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">SmartPointer keeps it alive</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">background thread runs doBackgroundTask()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">addMainThreadTask() back to main thread</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">tick() runs doMainThreadTask()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">SmartPointer destruction</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decRef() -> destroy()</span>
</div>

## Lifetime Layers

<MermaidDiagram title="Object lifetime layers">
flowchart TD
  A[C++ Object] --> B{ownership model}
  B --> C[ReferenceCount main-thread object]
  B --> D[SafeReferenceCount cross-thread shared object]
  B --> E[PyObjectPlus Python visible object]
  B --> F[raw ownership explicit delete]
  C --> G[Channel / TimeQueue / functors]
  D --> H[BackgroundTask / PacketFilter / Watcher]
  E --> I[Base / Entity / script exposed objects]
  F --> J[Witness / BufferedMessage / transient helpers]
  I --> K[real ghost migration]
  H --> L[background then main thread completion]
</MermaidDiagram>

This is why BigWorld cannot simply "be replaced wholesale with `shared_ptr`":

- Some objects depend on Python reference counting.
- Some objects are governed by the Mercury Channel lifecycle.
- Some objects must be destructed back on the main thread after crossing
  threads.
- Some objects become a ghost from a real during entity migration, rather than
  being destroyed and rebuilt.

## Background Task Lifecycle

`BackgroundTask` derives from `SafeReferenceCount` and defines two phases:

- `doBackgroundTask()`: runs on the background thread.
- `doMainThreadTask()`: runs on the main thread.

Source: [bgtask_manager.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.hpp:32).

`TaskManager::tick()` swaps the foreground task list into a local, then keeps
each one alive with a `BackgroundTaskPtr` and calls `doMainThreadTask()` one at
a time. Source:
[bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:744).

Design implications:

- Objects are handed over between the background thread and the main thread
  through reference counting.
- During main-thread execution, `BackgroundTaskPtr pTask = *iter` guarantees
  the task is not released early.
- A task may do blocking work in the background, but the main-thread
  submission phase still needs careful control over time spent.

Limitations:

- `stop_` is cooperative cancellation; the task must actively respond to it.
- If `doMainThreadTask()` is too slow, it slows down the Tick.
- `SafeReferenceCount` only protects the reference count, not the task's
  internal state.

## Network Object Lifecycle

`Channel` derives from `ReferenceCount`, see
[channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:35).

This matches BigWorld's network model:

- A Channel is a Mercury logical connection.
- Most Channel state is handled by the main Reactor thread.
- Reliable UDP resend, ack, and bundle state need a deterministic order.

`PacketFilter` derives from `SafeReferenceCount`, see
[packet_filter.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_filter.hpp:25).

This is reasonable too:

- A filter may be held by a Channel.
- Encryption filters, initial connection filters, and similar filters may be
  passed along different send/receive paths.
- Reference counting needs to be safer, but the filter's actual calls should
  still respect network-thread boundaries.

## MemoryOStream and Buffered Messages

`MemoryOStream` derives from both `BinaryOStream` and `BinaryIStream`, so it can
be written to first and read from afterwards. Source:
[memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:20).

Internally it maintains:

- `pBegin_`
- `pCurr_`
- `pEnd_`
- `pRead_`
- `shouldDelete_`
- `canRewind_`

Source: [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:61).

These kinds of streams are very central in BigWorld:

- Mercury Bundle construction.
- RPC argument serialization.
- Entity property serialization.
- BaseApp rate-limit message buffering.
- DB writes and migration streams.

For example, `RateLimitMessageFilter::BufferedMessage` transfers the remaining
content of an incoming `BinaryIStream` into a `MemoryOStream` for later replay.
See
[rate_limit_message_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/rate_limit_message_filter.cpp:38).

This means message rate limiting does not just store a pointer: it copies the
stream contents, avoiding data invalidation after the original packet's
lifetime ends.

## Entity Lifecycle

Entity lifetime is the most complex part of BigWorld.

`Entity::convertRealToGhost()` does several key things:

- Asserts the entity is currently real.
- Disables callbacks.
- If there is a Witness, calls `flushToClient()` first.
- If there is a target CellApp Channel, writes the real data into the migration
  stream.

Source: [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2005).

`RealEntity::destroy()` in an offload scenario will:

- Notify all ghosts that the real is about to migrate to the next address.
- Clear the Channel resend history, transferring responsibility to the new
  channel created by the target app.

Source: [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:246).

This is not an ordinary destructor but part of a state-migration protocol:

- The object identity `EntityID` does not change.
- The real's authoritative location changes.
- Ghosts must update the next real address.
- Ownership of the Channel resend history must transfer.
- The Witness must first flush the client-visible state.

## Python Object Lifecycle

Script-exposed objects such as Base and Entity are usually coupled with the
Python object model.

This adds several constraints:

- Python references must be considered before a C++ object is destroyed.
- The underlying Entity must not be deleted arbitrarily while a Python callback
  is running.
- Migration between old-interpreter objects and new-interpreter objects during
  hot reload is more complex.
- The GIL protects the Python runtime; that does not protect C++ lifetime.

This is also why BigWorld uses means such as "disable callbacks", "finish up
back on the main thread", and "migration stream" in many places.

## Source Trade-Offs

The source contains multiple ownership models that no single rule can explain:

- `SmartPointer` / `ReferenceCount` handles intrusive reference-counted
  objects.
- `SafeReferenceCount` is used for cross-thread reference-count safety, but it
  does not mean the object's internal state is thread-safe.
- The `delete this` pattern relies on the call path to guarantee when the last
  reference is released.
- Network objects such as Packet, Bundle, and MemoryStream emphasize low
  allocation and explicit lifetime.
- PyObjectPlus, Base, and Entity are influenced by both C++ and Python
  references.

Source-level costs:

- Lifetime rules are scattered, so the learning cost is high.
- The `delete this` pattern is hard to debug.
- Thread safety is easily misunderstood.
- Mixing reference cycles and raw pointers requires manual auditing.
- Dual Python/C++ lifetime adds hot-reload and migration risk.

## Source-Code Verification Focus

Lifetime testing should cover ownership boundaries, not just memory leaks:

- Channel, Packet, Bundle, Base, Entity, Proxy, BackgroundTask, and Watcher
  should have a clear ownership map.
- Cross-thread objects should distinguish "reference-count safe" from
  "internal-state safe".
- `delete this` paths should cover last-reference release, callback re-entry,
  and exceptional return.
- `MemoryOStream` and `BinaryIStream` deserialization should cover truncation,
  length overflow, and nesting too deep.
- Hot-reload and entity-migration paths should not rely on implicit destructor
  side effects to complete business-state transitions.
- Paths that destroy the underlying Entity while a Python callback is running
  should be stopped by a state machine or a callbacks guard.

## Chapter Boundary

This chapter explains object lifetime. The next chapter analyses the build,
platform, and dependencies: BigWorld's technical debt is not only in the
source, but also in CMake versions, Makefiles, embedded Python sources,
OpenSSL, third-party libraries, and the platform baseline.
