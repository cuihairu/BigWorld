# 待拍板/阻塞项批量决策分析（2026-10-30）

> 状态：已代决（2026-10-14 用户授权「按推荐拍，后续可推翻」）；需要密钥/账号/用户私有数据的事项标「需用户输入，暂缓」，不硬做。
> 范围：todo.md 中全部 BLOCKED/待决策/缺用户输入条目；分析基于仓库事实（file:line）与 CI 探针实测记录，不臆造。
> 本批次共 4 项：R3 两条剩余决策（三树删除／cellapp_extension 导出）、io_uring（用户已定暂缓）、集群级黑盒回归链。

---

## R3 Windows 构建（todo.md:38）——剩余两件决策

### R3-a 三树删除（vendored png/zip/sqlite）

- **推荐决策**：保留三树，不删除；R3 后半「三树删除」验收项按「保留」收口（不满足删除即不删，属可接受的 CI 经济取舍）。
- **理由**：
  1. 事实：Linux make 链早已迁 vcpkg，不再使用三树（`third_party/Makefile.rules` 头注「png/zip(zlib) 由 vcpkg 提供，in-tree 组件构建已移除」；`lib/sqlite/Makefile.rules` 头注「sqlite3 由 vcpkg 提供」）——**删除收益仅剩 Linux 轨，而 Linux 轨早已不用它们**。
  2. 成本：CMake 轨的 `FindBWOpenSSL.cmake:20` / `FindBWcurl.cmake` 的 vcpkg 分支是 `IF( NOT MSVC )` 门控——MSVC 侧走 vendored 预构建树自足（probe 实测 `Found BWOpenSSL: .../third_party/openssl-vs...`）；三树删除须先把 vcpkg 解析打通到 MSVC 侧（探针 job 装 vcpkg＋`find_package(ZLIB/PNG/SQLite3)`＋`zip`/`libpng` 保留为 IMPORTED 别名以免改 16 个消费方 `TARGET_LINK_LIBRARIES`），**CI 成本约 20–40 分钟/轮**（现探针 1 分钟）。
  3. 风险：改 16 个消费方链接表有回归面；vcpkg MSVC 接线是新未验证路径。
  4. 收益：近乎零——Windows 轨靠 vendored 自足（自足 C 代码，MSVC 可编），删除只换 CI 更贵、无交付面收益。
  5. 备选权衡（todo 原注）：备选为 Windows 轨保留三树，删除收益仅剩 Linux 轨——而 Linux 轨早已不用它们。
- **立即执行动作**：无（记录决策）。✅ 已执行（记录决策，commit aeb76683）
- **不做／延后**：不删除三树；不引入 vcpkg-MSVC 接线；R3 后半按「保留」作为可接受取舍记录。
- **教学**：三棵树=图片/压缩/数据库三套自带的 C 代码。Linux 这边早就不用它们了（改用 vcpkg），但 Windows 那边还在用，而且能编。删掉它们要先把 Windows 那边也换成 vcpkg，CI 每轮多花 20-40 分钟，还得改 16 处链接，风险不小、收益却几乎没有（省不了什么，因为 Linux 本来就不用）。所以不删。

### R3-b cellapp_extension 宿主符号导出三选一（.def / dllexport 宏 / 改托管接口）

- **推荐决策**：保持 cellapp_extension 门控出 MSVC 构建（现状，`BWConfiguration_server.cmake`），导出方案作为独立架构设计件**延后**；不在本轮三选一。
- **理由**：
  1. 事实：`examples/cellapp_extension/speed_extra.cpp`/`speed_controller.cpp` 引用的 `Entity::id/position/isReal`、`Controller::*`、`g_entityMovementCallback` 为 cellapp 可执行体独有符号；Linux .so 加载期向宿主解析、Windows MODULE 链接期必须全解析而引擎无 exe 导出机制。
  2. 收益：cellapp_extension 是**示例扩展模块**（examples/），非核心交付面；解锁它仅让一个示例能在 Windows 编译，价值有限。
  3. 成本：三选一均为架构级设计件（.def 导出表／dllexport 宏注入／改托管接口），需动构建系统与接口面，且与 Linux 轨行为需保持一致。
  4. 风险：改托管接口会波及既有扩展 API；.def/dllexport 需逐符号枚举，维护负担重。
  5. 影响面：已门控出 MSVC 构建，Windows 门禁不被其阻塞；11/11 exe 车道已全绿（探针 #31 d3a2336a），本件不入增量。
- **立即执行动作**：无（记录决策）。✅ 已执行（记录决策，commit aeb76683）
- **不做／延后**：不实现三选一；维持门控；待「示例扩展要在 Windows 上跑」成为真实需求再立项。
- **教学**：这个扩展模块是「插件」——它要借用游戏服务端主程序里的函数（实体位置、控制器等）。Linux 上插件加载时才找这些函数，Windows 上链接时就得全部对上，而主程序没把函数"送出来"（没有导出机制）。三选一就是三种"把函数送出来"的办法，每种都要改构建系统和接口，是架构级改动；而它只是个示例模块，不是核心功能，所以先放着，等真需要再说。

## io_uring / 批量 UDP 评估（todo.md:45）

- **推荐决策**：维持暂缓——用户 2026-10-15 已明确暂不开发；待性能数据触发，不预设。
- **理由**：
  1. 已定案：用户明确「暂缓」；保留条目待性能数据触发（todo 原文）。
  2. 触发前提：per-tick 网络耗时/超预算计数指标已落地（`EventDispatcher::processNetwork` 墙钟耗时扣除空闲 select 等待，暴露 maxNetworkTime/numOverBudgetTicks/networkTimeBudget 默认 10ms，挂 timing watcher；test_event_stall +2 用例，network_test 223→225）。
  3. 是否引入 io_uring/批量 UDP 仍凭该指标在生产侧的数据决定——数据未到，不预设。
  4. 风险：低——指标已埋好，随时可读数。
- **立即执行动作**：无（记录决策，确认用户已定暂缓口径）。✅ 已执行（记录决策，commit aeb76683）
- **不做／延后**：不开发 io_uring/批量 UDP；待生产侧指标触发再评估。
- **教学**：这个不是「做不做」的选择，用户已经说了暂缓。它的触发条件是「先测到网络每帧耗时超预算」——指标已经埋好，等生产环境跑出数据再决定要不要上更快的网络方案。

## 集群级黑盒回归链（todo.md:66）

- **推荐决策**：暂缓——需用户提供集群物料或拍板立项；不在仓内伪造物料。
- **理由**：
  1. 卡点事实：仓内无可拉起集群的物料——`MultiProcTestCase`（`lib/unit_test_lib/multi_proc_test_case.hpp`）只是同测试二进制多进程框架，非集群 harness；仍缺集群 bootstrap 编排＋DB 实例＋端到端断言脚本三件物料。
  2. 已到位：`programming/fantasydemo/res/server/bw.xml`（自足服务端配置）＋`res/scripts/db.xml`（LogOnMapping）＋ entity_defs 树与实体脚本（B 档 R4）。
  3. 属多日独立工程（集群 bootstrap＋配置＋DB＋断言），非单轮可推进；用户给物料或拍板立项后方可开工。
  4. 成本：在仓内伪造集群物料会失真（无真实 DB 组件与配置），且违反「不在仓内伪造物料」的既有记录。
  5. 风险：低——P3 优先级，不阻塞门禁。
- **立即执行动作**：无（记录决策）。✅ 已执行（记录决策，commit aeb76683）；**待用户**：提供集群物料（bootstrap 编排＋DB 实例＋端到端断言三件）或拍板立项——属用户侧物料/立项决策，仓内不伪造。
- **不做／延后**：不伪造集群物料；等用户提供物料或拍板立项后按最小链验收（login→实体创建→Cell 切换→BaseApp 崩溃恢复→DB digest 一键冒烟）。
- **教学**：这是一条「一键冒烟」：从登录、建角色、切场景，到故意让服务器崩溃再恢复，最后校验数据库，一条龙自动跑完。它需要先有一套能真正拉起来的游戏服务器集群（好几个进程＋数据库），现在仓库里只有服务器配置和实体脚本，还缺「拉起集群的编排」「数据库实例」「端到端断言」三样。这几样是好几天的独立工程，而且数据库得用户来定，所以先挂起，等用户拍板或给物料。

## 汇总

| 条目 | 推荐决策 | 立即动作 |
|---|---|---|
| R3-a 三树删除 | 保留三树，不删除（CI 经济取舍） | 无 |
| R3-b cellapp_extension 导出 | 维持门控，导出方案延后 | 无 |
| io_uring / 批量 UDP | 维持暂缓（用户已定） | 无 |
| 集群级黑盒回归链 | 暂缓，待用户给物料或拍板立项 | 无 |

> 本批为决策落档，无新实现；R3 后半按「保留三树＋导出延后」作为可接受收口，exe 车道 11/11 绿不受影响。
>
> 执行状态（2026-10-30 exec-actions 批）：4/4 项立即动作已执行（均为记录决策，见各项 ✅ 与 commit aeb76683）；待用户 1 项——集群级黑盒回归链（用户提供集群物料或拍板立项）。
