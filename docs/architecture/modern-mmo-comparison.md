# MMO 架构边界对比

<div class="arch-hero">

本章不是给 BigWorld 套外部架构模板，而是把前面源码分析过的边界集中对照：哪些能力属于 BigWorld 的核心 MMO 语义，哪些只是资源层、工具层或外围工程问题。

</div>

## 先给结论

BigWorld 最有价值的部分不是某个单点技术，而是几条闭环：

- BaseApp、CellApp、DBApp、LoginApp、Manager、machined、Reviver 组成进程级控制面。
- Base/Cell/Client 把实体权威域拆开，DB 持久化由 Base 协调。
- AOI、Witness、Ghost、Haunt、offload 构成跨 Cell 可见性和迁移闭环。
- Mercury 用 UDP Channel、可靠消息、ACK、重传、piggyback、version 处理高频通信。
- EntityDef 同时定义属性、方法、数据域、持久化、客户端可见性和协议 digest。
- Watcher、Profiler、EntityProfiler、Message Logger 直接嵌入运行时排障链路。

这些闭环不能被单一外部概念直接替代。源码分析时应先确认问题属于哪条闭环，再谈优化或替换。

## 网络边界

BigWorld 的网络模型不是“一玩家一 TCP 连接”的普通服务端模型：

- UDP socket 数量少，`UDPChannel` 是逻辑连接。
- `EventPoller` 只分发 readiness，handler 仍在主 Reactor 同步执行。
- `PacketReceiver::maxSocketProcessingTime` 保护单次 socket 处理预算。
- 可靠性由 Channel、Bundle、ACK、重发和 piggyback 共同表达。
- Channel version 参与实体迁移和组件恢复，不能只看包收发。

因此网络 I/O 后端只是边界之一。真正的正确性还依赖 Mercury 可靠层、实体消息顺序、request/reply 超时和主线程 handler 预算。

## 并发边界

BigWorld 的并发模型是主 Reactor + 后台任务 + 多进程分片：

- 主线程推进网络 handler、timer、frequent task、实体逻辑和脚本回调。
- 后台任务卸载 DB、文件、资源加载等阻塞路径。
- 任务完成后通过 foreground task 回主线程提交结果。
- BaseApp/CellApp 横向扩展依赖进程分片、Cell 切分和实体迁移。

这和“每个实体一个独立调度单元”不同。BigWorld 有 actor-like mailbox，但实体被拆成 Base、Cell、Client、Ghost 多个视图，且属性同步、AOI、DB 写入和迁移流都是一等机制。

## 状态边界

Base/Cell/Client 不是简单分层对象，而是权威域：

- Base 保存长期身份、Proxy、DBID、Cell mailbox 和持久化协调权。
- Cell 保存空间权威、real/ghost、AOI、物理、位置和 offload 状态。
- Client 只接收 Witness 筛选后的实体投影。
- DB 只保存 EntityDef persistent 视图和引擎运行时元数据。

这解释了为什么调试一个实体问题经常要串 BaseApp、CellApp、DBApp、Proxy、Witness、Mailbox、Channel 和客户端日志。

## 控制面边界

machined、Manager、Reviver 解决的是 BigWorld 组件发现、进程接纳、birth/death 通知和恢复决策：

- machined 不理解 Cell 分区、实体迁移或 DB 一致性。
- Manager 才维护 BaseApp、CellApp、DBApp、Space、Cell 和 load 状态。
- Reviver 只负责监控和请求重启，无法恢复未备份的内存状态。
- 新进程启动成功不代表游戏状态已经迁移完成。

所以扩容和容灾测试必须覆盖 App 接纳、Cell 边界变化、Entity offload、Ghost 准备和 death recovery，而不是只看进程是否存活。

## 协议边界

EntityDef + BinaryStream 不是普通 wire schema：

- `entities.xml` 顺序影响 EntityTypeID。
- `.def` 属性 flags 决定 Base、Cell、Client、Persistent、Ghost 数据域。
- exposed method range 决定客户端可调用方法。
- digest 决定客户端、服务端和 DB persistent 视图兼容性。
- DB 映射层消费的是 EntityDef persistent stream，不是 Python 对象本身。

因此 `.def` 改动必须同时看协议编号、客户端可见性、DB schema、热更新、迁移流和持久化 digest。

## 可观测边界

BigWorld 的可观测性不是单纯日志：

- Watcher 是运行时对象树，支持 GET/SET/TELL。
- ForwardingWatcher 会把 watcher command 转发到进程集合。
- Profiler 和 DogWatch 记录进程内耗时。
- EntityProfiler 参与实体负载分析。
- Message Logger 是独立集中日志链路，有 MLDB/MongoDB 后端和重连边界。

这里最重要的边界是：Watcher 既能观测也能控制，必须按管理面攻击面分析；Message Logger 能集中查询，但不能被当成强可靠审计日志。

## 验证重点

跨章节验证应围绕 BigWorld 的真实闭环，而不是按外部技术名词拆：

- 登录：LoginApp、DBApp、BaseAppMgr、BaseApp、Proxy、SessionKey 和成功回复缓存。
- 通信：InterfaceElement、Bundle、Channel、request/reply、exposed method range 和 stream 消费完整性。
- 实体：Base/Cell 创建、real/ghost 切换、offload/onload、destroy、restore 和 Witness flush。
- 数据库：Base 协调、Cell persistent 数据请求、DBApp writeEntity、PropertyMapping 和 secondary DB。
- 控制面：machined 注册、Mgr 接纳、birth/death、Reviver 恢复和 recently-dead channel。
- 网络：ACK、重发、fragment、piggyback、channel version、发送队列满和人工丢包/延迟。
- 测试：真实 dispatcher 时间、GameTime、TimeKeeper、timeout/retry/watchdog 和多进程故障注入。

## 本章边界

本章只做架构边界归纳。具体源码入口、调用链和验证细节应回到对应专题章节，而不是在这里展开。
