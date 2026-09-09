# 线程架构与后台任务

<div class="arch-hero">

BigWorld 服务端不是“每个玩家一个线程”，也不是把实体逻辑拆成全并行 job。源码呈现的是单 Reactor 主线程 + 后台任务池 + 少量专用线程。线程的主要职责是隔离阻塞 I/O、延迟任务和后台准备工作，而不是并发修改实体状态。

</div>

## 先给结论

BigWorld 服务端线程模型可以分成四类：

| 类型 | 代表源码 | 职责 |
|---|---|---|
| 主 Reactor 线程 | `EventDispatcher`, `ServerApp` | 网络、timer、frequent task、handler、实体逻辑 |
| 通用后台任务池 | `BgTaskManager`, `TaskManager` | 后台执行阻塞/耗时任务，再可选回主线程收尾 |
| 文件 I/O 线程 | `FileIOTaskManager` | 资源和文件 I/O 专用后台任务 |
| BaseApp 延迟线程 | `BaseApp::WorkerThread` | 按时间排序执行 `WorkerJob` |

关键源码：

- 主循环顺序见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:428)。
- `ServerApp::advanceTime()` 推进 tick 生命周期，见 [server_app.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/server_app.cpp:311)。
- `TaskManager::addBackgroundTask()` 投递后台任务，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:693)。
- `TaskManager::addMainThreadTask()` 投递主线程收尾任务，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:709)。
- `TaskManager::tick()` 在 owning thread 处理 foreground task，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:744)。
- `BackgroundTaskThread::run()` 拉取并执行后台任务，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:137)。
- `WorkerThread::run()` 按时间调度 `WorkerJob`，见 [worker_thread.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/worker_thread.cpp:66)。
- `Script::initThread()` 初始化后台线程的脚本 thread state，见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:661)。

## 主线程边界

服务端主线程由 `EventDispatcher` 驱动：

<div class="flow-strip">
  <span class="flow-node">FrequentTasks</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Timers</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Stats</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Network</span>
</div>

这条链路意味着：

- Mercury handler 通常在主线程同步执行。
- BaseApp、CellApp 的 tick 逻辑在主线程推进。
- 实体状态、AOI、Ghost、Witness、Proxy 等核心对象默认按主线程所有权推理。
- handler 或脚本回调阻塞，会直接影响本进程 tick 和网络处理。

主线程不是因为“简单”而存在，而是因为实体状态跨 Base/Cell/Client/DB 多个域流动，随意并发修改会破坏消息顺序、生命周期和回调边界。

## BgTaskManager 链

`BgTaskManager` 是后台任务池。任务对象有两个阶段：

| 阶段 | 方法 | 执行线程 | 用途 |
|---|---|---|---|
| 后台阶段 | `doBackgroundTask()` | 后台线程 | 执行阻塞 I/O 或耗时准备 |
| 前台阶段 | `doMainThreadTask()` | owning/main thread | 提交结果、回调主线程状态 |

基本调用链：

```
TaskManager::addBackgroundTask()
  -> bgTaskList_ 入队
  -> bgTaskSemaphore_ 唤醒后台线程
  -> BackgroundTaskThread::run()
  -> BackgroundTask::doBackgroundTask()
  -> 可选 TaskManager::addMainThreadTask()
  -> TaskManager::tick()
  -> BackgroundTask::doMainThreadTask()
```

源码里的关键边界：

| 边界 | 代表源码 |
|---|---|
| 后台任务可以从任意线程入队 | `TaskManager::addBackgroundTask()` |
| 主线程任务可以从后台线程投递 | `TaskManager::addMainThreadTask()` |
| foreground task 由 owning thread 消费 | `TaskManager::tick()` |
| 后台线程退出通过 `ThreadFinisher` 回主线程清理 | `BackgroundTaskThread::run()` |
| 长任务取消依赖协作检查 | `TaskManager::shouldAbortTask()` |

这说明后台任务不是并发实体逻辑，而是“后台准备 + 主线程提交”的两阶段模型。

## EntityApp 如何消费后台任务

`EntityApp::onTickProcessingComplete()` 会在 tick 收尾阶段处理后台任务的主线程回调，见 [entity_app.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/entity_app.cpp:136)。

这形成了一个重要节奏：

```
App tick / 网络 handler
  -> 后台任务执行阻塞工作
  -> addMainThreadTask()
  -> tick 收尾
  -> doMainThreadTask()
  -> 更新主线程可见状态
```

因此，后台任务结果并不是完成后立即改变游戏状态，而是等主线程在安全点消费。分析 DB、文件加载、资源处理等异步路径时，要确认最终提交点是不是 `doMainThreadTask()`。

## DB 后台任务

MySQL 后端大量使用后台任务：

| 源码 | 行为 |
|---|---|
| `lib/db_storage_mysql/mysql_database.cpp` | 将 get/put/delete/lookUp 等 DB 操作投递到 `bgTaskManager_` |
| `lib/db_storage_mysql/tasks/background_task.cpp` | DB task 完成后可回主线程执行收尾 |
| `lib/db_storage_mysql/buffered_entity_tasks.cpp` | 对同一实体相关任务做缓冲和顺序控制 |
| `server/dbapp/write_entity_handler.cpp` | `DBApp::writeEntity()` 后续写入 handler |

这条链说明 DBApp 不应在主线程直接做阻塞 SQL。主线程负责接收 Mercury 请求和发起 DB task；后台线程做数据库 I/O；完成后再通过 handler 回到主线程语义。

## FileIOTaskManager

CellApp 初始化时启动文件 I/O 线程：

```
fileIOTaskManager_.initWatchers("FileIO")
fileIOTaskManager_.startThreads("FileIO", 1)
```

源码见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:574)。

CellApp tick 中也会处理文件 I/O task manager，见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:958)。

这说明 CellApp 把部分资源加载和文件访问从主线程挪出，但仍通过 tick 安全点处理主线程收尾。它不是通用 job graph，也不改变实体主逻辑的线程归属。

## BaseApp WorkerThread

`server/baseapp/worker_thread.cpp` 是 BaseApp 的另一套线程机制。它不是 `BgTaskManager` 的封装，而是一个时间排序的 `WorkerJob` 调度器。

`WorkerThread::run()` 的核心行为：

1. 队列为空时等待，最多睡眠一段时间。
2. 队列按 `nextTime_` 排序。
3. 到期后取出 `WorkerJob` 执行。
4. job 返回下次延迟秒数。
5. 返回非负数则重新入队。
6. 返回负数则停止调度，必要时删除 job。

这更像延迟后台轮询器，用于 BaseApp 的特定后台工作，而不是面向实体逻辑的并行调度器。

## 脚本线程边界

BigWorld 的脚本层通过嵌入式解释器运行。源码中线程进入脚本层必须显式管理 thread state 和解释器锁。

关键链：

| 源码 | 作用 |
|---|---|
| `Script::init()` | 初始化解释器，保存主线程状态，设置主线程 idle 时释放/恢复锁的函数 |
| `Script::initThread()` | 为新线程创建或绑定 thread state |
| `Script::releaseLock()` | 通过解释器 API 释放脚本锁 |
| `Script::acquireLock()` | 恢复脚本 thread state 并重新进入脚本执行上下文 |

源码见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:334)、[script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:661)、[script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:748)。

这条链的结论是：后台线程可以进入脚本相关代码，但不能把脚本逻辑当成无约束并行执行。每个进入脚本层的线程都必须遵守解释器锁和 thread state 管理。

## 为什么后台任务要回主线程

后台线程可以做准备工作，但不能随意提交游戏状态。原因来自源码边界：

| 状态 | 主线程提交原因 |
|---|---|
| Entity / Base / Cell | 生命周期、脚本回调、mailbox、migration 状态强依赖执行顺序 |
| Mercury Channel / Bundle | 很多路径按主线程网络 handler 和发送队列推理 |
| Witness / AOI / Ghost | 客户端可见性和 ghost 同步必须保持 tick 内一致 |
| Watcher 暴露状态 | 观测和控制面读写需要避免跨线程对象生命周期问题 |
| 脚本对象 | 需要解释器锁和 thread state，跨线程析构风险高 |

因此 BigWorld 的后台任务更准确地说是“异步 I/O/准备层”，不是“并行业务状态层”。

## 源码取舍

BigWorld 采用这种线程模型的源码取舍：

| 收益 | 代价 |
|---|---|
| 主线程状态顺序清晰 | 单个 BaseApp/CellApp 有主线程瓶颈 |
| Entity/AOI/Ghost/Proxy 生命周期更容易推理 | 后台任务无法解决实体脚本热点 |
| 阻塞 DB 和文件 I/O 可被隔离 | foreground task 堆积仍会拖慢 tick |
| 跨线程提交点集中在 `doMainThreadTask()` | 任务取消依赖协作检查 |
| 与多进程 Base/Cell 分片模型一致 | 调试异步任务仍需同时看后台和主线程日志 |

这不是通用线程模型优劣比较，而是 BigWorld 源码在实体状态复杂度、分布式进程模型和脚本边界下形成的具体取舍。

## 源码验证重点

验证线程相关结论时，优先检查：

| 问题 | 检索入口 |
|---|---|
| 后台任务在哪里创建 | `addBackgroundTask(` |
| 后台任务在哪里回主线程 | `addMainThreadTask(` |
| foreground task 由谁消费 | `TaskManager::tick()` |
| 哪些 App 在 tick 中处理任务 | `onTickProcessingComplete`, `fileIOTaskManager_.tick()` |
| DB 是否走后台任务 | `bgTaskManager_.addBackgroundTask`, `BufferedEntityTasks` |
| 文件 I/O 是否走专用线程 | `FileIOTaskManager` |
| 长任务是否支持取消 | `shouldAbortTask()` |
| 后台线程是否进入脚本 | `Script::initThread`, `releaseLock`, `acquireLock` |

测试入口：

| 测试目录 | 覆盖方向 |
|---|---|
| `lib/cstdmf/unit_test/test_bgtasks.cpp` | `BgTaskManager` 启停、任务执行和队列行为 |
| `lib/cstdmf/unit_test/test_background_file_writer.cpp` | 后台文件写入 |
| `lib/terrain/unit_test/` | 资源任务与 `BgTaskManager::tick()` 使用 |
| `server/baseapp/unit_test/` | BaseApp 相关异步路径的局部验证 |

## 本章边界

本章只解释源码中的线程、后台任务和主线程提交边界。脚本热更新、EntityDef 迁移、实体 offload 和 DB 持久化会复用这些线程边界，但它们的状态机分别在后续专题中展开。
