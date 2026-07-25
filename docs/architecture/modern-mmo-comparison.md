# MMO 架构边界对比

<div class="arch-hero">

本章不是给 BigWorld 套外部架构模板，而是把前面源码分析过的边界集中对照：哪些能力属于 BigWorld 的核心 MMO 语义，哪些只是资源层、工具层或外围工程问题。

</div>

## 先给结论

BigWorld 最有价值的部分不是某个单点技术，而是几条闭环：

- BaseApp、CellApp、DBApp、LoginApp、Manager、machined、Reviver 组成进程级控制面。源码见 [进程拓扑与职责切分](/architecture/process-topology)。
- Base/Cell/Client 把实体权威域拆开，DB 持久化由 Base 协调。源码见 [实体模型 Base/Cell/Client](/architecture/entity-model)。
- AOI、Witness、Ghost、Haunt、offload 构成跨 Cell 可见性和迁移闭环。源码见 [AOI、Witness 与 Ghost](/architecture/aoi-witness-ghost)。
- Mercury 用 UDP Channel、可靠消息、ACK、重传、piggyback、version 处理高频通信。源码见 [Mercury 可靠 UDP](/architecture/mercury-reliable-udp)。
- EntityDef 同时定义属性、方法、数据域、持久化、客户端可见性和协议 digest。源码见 [EntityDef 契约与协议生成](/architecture/entitydef-contract-generation)。
- Watcher、Profiler、EntityProfiler、Message Logger 直接嵌入运行时排障链路。源码见 [Watcher、Profiler 与日志](/architecture/observability-watcher-profiler-logs)。

这些闭环不能被单一外部概念直接替代。源码分析时应先确认问题属于哪条闭环，再谈优化或替换。

## 网络边界

BigWorld 的网络模型不是”一玩家一 TCP 连接”的普通服务端模型：

- UDP socket 数量少，`UDPChannel` 是逻辑连接。源码见 [udp_channel.hpp:57](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:57)。
- `EventPoller` 只分发 readiness，handler 仍在主 Reactor 同步执行。源码见 [event_poller.cpp:1115](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:1115)。
- `PacketReceiver::maxSocketProcessingTime` 保护单次 socket 处理预算。源码见 [packet_receiver.cpp:83](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:83)。
- 可靠性由 Channel、Bundle、ACK、重发和 piggyback 共同表达。源码见 [bundle.hpp:27](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:27)。
- Channel version 参与实体迁移和组件恢复，不能只看包收发。源码见 [udp_channel.hpp:383](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:383)。

因此网络 I/O 后端只是边界之一。真正的正确性还依赖 Mercury 可靠层、实体消息顺序、request/reply 超时和主线程 handler 预算。

## 并发边界

BigWorld 的并发模型是主 Reactor + 后台任务 + 多进程分片：

- 主线程推进网络 handler、timer、frequent task、实体逻辑和脚本回调。源码见 [event_dispatcher.cpp:428](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_dispatcher.cpp:428)。
- 后台任务卸载 DB、文件、资源加载等阻塞路径。源码见 [bgtask_manager.cpp:137](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:137)。
- 任务完成后通过 foreground task 回主线程提交结果。源码见 [bgtask_manager.cpp:744](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:744)。
- BaseApp/CellApp 横向扩展依赖进程分片、Cell 切分和实体迁移。源码见 [cellappmgr.cpp:1245](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1245)。

这和”每个实体一个独立调度单元”不同。BigWorld 有 actor-like mailbox，但实体被拆成 Base、Cell、Client、Ghost 多个视图，且属性同步、AOI、DB 写入和迁移流都是一等机制。

## 状态边界

Base/Cell/Client 不是简单分层对象，而是权威域：

- Base 保存长期身份、Proxy、DBID、Cell mailbox 和持久化协调权。源码见 [base.hpp:58](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:58)。
- Cell 保存空间权威、real/ghost、AOI、物理、位置和 offload 状态。源码见 [entity.hpp:89](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:89)。
- Client 只接收 Witness 筛选后的实体投影。源码见 [entity.cpp:2475](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2475)。
- DB 只保存 EntityDef persistent 视图和引擎运行时元数据。源码见 [idatabase.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db/idatabase.hpp)。

这解释了为什么调试一个实体问题经常要串 BaseApp、CellApp、DBApp、Proxy、Witness、Mailbox、Channel 和客户端日志。

## 控制面边界

machined、Manager、Reviver 解决的是 BigWorld 组件发现、进程接纳、birth/death 通知和恢复决策：

- machined 不理解 Cell 分区、实体迁移或 DB 一致性。源码见 [machined_control_plane](/architecture/machined-control-plane)。
- Manager 才维护 BaseApp、CellApp、DBApp、Space、Cell 和 load 状态。源码见 [cellappmgr.cpp:190](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:190)。
- Reviver 只负责监控和请求重启，无法恢复未备份的内存状态。源码见 [reviver.cpp:440](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:440)。
- 新进程启动成功不代表游戏状态已经迁移完成。源码见 [scaling-fault-tolerance](/architecture/scaling-fault-tolerance)。

所以扩容和容灾测试必须覆盖 App 接纳、Cell 边界变化、Entity offload、Ghost 准备和 death recovery，而不是只看进程是否存活。

## 协议边界

EntityDef + BinaryStream 不是普通 wire schema：

- `entities.xml` 顺序影响 EntityTypeID。源码见 [entitydef-contract-generation](/architecture/entitydef-contract-generation)。
- `.def` 属性 flags 决定 Base、Cell、Client、Persistent、Ghost 数据域。源码见 [data_description.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.hpp)。
- exposed method range 决定客户端可调用方法。源码见 [exposed_message_range.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/exposed_message_range.hpp)。
- digest 决定客户端、服务端和 DB persistent 视图兼容性。源码见 [entity_description.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.hpp)。
- DB 映射层消费的是 EntityDef persistent stream，不是 Python 对象本身。源码见 [persistence-db-model](/architecture/persistence-db-model)。

因此 `.def` 改动必须同时看协议编号、客户端可见性、DB schema、热更新、迁移流和持久化 digest。

## 可观测边界

BigWorld 的可观测性不是单纯日志：

- Watcher 是运行时对象树，支持 GET/SET/TELL。源码见 [watcher.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/watcher.hpp)。
- ForwardingWatcher 会把 watcher command 转发到进程集合。源码见 [watcher_nub.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_nub.cpp)。
- Profiler 和 DogWatch 记录进程内耗时。源码见 [profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/profiler.hpp)。
- EntityProfiler 参与实体负载分析。源码见 [entity_profiler.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/entity_profiler.hpp)。
- Message Logger 是独立集中日志链路，有 MLDB/MongoDB 后端和重连边界。源码见 [message_logger.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/message_logger.hpp)。

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
