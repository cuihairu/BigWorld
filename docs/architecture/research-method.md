# 研究方法与证据分级

<div class="arch-hero">

本章先定义研究方法。BigWorld 这个仓库没有完整历史提交记录，也缺少完整游戏工程与资源树，因此文档必须区分“源码明确事实”“基于时代和平台的高置信推断”“无法确认的问题”。这比直接给结论更重要。

</div>

## 研究目标

这套文档的目标不是把目录翻译成中文，也不是为某个依赖版本升级做孤立清单，而是回答几个架构问题：

- 一个成熟 MMO 引擎如何拆分 Base、Cell、DB、Login、Manager、Reviver。
- 它为什么采用单 Reactor 主线程加后台任务，而不是全逻辑多线程。
- Mercury 为什么是可靠 UDP + 逻辑 Channel，而不是纯 TCP。
- Linux 下为什么使用 level-triggered `epoll`，而不是 edge-triggered `epoll` 或现代 `io_uring`。
- EntityDef 类型系统如何同时服务网络协议、持久化、脚本和热更新。
- 热更新、动态扩展、故障恢复这些能力的真实边界在哪里。
- 哪些源码设计仍然是核心约束，哪些只是外围工程问题。

## 证据分级

<div class="decision-grid">
  <div class="decision-card">
    <h3>一级：源码事实</h3>
    <p>能在仓库源码中直接定位到的实现，例如事件循环顺序、EPoller 注册方式、reloadScript 调用链、单包 recvfrom/sendto。</p>
  </div>
  <div class="decision-card">
    <h3>二级：高置信推断</h3>
    <p>结合源码、平台基线和技术年代得出的判断，例如 io_uring 不在原始设计选项内，CentOS 5/6/7 约束影响网络与构建方案。</p>
  </div>
  <div class="decision-card">
    <h3>三级：开放问题</h3>
    <p>仓库不能证明的内容，例如商业版内部调优参数、生产集群真实规模、原团队具体决策会议记录。</p>
  </div>
</div>

## 当前仓库边界

已确认事实：

- 服务端核心在 `programming/bigworld/server`。
- 通用网络栈在 `programming/bigworld/lib/network`，命名空间为 Mercury。
- 脚本嵌入与 Python 运行时在 `programming/bigworld/lib/pyscript`、`programming/bigworld/lib/script`。
- 实体定义和序列化核心在 `programming/bigworld/lib/entitydef`。
- 原始 PDF 保留在 `docs/pdf`。

需要注意的边界：

- 仓库缺少完整 `game/` 和资源树，无法完整验证 FantasyDemo 端到端运行。
- Git 历史基本不能还原原始商业开发阶段的设计讨论。
- 文档中涉及“为什么没有选择某技术”的部分，只能按源码和年代做工程推断，不能伪装成作者原话。

## 写作模板

每个核心专题固定使用以下结构：

1. 问题背景与设计目标
2. 关键进程、线程、对象
3. 完整调用链与源码入口
4. 核心数据结构与状态机
5. 正确性边界与失败模式
6. 性能瓶颈与可观测信号
7. 源码取舍
8. 源码验证重点
9. 本章边界

这套模板的目的很简单：避免文档变成“BaseApp 是什么、CellApp 是什么”的百科式目录。

## 样板章节

本轮先完成四个定调章节：

<div class="flow-strip">
  <span class="flow-node">研究方法</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">进程拓扑</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">事件循环</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">网络模型</span>
</div>

后续章节会沿用相同分析粒度，继续覆盖 Mercury 可靠 UDP、序列化、线程/GIL、热更新、测试、扩缩容、容灾、AOI、持久化、安全和可观测性。

## 原则

- KISS：每个结论先找最小源码证据，不用宏大叙事替代实现分析。
- YAGNI：只为真实源码存在的机制写分析，不为空想架构补章节。
- DRY：同一类架构取舍只建立一套判断框架，避免每章重复泛泛对比。
- SOLID：文档按职责拆专题，网络、线程、实体、持久化、运维各自独立，但通过调用链互相引用。
