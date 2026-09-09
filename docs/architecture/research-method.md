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

这套模板的目的很简单：避免文档变成”BaseApp 是什么、CellApp 是什么”的百科式目录。

## 证据分级实例

### 一级证据：源码事实

**定义**：能在仓库源码中直接定位到的实现。

**实例**：

| 结论 | 源码证据 | 可信度 |
|------|----------|--------|
| BigWorld 使用 level-triggered epoll | `event_poller.cpp:1079` 注释 “TODO: Could be good to use EPOLLET” | 100% |
| GameTick 默认 10Hz | `baseapp.cpp:2020` `1000000/Config::updateHertz()` | 100% |
| Mercury 支持四种可靠类型 | `bundle.hpp:27` `RELIABLE_NO/DRIVER/PASSENGER/CRITICAL` | 100% |
| Entity 有 real/ghost 两种状态 | `entity.hpp:89` 注释 “each entity only has one authoritative real” | 100% |

**验证方法**：直接阅读源码，找到对应行号。

### 二级证据：高置信推断

**定义**：结合源码、平台基线和技术年代得出的判断。

**实例**：

| 结论 | 推断依据 | 可信度 |
|------|----------|--------|
| io_uring 不在原始设计选项内 | CentOS 7 内核不支持，代码无相关引用 | 95% |
| 脚本执行受全局解释器锁边界约束 | 代码使用 `PyEval_InitThreads()`、`PyEval_SaveThread()`、`PyEval_RestoreThread()` 管理 thread state | 95% |
| 商业版可能有更多优化 | 开源版缺少 `control_cluster.py`，文档有漂移 | 80% |
| 设计年代约 2005-2010 | 使用 CentOS 5/6，Python 2.4 兼容探测 | 90% |

**验证方法**：检查平台约束、技术年代、代码风格。

### 三级证据：开放问题

**定义**：仓库不能证明的内容。

**实例**：

| 问题 | 原因 | 无法确认 |
|------|------|----------|
| 商业版内部调优参数 | 不在开源仓库中 | ❌ |
| 生产集群真实规模 | 无公开数据 | ❌ |
| 原团队具体决策会议记录 | Git 历史不完整 | ❌ |
| 性能基准测试结果 | 无公开 benchmark | ❌ |

**处理方式**：明确标注”无法确认”，不伪装成作者原话。

## 常见误判案例

### 误判 1：把”选择”当成”不知道”

**错误判断**：”BigWorld 使用 level-triggered epoll 是因为不知道 edge-triggered”

**正确分析**：源码注释明确写着 “TODO: Could be good to use EPOLLET (leave like select for now)”，说明作者知道 ET，但选择保守方案。

**教训**：先找源码注释，再下结论。

### 误判 2：把”约束”当成”缺陷”

**错误判断**：”BigWorld 使用 Python 2.7 是技术债，应该立即升级”

**正确分析**：Python 2.7 是设计年代的合理选择，GIL 限制了并行，但单线程模型本身是游戏服务器的常见模式。

**教训**：区分”时代约束”和”设计缺陷”。

### 误判 3：把”推断”当成”事实”

**错误判断**：”BigWorld 支持百万并发连接”

**正确分析**：源码使用单 UDP socket + 逻辑 Channel，不是”一玩家一 socket”模型。并发能力取决于协议处理、Tick 预算和 Cell 负载，不能简单套用”百万 FD”模型。

**教训**：区分”源码事实”和”工程推断”。

### 误判 4：把”外围”当成”核心”

**错误判断**：”BigWorld 构建系统老旧，说明引擎质量差”

**正确分析**：构建系统（CMake 2.8、CentOS 7）是外围工程问题，不影响核心架构（Mercury、EntityDef、Cell/BSP）的价值。

**教训**：区分”核心约束”和”外围依赖”。

## 验证方法论

### 方法 1：源码追溯

对于任何结论，必须找到源码入口：

```
结论 -> 源码文件 -> 行号 -> 函数名 -> 调用链
```

**示例**：
- 结论：BigWorld 使用单 Reactor 主线程
- 源码：`event_dispatcher.cpp:428` `processOnce()` 
- 调用链：FrequentTasks -> Timers -> Stats -> Network
- 验证：检查是否有线程池、并行任务调度

### 方法 2：交叉验证

对于关键结论，从多个角度验证：

| 角度 | 验证方式 |
|------|----------|
| 代码 | 源码实现 |
| 配置 | 构建脚本、平台约束 |
| 文档 | PDF、注释、README |
| 行为 | 运行时表现 |

**示例**：验证”Linux 使用 epoll”
- 代码：`event_poller.cpp` EPoller 类
- 配置：`#ifdef __linux__` 条件编译
- 文档：PDF 提到 Linux 网络优化
- 行为：`strace` 可以看到 epoll_wait 系统调用

### 方法 3：边界测试

对于性能、容量等结论，明确边界条件：

```
结论 + 条件 + 限制 + 例外
```

**示例**：
- 结论：BigWorld 支持动态扩展
- 条件：需要 BaseApp 和 Alpha DBApp 已知
- 限制：只支持 CellApp/BaseApp，不支持 Manager
- 例外：正在 recovery 时不接纳新 CellApp

### 方法 4：反例检查

对于任何结论，主动寻找反例：

```
结论 -> 反例 -> 是否成立 -> 修正结论
```

**示例**：
- 结论：单线程模型没有锁
- 反例：`SafeReferenceCount` 使用原子操作
- 修正：主线程内无锁，但跨线程场景有原子操作

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
