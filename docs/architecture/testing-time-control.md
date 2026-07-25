# 测试体系与可控时间

<div class="arch-hero">

BigWorld 有单元测试、网络协议测试和多进程测试辅助，但它的时间系统主要依赖真实 `timestamp()` 与 `EventDispatcher` Timer。对学习引擎架构来说，这正是一个重要边界：很多 MMO 关键行为难以完全确定性重放。

</div>

## 先给结论

BigWorld 的测试体系不是空白，尤其网络层有不少针对可靠 UDP、分片、乱序、flood、WebSocket、channel version 的测试。但从源码证据看，它没有统一的虚拟时钟注入层。

关键事实：

- 单元测试入口使用 `BWUnitTest::runTest()`，见 [unit_test.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test.hpp:14)。
- 多进程测试辅助 `MultiProcTestCase` 能 fork 多个子进程，见 [multi_proc_test_case.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.hpp:56)。
- `EventDispatcher::addTimerCommon()` 用 `timestamp() + interval` 注册 timer，见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:264)。
- `EventDispatcher::processTimers()` 用真实 `timestamp()` 驱动，见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:340)。
- Linux `timestamp()` 可走 `RDTSC`、`gettimeofday` 或 `clock_gettime`，见 [timestamp.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/timestamp.hpp:46)。
- BaseApp 的 `GameTick` 通过 dispatcher timer 启动，见 [baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:2016)。
- CellAppMgr 的 `GameTick` 通过 dispatcher timer 启动，并创建 `TimeKeeper`，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1774)。

## 测试覆盖画像

项目中存在大量 `unit_test` 目录，包括：

- `lib/network/unit_test`
- `lib/entitydef/unit_test`
- `server/baseapp/unit_test`
- `server/cellapp/unit_test`
- `server/cellappmgr/unit_test`
- `server/reviver/unit_test`
- `lib/unit_test_lib/unit_test`

网络层尤其值得学习：

- `test_reliable.cpp`
- `test_receive_window.cpp`
- `test_fragment.cpp`
- `test_channel_version.cpp`
- `test_flood.cpp`
- `test_piggybacks_longer_than_normal.cpp`
- `test_baseapp_death.cpp`

这些测试说明作者确实把 Mercury 可靠层和异常网络作为重点。

## 多进程测试

`MultiProcTestCase` 是一个比较有价值的测试基础设施。它提供：

- `ChildProcess` 抽象。
- `runChildren()` 启动多个子进程。
- `waitForAll()` 等待子进程。
- `killChildren()` 失败时清理。
- `checkAllChildrenPass()` 汇总结果。

源码见 [multi_proc_test_case.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.hpp:56)。

这对 MMO 服务器很重要，因为很多行为不是单进程函数调用能测出来的：

- Mercury 请求/回复。
- Channel death。
- BaseApp/CellApp 死亡。
- Reviver ping。
- machined birth/death 消息。
- 进程间注册和重连。

## Timer 的真实时间依赖

**概述：** BigWorld 的 Timer 系统依赖真实时间，不是虚拟时钟。这是测试和确定性重放的主要障碍。

**源码入口：** [event_dispatcher.cpp:264](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:264)

```cpp
// event_dispatcher.cpp:264 - Timer 添加
int EventDispatcher::addTimerCommon( int microseconds, 
    TimerHandler * pHandler, void * pUser, const char * name,
    bool isRepeating )
{
    // 1. 微秒转换为 timestamp interval
    uint64 interval = uint64(microseconds) * 1000;
    
    // 2. 计算到期时间（使用真实时间）
    uint64 startTime = timestamp() + interval;
    
    // 3. 插入 TimeQueue64
    TimeQueue64::iterator iter = pTimeQueue_->add( startTime, 
        pHandler, pUser, interval );
    
    return iter->id();
}
```

`processTimers()` 直接调用 `timestamp()` 驱动：

```cpp
// event_dispatcher.cpp:340 - Timer 处理
pTimeQueue_->process( timestamp() );
```

**流程图：**

<MermaidDiagram title="Timer 真实时间依赖">
sequenceDiagram
    participant App as 应用层
    participant Dispatcher as EventDispatcher
    participant TimeQueue as TimeQueue64
    participant System as 系统时间

    App->>Dispatcher: addTimer(100ms)
    Dispatcher->>System: timestamp()
    System->>Dispatcher: 返回当前时间
    Dispatcher->>TimeQueue: add(now + 100ms)
    
    Note over System: 等待 100ms...
    
    App->>Dispatcher: processTimers()
    Dispatcher->>System: timestamp()
    System->>Dispatcher: 返回当前时间
    Dispatcher->>TimeQueue: process(now)
    TimeQueue->>TimeQueue: 检查到期 Timer
    TimeQueue->>App: handleTimeout()
</MermaidDiagram>

**详细讲解：**

1. **真实时间依赖**：Timer 使用 `timestamp()` 驱动，不是虚拟时钟。这意味着测试必须等待真实时间流逝，无法加速或减速时间。

2. **测试障碍**：Timer、重发、超时、load balancing、Reviver ping 都依赖真实时间。测试这些行为需要等待真实时间，速度慢，且无法确定性重放。

3. **为什么不用虚拟时钟**：游戏服务器需要真实时间行为，虚拟时钟会增加复杂度。当时没有统一的虚拟时钟注入层。

## timestamp 的平台问题

`timestamp.hpp` 明确写到 RDTSC 的优点是快，缺点是 CPU 变频场景可能有问题。Linux 路径还支持 `gettimeofday` 和 `clock_gettime`，见 [timestamp.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/timestamp.hpp:7)。

BaseApp 的 `tickGameTime()` 还会在 timing result 异常时提示多核 TSC 不同步问题，见 [baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:1537)。

这反映了旧时代服务器时间源的现实问题：

- RDTSC 快但跨核/变频风险高。
- `gettimeofday` 可能受系统时间调整影响。
- `clock_gettime(CLOCK_MONOTONIC)` 更适合现代服务端。
- 测试依赖真实时间会带来 flaky 风险。

## GameTime 与真实时间

`GameTime` 是游戏 Tick 计数，不等同于 wall clock。它由 `updateHertz` 驱动。

BaseApp 启动 game tick timer：

```cpp
mainDispatcher_.addTimer(1000000 / Config::updateHertz(), ...)
```

源码见 [baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:2020)。

CellAppMgr 也用同样方式启动 game tick，并创建 `TimeKeeper`，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1778)。

CellAppMgr 会向所有 CellApp 发送当前 game time，见 [cellapps.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellapps.cpp:540)。

所以 BigWorld 同时有两套时间：

- `timestamp()`：真实时间，用于 timer、性能统计、超时、重发。
- `GameTime`：游戏逻辑 Tick，用于实体、AOI、备份、回放、同步。

## 可控时间缺口

从当前源码证据看，核心 timer 没有统一 `IClock` 或 `TimeProvider` 注入接口。`timestamp()` 是全局 inline 函数，散布在网络、任务、负载、重发、Reviver、统计和 Tick 中。

这导致：

- 单元测试难以瞬间推进 5 秒超时。
- 可靠 UDP 重发测试容易依赖 sleep 或真实 dispatcher 循环。
- Reviver missed ping 测试耗时且可能 flaky。
- 负载均衡和 overload 检测很难做确定性边界测试。
- Python timer、Entity timer 和 GameTick 的交错顺序难以完全重放。

这不是说引擎无法测试，而是说“可调节时间”不是一等架构抽象。

## 故障注入现状

已有网络测试说明作者关注异常网络：

- 乱序接收窗口。
- 可靠重发。
- 分片。
- flood。
- channel version。
- piggyback 边界。

但要达到现代服务器测试标准，还需要系统化故障注入：

- 丢包率、延迟、抖动、重复包。
- ACK 丢失和 ACK 延迟。
- handler 阻塞。
- background task 卡死。
- CellApp 死亡。
- BaseApp death restore。
- DBApp 超时。
- EntityDef 版本不兼容。
- GameTick late 和 clock jump。

## 源码取舍

源码没有统一虚拟时钟，直接带来几个工程边界：

- `timestamp()` inline 全局函数性能优先，抽象层会增加调用成本和侵入。
- 多进程、网络、Python、数据库混合后，完整 deterministic simulation 成本很高。
- EventDispatcher、TimeQueue、Mercury request timeout、Reviver ping 和 GameTick 都会受到真实时间推进影响。
- 测试需要区分 wall clock、GameTime、dispatcher timer 和 TimeKeeper 校准。

这不是合理化缺口，而是说明哪些源码路径不能简单靠 sleep 断言。

## 源码验证重点

时间相关测试应按时间来源拆分：

- `EventDispatcher::processOnce()` 中 timer 到期顺序应稳定。
- `TimeQueueT::process()` 应正确处理一次性 timer、循环 timer、取消 timer 和 callback 内取消。
- Mercury request timeout、reliable resend 和 Reviver ping 应覆盖正常到期、延迟和取消。
- GameTick 的 `GameTime` 推进不应被误当成 wall clock。
- `TimeKeeper` 调整 tracking timer interval 后应能恢复 nominal interval。
- handler 阻塞、background task 卡死、CellApp death、DBApp 超时应分别验证对 dispatcher 时间和 GameTime 的影响。
- EntityDef 版本不兼容、登录超时和 BaseApp restore 应避免依赖不可控 sleep 形成脆弱测试。

## 本章边界

本章解释测试与时间控制。下一章分析动态扩展和容灾：BigWorld 如何接纳新 App、广播时间、做负载均衡、通过 Reviver/machined 进行进程级恢复。
