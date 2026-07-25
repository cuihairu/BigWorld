# Watcher、Profiler 与日志

<div class="arch-hero">

BigWorld 的可观测性围绕 Watcher 树、Profiler、DogWatch、EntityProfiler、Message Logger 和管理工具构建。它不是单纯日志聚合，而是把运行时对象、进程控制面、性能采样和集中日志放进同一套引擎工具链里。

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

## Watcher 网络协议

Watcher 不只是本地 `Watcher::rootWatcher()`。服务进程可以通过 `WatcherNub` 把 watcher 树暴露给外部工具。

关键源码事实：

- `WatcherNub::init()` 同时创建 UDP socket 和 TCP socket，并尝试把两者绑定到同一个端口，见 [watcher_nub.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_nub.cpp:47) 和 [watcher_nub.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_nub.cpp:121)。
- `WatcherNub::attachTo()` 把 UDP/TCP fd 注册到 `EventDispatcher`，名字分别是 `WatcherUDP` 和 `WatcherTCP`，见 [watcher_nub.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_nub.cpp:212)。
- `WatcherNub::notifyMachineGuard()` 会向本机 machined 注册 watcher nub 的端口、进程 ID、简称和版本号，见 [watcher_nub.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_nub.cpp:222)。

Watcher 协议本身支持 GET/SET/TELL 两代消息：

- v1：`WATCHER_MSG_GET`、`WATCHER_MSG_SET`、`WATCHER_MSG_TELL`。
- v2：`WATCHER_MSG_GET2`、`WATCHER_MSG_SET2`、`WATCHER_MSG_TELL2`、`WATCHER_MSG_SET2_TELL2`。

定义见 [watcher_nub.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_nub.hpp:43)。

`WatcherPacketHandler` 负责把多个 path request 聚合成一个回复包，并处理 UDP 包大小限制：

- v1 UDP 回复包会在超过限制时写入 `<Err>` 和 `Exceeded maximum packet size`。
- v2 对 TCP 使用更大的 `WN_PACKET_SIZE_TCP`，对 UDP 仍受 `WN_PACKET_SIZE` 限制。

源码见 [watcher_packet_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_packet_handler.cpp:25) 和 [watcher_packet_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_packet_handler.cpp:114)。

这解释了 Watcher 的几个工程特征：

- 它是运行时远程 introspection 协议，不是单纯指标拉取协议。
- 它允许 SET，所以天然带控制面风险。
- 它依赖 machined 发现，不是所有工具都需要静态配置每个端口。
- 大 watcher 子树查询可能撞到 UDP 包大小，需要 TCP 或路径收敛。

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

安全分析时应把 Watcher 明确拆成“只读观测”和“可写控制”两类权限。

## Watcher 编码边界

v2 Watcher 数据不是 JSON，而是二进制类型流。

`WatcherProtocolDecoder::decodeNext()` 每次读取 `type` 和 `mode`，再按 watcher 类型分发到对应 handler，见 [watcher_protocol.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/watcher_protocol.cpp:22)。

当前支持的类型包括：

- `WATCHER_TYPE_INT`
- `WATCHER_TYPE_UINT`
- `WATCHER_TYPE_FLOAT`
- `WATCHER_TYPE_BOOL`
- `WATCHER_TYPE_STRING`
- `WATCHER_TYPE_TUPLE`

源码见 [watcher_protocol.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/watcher_protocol.cpp:44)。

`defaultHandler()` 会读取长度并校验剩余 stream，不允许越界读取，见 [watcher_protocol.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/watcher_protocol.cpp:78)。

这说明 Watcher 的安全边界不只是“有没有认证”。还包括：

- path 是否允许写。
- 类型和 mode 是否匹配。
- 长度字段是否可信。
- 单次请求是否可能放大成大量 forwarded watcher 请求。
- 返回包是否可能过大导致丢失或截断。

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

端到端链路如下：

<MermaidDiagram title="日志转发链路">
flowchart TD
  A[TRACE/DEBUG/INFO/WARNING/ERROR 宏] --> B[DebugFilter]
  B --> C[DebugMessageCallback]
  C --> D[LoggerMessageForwarder]
  D --> E[LoggerEndpoint TCP/UDP]
  E --> F[message_logger Logger]
  F --> G[LogStorage]
  G --> H[MLDB 文件后端或 MongoDB 后端]
</MermaidDiagram>

源码证据：

- `DebugFilter::handleMessage()` 会把消息交给注册的 `DebugMessageCallback`，见 [debug_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/debug_filter.cpp:138)。
- `LoggerMessageForwarder` 构造时调用 `DebugFilter::instance().addMessageCallback(this)`，把自己挂到日志回调链，见 [logger_message_forwarder.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/logger_message_forwarder.cpp:165)。
- `FindLoggerHandler` 通过 machined 的 `ProcessStatsMessage` 找到 MessageLogger 地址并添加 logger endpoint，见 [logger_message_forwarder.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/logger_message_forwarder.cpp:96)。
- `LoggerEndpoint` 负责连到远端 MessageLogger，并维护 TCP/UDP、重连、缓冲队列和 dropped message 计数，见 [logger_endpoint.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/logger_endpoint.hpp:18)。
- `Logger` 是 `message_logger` 进程主体，负责接收组件日志、注册组件、处理断开和写入存储，见 [logger.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/logger.hpp:18)。

这条链路体现了几个取舍：

- 业务进程不直接写集中日志文件，而是把日志作为网络消息转发。
- MessageLogger 可以按 UID、组件名、LoggerID、优先级过滤，见 [logger.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/logger.cpp:42)。
- Logger 进程自己也暴露 watcher，例如 `size`、`reattachAll`、`filter/TRACE` 到 `filter/CRITICAL`，见 [logger.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/logger.cpp:141)。

## MessageLogger 存储模型

`message_logger` 不是一个单一文本文件 writer。它有抽象存储层：

- `LogStorage` 定义 `addLogMessage()`、`writeLogToDB()`、`roll()`、`validateNextHostname()`、`setAppInstanceID()` 等接口，见 [log_storage.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/log_storage.hpp:19)。
- `LogStorage::addLogMessage()` 会读取网络 header、消息来源、优先级、格式串，再解析 host、category，最后交给后端写入，见 [log_storage.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/log_storage.cpp:43)。
- `mldb` 是本地文件型存储后端，目录在 [mldb/log_storage.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/mldb/log_storage.hpp:1)。
- MongoDB 后端由 `LogStorageMongoDB` 实现，支持缓冲、后台 TaskManager、重连、roll、过期清理和 BSON 构造，见 [mongodb/log_storage.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/mongodb/log_storage.hpp:32)。

MongoDB 后端还有一个重要运行时边界：

- 如果连接断开，`LogStorageMongoDB::addLogMessage()` 会暂停写日志并排入 `ReconnectTask`，连接恢复后再恢复 logging，见 [mongodb/log_storage.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/mongodb/log_storage.cpp:220)。

这说明 MessageLogger 是一个独立的日志数据库服务，而不是“每个进程 printf 到文件”。它可以集中查询，但也引入了自己的可靠性问题：

- LoggerEndpoint 缓冲满会丢消息。
- MessageLogger 进程故障会影响集中日志。
- MongoDB 连接故障时会暂停写入。
- 日志格式串和元数据协议需要前后端版本兼容。

## 日志字段与缺口

BigWorld 的日志已经有一些结构化雏形：

- `LoggerMessageHeader` 包含 component priority、message priority、message source、category，见 [logger_message_forwarder.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/logger_message_forwarder.cpp:67)。
- `LoggerComponentMessage` 包含 version、loggerID、uid、pid、componentName，见 [logger_message_forwarder.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/logger_message_forwarder.cpp:84)。
- `LogStorage::resolveUID()` 会通过 machined 把 uid 解析成 username，见 [log_storage.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/tools/message_logger/log_storage.cpp:107)。

但它还不是现代意义上的完整结构化日志：

- 结构化日志字段。
- trace/correlation id。
- EntityID、SpaceID、CellAppID、Channel addr 等关键上下文统一规范。
- 日志采样和动态级别控制。
- 与指标和 profiler 事件关联。

所以要区分两件事：

- BigWorld 已经有集中日志和部分结构化 metadata。
- BigWorld 没有统一的跨进程 trace/span 模型，也没有强制每条关键日志都携带 EntityID/DBID/SpaceID/SessionKey。

## 源码职责划分

这章不能把 Watcher、Profiler 和 Message Logger 混成一个“监控系统”。源码里的职责边界更细：

- Watcher 是运行时对象树和管理协议，路径可以读，也可能写或执行 TELL。
- `ForwardingWatcher` 是跨进程转发器，能把 watcher command 发到 all、leastLoaded 或指定进程集合。
- `Profiler` / `DogWatch` 是进程内耗时采样和分段统计，输出服务性能分析。
- `EntityProfiler` 直接服务实体负载分析，和 CellApp 负载均衡输入有交集。
- `LoggerEndpoint` 和 Message Logger 是集中日志链路，负责格式串、进程元数据、优先级、category 和存储后端。

因此源码分析时要先判断问题属于哪条链路：

- 想看运行时变量，查 watcher path 和 setter/TELL 权限。
- 想看 tick 内耗时，查 profiler scope 和 `DogWatch` 分段。
- 想看实体造成的 CellApp 负载，查 EntityProfiler 与 load 上报。
- 想看跨进程故障，查 Message Logger 是否收到完整日志，以及后端是否暂停写入。

## 源码边界

几个边界直接影响线上排障：

- Watcher 不是只读指标协议；SET/TELL 路径必须按控制面入口处理。
- Watcher UDP 回复受包大小限制，路径树过大或返回内容过长时不能假设一定完整。
- ForwardingWatcher 会改变命令作用范围，排障时要区分本进程 watcher 和转发 watcher。
- `LoggerEndpoint` 在 Message Logger 不可用或缓冲满时可能丢日志，不能把集中日志当成强可靠审计日志。
- MongoDB 后端断连时会暂停写入并排重连任务，恢复前日志可见性会下降。
- 日志已有 component、priority、source、category 等元数据，但源码没有统一 trace/span 模型。

## 源码验证重点

可观测性改造的测试不能只看“能看到指标”。至少要覆盖：

- Watcher v1/v2 GET/SET/TELL 兼容性。
- UDP watcher 回复包超过限制时的行为。
- ForwardingWatcher 对 all、leastLoaded、指定 ID 列表的转发结果。
- 可写 watcher 的权限、审计和失败回滚。
- LoggerEndpoint 在 MessageLogger 不可用、重连和缓冲满时的丢消息行为。
- MLDB/MongoDB 后端的 roll、过期清理和查询兼容性。
- DebugFilter category suppression 是否能动态生效。
- EntityProfiler load 是否和 Cell 负载均衡输入一致。

## 本章边界

本章解释可观测性设施。下一章分析内存和生命周期：BigWorld 大量使用自定义 SmartPointer、ReferenceCount、PyObjectPlus、Packet/Bundle 和实体状态转换，生命周期本身就是架构复杂度来源。
