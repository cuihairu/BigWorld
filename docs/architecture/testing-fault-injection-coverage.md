# 测试体系、故障注入与覆盖边界

<div class="arch-hero">

BigWorld 并不是“几乎没测试”的老引擎。它已经有一套可运行的 `unit_test` 体系，网络层还内建人工丢包/延迟能力，并覆盖了可靠 UDP、overflow、channel 切换、ghost 消息缓冲和 replay 文件读写等关键路径。但这些能力并不等于现代意义上的统一集成测试或 chaos framework。

</div>

## 先给结论

BigWorld 当前测试体系的真实画像是：

1. 有统一单元测试入口，核心基础库和多类服务端组件都存在 `unit_test` 目录。
2. 网络层测试最成熟，尤其关注可靠 UDP、重传、分片、overflow、flood 和 channel 切换。
3. 已有多进程测试基础设施 `MultiProcTestCase`，但实际测试里并不是所有场景都真的使用多进程。
4. 故障注入主要集中在网络层的人为丢包、延迟、超时和 watchdog，不是完整的系统级混沌测试。
5. 大量“分布式”测试本质上仍是在单进程、单 `EventDispatcher` 内模拟多个 `NetworkInterface`。

源码入口：

- 测试统一入口 `BWUnitTest::runTest()` 在 [unit_test.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test.hpp:14) 和 [unit_test.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test.cpp:40)。
- `MultiProcTestCase` 在 [multi_proc_test_case.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.hpp:60) 和 [multi_proc_test_case.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.cpp:33)。
- 网络层人工丢包/延迟在 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:158)。
- LoginApp 外部接口人工 loss/latency 开关在 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:308)。
- 可靠重传测试中的 `dropNextSend()` 样例在 [test_reliable.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_reliable.cpp:415)。

## 测试框架入口

BigWorld 没有把所有测试散成一堆 ad-hoc 可执行程序，而是有统一入口：

- `BWUnitTest::runTest( testName, argc, argv )` 负责运行注册到 `CppUnitLite2` 的测试集合，见 [unit_test.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test.cpp:40)。
- 默认关闭控制台调试输出，传 `-v` 或 `--verbose` 才放开，见 [unit_test.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test.cpp:44)。
- 非服务端构建还支持 `--xml` / `-x` 输出测试结果，见 [unit_test.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test.cpp:60)。

各模块测试程序都走这套入口：

- `lib/network/unit_test/main.cpp` 调用 `BWUnitTest::runTest( "network", ... )`，见 [main.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/main.cpp:12)。
- `server/baseapp/unit_test/main.cpp` 调用 `BWUnitTest::runTest( "baseapp", ... )`，见 [main.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/unit_test/main.cpp:9)。
- `server/cellapp/unit_test/main.cpp` 调用 `BWUnitTest::runTest( "cellapp", ... )`，见 [main.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/unit_test/main.cpp:8)。

这意味着项目并不缺测试执行机制，真正问题在于覆盖面和场景真实性，而不是“连测试框架都没有”。

## 测试分布地图

从目录看，`unit_test` 覆盖面其实很广：

- `lib/network/unit_test`
- `lib/entitydef/unit_test`
- `lib/cstdmf/unit_test`
- `lib/resmgr/unit_test`
- `lib/server/unit_test`
- `server/baseapp/unit_test`
- `server/cellapp/unit_test`
- `server/baseappmgr/unit_test`
- `server/cellappmgr/unit_test`
- `server/dbapp/unit_test`
- `server/loginapp/unit_test`
- `server/reviver/unit_test`

但覆盖深度并不均衡。高置信结论是：

- `lib/network/unit_test` 最系统化，测试文件数量明显最多。
- `entitydef`、`baseapp replay`、`cellapp ghost buffering` 也有相对成体系的场景。
- 多个 server 目录虽然有 `unit_test/main.cpp`，但不一定已经沉淀出同等丰富的测试集合。

## 测试层次

<MermaidDiagram title="BigWorld 测试层次">
flowchart TD
  A[CppUnitLite2 / BWUnitTest] --> B[基础类型与序列化测试]
  A --> C[网络协议与 Channel 测试]
  A --> D[服务端局部模块测试]
  A --> E[多进程测试基础设施]
  C --> F[artificial loss / latency]
  C --> G[timeout / watchdog]
  D --> H[replay 文件与 checksum]
  D --> I[ghost message 排列组合]
  E --> J[fork / waitpid / child status]
</MermaidDiagram>

这个分层很关键。BigWorld 的测试重点更像“协议与运行时部件正确性”，而不是“拉起完整集群做统一黑盒回归”。

## 多进程测试基础设施

`MultiProcTestCase` 是当前测试体系里最有价值的一层基础设施之一：

- `runChildren()` 按数量创建子进程，见 [multi_proc_test_case.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.cpp:33)。
- `runChild()` 在 `MF_SERVER` 下直接 `fork()`，子进程执行 `ChildProcess::run()`，见 [multi_proc_test_case.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.cpp:54)。
- `killChildren()` 失败时会 `SIGKILL` 所有子进程，见 [multi_proc_test_case.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.cpp:92)。
- `waitForAll()` 先跑主进程逻辑，再等待全部子进程退出，见 [multi_proc_test_case.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.cpp:156)。
- `checkAllChildrenPass()` 汇总退出码或信号，见 [multi_proc_test_case.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.cpp:203)。

辅助宏 `MULTI_PROC_TEST_CASE_WAIT_FOR_CHILDREN` 会在 `TEST()` 里统一等待并断言，见 [multi_proc_test_case.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/multi_proc_test_case.hpp:163)。

项目里还带了一个示例测试，展示如何 fork 多个子进程并等待收尾，见 [test_multi_proc_test_case.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/unit_test_lib/unit_test/test_multi_proc_test_case.cpp:83)。

## 重要现实边界：不是所有“分布式测试”都真的是多进程

这个边界必须明确写出来。

例如 `test_flood.cpp` 虽然看起来是在模拟 server/client 压力，但当前 `TEST( TestFlood_testFlood )` 中：

- 直接在同一个 `EventDispatcher` 里构造 `FloodServerApp` 和多个 `FloodClientApp`，见 [test_flood.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_flood.cpp:850)。
- 注释里还能看到原本打算用 `MULTI_PROC_TEST_CASE_WAIT_FOR_CHILDREN`，但当前已被注释掉，见 [test_flood.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_flood.cpp:882)。

这意味着很多“网络交互测试”验证的是 Mercury 逻辑与 dispatcher 驱动，而不是 OS 级多进程隔离、真实调度抖动、端口竞争或机器间时钟差异。

这不是否定这些测试，而是明确它们的真实性边界。

## 现有故障注入能力

网络层已经内建故障注入：

- `PacketSender::rescheduleSend()` 支持按概率人工丢包，或仅丢下一包，见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:161)。
- 同一函数也支持人工延迟发送，延迟大于等于 2ms 时通过 `RescheduledSender` 延后发出，见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:206)。
- 发送遇到 `EAGAIN` 或 `ENOBUFS` 时，`basicSendWithRetries()` 会等待 10ms 再重试，见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:304)。
- LoginApp 初始化时也可以直接给外部接口开启 latency/loss，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:308)。

这类故障注入适合验证：

- ACK 丢失与重传。
- 多包 bundle 中单包丢失。
- 外部网络抖动下的登录重试。
- flood / overflow / watchdog 行为。

但它主要仍是“网络层注入”，不是完整系统级故障平台。

## 已明确覆盖的代表性场景

### 可靠 UDP 重传

`test_reliable.cpp` 会主动 `dropNextSend()`，然后检查额外 resend 与 watcher 统计，见 [test_reliable.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_reliable.cpp:400)。

这类测试的价值很高，因为它验证的是：

- 丢包后是否重发。
- resend 统计是否正确。
- piggyback 行为是否符合预期。

### Channel auto-switch

`test_auto_switch.cpp` 在单 dispatcher 中模拟 BaseApp 与两个 CellApp，并把旧 channel 状态流式迁移到新 channel，再验证最终消息数量一致，见 [test_auto_switch.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_auto_switch.cpp:99)。

这类测试不是纯函数级别，而是协议状态机级别。

### BaseApp death / channel reset

`test_baseapp_death.cpp` 明确写着它在模拟 baseapp death 后实体 channel 的切换恢复，见 [test_baseapp_death.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_baseapp_death.cpp:1)。

它通过多个 `NetworkInterface` 与定时 watchdog 模拟 CellApp、BaseApp1、BaseApp2 之间的切换，测试入口在 [test_baseapp_death.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_baseapp_death.cpp:207)。

### Channel overflow

`test_overflow.cpp` 会发送超过窗口大小的数据，验证 overflow 情况下 dispatcher 和 channel 是否按预期工作，见 [test_overflow.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_overflow.cpp:40)。

### Flood 与 watchdog

`test_flood.cpp` 用高频 `msg1/msg2`、固定 payload 和 watchdog timer 测试压力场景：

- server 侧 `watchTimerHandle_` 超时会直接 `FAIL( "Timed out" )`，见 [test_flood.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_flood.cpp:232)。
- 收到错误发送结果时会 condemn channel，见 [test_flood.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_flood.cpp:282)。

### EntityDef BinaryStream round-trip

`lib/entitydef/unit_test/test_stream.cpp` 有一整组按数据类型分层的 stream round-trip 测试，例如基础类型、`ARRAY`、`TUPLE`、`FIXED_DICT` 等，入口从 [test_stream.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/unit_test/test_stream.cpp:324) 开始。

这证明 EntityDef 并不是“全靠人工回归”，序列化核心层已有较强单元测试支撑。

### Replay / Recording

`server/baseapp/unit_test/test_recording.cpp` 覆盖：

- `ReplayChecksumScheme_Basic`
- `ReplayHeader_Basic`
- `ReplayDataFileWriter_Basic`
- `ReplayDataFileReader_BasicReading`
- `ReplayTickLoader_Basic`

这些测试入口分布在 [test_recording.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/unit_test/test_recording.cpp:644)、[test_recording.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/unit_test/test_recording.cpp:720)、[test_recording.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/unit_test/test_recording.cpp:799) 和 [test_recording.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/unit_test/test_recording.cpp:1649)。

这说明 BigWorld 对回放文件格式、签名和 reader/writer 一致性是有明确测试意识的。

### Ghost 消息缓冲的排列组合

`server/cellapp/unit_test/test_buffered_ghost_messages.cpp` 最有意思的地方不是某个单例测试，而是 `TestRunner::visitPermutations()` 会把同一实体生命周期内不同来源地址的消息到达顺序做排列组合遍历，见 [test_buffered_ghost_messages.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/unit_test/test_buffered_ghost_messages.cpp:527)。

测试入口在 [test_buffered_ghost_messages.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/unit_test/test_buffered_ghost_messages.cpp:689)。

这类测试非常贴近 MMO 真实难点，因为 ghost 相关 bug 往往不是“某个函数返回错”，而是乱序消息下状态机走错。

## 覆盖边界

从源码证据出发，可以明确写出几个边界：

### 1. 大量交互测试仍是单进程仿真

高置信事实：

- 很多测试在一个 `EventDispatcher` 内挂多个 `NetworkInterface`。
- flood 测试当前没有启用 `MultiProcTestCase`。
- BaseApp death / auto-switch / overflow 等也主要是单进程驱动。

这类测试很好地覆盖了 Mercury 状态机，但不能等价替代：

- 真实多进程调度。
- 进程崩溃后的 OS 资源回收。
- socket backlog、端口复用和内核队列差异。
- 跨机器时间漂移和网络抖动。

### 2. 故障注入主要集中在网络层

已有：

- artificial loss
- artificial latency
- drop next send
- watchdog timeout
- channel condemn

缺少统一抽象的场景：

- DBApp 卡死或慢查询注入。
- 跨进程网络分区。
- manager / machined discovery 异常矩阵。
- clock jump / monotonic drift。
- Python GIL 饥饿或后台线程长阻塞。

### 3. 缺少统一 cluster 级黑盒回归框架

当前测试更像模块级和协议级验证。它们可以证明关键库和局部状态机是对的，但不能直接证明：

- 一整套 BaseApp/CellApp/DBApp/LoginApp/machined/Reviver 组合在长期运行下稳定。
- 脚本热更新、EntityDef 变更、DB 恢复和实体迁移一起发生时没有联动回归。

### 4. 与时间系统仍然强耦合

[测试体系与可控时间](/architecture/testing-time-control) 已经说明：核心 timer 主要依赖真实 `timestamp()`。因此很多带超时、retry、watchdog 的测试仍然要围绕真实 dispatcher 循环组织，而不是完全用 fake clock 做瞬时推进。

## 当时取舍

从当时背景看，这套测试体系的取舍是合理的：

- 优先把最危险的 Mercury 协议层和实体状态迁移问题测住。
- 通过 `unit_test_lib` 统一执行入口，降低模块加测试的门槛。
- 在服务器环境下保留 `fork()` 能力，允许做多进程测试。
- 用人工丢包/延迟代替外部网络仿真设施，降低依赖成本。

代价也很明确：

- 单进程仿真和真实分布式部署存在差距。
- chaos 范围主要停留在网络层。
- 真实时间依赖让长超时、重试和恢复测试不够彻底。
- 集群级端到端回归没有被同样系统化。

## 现代对比

<div class="decision-table">

| 能力 | BigWorld 当前能力 | 现代推荐 | 判断 |
| --- | --- | --- | --- |
| 单元测试入口 | `BWUnitTest` + `CppUnitLite2` | 保留并接 CI | 已有基础，不必推倒重来 |
| 多进程测试 | `fork()` + `waitpid()` | 容器化集成测试 | 当前能力可继续用来构建更高层场景 |
| 网络故障注入 | artificial loss / latency | netem / chaos mesh / toxiproxy | 当前足够做协议单测，但不够做系统 chaos |
| 协议状态机测试 | 可靠 UDP、overflow、switch、ghost | 保持并扩展 golden tests | 这是当前最强资产之一 |
| 集群黑盒回归 | 较弱 | 编排式端到端测试 | 需要补齐 |
| 时间控制 | 真实 dispatcher 时间 | fake clock + deterministic scheduler | 应与测试体系章节配合推进 |

</div>

## 现代化建议

1. 不要废弃现有 `BWUnitTest`，先把现有测试稳定纳入 CI。
2. 把 `MultiProcTestCase` 真正用到关键跨进程场景，而不是只停留在基础设施和示例代码。
3. 为 `test_flood`、`baseapp death`、`auto-switch` 明确区分“单进程仿真版”和“真实多进程版”。
4. 在现有 artificial loss/latency 之上，补充可脚本化的 DB、manager、reviver 故障注入。
5. 给 replay、EntityDef、ghost buffering、Mercury reliable 建立 golden regression 集合。
6. 与 [测试体系与可控时间](/architecture/testing-time-control) 配合，为 timeout/retry/watchdog 场景引入可注入 clock seam。
7. 补一个最小 cluster 黑盒测试套件，优先覆盖登录、实体创建、Cell 切换、BaseApp 恢复和 DB digest 校验。

## 本章边界

本章回答“现在有什么测试、故障注入做到了什么、边界在哪里”。时间驱动与虚拟时钟问题放在 [测试体系与可控时间](/architecture/testing-time-control)；具体网络背压和人工 loss/latency 实现细节放在 [网络背压与故障注入](/architecture/network-backpressure-fault-injection)。
