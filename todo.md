# todo.md — 巡检建账

- 账期：patrol-20261014-R3（2026-10-14）
- 盘点来源：README.md、docs/architecture/*、docs/upgrade-plan/*、docs/python-313-migration.md、docs/vcpkg-migration.md、docs/cpp23-migration.md、TESTING.md、git 历史
- 口径：已实现 = 代码/工作流已落地且可给 commit 或运行证据；只有设计 = 有方案/登记但无承载物；完全没有 = 连设计都没有。架构章是逆向源码分析，其「源码验证重点」属验收建议，不据此记为已验收。
- 期次：P1 = 下批立即；P2 = 近期；P3 = 远期或条件触发。

## A. 已实现（附证据）

- [x] Python 2.7 → 3.13.16 内嵌解释器迁移（换源、补丁重移植、BWHooks 经 PyMem_SetAllocator 重接线、全链构建+冒烟） — `3260bdd9` 起批次链，安全补丁升级 `2bdfc5aa`；台账 docs/python-313-migration.md
- [x] vendored OpenSSL 1.0.0d 消除，第三方依赖 vcpkg manifest 化（openssl/curl/jsoncpp/zlib/libpng/sqlite3/bzip2） — `2bdfc5aa`
- [x] vcpkg manifest 钉 builtin-baseline（可复现解析） — `1180ece6`
- [x] 单测门禁 21 模块 / 1355 用例全绿（2026-10-14 实测，网络故障注入批次后；批次 28 时为 1352） — `dc5cd043`、`5338b828`、`f2c3ed23`、`4e64cf87`、`8216684b`、`6db1b3e3`、`a93fcecf`；TESTING.md 与 docs/upgrade-plan/migration-status.md
- [x] 覆盖率补强批次 1–28 完成（profiler 6%→94%、watcher 14%→22%、fixed_sized_allocator 0%→99%、quad_tree 0%→98%、bsp 0%→91%、smartpointer 84/91→117/118、script_math 92.3%→97.0%、datasection 19.3%→99.7%、pywatcher 43.5%→93.4%、bwresource 43%→62%、method_description 32%→70%） — 各批 hash 见上
- [x] 每日构建 workflow（make 全量 + 21 模块门禁 + tar.gz/deb/rpm 打包，48 小时新鲜度判定、vcpkg 按 baseline 克隆 + binary cache、产物命名版本+日期+短sha） — `57c863c7`、`b365012e`
- [x] push/PR CI workflow（cstdmf CMake 样板构建 + ctest，vcpkg 缓存，失败日志按步骤定位） — `7a4ff703`
- [x] Linux CMake 双轨样板（cstdmf 组件独立构建 427 用例与 make 同口径绿 + 一键脚本 scripts/build-cstdmf-cmake.sh） — `8cd23406`、`c6bfaf74`（427 口径复核；398 为首次落地时的旧数）
- [x] libbwpython3.13 源码依赖缺口修复（vendored 源码变更触发 libpython.a → python.exe → 共享模块重建链） — `9877dd65`
- [x] 架构研究文档集 28 篇（docs/architecture/，逐章 file:line 源码证据）+ VitePress 文档站 — 以 docs/ 现存内容为证
- [x] README 项目状态数字与门禁实测一致 — `9a9e923f`；2026-10-13 审计同步为 1352 用例 / 批次 1–28；2026-10-14 同步为 1355（network_test 210 → 213，故障注入 +3）
- [x] 规划入口缺失（docs/implementation-plan.md 与 docs/adr/ 不存在，指令引用落空） — README 项目状态段登记本 todo.md 为规划入口 — `9a9e923f`；2026-10-14 复核：docs/adr/ 已建（索引 + ADR 0001/0002/0003）并挂 VitePress 侧栏，规划入口已落地 — `642e5cd5`

## B. 只有设计（有登记/方案，未落地）

- [x] 覆盖率批次 28：resmgr/bwresource.cpp（精准插桩 322/746 43% → 464/746 62%，17 用例）+ entitydef/method_description.cpp（精准插桩 153/466 32% → 329/466 70%，33 用例）。新用例全绿（21 模块 1352），TESTING.md/migration-status/台账已同步 — `e4872402`
- [x] R2 third_party/python_modules Py2 生态包（Twisted 11、SQLAlchemy 0.6、Zope 2.11、oursql 等）换 Py3 兼容版并重新启用。验收：依赖工具 import 冒烟通过，全量门禁绿。期次 P2 — 已落地：删 7 个 Py2 包（Twisted 11 / SQLAlchemy 0.6.6 / Zope 2.11.4 / Pympler 0.3.0 / pika 0.9.13 / msgpack-python 0.4.2 / Pympler 0.3.0 补丁），vendor 14 个 Py3 源（Twisted 24.7 / SQLAlchemy 1.4.54 / zope.interface 7 / Pympler 1.0 / pika 1.3.2 / msgpack 1.1.0 + setuptools 75.3 / typing_extensions 4.12 / attrs 24.2 / constantly / incremental / Automat / hyperlink / idna，Twisted 传递依赖闭包齐备）；`third_party_python_modules.mak` 重写为每包单规则（tar 解包 → cp 到 `game/res/bigworld/scripts/{common,server_common}/site-packages` → .pth 标记）；`Makefile:351` include 恢复启用（进默认构建的 bw-miscellaneous，单测门禁不受影响）；验收脚本 `scripts/python-modules-smoke-import.py`（tarball 源树 14 包 + 安装后目录 15 模块两相 import 冒烟，内嵌 3.13.16 全绿）。全量门禁 21 模块 / 1355 用例 / 0 失败。卡点②已按用户拍板核销（验收口径 = import 冒烟，不把 site-packages 入库，`.gitignore` 对 `game/` 的排除保持）；卡点③已按用户拍板落地（重新 vendor Py3 源）。遗留子项：oursql 是 Py2 C 扩展、无 Py3 移植，需另选 Py3 MySQL 连接器后才能恢复其目标（tarball 留档）；`Imaging-1.1.7`/`jpegsrc.v6b` 是 C 扩展树、服务 Windows `tools/` 轨，不在本次范围 — 见本次提交
- [x] 组件 CMake 样板推广（剩余 entitydef）。验收：每组件一条命令构建+单测绿，CI 纳入，用例数与 make 链一致。期次 P2 — 已落地：四个样板齐（cstdmf 427、resmgr 88、network 210、entitydef 318 用例，均与 make 链同口径），CI matrix `[cstdmf, resmgr, network, entitydef]`。时间线：resmgr `cmake/samples/resmgr`（2026-10-13，88 用例绿）→ network `cmake/samples/network`（2026-10-14，210 用例绿，CI matrix 三项）→ 前置工程 `cmake/samples/python`（vendored CPython 3.13 无 CMake 构建系统，用 ExternalProject 包 configure+make，只出 `libpython3.13.a`，拷 Lib 作 stdlib，构建门禁核对 8 个 BWHooks 符号）→ entitydef `cmake/samples/entitydef`（14 库 + `entitydef_test`，318 用例绿）。R2/R3 复核结论留档：① connection 无独立 unit_test 属重复建设，其 26 TU 已随 network_test 覆盖；② entitydef 的决定性卡点是 Python——`lib/pyscript/script.cpp` 直接调用 `BW_Py_malloc/free/calloc/realloc/setHooks`，只存在于 vendored CPython（`Python/bwhooks.c`），vcpkg python3 不提供，已由前置样板解决。工程要点：`BWPython::Python` 必须 `IMPORTED GLOBAL`（否则跨 add_subdirectory 不可见）；vendored `third_party/png`/`zip` 两棵树是死清单，实际走 vcpkg 的 `PNG::PNG`/`ZLIB::ZLIB`；导出宏面逐个照各 `Makefile.rules` 的 `exportFor` 推导。详见 vcpkg-migration §3.4/§3.5
- [x] 网络故障注入扩展（DBApp 卡死/慢查询注入、跨进程网络分区、clock jump、GIL 饥饿/后台线程长阻塞）。验收：四场景可在单测/MultiProcTestCase 框架内复现并可断言。期次 P2（2026-10-14 R2 进度：① DBApp 卡死/慢查询注入已落地——新建 `lib/network/unit_test/test_request_failure.cpp` 3 用例（network_test 210→213）：hang peer 一次一发请求超时断言 `REASON_TIMER_EXPIRED`、通道被拆断言 `REASON_CHANNEL_LOST`、正控用例证明同一进程内消息双向可达。此前全仓请求失败路径零断言（既有测试 handleException 仅记日志）。工程要点：channel 上请求按设计不超时（UDPBundle::startRequest 有 WARNING_MSG），超时场景须走一次一发请求，与 `DBAppInterfaceUtils::executeRawDatabaseCommand` 同形状；通道丢失用 Channel::destroy()（同步触发 onChannelGone→cancelRequestsFor），condemn() 会推迟到 CondemnedChannels 的 60s AGE_LIMIT。② clock jump 已落地：NetworkInterface 内部 dispatcher 跟随主 dispatcher 时钟源（生产两侧同为 timestamp()，行为不变），`RequestFail_clockJumpForwardTimesOutOnce`/`RequestFail_clockBackwardJumpHoldsDeadline` 通道级用例可复现可断言，hungPeerTimesOut 同步去墙钟。③ GIL 饥饿已落地：TimeQueueT 记录 timer 迟到量（maxLateness/numLateTimers/resetLateness），EventDispatcher 暴露 maxTimerLateness/numLateTimers/resetTimerLateness/now() 并挂 timing watcher（maxTimerLateness 按秒读出）；`lib/network/unit_test/test_event_stall.cpp` 4 用例（on-time 不记数、hog 任务推钟使 +1s timer 迟 1s 触发——GIL 饥饿的可断言信号、reset 后恢复 on-time、now() 与时钟源对齐）+ cstdmf `test_time_queue.cpp` 5 用例（on-time 归零、迟到单发、批次最差值、重复 timer 补发语义、reset 保队列）。顺带修两个既有测试 bug：`SafeReferenceCount_atomicHammer_threads` 的 CHECK 在 SimpleThread 析构 join 之前执行（与 hammer 线程竞态，负载下约 25% 翻车），改块作用域先 join；`TimeQueue_repeatingTimerLatenessIsPerDeadline` 期望改为补发语义（单 pass 触发全部逾期次数）。network_test 219→223，cstdmf_test 427→432，全量门禁 21 模块/1370 用例/0 失败，make/CMake 双轨绿）
- [x] ADR 体系（重大决策散落各长文）。验收：建 docs/adr/ + 补记首批 3 篇（Python 3.13、vcpkg 切换、make/cmake 双轨，一决策一页含背景/取舍/后果）。期次 P2 — 已建 docs/adr/（索引 + 0001/0002/0003）并挂 VitePress 侧栏，文档站构建绿
- [ ] R3 Windows 构建（PCbuild/VS 工程迁移，MSVC 未验证）。验收：平台无关库（cstdmf/math）过 MSVC 编译门禁，vendored png/zip/sqlite 三树删除。期次 P3
- [ ] exe 打包（依赖 R3）。验收：Windows 产物进入每日构建或独立 workflow，干净环境可复现。期次 P3
- [x] 虚拟时钟注入（核心 timer 无 IClock/TimeProvider，timestamp() 全局内联）。验收：EventDispatcher/TimeQueue 可注入时钟且默认行为不变，sleep 型时序测试改造。期次 P3 — 已落地：EventDispatcher ClockFn 注入构造（默认 realClock，行为不变）+ `lib/network/unit_test/test_virtual_clock.cpp` 4 用例，network_test 213→217，make/CMake 双轨绿。边界：NetworkInterface 内部 dispatcher 固定真实时钟，超时类用例仍走墙钟
- [ ] 观测面现代化（结构化日志字段/trace-correlation id、日志采样与动态级别、watcher 拆只读观测 vs 可写控制）。验收：跨进程日志带 EntityID/SpaceID/Channel 关联字段，watcher 写操作权限可配置并有审计点。期次 P3 — 2026-10-14 R3 进度：审计点已落地——网络侧 watcher 写的唯一汇入口是 `WatcherPathRequestV1/V2::setWatcherValue`（全仓仅 `lib/network/watcher_packet_handler.cpp` 实例化，本地控制台写也走同一 nub），两处统一发 `Watcher write audit` NOTICE（V1 带 path+value+结果，V2 带 path+结果，成功失败都记，失败保留原 WARNING）；`test_watcher.cpp` +3 用例（V1 accepted、V1 被 read-only 拒、V2 accepted），经 `DebugFilter::addMessageCallback` 拦截断言，cstdmf_test 432→435。剩余分叉：权限可配置需先定跨进程配置面（本仓无共享 config reader，各 app main.cpp 自管；ACL 可直接插在审计线旁的汇入口），结构化日志关联字段需传播设计（EntityID/SpaceID/Channel 如何跨进程随日志走）——两者都是设计决定，非纯编码。
- [ ] 热重载生产化（版本化 schema、双版本兼容窗口、dry-run、失败回滚、跨进程一致性屏障）。验收：迁移路径测试覆盖 Entity::migrate 语义保持 + 失败无半迁移状态。期次 P3
- [ ] R4 客户端/FantasyDemo 资源 Linux 验证。验收：fantasydemo 资产在 Linux 全量构建下可加载冒烟。期次 P3
- [ ] io_uring / 批量 UDP 评估（条件触发：定位到 per-packet syscall 瓶颈才做）。验收：先落 per-tick 网络耗时/超预算计数指标，凭数据决定。期次 P3 — 2026-10-14 R3 进度：per-tick 网络耗时/超预算计数指标已落地——`EventDispatcher::processNetwork` 用真实时钟测墙钟耗时并扣除 poller spareTime 里的空闲 select 等待（空闲 tick 不记超支），暴露 maxNetworkTime/numOverBudgetTicks/resetNetworkTimeStats/networkTimeBudget（默认 10ms），并挂 timing watcher（maxNetworkTime 按秒读出）；test_event_stall 加 2 用例（socketpair 数据报 tick 记入 max 与超预算计数、idle tick 不记 + reset 保预算）。network_test 223→225。是否引入 io_uring/批量 UDP 仍凭该指标在生产侧的数据决定，不预设。

## C. 完全没有（无设计无承载物）

- [ ] 集群级黑盒回归链（login → 实体创建 → Cell 切换 → BaseApp 崩溃恢复 → DB digest 的端到端一键冒烟，现无任何脚本/框架）。验收：最小链可在 CI 或本机一键运行并断言终态。期次 P3
- [ ] Windows CI runner（无，依赖 R3）。期次 P3

## 维护规则

- 每完成一项：勾选并附 commit hash；新事项按 B/C 档入账并给验收口径与期次。
- 不许把设计稿/分析章记成已完成；架构章的「源码验证重点」只是验收建议来源。
- 每次巡检（patrol-*）更新本账并单独 commit。
