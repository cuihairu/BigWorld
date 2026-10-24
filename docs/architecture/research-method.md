# Research Method and Evidence Grading


<div class="arch-hero">

This chapter defines the research method. The BigWorld repository lacks a
complete historical commit record, a complete game project, and a complete asset
tree. Its documentation must therefore distinguish “facts established by the
source,” “high-confidence inference based on platform and era,” and “questions
that cannot be confirmed.” Making those distinctions matters more than jumping
straight to conclusions.

</div>

## Research Goals

These documents do not merely translate directory names into Chinese, nor do
they produce an isolated dependency-upgrade checklist. They answer architectural
questions:

- How does a mature MMO engine split Base, Cell, DB, Login, Manager, and Reviver?
- Why does it use a single Reactor main thread plus background work rather than
  fully multithreaded game logic?
- Why is Mercury a reliable UDP protocol with logical channels rather than pure TCP?
- Why does Linux use level-triggered `epoll` rather than edge-triggered `epoll`
  or modern `io_uring`?
- How does the EntityDef type system simultaneously serve network protocols,
  persistence, scripts, and hot reload?
- What are the real boundaries of hot reload, dynamic scaling, and fault recovery?
- Which source designs remain core constraints, and which are merely peripheral
  engineering problems?

## Evidence Grades

<div class="decision-grid">
  <div class="decision-card">
    <h3>Grade 1: Source facts</h3>
    <p>Implementations that can be located directly in repository source, such as event-loop ordering, EPoller registration, the reloadScript call chain, or single-socket recvfrom/sendto.</p>
  </div>
  <div class="decision-card">
    <h3>Grade 2: High-confidence inference</h3>
    <p>Judgments combining source, platform baseline, and technical era—for example, that io_uring was not an original design option, or that CentOS 5/6/7 constraints shaped network and build choices.</p>
  </div>
  <div class="decision-card">
    <h3>Grade 3: Open questions</h3>
    <p>Things the repository cannot prove, such as internal commercial tuning parameters, actual production-cluster scale, or the original team's decision records.</p>
  </div>
</div>

## Current Repository Boundaries

Established facts:

- The server core is in `programming/bigworld/server`.
- The common network stack is in `programming/bigworld/lib/network`, in the Mercury namespace.
- Script embedding and the Python runtime are in `programming/bigworld/lib/pyscript` and `programming/bigworld/lib/script`.
- Entity-definition and serialization cores are in `programming/bigworld/lib/entitydef`.
- The original PDFs remain under `docs/pdf`.

Important boundaries:

- The repository lacks the complete `game/` and asset trees, so FantasyDemo cannot be verified end to end.
- Git history cannot reconstruct the original commercial development team's design discussions.
- Claims about “why a technology was not chosen” can only be engineering inferences from source and era; they must not be presented as the authors' own words.

## Writing Template

Each core topic uses the same structure:

1. Problem context and design goals
2. Key processes, threads, and objects
3. Complete call chains and source entry points
4. Core data structures and state machines
5. Correctness boundaries and failure modes
6. Performance bottlenecks and observable signals
7. Source trade-offs
8. Source verification focus
9. Chapter boundary

The purpose is simple: prevent the documentation from becoming an encyclopedic
directory of “what BaseApp is” and “what CellApp is.”

## Evidence-Grading Examples

### Grade 1 evidence: source facts

**Definition**: implementation that can be located directly in repository source.

**Examples**:

| Conclusion | Source evidence | Confidence |
|------|----------|--------|
| BigWorld uses level-triggered epoll | `event_poller.cpp:1079`, comment “TODO: Could be good to use EPOLLET” | 100% |
| GameTick defaults to 10 Hz | `baseapp.cpp:2020`, `1000000/Config::updateHertz()` | 100% |
| Mercury supports four reliability types | `bundle.hpp:27`, `RELIABLE_NO/DRIVER/PASSENGER/CRITICAL` | 100% |
| Entities have real and ghost states | `entity.hpp:89`, comment “each entity only has one authoritative real” | 100% |

**Verification method**: read the source directly and locate the corresponding lines.

### Grade 2 evidence: high-confidence inference

**Definition**: judgment based on source code, platform baseline, and technical era.

**Examples**:

| Conclusion | Inference basis | Confidence |
|------|----------|--------|
| io_uring was not among the original design options | CentOS 7 kernels do not support it and the code has no reference | 95% |
| Script execution is bounded by the global interpreter lock | The code manages thread state with `PyEval_InitThreads()`, `PyEval_SaveThread()`, and `PyEval_RestoreThread()` | 95% |
| The commercial release may contain more optimization | The open-source tree lacks `control_cluster.py` and its documentation has drifted | 80% |
| The design era is approximately 2005–2010 | CentOS 5/6 is used and Python 2.4 compatibility was probed | 90% |

**Verification method**: check platform constraints, technical era, and code style.

### Grade 3 evidence: open questions

**Definition**: content the repository cannot prove.

**Examples**:

| Question | Reason | Confirmable? |
|------|------|----------|
| Internal commercial tuning parameters | Not in the open-source repository | ❌ |
| Real production-cluster scale | No public data | ❌ |
| The original team's decision records | Git history is incomplete | ❌ |
| Performance benchmark results | No public benchmark | ❌ |

**Handling**: explicitly mark such content “unconfirmed”; never present it as the
authors' words.

## Common Misreading Cases

### Misreading 1: treating a “choice” as “not knowing”

**Incorrect judgment**: “BigWorld uses level-triggered epoll because it did not know about edge triggering.”

**Correct analysis**: the source comment explicitly says “TODO: Could be good to use EPOLLET (leave like select for now).” The authors knew about edge triggering but chose the conservative option.

**Lesson**: inspect source comments before drawing conclusions.

### Misreading 2: treating a “constraint” as a “defect”

**Incorrect judgment**: “Python 2.7 is technical debt and should have been upgraded immediately.”

**Correct analysis**: Python 2.7 was a reasonable choice in the design era. The GIL limited parallelism, but a single-threaded model was itself a common game-server pattern.

**Lesson**: distinguish the constraints of an era from defects in a design.

> Note: this repository completed its Python 2.7 → 3.13 migration in 2026-09; see [Migration and coverage status](/upgrade-plan/migration-status). That later engineering decision does not invalidate the methodology in this example—the judgment that 2.7 was era-appropriate at the time remains valid.

### Misreading 3: treating an “inference” as a “fact”

**Incorrect judgment**: “BigWorld supports one million concurrent connections.”

**Correct analysis**: the source uses one UDP socket plus logical channels, not a “one socket per player” model. Concurrency depends on protocol processing, tick budget, and Cell load; it cannot be reduced to a “million file descriptors” model.

**Lesson**: distinguish source facts from engineering inference.

### Misreading 4: treating the “periphery” as the “core”

**Incorrect judgment**: “The old build system means the engine is poor quality.”

**Correct analysis**: the build system (CMake 2.8, CentOS 7) is a peripheral engineering issue. It does not determine the value of the core architecture (Mercury, EntityDef, Cell/BSP).

**Lesson**: distinguish core constraints from peripheral dependencies.

## Verification Methodology

### Method 1: source tracing

Every conclusion must identify a source entry point:

```text
conclusion -> source file -> line -> function -> call chain
```

**Example**:
- Conclusion: BigWorld uses a single Reactor main thread
- Source: `event_dispatcher.cpp:428`, `processOnce()`
- Call chain: FrequentTasks -> Timers -> Stats -> Network
- Verification: check whether a thread pool or parallel task scheduler exists

### Method 2: cross-validation

Validate important conclusions from several angles:

| Angle | Verification |
|------|----------|
| Code | Source implementation |
| Configuration | Build scripts and platform constraints |
| Documentation | PDFs, comments, and README |
| Behavior | Runtime behavior |

**Example**: verify “Linux uses epoll”
- Code: the `EPoller` class in `event_poller.cpp`
- Configuration: `#ifdef __linux__` conditional compilation
- Documentation: the PDF mentions Linux network optimization
- Behavior: `strace` shows `epoll_wait` system calls

### Method 3: boundary testing

For performance and capacity claims, state the boundary explicitly:

```text
conclusion + condition + limitation + exception
```

**Example**:
- Conclusion: BigWorld supports dynamic scaling
- Condition: BaseApp and Alpha DBApp must already be known
- Limitation: only CellApp/BaseApp scaling is supported, not Manager
- Exception: a CellApp is not accepted while recovery is in progress

### Method 4: counterexample checking

Actively seek counterexamples to every conclusion:

```text
conclusion -> counterexample -> does it hold -> corrected conclusion
```

**Example**:
- Conclusion: the single-threaded model has no locks
- Counterexample: `SafeReferenceCount` uses atomic operations
- Correction: the main thread itself is lock-free, but cross-thread paths use atomics

## Template Chapters

The first four framing chapters are:

<div class="flow-strip">
  <span class="flow-node">Research method</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Process topology</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Event loop</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Network model</span>
</div>

Later chapters retain the same granularity and cover Mercury reliable UDP,
serialization, threads/GIL, hot reload, testing, scaling, fault tolerance, AOI,
persistence, security, and observability.

## Principles

- KISS: find the smallest source evidence for each conclusion; do not replace implementation analysis with grand narrative.
- YAGNI: analyze only mechanisms present in the source; do not invent chapters for imaginary architecture.
- DRY: use one judgment framework for each class of trade-off instead of repeating generic comparisons.
- SOLID: split topics by responsibility—network, threads, entities, persistence, and operations are independent but connected through call chains.
