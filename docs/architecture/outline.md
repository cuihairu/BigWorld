# 完整专题大纲

<div class="arch-hero">

这是后续重写 `docs/` 的完整地图。所有专题都按“源码事实 -> 调用链 -> 源码取舍 -> 验证重点 -> 本章边界”的结构展开。

</div>

## 第一组：基础架构

1. [研究方法与证据分级](/architecture/research-method)
2. [进程拓扑与职责切分](/architecture/process-topology)
3. [主循环、Tick 与事件分发](/architecture/event-loop)
4. [网络 I/O 模型选择](/architecture/network-io-model)
5. [Mercury 协议与可靠 UDP](/architecture/mercury-reliable-udp)
6. [通信抽象、接口定义与组件间 RPC](/architecture/communication-rpc)
7. [收发路径、背压与故障注入](/architecture/network-backpressure-fault-injection)

## 第二组：游戏状态模型

8. [实体模型：Base / Cell / Client 三层语义](/architecture/entity-model)
9. [EntityDef、属性、方法与协议生成](/architecture/entitydef-contract-generation)
10. [序列化与反序列化：BinaryStream、DataType、DataDescription](/architecture/serialization-entitydef)
11. [AOI、Witness、Ghost 与带宽调度](/architecture/aoi-witness-ghost)
12. [空间划分、Cell 分区与负载均衡](/architecture/cell-partition-load-balance)
13. [实体迁移、Cell 退休与跨进程状态移动](/architecture/entity-migration-offload)
14. [实体生命周期状态机](/architecture/entity-lifecycle-state-machine)

## 第三组：运行时工程

15. [登录、会话与 Proxy 接管](/architecture/login-session-proxy-flow)
16. [线程架构、后台任务与 Python GIL](/architecture/threading-background-tasks)
17. [时间系统、Timer、GameTime 与测试可控时间](/architecture/testing-time-control)
18. [脚本热更新、解释器切换与实体迁移](/architecture/hot-reload-script-migration)
19. [持久化、一致性与数据库线程模型](/architecture/persistence-db-model)
20. [动态扩展、控制面与容量治理](/architecture/scaling-fault-tolerance)
21. [Reviver、machined 与故障恢复边界](/architecture/machined-control-plane)

## 第四组：工程质量与源码边界

22. [测试体系、故障注入与覆盖边界](/architecture/testing-fault-injection-coverage)
23. [安全、限流、加密与攻击面](/architecture/security-rate-limit)
24. [Watcher、Profiler、日志与可观测性](/architecture/observability-watcher-profiler-logs)
25. [内存、对象生命周期与资源管理](/architecture/memory-lifecycle)
26. [构建、平台与依赖治理](/architecture/build-platform-dependencies)
27. [MMO 架构边界对比](/architecture/modern-mmo-comparison)

## 已明确要覆盖的问题

<div class="decision-grid">
  <div class="decision-card">
    <h3>网络模型</h3>
    <p>select、poll、LT epoll、ET epoll、io_uring、recvmmsg/sendmmsg、SO_REUSEPORT、AF_XDP/DPDK。</p>
  </div>
  <div class="decision-card">
    <h3>线程模型</h3>
    <p>单 Reactor 主线程、后台任务、BaseApp WorkerThread、Python GIL、actor-like mailbox 与实体调度边界。</p>
  </div>
  <div class="decision-card">
    <h3>热更新</h3>
    <p>新解释器加载、旧解释器迁移、EntityType/UDO/Mailbox/实体迁移、生产环境边界。</p>
  </div>
  <div class="decision-card">
    <h3>测试与时间</h3>
    <p>unit_test、网络故障注入、Timer 驱动、GameTime、虚拟时钟缺口和可重复测试边界。</p>
  </div>
  <div class="decision-card">
    <h3>登录与会话</h3>
    <p>LoginApp、DBApp、BaseAppMgr、BaseApp、Proxy、SessionKey、PendingLogins 和 NAT/firewall 处理。</p>
  </div>
  <div class="decision-card">
    <h3>实体生命周期</h3>
    <p>Base-only、pending cell、real、ghost、offload、onload、destroy、restore 和 zombie ghost 状态。</p>
  </div>
  <div class="decision-card">
    <h3>通信与序列化</h3>
    <p>Mercury Interface、Bundle、BinaryStream、DataType、EntityDescription、协议兼容与 EntityDef 契约。</p>
  </div>
  <div class="decision-card">
    <h3>动态扩展</h3>
    <p>Manager 接纳新 App、Cell 负载均衡、实体迁移、Reviver 恢复和状态迁移边界。</p>
  </div>
</div>

## 每章固定输出

每章必须包含：

- 源码入口
- 关键调用链
- 线程/进程归属
- 数据结构或状态机
- 源码取舍
- 源码边界
- 验证重点
- 本章边界

如果某项在源码中不存在，文档必须明确写“不存在或未确认”，并解释可能原因，而不是等读者指出后再补。
