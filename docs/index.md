---
layout: home

hero:
  name: "BigWorld 引擎架构研究"
  text: "从源码证据链学习 MMO 服务器设计"
  tagline: "重点分析线程、网络、Tick、通信、序列化、热更新、扩缩容与故障恢复，并给出当年取舍和现代方案对比。"
  actions:
    - theme: brand
      text: 开始研究
      link: /architecture/research-method
    - theme: alt
      text: 网络模型
      link: /architecture/network-io-model

features:
  - title: 不是目录导览
    details: 文档按架构决策组织，每章固定覆盖设计目标、调用链、线程归属、边界、历史取舍和现代对比。
  - title: 源码证据优先
    details: 对关键结论标注源码入口，明确区分源码事实、高置信推断和无法确认的开放问题。
  - title: 面向现代化
    details: Python 3.12 迁移作为专题处理，先理解引擎架构，再判断哪些改造值得做、哪些只是代价高的表面升级。
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
    <p>重点比较 select、poll、epoll、io_uring、批量 UDP、SO_REUSEPORT、AF_XDP/DPDK。</p>
  </div>
</div>

## 阅读顺序

1. [研究方法与证据分级](/architecture/research-method)
2. [进程拓扑与职责切分](/architecture/process-topology)
3. [主循环、Tick 与事件分发](/architecture/event-loop)
4. [网络 I/O 模型选择](/architecture/network-io-model)
5. [完整专题大纲](/architecture/outline)

## 核心判断

- BigWorld 的服务端核心是分布式多进程，而不是单体服务器。
- 每个主要进程内部更偏单 Reactor 主线程，后台线程用于阻塞 I/O、资源加载、数据库任务等，不是全逻辑多线程。
- Linux 网络后端使用 `epoll`，但保持类似 `select` 的 level-triggered 语义，不是 ET 或 `io_uring` 架构。
- Mercury 的 UDP Channel 是逻辑连接，不是一玩家一 socket，因此网络优化不能只按“百万 FD”思路套模型。
- Python 3.12 迁移必须建立在架构理解之上，否则很容易只升级解释器，却破坏脚本、实体、热更和构建链路。
