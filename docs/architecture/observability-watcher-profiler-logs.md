# Watcher、Profiler 与日志

<div class="arch-hero">

BigWorld 的可观测性不是 Prometheus + OpenTelemetry 这类现代标准栈，而是围绕 Watcher 树、Profiler、DogWatch、Message Logger 和管理工具构建的运行时 introspection 系统。它更贴近商业游戏引擎的调试、运营和现场排障需求。

</div>

## 先给结论

BigWorld 的可观测性非常有研究价值，因为它不是事后外挂的日志，而是深度嵌入引擎对象模型：

- Watcher 提供树状路径，可读写运行时变量。
- Manager 进程通过 Watcher 暴露 App、Space、Cell、load、DB 等状态。
- `ForwardingWatcher` 支持把 watcher 请求转发到 BaseApp/CellApp 集合。
- `PacketReceiver` 暴露网络 stats 和 socket 处理时间预算。
- Profiler 支持 C++/GPU/Python 分类、层级统计、hitch detection、JSON/CSV 输出。
- EntityProfiler 直接把实体执行时间转成 load，进入负载均衡决策。
- Message Logger 是独立工具链，不只是进程 stdout。

关键源码：

- Watcher 类型系统在 [watcher.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/watcher.hpp:35)。
- `PacketReceiver::pWatcher()` 在 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:1367)。
- `BaseAppMgr::addWatchers()` 在 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:705)。
- `Space::pWatcher()` 在 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:932)。
- `ForwardingWatcher` 在 [watcher_forwarding.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/watcher_forwarding.cpp:64)。
- Profiler 宏与事件模型在 [profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/profiler.hpp:43)。
- EntityProfiler 在 [entity_profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/entity_profiler.hpp:38)。
- Guard/Profiler/MemTracker 组合宏在 [guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/guard.hpp:13)。

## Watcher 是什么

Watcher 可以理解为 BigWorld 内建的“运行时对象树”。

它不是单纯指标系统，而是同时具备：

- 读：查询变量、统计和对象状态。
- 写：修改可写配置或触发命令。
- 路径：用类似目录的路径组织对象。
- 类型：支持 int、uint、float、bool、string、tuple、type。
- 远程访问：通过 watcher network 协议读取其他组件。

Watcher 类型定义见 [watcher.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/watcher.hpp:40)。

`WATCHER_SEPARATOR` 是 `/`，说明 Watcher 的组织方式天然是路径树。见 [watcher.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/watcher.hpp:70)。

## Watcher 树形模型

<MermaidDiagram title="Watcher 观测模型">
flowchart TD
  Root[Watcher root] --> ServerApp[ServerApp common watchers]
  Root --> Mgr[BaseAppMgr / CellAppMgr]
  Mgr --> Apps[baseApps / cellApps / dbApps]
  Mgr --> Loads[load min avg max]
  Mgr --> Forward[forwardTo]
  Forward --> Remote[remote component watcher]
  Root --> Network[NetworkInterface / PacketReceiver]
  Network --> Stats[packet stats]
  Network --> Budget[maxSocketProcessingTime]
  Root --> Space[Space]
  Space --> Cells[cells / bsp / geometry / load]
</MermaidDiagram>

设计含义：

- 可观测性按运行时对象组织，而不是按文件或日志关键字组织。
- Manager 能看到集群控制面的关键数据。
- 负载均衡、空间划分、网络处理预算都可以被运行时观察。
- Watcher 既是调试工具，也是运维控制面的一部分。

## Manager Watcher

`BaseAppMgr::addWatchers()` 暴露：

- `numBaseApps`
- `numServiceApps`
- `numBases`
- `numProxies`
- `config/shouldShutDownOthers`
- `baseAppLoad/min`
- `baseAppLoad/average`
- `baseAppLoad/max`
- `serviceAppLoad/min`
- `serviceAppLoad/average`
- `serviceAppLoad/max`

源码见 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:705)。

这些 watcher 直接对应运营关心的问题：

- 当前有多少 BaseApp。
- 当前有多少在线代理。
- BaseApp 是否负载不均。
- 是否需要迁移 Base 或扩容进程。

CellAppMgr 的 Space watcher 暴露：

- `id`
- `cells`
- `areaNotLoaded`
- `numRetiringCells`
- `loadMin`
- `loadMax`
- `loadAvg`
- `artificialMinLoad`
- `numCells`
- `geometry`
- `bsp`

源码见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:932)。

这说明 BigWorld 对 Cell 空间划分不是黑箱。空间、BSP、负载和退休 Cell 都能被观察。

## ForwardingWatcher

`ForwardingWatcher` 支持将 watcher 请求转发到目标组件集合。它定义了几个目标：

- `cellApps`
- `baseApps`
- `serviceApps`
- `baseServiceApps`
- `leastLoaded`

源码见 [watcher_forwarding.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/watcher_forwarding.cpp:10)。

`setFromStream()` 解析路径时支持：

- `all/command/addBot`
- `leastLoaded/command/addBot`
- `1,2,45,87/command/addBot`

源码注释见 [watcher_forwarding.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/watcher_forwarding.cpp:64)。

这个设计很强，但也有风险：

- 强：可以从 Manager 对多组件执行统一 watcher 操作。
- 风险：如果写权限暴露到不可信网络，会变成远程管理入口。

现代化时应把 Watcher 明确拆成“只读观测”和“可写控制”两类权限。

## 网络观测

`PacketReceiver::pWatcher()` 暴露：

- `stats`
- `maxSocketProcessingTimeStamps`

源码见 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:1372)。

`maxSocketProcessingTime()` 注释明确说明：

- 非零时限制一次 socket 输入通知允许处理的时间。
- 为零时不限制，会处理到 receive queue 为空。

源码见 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:1400)。

这对学习网络模型很关键。BigWorld 不只是用 epoll 等待事件，它还通过可观测/可调参数限制一次网络处理占用主线程的时间。

## Profiler

Profiler 不是简单计时器。`profiler.hpp` 暴露了多种模式：

- `HIERARCHICAL`
- `SORT_BY_TIME`
- `SORT_BY_NUMCALLS`
- `SORT_BY_NAME`
- `CPU_GPU`
- `GRAPHS`
- `CORES`

源码见 [profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/profiler.hpp:130)。

事件类别包括：

- `CATEGORY_CPP`
- `CATEGORY_GPU`
- `CATEGORY_PYTHON`

源码见 [profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/profiler.hpp:162)。

宏层包括：

- `PROFILER_SCOPED`
- `PROFILER_COUNTER`
- `PROFILE_FILE_SCOPED`
- `PROFILER_IDLE_SCOPED`
- `PROFILER_LEVEL_COUNTER`

源码见 [profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/profiler.hpp:43)。

这说明 BigWorld 关注的不只是“哪里慢”，还包括：

- 调用层级。
- 调用次数。
- idle 时间。
- 文件 I/O。
- Python 与 C++ 分类。
- hitch detection。

## EntityProfiler 与负载

`EntityProfiler` 是 BigWorld 很有代表性的设计：它把实体执行耗时变成 load，供 Cell 负载均衡使用。

源码中 `EntityProfiler` 暴露：

- `load()`
- `rawLoad()`
- `maxRawLoad()`
- `artificialMinLoad()`
- `tick()`

并使用指数平滑计算 load。见 [entity_profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/entity_profiler.hpp:53)。

`AUTO_SCOPED_ENTITY_PROFILE` 使用 RAII 在作用域进入/退出时 start/stop，见 [entity_profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/entity_profiler.hpp:13)。

设计含义：

- 负载均衡不是只看实体数量。
- 单个昂贵实体会反映到 load。
- 人工最小负载可用于测试或调参。
- 观测数据直接影响调度策略。

## Guard、Stack Tracker 与 MemTracker

`guard.hpp` 定义了组合宏：

- `BW_GUARD`
- `BW_GUARD_PROFILER`
- `BW_GUARD_MEMTRACKER`
- `BW_GUARD_PROFILER_MEMTRACKER`

源码见 [guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/guard.hpp:13)。

这类宏在旧 C++ 游戏引擎里很常见，作用是：

- 函数级栈跟踪。
- Profiling 作用域。
- 内存跟踪作用域。
- 崩溃或断言时提供上下文。

它的优势是侵入式但低门槛，代价是宏散布和编译开关复杂。

## 日志系统

仓库中存在独立工具链 `server/tools/message_logger`。这说明 BigWorld 的日志不是仅靠每个进程写 stdout，而是有集中消息日志工具。

从架构角度看，Message Logger 的价值在于：

- 多进程集群需要统一收集日志。
- MMO 问题往往跨 BaseApp、CellApp、DBApp、Mgr。
- 只看单进程日志无法还原实体迁移、Channel death、登录链路。

但现代化时仍需补齐：

- 结构化日志字段。
- trace/correlation id。
- EntityID、SpaceID、CellAppID、Channel addr 等关键上下文统一规范。
- 日志采样和动态级别控制。
- 与指标和 profiler 事件关联。

## 当时为什么不是 Prometheus / OTel

原因很直接：

- BigWorld 的主要设计年代早于 Prometheus 和 OpenTelemetry。
- 游戏引擎更需要现场调试和运行时对象 introspection，而不只是指标拉取。
- Watcher 的读写能力更接近“运维控制台”，这不是 Prometheus 的定位。
- Profiler 和 EntityProfiler 直接服务性能调优和负载均衡。

不能因为它不是现代标准栈就说它落后。Watcher 的运行时对象树对游戏服务器仍有学习价值。

真正的问题是：现代生产环境需要把这些能力接到标准观测系统，并加权限边界。

## 现代方案对比

<div class="decision-table">

| 能力 | BigWorld | 现代常见方案 | 判断 |
| --- | --- | --- | --- |
| 运行时变量 | Watcher path tree | admin API / config service | Watcher 表达力强，但权限弱 |
| 指标 | Watcher stats / DogWatch | Prometheus / StatsD | 应增加只读指标导出 |
| Trace | 日志 + 手工上下文 | OpenTelemetry trace | BigWorld 缺少跨进程 trace |
| Profiling | 内建 Profiler | perf/flamegraph/eBPF/pprof | 内建 profiler 可保留，外部工具补盲区 |
| 实体负载 | EntityProfiler | actor mailbox metrics / scheduler cost | BigWorld 很先进，值得保留 |
| 日志 | message_logger | ELK/Loki/ClickHouse | 需要结构化和关联 ID |
| 控制面 | Watcher 写路径 | RBAC admin API | 现代化必须拆权限 |

</div>

## 现代化建议

优先级建议：

1. 保留 Watcher，但默认只导出只读路径到指标系统。
2. 将可写 Watcher 路径列成高危清单，必须有认证、授权、审计和网络隔离。
3. 给 BaseApp/CellApp/DBApp 所有关键消息增加 correlation id 或至少统一上下文字段。
4. 将 EntityProfiler、Space load、Network stats、TaskManager 队列长度导出为标准指标。
5. 对 Profile JSON/CSV 输出建立离线分析流程，和 flamegraph/eBPF 互补。
6. 为实体迁移、热更新、BaseApp death、DB 写入建立跨进程事件链路。
7. 不要用日志替代指标，也不要用指标替代 Watcher；三者职责不同。

## 本章边界

本章解释可观测性设施。下一章分析内存和生命周期：BigWorld 大量使用自定义 SmartPointer、ReferenceCount、PyObjectPlus、Packet/Bundle 和实体状态转换，生命周期本身就是架构复杂度来源。
