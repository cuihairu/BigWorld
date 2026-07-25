# 主循环、Tick 与事件分发

<div class="arch-hero">

BigWorld 每个主要服务进程不是“每个玩家一个线程”，而是以主 Reactor 线程驱动网络、定时器、统计和频繁任务。理解主循环顺序，是研究线程模型、网络公平性、Tick 抖动和热路径瓶颈的前提。

</div>

## 核心结论

`EventDispatcher::processOnce()` 的顺序是：

<div class="flow-strip">
  <span class="flow-node">FrequentTasks</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Timers</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Stats</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Network</span>
</div>

源码入口是 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:428)。

### EventDispatcher 核心数据结构

```cpp
// event_dispatcher.hpp - EventDispatcher 类定义
class EventDispatcher
{
public:
    EventDispatcher();
    ~EventDispatcher();

    // 主循环入口
    int processOnce( bool shouldIdle = true );
    int processContinuously();
    int processUntilBreak();

    // Timer 管理
    int addTimer( int microseconds, TimerHandler * pHandler, 
                  void * pUser, const char * name );
    int addOnceOffTimer( int microseconds, TimerHandler * pHandler,
                         void * pUser, const char * name );

    // FrequentTask 管理
    void addFrequentTask( FrequentTask * pTask );
    void delFrequentTask( FrequentTask * pTask );

private:
    // 核心组件
    FrequentTasks * pFrequentTasks_;
    TimeQueue64 * pTimeQueue_;
    EventPoller * pPoller_;
    
    // 等待策略
    double maxWait_;  // 默认 0.1 秒 (100ms)
    bool breakProcessing_;
    
    // 统计
    uint64 lastProcessTime_;
    int numTimerCalls_;
    int numFrequentTaskCalls_;
};
```

**设计要点：**
- `pFrequentTasks_` 是同步回调集合，每轮主循环都执行
- `pTimeQueue_` 是基于 timestamp 的定时器队列，支持一次性和循环 timer
- `pPoller_` 是 epoll/kqueue 封装，只负责 FD readiness 事件
- `maxWait_` 默认 100ms，控制网络 poll 的最大等待时间
- `breakProcessing_` 标志用于提前退出循环（如收到 SIGINT）

### processOnce() 实现细节

```cpp
// event_dispatcher.cpp:428
int EventDispatcher::processOnce( bool shouldIdle )
{
    // 1. 执行 FrequentTasks
    if (pFrequentTasks_)
    {
        pFrequentTasks_->process();
    }
    
    if (breakProcessing_) return 0;
    
    // 2. 处理 Timer
    if (pTimeQueue_)
    {
        pTimeQueue_->process( timestamp() );
    }
    
    if (breakProcessing_) return 0;
    
    // 3. 更新统计
    this->processStats();
    
    if (breakProcessing_) return 0;
    
    // 4. 处理网络事件
    if (pPoller_)
    {
        double maxWait = shouldIdle ? this->calculateWait() : 0;
        pPoller_->processPendingEvents( maxWait );
    }
    
    return 0;
}
```

**关键细节：**
- FrequentTasks 在 Timer 之前执行，所以频繁任务优先级更高
- Timer 使用 `timestamp()` 驱动，不是 wall-clock
- 网络处理在最后，受前面任务耗时影响
- `calculateWait()` 取 `maxWait_` 和最近 timer 到期时间的较小值

这说明网络事件不是孤立调度的，它被放在一轮主循环的后半段。任何 Timer、FrequentTask 或主线程消息处理耗时，都会影响网络处理的及时性。

从服务进程视角看，默认入口更简单：`ServerApp::run()` 调用 `mainDispatcher_.processUntilBreak()`，运行结束后才执行 `onRunComplete()`，见 [server_app.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/server_app.cpp:240)。所以 BaseApp、CellApp、LoginApp、Manager 的“主线程”本质上就是围绕同一个 `EventDispatcher` 轮转。

## 调用链

<MermaidDiagram title="EventDispatcher 主循环">
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

源码锚点：

- `processContinuously()` 循环调用 `processOnce(true)`，见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:409)。
- `processOnce()` 的执行顺序见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:428)。
- `processNetwork()` 通过 `calculateWait()` 计算最大等待时间，见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:367)。
- `calculateWait()` 会取默认 `maxWait_` 与最近 timer 到期时间的较小值，见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:381)。
- `processUntilBreak()` 在循环结束后统一报告 pending exceptions，见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:453)。

## 等待策略

`processNetwork()` 的 `maxWait` 由 `shouldIdle` 决定：

- 如果 `shouldIdle=false`，网络 poll 不阻塞。
- 如果 `shouldIdle=true`，最多等到最近 timer 到期或默认最大等待值。

这是一种典型 Reactor 设计：主线程在没有网络事件时可以休眠，但不会错过定时器。

代价是：

- Timer 太密集会减少网络等待，增加循环频率。
- 某个 handler 执行过久会阻塞后续所有事件。
- 如果网络接收一次处理太多包，Timer 和其他任务可能被拖延。

`EventDispatcher` 构造时默认 `maxWait_ = 0.1`，也就是没有更早 timer 时一次网络等待最多 100ms，见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:53)。这不是固定 tick sleep，而是“网络 poll 等待上限 + 最近 timer 到期时间”的组合。

## TimerQueue 与 GameTick

**概述：** BigWorld 的游戏循环不是固定 sleep，而是通过 Timer 驱动。每个 App 有自己的 GameTick timer，到期后执行游戏逻辑。Timer 使用 `timestamp()` 驱动，不是 wall-clock。

**源码入口：** [event_dispatcher.cpp:264](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:264)

```cpp
// event_dispatcher.cpp:264 - 添加 Timer
int EventDispatcher::addTimerCommon( int microseconds, 
    TimerHandler * pHandler, void * pUser, const char * name,
    bool isRepeating )
{
    // 1. 微秒转换为 timestamp interval
    uint64 interval = uint64(microseconds) * 1000;
    
    // 2. 计算到期时间
    uint64 startTime = timestamp() + interval;
    
    // 3. 插入 TimeQueue64
    TimeQueue64::iterator iter = pTimeQueue_->add( startTime, 
        pHandler, pUser, interval );
    
    // 4. 返回 timer ID
    return iter->id();
}
```

**流程图：**

<MermaidDiagram title="Timer 添加与触发流程">
sequenceDiagram
    participant App as BaseApp/CellApp
    participant Dispatcher as EventDispatcher
    participant TimeQueue as TimeQueue64
    participant Timer as Timer Node
    participant Handler as TimerHandler

    App->>Dispatcher: addTimer(100ms)
    Dispatcher->>TimeQueue: add(timestamp + 100ms)
    TimeQueue->>Timer: 创建 Timer 节点
    Timer->>Timer: 设置 interval = 100ms
    
    Note over Timer: 等待 100ms...
    
    Timer->>TimeQueue: process(timestamp())
    TimeQueue->>Timer: 检查是否到期
    Timer->>Handler: handleTimeout()
    Handler->>App: tickGameTime()
    Timer->>TimeQueue: 重新入队 (循环 timer)
</MermaidDiagram>

**详细讲解：**

1. **时间单位**：`microseconds` 是输入，`timestamp()` 返回纳秒。所以 `interval = microseconds * 1000`。

2. **TimeQueue64 结构**：基于时间戳的优先队列，最早到期的 timer 在队首。`process()` 方法检查队首是否到期。

3. **循环 Timer**：如果 `interval > 0`，timer 到期后会重新入队，实现周期性触发。GameTick 就是循环 timer。

4. **Timer 触发**：`triggerTimer()` 直接调用 `TimerHandler::handleTimeout()`，在主线程同步执行。

### GameTick 启动

**源码入口：** [baseapp.cpp:2016](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:2016)

```cpp
// baseapp.cpp:2016 - BaseApp 启动 GameTick
void BaseApp::onManagerRebirth( ... )
{
    // ... 其他初始化 ...
    
    // 启动 GameTick timer，10Hz (100ms)
    gameTimerHandle_ = mainDispatcher_.addTimer(
        1000000 / Config::updateHertz(),  // 1000000 / 10 = 100000us = 100ms
        this,                              // TimerHandler
        NULL,                              // pUser
        "GameTick"                         // 名称
    );
}
```

**GameTick 执行流程：**

<MermaidDiagram title="GameTick 执行流程">
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
    EntityApp->>Script: 执行脚本 timer
</MermaidDiagram>

**详细讲解：**

1. **Tick 频率**：`Config::updateHertz()` 默认 10，表示 10Hz，每 100ms 一个 tick。这是 MMO 服务器的典型配置。

2. **Tick Hook 顺序**：`advanceTime()` 定义了所有 App 共享的 hook 顺序，确保每个 tick 的执行顺序一致。

3. **脚本 Timer**：`EntityApp::onTickProcessingComplete()` 会执行 Python 脚本的 timer，这些 timer 绑定到 GameTime，不是 wall-clock。

4. **负载上报**：每个 tick 结束时，App 会向 Mgr 上报负载信息，用于负载均衡决策。

## Tick Hook 顺序

`ServerApp::advanceTime()` 定义了所有 EntityApp 派生进程共享的 tick hook 顺序：

1. 计算上一个 tick 周期并调用 `onTickPeriod()`。
2. 调用 `onEndOfTick()`。
3. 递增 `time_`。
4. profiler tick。
5. 调用 `onStartOfTick()`。
6. 调用 `callUpdatables()`。
7. 调用 `onTickProcessingComplete()`。

源码见 [server_app.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/server_app.cpp:311)。这些 hook 的语义在 [server_app.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/server_app.hpp:158) 有明确注释。

`EntityApp::onTickProcessingComplete()` 会在 ServerApp hook 后调用脚本 timer 队列，见 [entity_app.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/entity_app.cpp:136)。这说明脚本 timer 不是 `EventDispatcher` 的 wall-clock timer，而是跟 `EntityAppTimeQueue` 和 `GameTime` 绑定，`EntityAppTimeQueue::time()` 返回当前实体 app 的 `time_`，见 [entity_app.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/entity_app.cpp:25)。

`BaseApp::tickGameTime()` 在 `advanceTime()` 前后还会执行一批 BaseApp 专属工作：

- 计算 tick 是否迟到和 spare time。
- 更新 profiler 和负载。
- tick pending login。
- backup/archive。
- 同步 TimeKeeper。
- 向 BaseAppMgr 上报 load、base 数和 proxy 数。
- tick worker jobs、rate limit filters、send window overflow、idle proxy channels、stats 和 dead CellApp 处理。

源码见 [baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:1504)。

`CellApp::handleGameTickTimeSlice()` 的 tick 工作更偏空间模拟：

- shutdown pause 时进入 `tickShutdown()`。
- 更新 load 并通知 CellAppMgr。
- 更新边界。
- `advanceTime()`。
- tick backup。
- 检查 send window overflow。
- 执行 offload 检查、删除旧 Space、同步时间、tick stats。
- 播放 buffered entity/input messages。
- tick recordings。

源码见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:1302)。

关键结论：`EventDispatcher` 的 Timer 只是触发点，真实游戏 tick 是 `ServerApp::advanceTime()` 和各 App 自己的 tick body 共同组成的。

## TimeKeeper 校准

BigWorld 的 GameTick timer 不是完全固定的本地节拍。`TimeKeeper` 保存 game time、目标 tick 频率、被跟踪的 timer handle 和 master address，见 [time_keeper.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/time_keeper.hpp:13)。

当从 master 收到时间读数后，`TimeKeeper::inputMasterReading()` 会估算 round trip，计算本地 reading 与 master reading 的 offset。如果本地慢了，会把 tracking timer 的 interval 缩短；如果本地快了，会把 interval 拉长；调整量是 nominal interval 的 1/20，见 [time_keeper.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/time_keeper.cpp:110)。

同步请求通过 `synchroniseWithMaster()` 发 Mercury request，携带当前 `readingNow()`，见 [time_keeper.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/time_keeper.cpp:283)。`scheduleSyncCheck()` 会在下一次 tracking timer delivery 附近注册一次性 timer，用来检查或恢复 interval，见 [time_keeper.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/time_keeper.cpp:212)。

工程含义：

- Tick 同步是通过调节 timer interval 实现的，不是简单地把 `time_` 改成 master 值。
- 多进程 MMO 需要跨 BaseApp/CellApp/Manager 维持相近 GameTime，否则 AOI、迁移、备份和客户端同步都会出现时间语义偏差。
- `TimeKeeper` 本身也跑在主 dispatcher 上，所以同步回调仍然受主线程阻塞影响。

## 网络处理不是异步完成模型

`EventPoller` 只负责把 FD readiness 分派给 handler。handler 仍在主线程同步执行。Linux 下 `EPoller::processPendingEvents()` 调用 `epoll_wait()`，然后逐个处理事件。源码见 [event_poller.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:1115)。

源码边界很清楚：

- `EventPoller` 只把可读/可写事件分派给 handler。
- handler 执行、Mercury packet 解析、Channel 状态更新仍占用主 dispatcher。
- 当前代码没有提交队列、完成队列或异步完成回调语义。

因此不能把 `EventPoller` 理解成后台 I/O 完成层；它只是 Reactor 的 FD 分派层。

## Tick 公平性

接收路径中 `PacketReceiver::handleInputNotification()` 会在一次可读事件中持续调用 `processSocket()`，直到不能继续或超过 `maxSocketProcessingTime`。源码见 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:83)。

这个设计说明开发者已经意识到“单个 socket 排空”可能压垮一帧：

- 继续排空 socket 可以提高吞吐，减少 UDP receive queue 堆积。
- 设置处理时间上限可以保护主循环公平性。
- 超时后停止处理会留下内核队列压力，可能增加延迟或丢包。

这是一种非常游戏服务器式的折中：吞吐不是唯一指标，Tick 预算和尾延迟同样重要。

## 子 Dispatcher

`NetworkInterface` 不是只拿一个外部 dispatcher 使用。`EventDispatcher::attach()` 会把 child dispatcher 附到 parent dispatcher，并把 child poller fd 注册到 parent，见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:126)。

`DispatcherCoupling` 本身是一个 `FrequentTask`。构造时注册到 main dispatcher，`doTask()` 每轮调用 `childDispatcher_.processOnce()`，见 [dispatcher_coupling.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/dispatcher_coupling.hpp:18)。

`NetworkInterface::processUntilChannelsEmpty()` 会在 flush 阶段交替处理 main dispatcher 和 interface 自己的 dispatcher。源码见 [network_interface.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/network_interface.cpp:255)。

这说明 Mercury 不是只有一个全局 dispatcher，而是存在主 dispatcher 与网络接口 dispatcher 的组合。文档后续分析 Channel 关闭、可靠包 flush、进程退出时会继续引用这条链路。

## FrequentTask 边界

`FrequentTasks::process()` 每轮遍历注册的 `FrequentTask` 并调用 `doTask()`，但它不是一个任务队列调度器：

- 如果正在递归处理且集合变脏，会直接返回。
- task 执行期间如果 `FrequentTasks` 自身被销毁，会通过 `pGotDestroyed_` 立即退出。
- 如果 task 增删导致容器变脏，当前轮会 break，避免迭代器失效。

源码见 [frequent_tasks.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/frequent_tasks.cpp:77)。

脚本层也可以注册 frequent task。`ScriptFrequentTasks::addFrequentTask()` 会把 Python callable 包装成 `ScriptFrequentTask` 并注册到 `Updatables`，其 `update()` 调用 Python callback，返回 true 时移除自身，见 [script_frequent_tasks.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/script_frequent_tasks.cpp:35)。

这再次说明：主循环里的 frequent task 是同步回调集合，不是后台线程池。耗时 callback 会直接占用主线程预算。

## 源码取舍

源码选择的不是通用任务调度器，而是主 dispatcher 串起网络、timer、统计和频繁任务：

- 逻辑执行顺序由 `processOnce()` 固定，便于定位实体、脚本和网络消息的先后关系。
- 实体状态默认在主线程推进，避免 Base/Cell 热路径被锁竞争打碎。
- 后台线程主要承接数据库、文件和长耗时任务，完成后再回主线程交付结果。
- CellApp/BaseApp 的扩展点更多依赖多进程分片、Cell 切分和实体迁移，而不是在单进程内并行执行实体逻辑。

源码也暴露出明确约束：

- 任意 handler、timer callback、frequent task 都不能长时间阻塞。
- 单个热点进程会被主 dispatcher 上限卡住。
- `PacketReceiver::maxSocketProcessingTime` 只能限制单次 socket 处理时间，不能消除所有主线程抖动。
- 脚本 frequent task 虽然从 Python callable 包装而来，但执行位置仍是主线程回调。

## 源码验证重点

主循环相关验证应覆盖调度顺序和阻塞边界：

- `processOnce()` 中 FrequentTasks、Timers、Stats、Network 的顺序不能被改乱。
- `breakProcessing()` 在 FrequentTask 中触发时应跳过 Timers 和 Network，但 Stats 仍会执行。
- `breakProcessing()` 在 Timer 中触发时应跳过 Network，但 Stats 仍会执行。
- 循环 timer 在 callback 后应按 interval 重新入队，一次性 timer 应自动取消。
- GameTick timer 的 interval 应等于 `1000000 / updateHertz`，并能被 `TimeKeeper` 调整后恢复。
- `PacketReceiver::maxSocketProcessingTime` 非零时，一次 socket 可读事件不能无限排空。
- `DispatcherCoupling` 下 child dispatcher 的 `processOnce()` 不应阻塞 main dispatcher。
- 脚本 frequent task 或 updatable 抛错时不能破坏主循环异常报告路径。

## 后续问题

本章只解释“事件如何被调度”。下一章会进入网络 I/O 模型本身：`EventPoller`、`PacketReceiver`、Bundle 收发和 Channel 可靠层如何共同构成 Mercury 的网络路径。
