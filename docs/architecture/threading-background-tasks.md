# 线程架构与后台任务

<div class="arch-hero">

BigWorld 不是“每个玩家一个线程”，也不是现代 job system 驱动的全并行 ECS。它的核心运行时更接近单 Reactor 主线程 + 后台任务池 + 少量专用工作线程。线程的主要职责是隔离阻塞 I/O 和延迟任务，而不是让实体逻辑自由并发。

</div>

## 先给结论

BigWorld 服务端线程架构可以分成四类：

- 主 Reactor 线程：网络、Timer、FrequentTask、消息 handler、实体逻辑、Python 调用的主要执行位置。
- `BgTaskManager`：通用后台任务池，后台执行后可把收尾任务投回主线程。
- `FileIOTaskManager`：文件 I/O 专用后台任务管理器。
- `BaseApp::WorkerThread`：基于时间排序的延迟任务队列，不是通用 CPU job system。

关键源码：

- 主循环顺序在 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:428)。
- `BackgroundTaskThread::run()` 在线程中拉取并执行后台任务，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:137)。
- `TaskManager::addBackgroundTask()` 和 `addMainThreadTask()` 分别投递后台任务与主线程任务，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:693)。
- `TaskManager::tick()` 在主线程处理 foreground tasks，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:744)。
- `WorkerThread::run()` 按时间排序执行 `WorkerJob`，见 [worker_thread.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/worker_thread.cpp:66)。
- `Script::initThread()` 为新线程初始化 Python thread state，并以持有 GIL 的状态返回，见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:661)。

## 主线程边界

主线程承担大多数状态权威逻辑。上一章已经分析过 `EventDispatcher::processOnce()` 的顺序：

<div class="flow-strip">
  <span class="flow-node">FrequentTasks</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Timers</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Stats</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Network</span>
</div>

这意味着：

- Mercury handler 通常在主线程同步执行。
- Python 脚本回调主要在主线程执行。
- 实体状态修改默认不需要跨线程锁。
- handler、Timer、脚本不能阻塞，否则拖慢整个进程 Tick。

这种设计牺牲了单进程内多核扩展性，换取确定性、可调试性和低锁复杂度。

## BgTaskManager 模型

`BgTaskManager` 是典型的后台任务池。它的任务对象分两段：

- `doBackgroundTask()`：后台线程执行。
- `doMainThreadTask()`：可选，回到主线程执行。

<MermaidDiagram title="BgTaskManager 执行模型">
sequenceDiagram
  participant Main as Main Thread
  participant Manager as TaskManager
  participant Worker as BackgroundTaskThread
  participant Task as BackgroundTask

  Main->>Manager: addBackgroundTask(task)
  Worker->>Manager: pullBackgroundTask()
  Worker->>Task: doBackgroundTask()
  Task->>Manager: addMainThreadTask(this)
  Main->>Manager: tick()
  Manager->>Task: doMainThreadTask()
</MermaidDiagram>

源码中 `CStyleBackgroundTask::doBackgroundTask()` 执行后台函数后，如果存在 foreground 函数，就把自己加入主线程任务队列，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:62)。

`BackgroundTaskThread::run()` 会循环：

1. 等待并拉取后台任务。
2. 设置线程本地的停止标志和阻塞回调。
3. 执行 `pTask->doBackgroundTask()`。
4. 记录任务耗时。
5. 收到空任务时退出，并投递 `ThreadFinisher` 到主线程。

源码见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:141)。

## 为什么后台任务要回主线程收尾

后台线程可以做阻塞或耗时工作，但最终修改实体、Python 对象或主线程状态时，往往必须回主线程。

这不是保守，而是必要：

- Entity 状态默认由主线程拥有。
- Mercury Channel 和 Bundle 生命周期很多路径在主线程假设下工作。
- Python 2.7 有 GIL，即使多线程也不能让 Python 字节码并行执行。
- C++ 对象生命周期、SmartPointer、Watcher 和脚本对象混合后，跨线程析构风险高。

所以 BigWorld 的后台任务不是“并发业务逻辑”，而是“异步准备 + 主线程提交”。

## FileIOTaskManager

CellApp 初始化时启动文件 I/O 线程：

- `fileIOTaskManager_.initWatchers("FileIO")`
- `fileIOTaskManager_.startThreads("FileIO", 1)`

源码见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:574)。

这说明 BigWorld 把部分资源加载、文件访问等阻塞 I/O 从主线程挪走，但线程数保守。它不是把所有计算拆成 job graph。

## BaseApp WorkerThread

`server/baseapp/worker_thread.cpp` 是另一套机制。它不是 `BgTaskManager` 的简单封装，而是一个时间排序的 `WorkerJob` 调度器。

`WorkerThread::run()` 的核心逻辑：

- 如果队列空，最多睡 100ms。
- 队列按 `nextTime_` 排序。
- 到期后取出 job 执行。
- job 返回下次延迟秒数。
- 返回非负数则重新加入队列。
- 返回负数则不再调度，低于 `-1` 时删除自身。

源码见 [worker_thread.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/worker_thread.cpp:66)。

这更像“延迟后台轮询器”，不是现代 work-stealing job system。

## Python GIL 与线程

`Script::init()` 初始化 Python 后调用：

- `PyEval_InitThreads()`
- 保存主线程 `PyThreadState`
- 设置 `BWConcurrency::setMainThreadIdleFunctions(&Script::releaseLock, &Script::acquireLock)`

源码见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:461)。

`Script::initThread()` 做的事情：

- `PyEval_AcquireLock()` 获取全局锁。
- 根据参数创建新解释器或复用主解释器创建 thread state。
- `PyEval_ReleaseLock()`。
- 设置当前线程默认 Python context。
- 调用 `Script::acquireLock()`，返回时持有 GIL。

源码见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:661)。

`Script::releaseLock()` 使用 `PyEval_SaveThread()` 释放 GIL，`Script::acquireLock()` 使用 `PyEval_RestoreThread()` 恢复，见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:748)。

这说明后台线程可以进入 Python，但必须遵守 GIL 和 thread state 管理。它不是无约束并行脚本执行。

## 线程安全边界

`TaskManager::addBackgroundTask()` 注释说明任意线程可以访问，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:691)。

但 `TaskManager::tick()` 注释说明只能由 owning thread 调用，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:739)。

这个边界很重要：

- 投递任务可以跨线程。
- 处理主线程任务不能跨线程。
- 后台任务可以请求停止，`TaskManager::shouldAbortTask()` 提供快速检查，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:875)。
- 真正的实体状态提交应该在主线程完成。

## 当时取舍

这种线程架构在 BigWorld 的设计年代是合理的：

- Python 2.7 GIL 限制脚本并行。
- MMO 实体状态复杂，多线程共享实体会引入大量锁和竞态。
- 多进程 Cell/Base 分片比单进程内多线程更符合水平扩展。
- 物理机时代进程级隔离比大型线程池更容易运维和恢复。
- 背景任务池足以卸载数据库、文件 I/O、资源加载等阻塞路径。

代价是：

- 单个 BaseApp/CellApp 仍有主线程瓶颈。
- 后台线程无法解决脚本或实体主逻辑热点。
- 回主线程收尾可能形成 foreground task 堆积。
- 任务取消是协作式，依赖任务主动检查停止状态。

## 现代对比

<div class="decision-table">

| 模型 | 优点 | 代价 | 对 BigWorld 的判断 |
| --- | --- | --- | --- |
| 单 Reactor + 后台任务 | 顺序清晰、低锁、易调试 | 单核瓶颈 | 当前模型，适合先补观测 |
| 固定线程池 | 简单卸载 I/O 和耗时任务 | 任务依赖和主线程提交仍复杂 | 可保留并增强 |
| Work-stealing Job System | CPU 利用率高 | 数据依赖、对象生命周期重构大 | 适合新计算模块，不适合直接改 Entity |
| Actor | 状态归属清晰，消息串行 | 调度器和协议重构大 | 可作为长期重构方向 |
| Python asyncio | I/O 并发语义清晰 | 嵌入 C++/Python 2 迁移成本高 | Python 3.12 后也不应直接替代 Mercury |
| 多解释器并行 | Python 3.12 后有新可能 | C API、扩展模块、对象共享风险高 | 只能专项评估，不能假设收益 |

</div>

## 现代化建议

优先级建议：

1. 统计后台任务队列长度、等待时间、执行时间、主线程收尾耗时。
2. 给 `TaskManager::tick()` 增加预算，避免 foreground tasks 一次性拖垮 Tick。
3. 审计后台任务是否触碰 Python 对象和 Entity 状态。
4. 把数据库、文件 I/O、压缩、加密、资源加载优先下沉后台。
5. 新增 CPU 密集模块时使用独立 job system，但只通过主线程提交状态变更。
6. Python 3.12 迁移前不要假设多线程脚本能提升吞吐，先隔离 C API 和 GIL 边界。

## 本章边界

本章解释线程和后台任务。下一章分析脚本热更新，它正是 Python 解释器、EntityDef、Mailbox 和实体对象生命周期交织最复杂的路径之一。
