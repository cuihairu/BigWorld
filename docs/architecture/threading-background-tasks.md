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

## BgTaskManager 完整调用链

### 任务投递流程

**概述：** 从主线程投递后台任务到执行的完整流程。BgTaskManager 使用信号量通知后台线程，实现高效的线程间通信。

**源码入口：** [bgtask_manager.cpp:693](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:693)

```cpp
// bgtask_manager.cpp:693 - 投递后台任务
void TaskManager::addBackgroundTask( BackgroundTask * pTask )
{
    // 1. 记录入队时间
    pTask->timeEnqueuedToBackground_ = timestamp();
    
    // 2. 加锁并入队
    bgTaskListMutex_.grab();
    bgTaskList_.push_back( pTask );
    bgTaskListMutex_.release();
    
    // 3. 信号量通知后台线程
    bgTaskSemaphore_.push();
}
```

**流程图：**

<MermaidDiagram title="任务投递流程">
sequenceDiagram
    participant Main as 主线程
    participant Queue as bgTaskList_
    participant Semaphore as bgTaskSemaphore_
    participant Worker as 后台线程

    Main->>Main: 记录 timeEnqueuedToBackground_
    Main->>Queue: 加锁并入队
    Main->>Semaphore: push() 通知
    Semaphore->>Worker: 唤醒
    Worker->>Queue: pullBackgroundTask()
    Queue->>Worker: 返回任务
</MermaidDiagram>

**详细讲解：**

1. **时间戳记录**：`timeEnqueuedToBackground_` 记录任务入队时间，用于统计任务等待时间。

2. **互斥锁保护**：`bgTaskListMutex_` 保护任务队列，防止多线程同时修改。

3. **信号量通知**：`bgTaskSemaphore_.push()` 通知后台线程有新任务。信号量比条件变量更高效，减少上下文切换。

4. **批量处理**：后台线程可以一次拉取多个任务，减少锁竞争。

### 后台线程执行流程

**概述：** 后台线程循环拉取并执行任务。每个任务在独立的后台线程中执行，完成后可以选择投递回主线程。

**源码入口：** [bgtask_manager.cpp:137](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:137)

```cpp
// bgtask_manager.cpp:137 - 后台线程主循环
void BackgroundTaskThread::run()
{
    while (true)
    {
        // 1. 等待任务
        BackgroundTask * pTask = pManager_->pullBackgroundTask();
        
        // 2. 检查退出条件
        if (pTask == NULL)
        {
            break;
        }
        
        // 3. 设置线程本地数据
        pTask->setStaticThreadData();
        
        // 4. 检查阻塞回调
        if (pManager_->hasThreadBlockCallback())
        {
            pManager_->callThreadBlockCallback();
        }
        
        // 5. 执行任务
        pTask->doBackgroundTask();
        
        // 6. 记录耗时
        pTask->onBackgroundTaskFinished();
    }
    
    // 7. 投递 ThreadFinisher
    pManager_->addMainThreadTask( new ThreadFinisher(this) );
}
```

**流程图：**

<MermaidDiagram title="后台线程执行流程">
sequenceDiagram
    participant Worker as 后台线程
    participant Manager as TaskManager
    participant Task as BackgroundTask
    participant Main as 主线程

    loop 任务循环
        Worker->>Manager: pullBackgroundTask()
        Manager->>Worker: 返回任务
        
        alt 任务为空
            Worker->>Worker: 退出循环
        else 有任务
            Worker->>Task: setStaticThreadData()
            Worker->>Task: doBackgroundTask()
            Task->>Task: 执行后台逻辑
            
            alt 需要回主线程
                Task->>Manager: addMainThreadTask(this)
            end
            
            Worker->>Task: onBackgroundTaskFinished()
        end
    end
    
    Worker->>Manager: addMainThreadTask(ThreadFinisher)
    Worker->>Worker: 线程退出
</MermaidDiagram>

**详细讲解：**

1. **任务拉取**：`pullBackgroundTask()` 使用信号量等待，没有任务时线程休眠，不消耗 CPU。

2. **线程本地数据**：`setStaticThreadData()` 设置线程本地存储，用于跟踪当前执行的任务。

3. **阻塞回调**：`ThreadBlockCallback` 用于在任务执行前检查是否需要阻塞（如等待其他资源）。

4. **任务执行**：`doBackgroundTask()` 在后台线程中执行，不能访问主线程数据（除非线程安全）。

5. **线程退出**：收到 NULL 任务时，线程退出循环，投递 `ThreadFinisher` 通知主线程清理资源。

### 回主线程收尾流程

**概述：** 后台任务完成后，如果需要在主线程执行收尾操作（如更新主线程数据结构），通过 `addMainThreadTask()` 投递回主线程。

**源码入口：** [bgtask_manager.cpp:709](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:709)

```cpp
// bgtask_manager.cpp:709 - 投递主线程任务
void TaskManager::addMainThreadTask( BackgroundTask * pTask )
{
    // 1. 加锁
    fgTaskListMutex_.grab();
    
    // 2. 入队
    fgTaskList_.push_back( pTask );
    
    // 3. 解锁
    fgTaskListMutex_.release();
}
```

**流程图：**

<MermaidDiagram title="回主线程收尾流程">
sequenceDiagram
    participant Task as BackgroundTask
    participant Manager as TaskManager
    participant Queue as fgTaskList_
    participant Main as 主线程

    Task->>Manager: addMainThreadTask(this)
    Manager->>Queue: 加锁并入队
    
    Note over Main: 等待 tick...
    
    Main->>Manager: tick()
    Manager->>Queue: swap(newTasks_)
    Manager->>Task: doMainThreadTask()
    Task->>Task: 执行收尾逻辑
</MermaidDiagram>

**详细讲解：**

1. **线程安全**：`addMainThreadTask()` 可以在后台线程中调用，通过互斥锁保护队列。

2. **队列交换**：`tick()` 使用 swap 操作，将任务队列交换到临时变量，减少锁持有时间。

3. **收尾执行**：`doMainThreadTask()` 在主线程中执行，可以安全访问主线程数据。

4. **延迟执行**：收尾任务不会立即执行，而是等待主线程 tick，确保在正确的时机处理。

### 主线程 tick 处理流程

**概述：** 主线程定期处理 foreground tasks。这是 BgTaskManager 与主线程交互的关键点。

**源码入口：** [bgtask_manager.cpp:744](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:744)

```cpp
// bgtask_manager.cpp:744 - 主线程 tick
void TaskManager::tick()
{
    // 1. 加锁
    fgTaskListMutex_.grab();
    
    // 2. 交换队列（减少锁持有时间）
    BackgroundTaskList newTasks;
    newTasks.swap( fgTaskList_ );
    
    // 3. 解锁
    fgTaskListMutex_.release();
    
    // 4. 处理任务
    for (BackgroundTask * pTask : newTasks)
    {
        // 记录开始时间
        uint64 startTime = timestamp();
        
        // 执行收尾任务
        pTask->doMainThreadTask();
        
        // 记录耗时
        uint64 elapsed = timestamp() - startTime;
        pTask->onMainThreadTaskFinished( elapsed );
    }
}
```

**流程图：**

<MermaidDiagram title="主线程 tick 处理流程">
sequenceDiagram
    participant Main as 主线程
    participant Manager as TaskManager
    participant Queue as fgTaskList_
    participant Tasks as newTasks

    Main->>Manager: tick()
    Manager->>Queue: 加锁
    Manager->>Tasks: swap(fgTaskList_)
    Manager->>Queue: 解锁
    
    loop 处理任务
        Manager->>Tasks: 遍历
        Tasks->>Main: doMainThreadTask()
        Main->>Main: 执行收尾逻辑
        Main->>Tasks: onMainThreadTaskFinished()
    end
</MermaidDiagram>

**详细讲解：**

1. **队列交换**：使用 `swap()` 而不是逐个取出，减少锁持有时间，提高并发性能。

2. **耗时统计**：每个任务记录执行时间，用于性能分析和负载监控。

3. **顺序执行**：foreground tasks 在主线程中顺序执行，确保线程安全。

4. **与 GameTick 关系**：`tick()` 在 `EntityApp::onTickProcessingComplete()` 中调用，与游戏逻辑同步。

### 线程退出流程

后台线程退出时的清理调用链：

源码入口：[bgtask_manager.cpp:186](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:186)

<div class="flow-strip">
  <span class="flow-node">pullBackgroundTask() 返回 NULL</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">shouldRun = false</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">onEnd() 清理线程本地数据</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">创建 ThreadFinisher</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">addMainThreadTask() 通知主线程</span>
</div>

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

## 为什么选择单 Reactor 主线程：游戏服务器视角

### 游戏服务器的核心约束

游戏服务器与 Web 服务器、数据库服务器有本质区别：

<div class="decision-grid">
  <div class="decision-card">
    <h3>确定性优先</h3>
    <p>游戏状态必须可预测、可重现。多线程并发修改实体状态会导致竞态条件，使得 Bug 难以复现和调试。</p>
  </div>
  <div class="decision-card">
    <h3>帧率稳定性</h3>
    <p>BigWorld 默认 10Hz (100ms/Tick)，每个 Tick 必须在预算内完成。多线程锁竞争会导致不可预测的延迟抖动。</p>
  </div>
  <div class="decision-card">
    <h3>状态一致性</h3>
    <p>实体属性、AOI、Ghost、Witness 必须在同一 Tick 内保持一致。多线程需要复杂的锁顺序来避免死锁。</p>
  </div>
  <div class="decision-card">
    <h3>脚本安全</h3>
    <p>Python 2.7 有 GIL，多线程无法真正并行执行脚本。单线程模型避免了 GIL 竞争问题。</p>
  </div>
</div>

### BigWorld 的 Tick 模型

BigWorld 使用固定频率的 GameTick 驱动游戏逻辑：

源码入口：[baseapp.cpp:2016](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:2016)

```cpp
gameTimer_ = mainDispatcher_.addTimer( 1000000/Config::updateHertz(),
    this, reinterpret_cast< void * >( TIMEOUT_GAME_TICK ), "GameTick" );
```

默认配置：
- `updateHertz = 10` (10Hz，每 100ms 一个 Tick)
- `reservedTickTime` 用于判断下一个 Tick 是否 pending

源码见 [baseapp.cpp:1422](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:1422)。

### 单线程模型的游戏优势

#### 1. 实体状态修改无锁

```cpp
// 单线程模型：直接修改，无需加锁
entity->setPosition(newPos);
entity->setHealth(newHealth);
entity->updateAOI();
```

如果使用多线程：

```cpp
// 多线程模型：需要加锁
std::lock_guard<std::mutex> lock(entity->mutex);
entity->setPosition(newPos);
// 可能死锁：如果另一个线程持有 AOI 锁并等待实体锁
```

#### 2. 事件顺序可预测

单线程模型下，事件处理顺序固定：

<div class="flow-strip">
  <span class="flow-node">FrequentTasks</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Timers</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Stats</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Network</span>
</div>

多线程模型下，事件顺序取决于线程调度，难以重现。

#### 3. 调试简单

单线程模型下：
- 堆栈跟踪清晰，只有一个主线程
- 断点可以捕获所有状态变更
- 日志输出顺序与执行顺序一致

多线程模型下：
- 堆栈跟踪混杂多个线程
- 断点可能错过竞态条件
- 日志输出顺序混乱

### 多线程模型的游戏劣势

#### 1. 实体间依赖复杂

游戏实体之间存在大量依赖关系：

- **AOI 依赖**：实体 A 看到实体 B，B 的属性变更必须通知 A
- **Ghost 依赖**：Cell 边界的 Ghost 必须与 Real 保持同步
- **Witness 依赖**：玩家的 Witness 管理着可见实体列表
- **Mailbox 依赖**：Base/Cell/Client 之间的方法调用

如果使用多线程处理不同实体，这些依赖会导致大量锁竞争。

#### 2. 状态一致性难保证

考虑一个简单场景：玩家攻击怪物

单线程模型：
```
1. 玩家实体调用 attack(monster)
2. 怪物实体扣血
3. 怪物实体检查死亡
4. 怪物实体触发 onDestroy 回调
5. 通知周围玩家
```

多线程模型：
```
线程1: 玩家实体调用 attack(monster)
线程2: 怪物实体被另一个玩家攻击
线程3: 怪物实体被 AOI 移除
// 需要复杂的锁顺序来保证一致性
```

#### 3. 脚本执行受限 (历史原因)

Python 2.7 的 GIL 限制：

- 同一时刻只有一个线程执行 Python 字节码
- 多线程无法真正并行执行脚本
- GIL 竞争会导致性能下降

BigWorld 的解决方案：
- 主线程执行所有脚本逻辑
- 后台线程只做阻塞 I/O
- 通过 `Script::releaseLock()` / `Script::acquireLock()` 管理 GIL

**重要更新：Python 3.13+ 的突破**

Python 3.13 (2024 年 10 月发布) 引入了 **free-threaded 模式** (PEP 703)，也称为 no-GIL：

- 通过编译选项 `--disable-gil` 启用
- 安装特殊变体 `python3.13t` 可直接使用
- 实现了真正的多线程并行，无需 GIL
- 内存管理和引用计数变为线程安全
- 单线程性能有 ~5-10% 开销

这意味着如果 BigWorld 迁移到 Python 3.13+，可以：
- 真正并行执行脚本逻辑
- 减少主线程瓶颈
- 保持代码简单性

详细迁移方案见 [Python 升级路线图](/upgrade-plan/python-upgrade-plan)（可扩展到 3.13+）。

### 何时使用多线程

BigWorld 在以下场景使用多线程：

| 场景 | 线程类型 | 原因 |
|------|----------|------|
| 数据库操作 | BgTaskManager | 阻塞 I/O，不涉及实体状态 |
| 文件 I/O | FileIOTaskManager | 阻塞 I/O，不涉及实体状态 |
| 定时任务 | WorkerThread | 延迟执行，结果回主线程 |
| 加密计算 | 网络线程 | CPU 密集，不修改实体状态 |

关键原则：**多线程只用于"准备数据"，不用于"修改状态"**

### 与现代游戏服务器对比

| 引擎 | 线程模型 | 优势 | 劣势 |
|------|----------|------|------|
| BigWorld | 单 Reactor + 后台任务 | 确定性高、调试简单 | 单核瓶颈 |
| Unreal | 多线程 Task Graph | 多核利用 | 复杂度高 |
| Unity | 主线程 + Job System | 平衡性好 | 仍有限制 |
| 分布式 ECS | 全并行 ECS | 最大并行 | 复杂度极高 |

BigWorld 选择单线程模型是基于时代约束和工程权衡：
- 2000 年代的多核 CPU 还不普及
- Python 2.7 的 GIL 限制了并行能力
- MMO 的复杂状态关系难以并行化
- 调试和运维的复杂度是重要考量

### 现代化建议

如果要在现代环境下改进 BigWorld 的线程模型：

#### 1. 升级到 Python 3.13+ free-threaded 模式

这是最有价值的改进，可以彻底改变线程模型：

```bash
# 安装 Python 3.13t (free-threaded)
./configure --disable-gil
make
# 或直接使用 python3.13t
```

**收益**：
- 真正的多线程脚本并行
- 可以并行处理多个实体的脚本逻辑
- 减少主线程瓶颈

**风险**：
- C 扩展兼容性问题
- 单线程性能有 5-10% 开销
- 需要全面测试

#### 2. 引入 Actor 模型

每个实体独立消息队列，避免共享状态：

```cpp
// 每个实体有自己的消息队列
class Entity {
    std::queue<Message> inbox;
    void processMessages() {
        while (!inbox.empty()) {
            auto msg = inbox.pop();
            handleMessage(msg);  // 无锁处理
        }
    }
};
```

#### 3. 分离读写路径

读操作可并行，写操作串行：

```cpp
// 读操作：可并行
auto pos = entity->getPosition();  // 无锁
auto health = entity->getHealth();  // 无锁

// 写操作：串行
entity->setPosition(newPos);  // 需要同步
entity->setHealth(newHealth);  // 需要同步
```

#### 4. 使用 Lock-free 数据结构

减少锁竞争：

```cpp
// 使用原子操作
std::atomic<int> health{100};
health.store(newHealth, std::memory_order_relaxed);

// 使用无锁队列
moodycamel::ConcurrentQueue<Message> queue;
```

#### 5. Profile 驱动优化

先找到热点，再决定并行化：

```bash
# 使用 perf 分析
perf record -g ./baseapp
perf report

# 使用火焰图
flamegraph.pl perf.data > flame.svg
```

### 参考资料

- [PEP 703: Making the Global Interpreter Lock Optional](https://peps.python.org/pep-0703/)
- [Python 3.13 Release Notes](https://docs.python.org/3.13/whatsnew/3.13.html)
- [BigWorld Python 迁移方案](/upgrade-plan/python-upgrade-plan)

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

## 源码取舍

这种线程架构的职责边界很清楚：

- MMO 实体状态复杂，多线程共享实体会引入大量锁和竞态。
- 多进程 Cell/Base 分片比单进程内多线程更符合水平扩展。
- 背景任务池足以卸载数据库、文件 I/O、资源加载等阻塞路径。
- `TaskManager::addBackgroundTask()` 可以跨线程投递，但 `tick()` 只能由 owning thread 调用。
- 后台任务完成后通过 foreground task 回主线程交付结果，避免直接改实体状态。

源码代价：

- 单个 BaseApp/CellApp 仍有主线程瓶颈。
- 后台线程无法解决脚本或实体主逻辑热点。
- 回主线程收尾可能形成 foreground task 堆积。
- 任务取消是协作式，依赖任务主动检查停止状态。
- 后台线程进入 Python 时必须显式处理 GIL 和 thread state。

## 源码验证重点

线程和后台任务测试应覆盖所有权和回主线程边界：

- 任意线程调用 `addBackgroundTask()` 应安全入队。
- 非 owning thread 不应调用 `TaskManager::tick()`。
- 后台任务完成后应通过 foreground task 回到主线程提交结果。
- `shouldAbortTask()` 为 true 时，长任务应能协作退出。
- DB、文件 I/O、资源加载类任务不能直接访问实体主线程状态。
- 后台线程进入 Python 前后必须成对释放/恢复 GIL。
- foreground task 堆积时应能观察到 tick 耗时和队列长度变化。

## 本章边界

本章解释线程和后台任务。下一章分析脚本热更新，它正是 Python 解释器、EntityDef、Mailbox 和实体对象生命周期交织最复杂的路径之一。
