---
layout: home

hero:
  name: "BigWorld 引擎架构研究"
  text: "从源码证据链学习 MMO 服务器设计"
  tagline: "重点分析线程、网络、Tick、通信、序列化、热更新、扩缩容与故障恢复，并给出源码证据、取舍和验证边界。"
  actions:
    - theme: brand
      text: 开始研究
      link: /architecture/research-method
    - theme: alt
      text: 网络模型
      link: /architecture/network-io-model

features:
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><rect x="3" y="4" width="18" height="5" rx="1.5"/><rect x="3" y="15" width="8" height="5" rx="1.5"/><rect x="13" y="15" width="8" height="5" rx="1.5"/><path d="M12 9v3"/><path d="M7 15v-3h10v3"/></svg>'
    title: 不是目录导览
    details: 文档按架构决策组织，每章固定覆盖设计目标、调用链、线程归属、源码取舍和验证边界。
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><circle cx="10.5" cy="10.5" r="6.5"/><path d="m21 21-5.7-5.7"/><path d="m8 10.5 1.8 1.8 3.4-3.6"/></svg>'
    title: 源码证据优先
    details: 对关键结论标注源码入口，明确区分源码事实、高置信推断和无法确认的开放问题。
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><circle cx="12" cy="12" r="5.5"/><ellipse cx="12" cy="12" rx="10" ry="4" transform="rotate(-24 12 12)"/></svg>'
    title: 边界清晰
    details: 先理解引擎架构，再判断哪些属于源码核心约束，哪些只是外围依赖或工具链问题。
---

<div class="arch-hero">

**研究主线**：BigWorld 不是普通 Python 项目，而是 C/C++ 主导、嵌入 Python 2.7 的分布式 MMO 服务器引擎。学习它的价值不在“照搬旧代码”，而在理解一个成熟商业 MMO 引擎如何在当时的平台约束下处理网络、实体、空间、负载、脚本和运维。

</div>

## 当前样板章

<div class="topology-grid">
  <div class="topology-card">
    <h3>研究方法</h3>
    <p>先建立证据分级，避免把源码事实、时代推断和主观猜测混在一起。</p>
  </div>
  <div class="topology-card">
    <h3>进程拓扑</h3>
    <p>从 BaseApp、CellApp、DBApp、Mgr、LoginApp、Reviver 的职责边界理解分布式架构。</p>
  </div>
  <div class="topology-card">
    <h3>事件循环</h3>
    <p>分析主 Reactor、Timer、FrequentTask、Network 的执行顺序和 Tick 公平性。</p>
  </div>
  <div class="topology-card">
    <h3>网络模型</h3>
    <p>分析 select、poll、epoll、批量 UDP、SO_REUSEPORT、io_uring 与 Mercury 执行模型的边界。</p>
  </div>
  <div class="topology-card">
    <h3>可靠 UDP</h3>
    <p>分析 Mercury Channel 的 ACK、重发、窗口、piggyback、indexed channel 与版本语义。</p>
  </div>
  <div class="topology-card">
    <h3>通信与序列化</h3>
    <p>解释 InterfaceElement、Bundle、RPC、EntityDef 契约、BinaryStream、DataType 和 DataDescription。</p>
  </div>
  <div class="topology-card">
    <h3>运行时工程</h3>
    <p>覆盖线程架构、后台任务、Python GIL、热更新、测试时间控制、动态扩展和容灾。</p>
  </div>
  <div class="topology-card">
    <h3>游戏状态治理</h3>
    <p>分析 Base/Cell/Client、AOI/Witness/Ghost、Cell 分区、实体迁移和 DB 持久化。</p>
  </div>
  <div class="topology-card">
    <h3>工程质量</h3>
    <p>补齐安全限流、加密、Watcher/Profiler/日志、内存生命周期、构建依赖与 MMO 架构边界。</p>
  </div>
</div>

## 阅读顺序

1. [研究方法与证据分级](/architecture/research-method)
2. [进程拓扑与职责切分](/architecture/process-topology)
3. [主循环、Tick 与事件分发](/architecture/event-loop)
4. [网络 I/O 模型选择](/architecture/network-io-model)
5. [Mercury 可靠 UDP](/architecture/mercury-reliable-udp)
6. [通信抽象与 RPC](/architecture/communication-rpc)
7. [序列化与 EntityDef](/architecture/serialization-entitydef)
8. [网络背压与故障注入](/architecture/network-backpressure-fault-injection)
9. [实体模型：Base / Cell / Client](/architecture/entity-model)
10. [EntityDef 契约与协议生成](/architecture/entitydef-contract-generation)
11. [AOI、Witness 与 Ghost](/architecture/aoi-witness-ghost)
12. [Cell 分区与负载均衡](/architecture/cell-partition-load-balance)
13. [实体迁移与 Offload](/architecture/entity-migration-offload)
14. [实体生命周期状态机](/architecture/entity-lifecycle-state-machine)
15. [持久化与 DB 线程模型](/architecture/persistence-db-model)
16. [登录、会话与 Proxy 接管](/architecture/login-session-proxy-flow)
17. [线程架构与后台任务](/architecture/threading-background-tasks)
18. [脚本热更新与迁移](/architecture/hot-reload-script-migration)
19. [测试体系与可控时间](/architecture/testing-time-control)
20. [动态扩展与容灾](/architecture/scaling-fault-tolerance)
21. [machined 控制面与进程发现](/architecture/machined-control-plane)
22. [测试体系、故障注入与覆盖边界](/architecture/testing-fault-injection-coverage)
23. [安全、限流与加密](/architecture/security-rate-limit)
24. [Watcher、Profiler 与日志](/architecture/observability-watcher-profiler-logs)
25. [内存、对象生命周期与资源管理](/architecture/memory-lifecycle)
26. [构建、平台与依赖治理](/architecture/build-platform-dependencies)
27. [MMO 架构边界对比](/architecture/modern-mmo-comparison)
28. [完整专题大纲](/architecture/outline)

## 核心判断

- BigWorld 的服务端核心是分布式多进程，而不是单体服务器。
- 每个主要进程内部更偏单 Reactor 主线程，后台线程用于阻塞 I/O、资源加载、数据库任务等，不是全逻辑多线程。
- Linux 网络后端使用 `epoll`，但保持类似 `select` 的 level-triggered 语义，不是 ET 或 `io_uring` 架构。
- Mercury 的 UDP Channel 是逻辑连接，不是一玩家一 socket，因此网络优化不能只按“百万 FD”思路套模型。
- Mercury 可靠层按消息和 Bundle 表达可靠性，支持 ACK、重发、乱序窗口、piggyback、indexed channel 和实体迁移版本号。
- EntityDef 序列化不是单纯网络编码，而是脚本、实体属性、方法参数、持久化、迁移和热更新共同依赖的类型契约。
- EntityDef 契约由 `entities.xml`、`.def`、组件、接口、脚本分布、消息范围和 digest 共同生成，实体顺序和 exposed 编号具有协议含义。
- 网络背压由接收预算、发送队列满等待、人工丢包/延迟和 Mercury 可靠层共同组成，不是单个 epoll 参数能解释。
- BigWorld 的实体模型是 Base/Cell/Client 三层权威域，Cell 内又区分 real 和 ghost。
- AOI 由 Witness 管理，包含 hysteresis、带宽预算、实体 alias、可靠位置和 SpaceData 同步。
- Cell 负载均衡调整的是空间 BSP 边界和实体归属，不是普通请求分发。
- 实体生命周期本质上是 Base、Cell real、Ghost、Offload、Onload、Destroy 和 Restore 的状态机。
- 持久化由 BaseApp 协调 Base/Cell 数据，DBApp 通过 IDatabase 和后台任务隔离阻塞数据库操作。
- 登录链路通过 LoginApp、DBApp、BaseAppMgr、BaseApp 和 Proxy 二次握手完成，成功回复会缓存，失败回复刻意不可靠以降低 DoS 风险。
- BigWorld 的线程模型主要是主 Reactor + 后台任务回主线程提交，不是全逻辑多线程。
- `reloadScript` 是开发调试导向的高风险迁移机制，源码明确警告不要用于生产环境。
- 当前时间系统主要依赖真实 `timestamp()`，可控虚拟时间是可重复测试的重要缺口。
- 当前测试体系在 Mercury、EntityDef、Replay、Ghost buffering 上并不薄弱，但 cluster 级黑盒回归和统一 chaos 注入仍然不足。
- 动态扩展的核心是 Manager 接纳进程并驱动 Cell/Entity 状态迁移，不是简单拉起无状态副本。
- machined 提供进程注册、接口发现、birth/death listener 和启动组件能力，是 BigWorld 旧式控制面的基础。
- BigWorld 有明确的入口限流、消息预算和 Channel 加密抽象，但不是现代全链路安全体系。
- Watcher/Profiler/EntityProfiler 是理解运行时状态和负载治理的核心设施，也需要权限边界和指标导出。
- 对象生命周期依赖侵入式引用计数、Python 对象、Mercury Channel 和实体 real/ghost 转换，不适合机械替换成标准智能指针。
- 构建治理必须处理 CMake/Makefile、OpenSSL、嵌入式 Python、第三方库和 server/client/tools 多目标，而不是只改某个依赖版本号。
