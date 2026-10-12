# todo.md — 巡检建账

- **账期**: patrol-20261013-B1（2026-10-13）
- **盘点来源**: `README.md`、`docs/architecture/*`（27 篇源码分析章）、`docs/upgrade-plan/*`、`docs/python-313-migration.md`、`docs/vcpkg-migration.md`、`docs/cpp23-migration.md`、`TESTING.md`、git 历史
- **口径**: 「已实现」= 代码/工作流已落地且可给 commit 或运行证据；「只有设计」= 有方案/登记/验证清单但无承载物；「完全没有」= 连设计都没有。
  架构章（architecture/*）是逆向源码分析，其「源码验证重点」清单属于验收建议，**不**据此把引擎机制记成"本仓已验收"。
- **任务引用核对**: 指令引用的 `docs/implementation-plan.md` 与 `docs/adr/` 在仓内**不存在**，已如实记入"完全没有"档。
- **期次**: P1 = 下批立即；P2 = 近期；P3 = 远期或条件触发（写明触发条件）。

## A. 已实现（附证据）

- [x] Python 2.7 → 3.13.16 内嵌解释器迁移（换源、补丁重移植、BWHooks 经 PyMem_SetAllocator 重接线、全链构建+冒烟） — `3260bdd9` 起批次链，安全补丁升级 `2bdfc5aa`；台账 `docs/python-313-migration.md`
- [x] R1 vendored OpenSSL 1.0.0d 消除：第三方依赖 vcpkg manifest 化（openssl/curl/jsoncpp/zlib/libpng/sqlite3/bzip2），`_ssl/_hashlib` 恢复 — `2bdfc5aa`
- [x] vcpkg manifest 钉 `builtin-baseline`（可复现解析） — `1180ece6`
- [x] 单测门禁 21 模块 / 1220 用例全绿 + 覆盖率补强批次 1–23（profiler.cpp 6%→94%、watcher/fixed_sized_allocator/quad_tree/bsp 收口、假绿审计 13 处修复+孤儿测试复活） — `dc5cd043`、`5338b828`、`f2c3ed23`；现状 `TESTING.md` 与 `docs/upgrade-plan/migration-status.md`
- [x] 每日构建 workflow：make 全量 + 21 模块门禁 + tar.gz/deb/rpm 打包，48 小时新鲜度判定、vcpkg 按 baseline 克隆 + binary cache、产物命名版本+日期+短sha — 首建 `57c863c7`，48h+缓存+命名 `b365012e`；本机三包实跑验证
- [x] push/PR CI workflow（cstdmf CMake 样板构建+ctest，vcpkg 缓存，失败日志按步骤定位） — `7a4ff703`
- [x] Linux CMake 双轨样板：cstdmf 组件独立构建（398 用例与 make 同口径绿）+ 一键脚本 `scripts/build-cstdmf-cmake.sh` — `8cd23406`
- [x] libbwpython3.13 源码依赖缺口：vendored 源码变更现在触发重建链（libpython.a → python.exe → 共享模块），无变更时保持 no-op，触发后收敛不再空转 — `9877dd65`（验证：touch gc.c 触发四级链、还原后 no-op；全量门禁 1220 绿）
- [x] 架构研究文档集 27 篇（`docs/architecture/`，逐章 file:line 源码证据）+ VitePress 文档站 — 以 `docs/` 现存内容为证

## B. 只有设计（有登记/方案，未落地）

- [ ] **覆盖率批次 24+**：首位 `cstdmf/smartpointer.hpp`（679/1519 行 55%）；候选 `resmgr/datasection.cpp`、`pyscript/script_math.cpp`、`pyscript/pywatcher.cpp`、`resmgr/bwresource.cpp`、`entitydef/method_description.cpp`（TESTING.md TODO 表）。
  验收：新用例全绿；模块级数字走全量插桩复测口径；TESTING.md/migration-status/台账同步。 期次 **P1**
- [ ] **README 项目状态数字过期**：仍写"1113 用例、批次 1–18"，现为 1220 用例、批次 1–23（Python 3.13 亦应写明 3.13.16）。
  验收：README 与 migration-status 数字一致。 期次 **P1**
- [ ] **R2 `third_party/python_modules` Py2 生态包**（Twisted 11、SQLAlchemy 0.6、Zope 2.11、oursql 等）：集群监控/日志类运行时 Python 服务不可用（python-313-migration.md 风险表）。
  验收：所列包换 Py3 兼容版；对应 mak 重新启用；依赖工具 import 冒烟通过；全量门禁绿。 期次 **P2**（不在编译关键路径）
- [ ] **组件 CMake 样板推广**：network / entitydef / connection 等按 `cmake/samples/cstdmf` 模式逐个复制（vcpkg-migration.md §3.1）。
  验收：每组件一条命令构建+单测绿；CI 纳入；用例数与 make 链一致。 期次 **P2**
- [ ] **网络故障注入扩展**（testing-fault-injection-coverage.md 登记缺口）：DBApp 卡死/慢查询注入、跨进程网络分区、clock jump、GIL 饥饿/后台线程长阻塞——现有注入仅网络层（PacketSender drop/latency）。
  验收：四场景可在单测/MultiProcTestCase 框架内复现并可断言；场景描述与结果报告成文。 期次 **P2**（统一 chaos 框架为 P3）
- [ ] **ADR 体系**：`docs/adr/` 不存在，重大决策（Python 3.13、vcpkg 切换、make/cmake 双轨）散落各长文。
  验收：建 `docs/adr/` + 补记首批 3 篇（一决策一页，含背景/取舍/后果）。 期次 **P2**
- [ ] **R3 Windows 构建**（PCbuild/VS 工程迁移；python-313-migration.md 风险表）：lib/* 下 BWStandardProject CMake 超级工程未现代化，MSVC 未验证。
  验收：平台无关库（cstdmf/math）过 MSVC 编译门禁 → 客户端/工具工程可配置构建；vendored `third_party/png|zip|sqlite` 三树删除（vcpkg-migration.md §1 前提）。 期次 **P3**
- [ ] **exe 打包**（依赖 R3；packaging-and-daily-build.md 已登记卡点）。
  验收：Windows 产物进入每日构建或独立 workflow；干净环境可复现。 期次 **P3**（卡点=R3）
- [ ] **虚拟时钟注入**（testing-time-control.md：核心 timer 无 IClock/TimeProvider，`timestamp()` 全局内联）：超时/重试/看门狗类测试无法摆脱真实时间。
  验收：EventDispatcher/TimeQueue 可注入时钟且默认行为不变；sleep 型时序测试改造；全量门禁绿。 期次 **P3**
- [ ] **观测面现代化**（observability 章）：结构化日志字段/trace-correlation id、日志采样与动态级别、watcher 拆"只读观测 vs 可写控制"权限。
  验收：跨进程日志带 EntityID/SpaceID/Channel 关联字段；watcher 写操作权限可配置并有审计点。 期次 **P3**
- [ ] **热重载生产化**（hot-reload 章；源码自警"绝不能用于生产"，仅属性回调迁移无 schema 版本化）：版本化 schema、双版本兼容窗口、dry-run、失败回滚、跨进程一致性屏障均无承载物。
  验收：迁移路径测试覆盖 Entity::migrate 语义保持 + 失败无半迁移状态 + EntityDef 不兼容显式拒绝。 期次 **P3**（研究项）
- [ ] **R4 客户端/FantasyDemo 资源 Linux 验证**（python-313-migration.md 风险表）。
  验收：fantasydemo 资产在 Linux 全量构建下可加载冒烟。 期次 **P3**
- [ ] **io_uring / 批量 UDP 评估**（network-io-model.md：EPOLLET 有源码 TODO 但结论是先测瓶颈，"批量 UDP 收发"比换执行模型更贴边界）。
  验收（若立项）：先落 per-tick 网络耗时/超预算计数指标（该章 instrumentation 建议），凭数据决定。 期次 **P3**（条件触发：定位到 per-packet syscall 瓶颈才做）

## C. 完全没有（无设计无承载物）

- [ ] `docs/implementation-plan.md`：不存在（指令引用落空）。规划散落 `docs/upgrade-plan/*` 与本台账。
  验收：要么建单页规划索引，要么在 README 文档站段把本 todo.md 登记为规划入口（二选一）。 期次 **P1**
- [ ] **集群级黑盒回归链**（testing 章"最小 cluster 回归"）：login → 实体创建 → Cell 切换 → BaseApp 崩溃恢复 → DB digest 的端到端一键冒烟，现无任何脚本/框架。
  验收：最小链可在 CI 或本机一键运行并断言终态。 期次 **P3**
- [ ] **Windows CI runner**：无（依赖 R3）。 期次 **P3**

## 维护规则

- 每完成一项：勾选并附 commit hash；新事项按 B/C 档入账并给验收口径与期次。
- 不许把设计稿/分析章记成已完成；架构章的"源码验证重点"只是验收建议来源。
- 每次巡检（patrol-*）更新本账并单独 commit。
