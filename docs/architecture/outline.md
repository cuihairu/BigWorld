# 完整专题大纲

<div class="arch-hero">

这是后续重写 `docs/` 的完整地图。所有专题都按“源码事实 -> 调用链 -> 历史取舍 -> 现代对比 -> 改造建议”的结构展开。

</div>

## 第一组：基础架构

1. 研究方法与证据分级
2. 进程拓扑与职责切分
3. 主循环、Tick 与事件分发
4. 网络 I/O 模型选择
5. Mercury 协议与可靠 UDP
6. 通信抽象、接口定义与组件间 RPC
7. 收发路径、背压与限速

## 第二组：游戏状态模型

8. 实体模型：Base / Cell / Client 三层语义
9. EntityDef、属性、方法与协议生成
10. 序列化与反序列化：BinaryStream、DataType、DataDescription
11. AOI、Witness、Ghost 与带宽调度
12. 空间划分、Cell 分区与负载均衡
13. 实体迁移、Cell 退休与跨进程状态移动

## 第三组：运行时工程

14. 线程架构、后台任务与 Python GIL
15. 时间系统、Timer、GameTime 与测试可控时间
16. 脚本热更新、解释器切换与实体迁移
17. 持久化、一致性与数据库线程模型
18. 动态扩展、控制面与容量治理
19. Reviver、machined 与故障恢复边界

## 第四组：工程质量与现代化

20. 测试体系、故障注入与覆盖边界
21. 安全、限流、加密与攻击面
22. Watcher、Profiler、日志与可观测性
23. 内存、对象生命周期与资源管理
24. 构建、平台与依赖治理
25. 当时方案与现代 MMO 架构对比
26. Python 3.12 迁移路线

## 已明确要覆盖的问题

<div class="decision-grid">
  <div class="decision-card">
    <h3>网络模型</h3>
    <p>select、poll、LT epoll、ET epoll、io_uring、recvmmsg/sendmmsg、SO_REUSEPORT、AF_XDP/DPDK。</p>
  </div>
  <div class="decision-card">
    <h3>线程模型</h3>
    <p>单 Reactor 主线程、后台任务、BaseApp WorkerThread、Python GIL、现代 actor/job system 对比。</p>
  </div>
  <div class="decision-card">
    <h3>热更新</h3>
    <p>新解释器加载、旧解释器迁移、EntityType/UDO/Mailbox/实体迁移、生产环境边界。</p>
  </div>
  <div class="decision-card">
    <h3>测试与时间</h3>
    <p>unit_test、网络故障注入、Timer 驱动、GameTime、虚拟时钟缺口和现代仿真测试对比。</p>
  </div>
  <div class="decision-card">
    <h3>通信与序列化</h3>
    <p>Mercury Interface、Bundle、BinaryStream、DataType、EntityDescription、协议兼容与现代 IDL 对比。</p>
  </div>
  <div class="decision-card">
    <h3>动态扩展</h3>
    <p>Manager 接纳新 App、Cell 负载均衡、实体迁移、Reviver 恢复、与云原生扩缩容对比。</p>
  </div>
</div>

## 每章固定输出

每章必须包含：

- 源码入口
- 关键调用链
- 线程/进程归属
- 数据结构或状态机
- 当时平台约束
- 当时可选方案
- 现代替代方案
- 改造优先级
- 风险与验证方式

如果某项在源码中不存在，文档必须明确写“不存在或未确认”，并解释可能原因，而不是等读者指出后再补。
