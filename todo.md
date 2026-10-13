# todo.md — 巡检建账

- 账期：patrol-20261013-B1（2026-10-13）
- 盘点来源：README.md、docs/architecture/*、docs/upgrade-plan/*、docs/python-313-migration.md、docs/vcpkg-migration.md、docs/cpp23-migration.md、TESTING.md、git 历史
- 口径：已实现 = 代码/工作流已落地且可给 commit 或运行证据；只有设计 = 有方案/登记但无承载物；完全没有 = 连设计都没有。架构章是逆向源码分析，其「源码验证重点」属验收建议，不据此记为已验收。
- 期次：P1 = 下批立即；P2 = 近期；P3 = 远期或条件触发。

## A. 已实现（附证据）

- [x] Python 2.7 → 3.13.16 内嵌解释器迁移（换源、补丁重移植、BWHooks 经 PyMem_SetAllocator 重接线、全链构建+冒烟） — `3260bdd9` 起批次链，安全补丁升级 `2bdfc5aa`；台账 docs/python-313-migration.md
- [x] vendored OpenSSL 1.0.0d 消除，第三方依赖 vcpkg manifest 化（openssl/curl/jsoncpp/zlib/libpng/sqlite3/bzip2） — `2bdfc5aa`
- [x] vcpkg manifest 钉 builtin-baseline（可复现解析） — `1180ece6`
- [x] 单测门禁 21 模块 / 1352 用例全绿（2026-10-13 实测，批次 28 后） — `dc5cd043`、`5338b828`、`f2c3ed23`、`4e64cf87`、`8216684b`、`6db1b3e3`、`a93fcecf`；TESTING.md 与 docs/upgrade-plan/migration-status.md
- [x] 覆盖率补强批次 1–28 完成（profiler 6%→94%、watcher 14%→22%、fixed_sized_allocator 0%→99%、quad_tree 0%→98%、bsp 0%→91%、smartpointer 84/91→117/118、script_math 92.3%→97.0%、datasection 19.3%→99.7%、pywatcher 43.5%→93.4%、bwresource 43%→62%、method_description 32%→70%） — 各批 hash 见上
- [x] 每日构建 workflow（make 全量 + 21 模块门禁 + tar.gz/deb/rpm 打包，48 小时新鲜度判定、vcpkg 按 baseline 克隆 + binary cache、产物命名版本+日期+短sha） — `57c863c7`、`b365012e`
- [x] push/PR CI workflow（cstdmf CMake 样板构建 + ctest，vcpkg 缓存，失败日志按步骤定位） — `7a4ff703`
- [x] Linux CMake 双轨样板（cstdmf 组件独立构建 398 用例与 make 同口径绿 + 一键脚本 scripts/build-cstdmf-cmake.sh） — `8cd23406`
- [x] libbwpython3.13 源码依赖缺口修复（vendored 源码变更触发 libpython.a → python.exe → 共享模块重建链） — `9877dd65`
- [x] 架构研究文档集 28 篇（docs/architecture/，逐章 file:line 源码证据）+ VitePress 文档站 — 以 docs/ 现存内容为证
- [x] README 项目状态数字与门禁实测一致 — `9a9e923f`；2026-10-13 审计同步为 1352 用例 / 批次 1–28
- [x] 规划入口缺失（docs/implementation-plan.md 与 docs/adr/ 不存在，指令引用落空） — README 项目状态段登记本 todo.md 为规划入口 — `9a9e923f`

## B. 只有设计（有登记/方案，未落地）

- [x] 覆盖率批次 28：resmgr/bwresource.cpp（精准插桩 322/746 43% → 464/746 62%，17 用例）+ entitydef/method_description.cpp（精准插桩 153/466 32% → 329/466 70%，33 用例）。新用例全绿（21 模块 1352），TESTING.md/migration-status/台账已同步 — `e4872402`
- [ ] R2 third_party/python_modules Py2 生态包（Twisted 11、SQLAlchemy 0.6、Zope 2.11、oursql 等）换 Py3 兼容版并重新启用。验收：依赖工具 import 冒烟通过，全量门禁绿。期次 P2（2026-10-13 复核为卡点：安装目标树 `game/res` 被 .gitignore:17 整目录排除、源包目录未纳入 git，产物无处落地无从验收；in-repo 唯一消费方 `libpython_tools`→`tools/worldeditor` 属 R3 Windows 范围；需先定依赖获取方式〔pip/vcpkg 取代 vendored tarball〕才能开工）
- [ ] 组件 CMake 样板推广（network/entitydef/connection 等按 cmake/samples/cstdmf 模式逐个复制）。验收：每组件一条命令构建+单测绿，CI 纳入，用例数与 make 链一致。期次 P2（2026-10-14 进度：resmgr 样板落地 `cmake/samples/resmgr` + `scripts/build-resmgr-cmake.sh`，88 用例与 make 同口径绿，CI job 改 matrix `[cstdmf, resmgr]`；两样板 `--root` 多一级 latent 错位修正；复核剩余：connection 无 unit_test 需先建测试面、entitydef 依赖 8 库、network 待做）
- [ ] 网络故障注入扩展（DBApp 卡死/慢查询注入、跨进程网络分区、clock jump、GIL 饥饿/后台线程长阻塞）。验收：四场景可在单测/MultiProcTestCase 框架内复现并可断言。期次 P2
- [x] ADR 体系（重大决策散落各长文）。验收：建 docs/adr/ + 补记首批 3 篇（Python 3.13、vcpkg 切换、make/cmake 双轨，一决策一页含背景/取舍/后果）。期次 P2 — 已建 docs/adr/（索引 + 0001/0002/0003）并挂 VitePress 侧栏，文档站构建绿
- [ ] R3 Windows 构建（PCbuild/VS 工程迁移，MSVC 未验证）。验收：平台无关库（cstdmf/math）过 MSVC 编译门禁，vendored png/zip/sqlite 三树删除。期次 P3
- [ ] exe 打包（依赖 R3）。验收：Windows 产物进入每日构建或独立 workflow，干净环境可复现。期次 P3
- [ ] 虚拟时钟注入（核心 timer 无 IClock/TimeProvider，timestamp() 全局内联）。验收：EventDispatcher/TimeQueue 可注入时钟且默认行为不变，sleep 型时序测试改造。期次 P3
- [ ] 观测面现代化（结构化日志字段/trace-correlation id、日志采样与动态级别、watcher 拆只读观测 vs 可写控制）。验收：跨进程日志带 EntityID/SpaceID/Channel 关联字段，watcher 写操作权限可配置并有审计点。期次 P3
- [ ] 热重载生产化（版本化 schema、双版本兼容窗口、dry-run、失败回滚、跨进程一致性屏障）。验收：迁移路径测试覆盖 Entity::migrate 语义保持 + 失败无半迁移状态。期次 P3
- [ ] R4 客户端/FantasyDemo 资源 Linux 验证。验收：fantasydemo 资产在 Linux 全量构建下可加载冒烟。期次 P3
- [ ] io_uring / 批量 UDP 评估（条件触发：定位到 per-packet syscall 瓶颈才做）。验收：先落 per-tick 网络耗时/超预算计数指标，凭数据决定。期次 P3

## C. 完全没有（无设计无承载物）

- [ ] 集群级黑盒回归链（login → 实体创建 → Cell 切换 → BaseApp 崩溃恢复 → DB digest 的端到端一键冒烟，现无任何脚本/框架）。验收：最小链可在 CI 或本机一键运行并断言终态。期次 P3
- [ ] Windows CI runner（无，依赖 R3）。期次 P3

## 维护规则

- 每完成一项：勾选并附 commit hash；新事项按 B/C 档入账并给验收口径与期次。
- 不许把设计稿/分析章记成已完成；架构章的「源码验证重点」只是验收建议来源。
- 每次巡检（patrol-*）更新本账并单独 commit。
