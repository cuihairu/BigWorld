# 迁移与覆盖率现状

<div class="arch-hero">

本页是 Python 3.13 迁移与覆盖率补强工程的**单一现状仪表**：当前推进到哪、各模块覆盖到多少、哪些面已登记为不可达。逐批的完整过程、代码证据与陷阱记录在 [Python 3.13 迁移全量台账](/python-313-migration)，本页只回答“现在是什么状态”。

</div>

## 当前状态

<div class="decision-table">

| 事项 | 状态 | 说明 |
|------|------|------|
| 嵌入式解释器 | ✅ Python 2.7 → **3.13** | 已完成并合入 dev 分支；版本从规划期的 3.12 随依赖链上调至 3.13 |
| 全量单测门禁 | ✅ 21 模块 / 1151 用例全绿 | 2026-09-30 实测（批次20 后；physics2_test 20 → 44） |
| 覆盖率补强工程 | ✅ 批次 1–20 已完成 | 批次20 收口 `quad_tree.ipp` 漏列激活；缺口 top 清单落 [TESTING.md](../../TESTING.md) |
| 下批首位候选 | ⏳ `cstdmf/fixed_sized_allocator.cpp` | 289 可执行行、0%，纯内存分配器不变量、无外部依赖 |

</div>

## 模块覆盖率基线（2026-09-30 插桩实跑）

gcovr 全量聚合值。客户端大件（moo / physics2 / server / chunk）接近零并非“没人测”，而是结构性的：21 个单测宿主全部来自服务端构建，这些模块的执行路径基本不在单测进程里。

<div class="decision-table">

| 模块 | 覆盖率 | 解读 |
|------|--------|------|
| script | 92% | 脚本抽象层，覆盖率最高的模块（批次1 起主攻） |
| math | 68% | 几何与数学工具 |
| pyscript | 68% | 嵌入解释器封装，批次4–9 六连批收口 |
| network | 66% | Mercury 网络层，批次10–18 主攻区 |
| resmgr | 58% | 资源管理器 |
| cstdmf | 51% | 基础设施（调试、内存、流） |
| entitydef | 43% | 类型契约；装载机依赖重 |
| terrain | 34% | 地形（含 gcovr 记录异常项，见台账批次18） |
| connection | 24% | 服务端连接面；`server_connection.cpp`（990 行 0%）维持不可测登记拖底 |
| moo | 13% | 客户端渲染支撑，服务端单测不触 |
| physics2 | **76%** | 客户端物理；批次20 收口 `quad_tree.ipp` 漏列激活（61% → 76%） |
| server | 13% | 服务端骨架，需完整 app 装配 |
| chunk | 3% | 客户端 chunk 体系，需 ChunkSpace 装载机 |

</div>

文件级亮点：`physics2/quad_tree.ipp` 0% → 98%（测试 TU 视角 860/876，批次20 Makefile.rules 漏列修复激活）；`physics2/bsp.cpp` 0% → 91%（批次19 死门激活）；`network/tcp_bundle.cpp` 达 100%（批次16 收口）；`network/machine_guard.cpp` 28%（bwmachined 面不可达，批次17 登记）；批次18 派测的 `event_poller.cpp` 45% → 57%（其余缺口为 Linux 死代码的 SelectPoller/PollPoller，175 行结构性登记）。

## 批次时间线

<div class="decision-table">

| 批次 | 日期 | 主对象 |
|------|------|--------|
| 1 | 2026-09-26 | lib/pyscript |
| 2–3 | 2026-09-26 | lib/connection |
| 4–9 | 2026-09-26 | lib/pyscript（残留面 / script_math / 基础设施 / 回溯打印器 / 零覆盖文件 / 收尾） |
| 10–11 | 2026-09-26 | lib/connection（零覆盖面 / filter/login 面） |
| 12–13 | 2026-09-27 | connection 挑战/replay 面 + cstdmf/math/resmgr/db 零覆盖；replay 线索证伪 |
| 14 | 2026-09-27 | encryption_filter 流式加密面 + 零覆盖清单可测性判定 |
| 15 | 2026-09-27 | file_stream 零覆盖 |
| 16 | 2026-09-27 | tcp_bundle 线格式整层 |
| 17 | 2026-09-27 | machine_guard MGM 编解码面 |
| 18 | 2026-09-28 | 全量覆盖率实跑基线 + event_poller 轮询器面 |
| 19 | 2026-09-28 | bsp.cpp 死门激活（MF_SERVER 守卫移除 + 14 用例） |
| 20 | 2026-09-30 | quad_tree.ipp 激活（Makefile.rules 漏列修复 + 22 用例 + 四死函数登记） |

</div>

用例总量演进：批次14 后 1061 → 批次15 后 1071 → 批次16 后 1083 → 批次17 后 1099 → 批次18 后 1113 → 批次19 后 1127 → 批次20 后 **1151**（全量 21 模块门禁全绿）。

## 不可达 / 受限登记摘要

逐臂判定的完整记录见台账各批次；这里是 0% 大文件的结论摘要：

- `connection/server_connection.cpp`（990 行）— **维持不可测**：真登录管线 + TCPChannel 状态机（批次14/16 口径）。
- `connection/message_handlers.hpp`（538 行）— **结构性死代码**：服务端 app 实例化的模板 handler 表，单测无人实例化。
- `entitydef/entity_description.cpp`（665 行）— 受限：需 EntityType/EntityDef XML 装载机全套。
- `moo/image.ipp`（589 行）— 受限：客户端图像合成面。
- `chunk/chunk_space.cpp`（506 行）— 受限：需 ChunkSpace + chunk 装载机。
- `connection/replay_controller.cpp`（495 行）— 受限：录像/回放文件管线（线格式已批次12 测）。
- `network/logger_endpoint.cpp` / `logger_message_forwarder.cpp` / `watcher_nub.cpp` — 维持受限登记（真消息日志管线、UDP 绑 machined 标准端口，批次17）。
- `terrain/terrain2/terrain_height_map2.cpp`（755 行）— gcovr 对该记录丢失文件名列（空名条目），未逐臂判定。
- `physics2/quad_tree.ipp` 死函数族 — **结构性死代码**（批次20 三重取证）：`QuadTree::del`（calculateQTRange 少传 origin_）、`QuadTree::testPoint`（调不存在的 `QuadTreeNode::testPoint`）、`print`/`printQTNode`（`node.elements()` 不存在）、`countAt`（int 实参传 Quad 形参）——从未实例化，一实例化即编译错；生产侧 chunk 的 `pChunkTree_` 是 HullTree，对 ObstacleTree 只用 addToRoot。

已收口（移出登记）：`physics2/quad_tree.ipp`（0% → **98%** 测试 TU 视角，批次20 漏列激活；残余 16 行为断言/日志失败臂 + 上述死函数族）；`physics2/bsp.cpp`（778 行，0% → **91%**，批次19 死门激活；残余 67 行为构造校验/加载错误臂，逐段登记见台账 §7.19）。缺口 top 全清单见 [TESTING.md](../../TESTING.md) TODO 段。

## 口径与边界

- 覆盖率为 **gcovr 8.6 插桩全量实跑**值（`user_shouldBuildCodeCoverage=1`，21 模块测试全跑完、498 个 .gcda 聚合），不是估算。嵌 Python 三模块的可用前提：树重建后显式 `make python_install`（`bw-run-all-unit-tests` 链路不触发安装，台账 §7.19）。
- 台账中部分批次的门禁措辞为等效口径：本仓无 ctest、无独立覆盖率门禁脚本，等效标准 = `bw-run-all-unit-tests` 21 模块全绿 + gcovr 聚合成功。
- 覆盖率数字会随后续批次继续变动，本页与台账同步更新；冲突时以台账为准。

延伸阅读：[Python 升级路线图](/upgrade-plan/python-upgrade-plan)（原规划决策记录）· [Python C API 迁移](/upgrade-plan/python-c-api-guide) · [第三方依赖升级](/upgrade-plan/third-party-deps) · [全量台账](/python-313-migration)
