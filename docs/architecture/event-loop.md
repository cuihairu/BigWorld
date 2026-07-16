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

这说明网络事件不是孤立调度的，它被放在一轮主循环的后半段。任何 Timer、FrequentTask 或主线程消息处理耗时，都会影响网络处理的及时性。

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

## 等待策略

`processNetwork()` 的 `maxWait` 由 `shouldIdle` 决定：

- 如果 `shouldIdle=false`，网络 poll 不阻塞。
- 如果 `shouldIdle=true`，最多等到最近 timer 到期或默认最大等待值。

这是一种典型 Reactor 设计：主线程在没有网络事件时可以休眠，但不会错过定时器。

代价是：

- Timer 太密集会减少网络等待，增加循环频率。
- 某个 handler 执行过久会阻塞后续所有事件。
- 如果网络接收一次处理太多包，Timer 和其他任务可能被拖延。

## 网络处理不是异步完成模型

`EventPoller` 只负责把 FD readiness 分派给 handler。handler 仍在主线程同步执行。Linux 下 `EPoller::processPendingEvents()` 调用 `epoll_wait()`，然后逐个处理事件。源码见 [event_poller.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:1115)。

这与 `io_uring` 完成队列模型不同：

- BigWorld：ready -> callback -> 同步处理。
- `io_uring`：submit -> kernel async -> completion -> CQE 消费。

因此，如果要现代化到 `io_uring`，不是替换一个 poller 类那么简单，而是要重构 bundle 收发、生命周期、背压和 handler 执行模型。

## Tick 公平性

接收路径中 `PacketReceiver::handleInputNotification()` 会在一次可读事件中持续调用 `processSocket()`，直到不能继续或超过 `maxSocketProcessingTime`。源码见 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:83)。

这个设计说明开发者已经意识到“单个 socket 排空”可能压垮一帧：

- 继续排空 socket 可以提高吞吐，减少 UDP receive queue 堆积。
- 设置处理时间上限可以保护主循环公平性。
- 超时后停止处理会留下内核队列压力，可能增加延迟或丢包。

这是一种非常游戏服务器式的折中：吞吐不是唯一指标，Tick 预算和尾延迟同样重要。

## 子 Dispatcher

`NetworkInterface::processUntilChannelsEmpty()` 会在 flush 阶段交替处理 main dispatcher 和 interface 自己的 dispatcher。源码见 [network_interface.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/network_interface.cpp:255)。

这说明 Mercury 不是只有一个全局 dispatcher，而是存在主 dispatcher 与网络接口 dispatcher 的组合。文档后续分析 Channel 关闭、可靠包 flush、进程退出时会继续引用这条链路。

## 当时取舍

选择单 Reactor 主线程有明显收益：

- 逻辑执行顺序确定，便于调试实体和脚本行为。
- 避免实体状态在多个线程间频繁加锁。
- Python 2.7 GIL 存在，脚本层并不能靠多线程线性扩展。
- 多进程横向扩展比单进程内多线程更符合 MMO 状态分片。

代价也明显：

- handler 不能阻塞，否则影响整个进程。
- 单个热点 CellApp / BaseApp 会被主线程上限卡住。
- 后台线程只能卸载部分 I/O 和计算，不能解决所有逻辑热点。

## 现代对比

<div class="decision-table">

| 模型 | 特征 | 优点 | 代价 | 对 BigWorld 的适配性 |
| --- | --- | --- | --- | --- |
| 单 Reactor | 一个主线程处理 FD、Timer、逻辑回调 | 顺序确定、调试简单 | 单核瓶颈、handler 不可阻塞 | 当前实际模型 |
| 多 Reactor | 多个 I/O 线程分摊连接 | I/O 扩展更好 | 状态归属复杂 | 对少量 UDP socket 收益有限 |
| Actor | 每个 actor/mailbox 串行处理 | 状态隔离清晰 | 调度器和消息开销 | 适合现代重构，但改造大 |
| Job System | 任务图拆分并行 | CPU 利用率高 | 数据依赖难管 | 适合路径/AI/资源，不适合直接改实体主逻辑 |
| async/await | 异步 I/O 语义清晰 | 适合高并发 I/O | C++ 旧代码迁移重 | 不适合直接套到现有 Mercury |

</div>

## 现代化建议

近期不建议直接把主循环改成多线程或 `io_uring`。更稳妥的路线：

1. 先给 handler 增加耗时观测，确认真实热点。
2. 把可阻塞路径继续下沉到后台任务。
3. 对 UDP 收发尝试批量系统调用实验，例如 `recvmmsg/sendmmsg`。
4. 对单进程热点，通过 Cell 切分和实体迁移解决，而不是先改主线程模型。
5. 如果要引入 actor/job system，应先限定在新模块，不要直接穿透 Entity 主模型。

## 后续问题

本章只解释“事件如何被调度”。下一章会进入网络 I/O 模型本身：为什么源码选择 `epoll`，为什么没有 ET、`io_uring`、`sendmmsg/recvmmsg`，以及今天评估这些方案时应该看哪些指标。
