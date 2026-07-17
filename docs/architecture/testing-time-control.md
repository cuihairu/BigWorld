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

`EventDispatcher::addTimerCommon()` 将微秒转换成 timestamp interval，再用当前 `timestamp()` 加 interval 作为过期时间。见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:275)。

`processTimers()` 直接调用：

```cpp
pTimeQueue_->process( timestamp() );
```

源码见 [event_dispatcher.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:340)。

这意味着测试 Timer、重发、超时、load balancing、Reviver ping 时，默认依赖真实时间流逝。

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

## 当时取舍

为什么没有统一虚拟时钟？高置信推断有几个原因：

- 当时 C++ 游戏服务器更强调集成测试和实际进程运行。
- `timestamp()` inline 全局函数性能优先，抽象层会增加调用成本和侵入。
- 多进程、网络、Python、数据库混合后，完整 deterministic simulation 成本很高。
- 商业引擎可能有内部测试工具，但开源版本未完整包含。

这不是合理化缺口，而是解释当时工程环境下的取舍。

## 现代对比

<div class="decision-table">

| 能力 | BigWorld 当前倾向 | 现代推荐 | 收益 |
| --- | --- | --- | --- |
| 单元测试 | CppUnitLite 风格 | 保留并接 CI | 快速防回归 |
| 多进程测试 | `MultiProcTestCase` | 容器化/进程编排测试 | 更贴近真实集群 |
| 时间控制 | 真实 `timestamp()` | 注入 `IClock` / fake clock | 超时测试确定性 |
| 网络故障 | 部分单测覆盖 | 可编程 packet filter / netem / chaos | 覆盖可靠层边界 |
| 热更新测试 | 未形成显式框架 | EntityDef 兼容矩阵 + migration dry-run | 降低脚本迁移风险 |
| 性能测试 | DogWatch/Watcher | 指标导出 + flamegraph + soak test | 找真实瓶颈 |

</div>

## 现代化建议

1. 不要一开始全局替换 `timestamp()`，先给测试构建加可选 clock seam。
2. 从 `EventDispatcher` 和 `TimeQueue` 开始引入可注入时间源。
3. 给 Mercury 重发、Request timeout、Reviver ping 写 fake clock 测试。
4. 保留真实时间集成测试，补充 fake time 单元测试。
5. 对 GameTime 与 wall clock 的转换建立明确文档和断言。
6. 建立故障注入工具，优先覆盖可靠 UDP、CellApp death、BaseApp restore、热更新失败。
7. 把核心 wire format 和 EntityDef 迁移加入 golden tests。

## 本章边界

本章解释测试与时间控制。下一章分析动态扩展和容灾：BigWorld 如何接纳新 App、广播时间、做负载均衡、通过 Reviver/machined 进行进程级恢复。
