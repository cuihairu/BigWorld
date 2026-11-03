# todo.md — 需求账本

- 账期：patrol-20261102-R4（2026-11-02）
- 口径：已实现 = 代码/工作流已落地且有 commit 或运行证据；只有设计 = 有方案/登记但无承载物；完全没有 = 连设计都没有。
- 期次：P1 = 下批立即；P2 = 近期；P3 = 远期或条件触发。
- 每条需求一行；已完成项保留一条证据行（commit + 测试数），叙事细节见 docs/ 对应台账。

## A. 已实现（附证据）

- [x] Python 2.7 → 3.13.16 内嵌解释器迁移 — `3260bdd9` 起批次链，安全补丁 `2bdfc5aa`；台账 docs/python-313-migration.md
- [x] 第三方依赖 vcpkg manifest 化（openssl/curl/jsoncpp/zlib/libpng/sqlite3/bzip2） — `2bdfc5aa`
- [x] vcpkg manifest 钉 builtin-baseline — `1180ece6`
- [x] 单测门禁全绿（21 模块 / 1505 用例，2026-10-15 实测；批次 52 后 1521 用例） — `dc5cd043` 等；TESTING.md 与 docs/upgrade-plan/migration-status.md
- [x] 覆盖率补强批次 1–34 完成（语义证据 lane：supersede 改名语义、method_description 纯助手族） — 各批 hash 见迁移台账
- [x] 每日构建 workflow（make 全量 + 21 模块门禁 + tar.gz/deb/rpm，48 小时新鲜度） — `57c863c7`、`b365012e`
- [x] push/PR CI workflow（cstdmf CMake 样板构建 + ctest） — `7a4ff703`
- [x] Linux CMake 双轨样板（cstdmf 427 用例与 make 同口径绿） — `8cd23406`、`c6bfaf74`
- [x] libbwpython3.13 源码依赖缺口修复 — `9877dd65`
- [x] 架构研究文档集 28 篇 + VitePress 文档站 — 以 docs/ 现存内容为证
- [x] README 项目状态数字与门禁实测一致 — `9a9e923f`
- [x] 规划入口缺失（docs/implementation-plan.md 与 docs/adr/ 不存在） — `642e5cd5`（docs/adr/ 已建）

## B. 只有设计（有登记/方案，未落地）

- [x] 覆盖率批次 28：resmgr/bwresource.cpp 43%→62%（17 用例）+ entitydef/method_description.cpp 32%→70%（33 用例） — `e4872402`
- [x] 覆盖率批次 29：entitydef/entity_description.cpp 0%→64%（30 用例） — `9d9dc38b`
- [x] 覆盖率批次 30：network/machine_guard.cpp 28%→87%（27 用例） — 台账 §7.30
- [x] 覆盖率批次 31：entitydef/entity_description.cpp MD5 族 64%→70%（11 用例） — 台账 §7.31
- [x] 覆盖率批次 32：entitydef/entity_description.cpp 流写侧访客族 70%→89%（7 用例） — 台账 §7.32
- [x] 覆盖率批次 33（语义证据 lane）：entitydef supersede 族（entitydef_test 366→370，全套 1472→1476） — 台账 §7.33
- [x] 覆盖率批次 34：entitydef/method_description.cpp 纯助手族 70%→72%（5 用例，全套 1481） — 台账 §7.34
- [x] 覆盖率批次 35（观测面 lane）：cellapp 实体消息 EntityID/SpaceID 关联接线（cellapp_test 8→12，全套 1485） — 台账 §7.35
- [x] R2 third_party/python_modules Py2 生态包换 Py3 兼容版并重新启用（删 7 个 Py2 包，vendor 14 个 Py3 源；验收 import 冒烟全绿） — 台账与 scripts/python-modules-smoke-import.py
- [x] 组件 CMake 样板推广（剩余 entitydef；四样板齐：cstdmf 427 / resmgr 88 / network 210 / entitydef 318 用例） — vcpkg-migration §3.4/§3.5
- [x] 网络故障注入扩展（DBApp 卡死/慢查询注入、跨进程网络分区、clock jump、GIL 饥饿/后台线程长阻塞） — 台账与 TESTING.md
- [x] ADR 体系（docs/adr/ 索引 + 0001/0002/0003） — `642e5cd5`
- [ ] R3 Windows 构建（PCbuild/VS 工程迁移，MSVC 未验证）：验收前半已达成（cstdmf+math 双腿过 MSVC 门禁），三树删除与 cellapp_extension 宿主符号导出仍待用户决 — `182e7c55`
- [x] exe 打包（Windows 产物进入每日构建或独立 workflow，干净环境可复现） — `14ebd63a`→`31ce396c`→`68c2c1f1`→`98fd9b19`
- [x] 虚拟时钟注入（EventDispatcher/TimeQueue 可注入时钟且默认行为不变） — lib/network/unit_test/test_virtual_clock.cpp 4 用例
- [x] 观测面现代化（结构化日志字段/trace-correlation id、日志采样与动态级别、watcher 拆只读观测 vs 可写控制） — `33a06559`
- [ ] 热重载生产化（版本化 schema、双版本兼容窗口、dry-run、失败回滚、跨进程一致性屏障）：迁移路径测试已覆盖 Entity::migrate 语义，失败回滚已建模但引擎接线待执行承载 — `2d65213e`、`66d7cb48`、`e19b07d4`
- [x] R4 客户端/FantasyDemo 资源 Linux 验证（程序化生成最小占位树 + 生成脚本可重建） — entitydef_test 375→380
- [ ] io_uring / 批量 UDP 评估（暂缓：用户 2026-10-15 明确暂不开发；保留条目待性能数据触发） — 无
- [ ] 集群级黑盒回归链（login → 实体创建 → Cell 切换 → BaseApp 崩溃恢复 → DB digest 端到端一键冒烟）：缺集群 bootstrap 编排 + DB 实例 + 端到端断言脚本 — 无
- [x] Windows CI runner（无，依赖 R3）：`.github/workflows/ci-windows.yml` 两腿全绿（MSVC 19.51 / VS 18 / x64） — `466ff3e3`→`9b95a921`

## C. 完全没有（无设计无承载物）

- [x] server 侧 reload 迁移语义测试承载（cellapp 级 scaffold：让 `EntityType::migrate` 的 modules 换窗与 `Entity::migrate` 的 `__class__` 换回滚可断言） — `9b85a334`（cellapp_test 31 用例）
- [ ] 集群级黑盒回归链（无任何脚本/框架）：缺集群 bootstrap 编排 + DB 实例 + 端到端断言脚本三件物料，需用户给物料或拍板立项 — 无

## 维护规则

- 每完成一项：勾选并附 commit hash；新事项按 B/C 档入账并给验收口径与期次。
- 不许把设计稿/分析章记成已完成；架构章的「源码验证重点」只是验收建议来源。
- 每次巡检（patrol-*）更新本账并单独 commit。

## D. docs 英文对应缺口（子目录）登记（2026-10-16，2026-10-24 全部闭合）

- [x] docs 双语化：49 个 `.en.md` 镜像命名协议与页内手写切换器废弃，改用 VitePress 官方 `locales`（root 英文 / `docs/zh/` 中文，54 页 ↔ 54 页 1:1 全配对，内部链接 0 死链） — `3ed88fe5`、`5b3775cf`
- [x] 子目录缺口逐项登记：adr 4 / architecture 28 / getting-started 4 / upgrade-plan 7 / references 1 / analysis 5 全配对 — `3ed88fe5`（2026-10-24 闭合）

## 待办（P2，2026-11-01 用户令全舰队推广）

- [ ] UI-AUDIT · P2 · 界面审核（impeccable 流程）：原生桌面界面此前不在 web 界面审核范围，补审——按 impeccable 审计（层级/排版/对比度 a11y/布局/边界态），报告 + 截图入 docs/review/，坏损当场修。
- [ ] DOC-GIF · P2 · 文档 · 配置开关效果动态图（每个开关 off→on 对比 + GIF）：枚举本仓全部「开启后有效果」的开关/配置项；自动流水线生成证据（GUI 用应用截图或录屏，CLI 用终端录屏）；文档「配置效果」页逐开关嵌入 off/on 图 + GIF；验收：清单内每个开关都有 off/on + GIF，流水线可重跑，兼作配置效果回归测试。

## 漏洞扫描遗留（2026-11-03 全舰队扫描，源 scratch/vuln-scan-*.md）

- [ ] VULN-BIGWORLD-001 [严重·GHSA-cg2g-xh64-v4q7/GHSA-mrv2-3pqr-j5jr] dompurify 3.4.16→≥3.4.17（monaco-editor 引入）（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-002 [高危] @vue/server-renderer <3.5.42→≥3.5.42（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-003 [高危] vue 3.2.13–3.5.41→≥3.5.42（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-004 [高危·GHSA-28wg-ghj8-5hjv/GHSA-2v37-7h3g-55p8] nanoid 3.3.12→≥3.3.18（舰队安全钉口径 3.3.20）（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-005 [高危] source-map-js 1.0.0–1.2.1→≥1.2.2（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-006 [高危] vite ≤6.4.3→≥6.4.4（npm audit fixAvailable=false，需先迁 pnpm/升直接依赖）（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
