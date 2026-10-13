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
- [x] 单测门禁 21 模块 / 1280 用例全绿 + 覆盖率补强批次 1–26（profiler.cpp 6%→94%、watcher/fixed_sized_allocator/quad_tree/bsp 收口、假绿审计 13 处修复+孤儿测试复活；批次24 smartpointer.hpp 84/91→117/118 测试 TU 视角；批次25 script_math.cpp 92.3%→97.0%、批次26 datasection.cpp 19.3%→99.7% 精准口径收口） — `dc5cd043`、`5338b828`、`f2c3ed23`、`4e64cf87`；现状 `TESTING.md` 与 `docs/upgrade-plan/migration-status.md`
- [x] 每日构建 workflow：make 全量 + 21 模块门禁 + tar.gz/deb/rpm 打包，48 小时新鲜度判定、vcpkg 按 baseline 克隆 + binary cache、产物命名版本+日期+短sha — 首建 `57c863c7`，48h+缓存+命名 `b365012e`；本机三包实跑验证
- [x] push/PR CI workflow（cstdmf CMake 样板构建+ctest，vcpkg 缓存，失败日志按步骤定位） — `7a4ff703`
- [x] Linux CMake 双轨样板：cstdmf 组件独立构建（398 用例与 make 同口径绿）+ 一键脚本 `scripts/build-cstdmf-cmake.sh` — `8cd23406`
- [x] libbwpython3.13 源码依赖缺口：vendored 源码变更现在触发重建链（libpython.a → python.exe → 共享模块），无变更时保持 no-op，触发后收敛不再空转 — `9877dd65`（验证：touch gc.c 触发四级链、还原后 no-op；全量门禁 1220 绿）
- [x] 架构研究文档集 27 篇（`docs/architecture/`，逐章 file:line 源码证据）+ VitePress 文档站 — 以 `docs/` 现存内容为证

## B. 只有设计（有登记/方案，未落地）

- [x] **覆盖率批次 24**：`cstdmf/smartpointer.hpp` 收口——此前无专属测试（覆盖全靠 watcher/circular_queue/memory_debug 顺带），新建 `test_smartpointer.cpp` 29 用例直驱两类引用计数基类（含 4 线程原子锤击与 destroy() 虚钩子）、自由函数族、ConstSmartPointer/SmartPointer 全操作面与派生→基转换；测试 TU 视角 84/91 → 117/118，唯一残余 `:121` 基析构空体为结构性死臂；表值 679/1519 属全量插桩口径撤出 TESTING.md TODO 表；下批首位候选 `pyscript/script_math.cpp`。
  验收（全达成）：cstdmf_test 398→427 全绿连跑 4×；删桩恢复后全量 21 模块门禁 1249 全绿（GATE_EXIT=0，gcda=0、gcov 符数=0 复验）；cmake 双轨同步 49 TU 同跑 427 绿；TESTING.md/migration-status/台账 §7.24 同步。 期次 **P1** — `4e64cf87`
- [x] **覆盖率批次 25**：`pyscript/script_math.cpp` 收口——精准插桩复判 1000/1083（92.3%，单 TU 实例宇宙，表值 655/1920 属全量插桩口径、复判后真实暗区仅 83 行）后新建 `test_script_math_gaps.cpp` 10 用例直驱：通用 PyVector number 协议（一元负/正、标量左乘）、Vector2/4 原地运算符与 NotImplemented 臂、富比较 GT/LE/GE 与 Vector2/4 分发槽、distSqrTo/flatDistSqrTo/flatDistTo/setPitchYaw 实参校验回退、z setter 与 yaw/pitch getter、Matrix/Vector4 Product 单操作数分支、Combiner 缺操作数早退、look-at 近 +Y 上向量回退、Vector4Shader 用户寄存器上限与非临时输出寄存器两拒绝臂；**1050/1083（97.0%）**，残余 33 行全数分类（13 行 Windows-only 构造性死区——ProviderStore 矩阵半边、唯一 autoTick 的 MatrixAnimation 整体 `#if defined( _WIN32 )` 档案符号数 0；3 行 safeSetVector 死臂；8 行 pyGet_x/y/z/w 死成员；9 行宏展开点幻影行）；表值撤出 TESTING.md TODO 表；下批首位候选 `resmgr/datasection.cpp`。
  验收（全达成）：pyscript_test 118→128 全绿连跑 3×；删桩复验后全量 21 模块门禁 1259 全绿（GATE_EXIT=0，gcda=0、gcov 符数=0 复验）；TESTING.md/migration-status/台账 §7.25 同步。 期次 **P1** — `8216684b`
- [x] **覆盖率批次 26**：`resmgr/datasection.cpp` 收口——此前无专属测试，精准插桩复判 169/876（19.3%，表值 600/878 属全量插桩口径撤出 TESTING.md TODO 表）后新建 `test_datasection.cpp` 21 用例直驱：迭代器全协议、索引/首子、openSection 路径学、单复数 16 型读写往返、基类 as*/set* 默认与拒绝面、createAppropriateSection 五臂、经 BinSection::save 的 splitSaveAsFileName 三臂（purgeAll 包夹防缓存污染）、setWatcherValues、copy/copySections、compare/binaryCompare 全差异臂、sanitise 族；复数读写 20 条缺失父守卫以「DirSection 挂不存在基路径」一次驱动零副作用；**873/876（99.7%）**，唯一残余 :90-93 迭代器 operator* 空子回退臂登记为到达即崩的结构性死臂兼源码级隐患（回退体经基类 childSectionName NULL 解引用，gdb SIGSEGV 实证）；下批首位候选 `pyscript/pywatcher.cpp`。
  验收（全达成）：resmgr_test 50→71 全绿；删桩复验后全量 21 模块门禁 1280 全绿（GATE_EXIT=0，gcda=0、gcov 符数=0 复验）；TESTING.md/migration-status/台账 §7.26 同步。 期次 **P1** — `6db1b3e3`
- [x] **覆盖率批次 27**：`pyscript/pywatcher.cpp` 收口——精准插桩复判 312/717（43.5%，表值 416/730 属全量插桩口径撤出 TESTING.md TODO 表）后新建 `test_pywatcher_gaps.cpp` 22 用例直驱：PyObjectWatcher 特化分发、SimplePythonWatcher 全操作面（生产树零实例化公共类）、PyAccessorWatcher 三错误族与 set 失败臂、PyFunctionWatcher 实参/坏载荷/返回值/deferred 全分支、模块级错误臂；**670/717（93.4%）**，残余 47 行全数分类（putPyObjectOnStream 失败臂结构性死代码、Py3 迁移死臂、序列分支不可达、需活钩子臂）；登记源码级隐患三处（visitChildren 映射臂 Py_DECREF(NULL) 崩溃、getPyObjectWatcherData 孤代理 MF_ASSERT 中止、py_addFunctionWatcher comment 不可省）；**假绿新形态取证**：pending Python 异常毒化使 fixture 导入静默失败、用例静默跳过仍报绿——对策 PyErr_Clear + fixture CHECK 化 + 失败即打印；:pyscript_test 128→150；下批首位候选 `resmgr/bwresource.cpp`。
  验收（全达成）：pyscript_test 150 全绿连跑；删桩复验后全量 21 模块门禁 1302 全绿（GATE_EXIT=0，gcda=0、gcov 符数=0 复验）；TESTING.md/migration-status/台账 §7.27 同步。 期次 **P1** — `a93fcecf`
- [ ] **覆盖率批次 28+**：候选 `resmgr/bwresource.cpp`（386/746、48%）、`entitydef/method_description.cpp`（323/478、32%）（TESTING.md TODO 表）。
  验收：新用例全绿；文件级数字走精准插桩口径（模板头分母随插桩 TU 集缩放）；TESTING.md/migration-status/台账同步。 期次 **P1**
- [x] **README 项目状态数字过期**：仍写"1113 用例、批次 1–18"，现为 1220 用例、批次 1–23（Python 3.13 亦应写明 3.13.16）。
  验收：README 与 migration-status 数字一致。 期次 **P1** — `9a9e923f`
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

- [x] `docs/implementation-plan.md`：不存在（指令引用落空）。规划散落 `docs/upgrade-plan/*` 与本台账。
  验收：要么建单页规划索引，要么在 README 文档站段把本 todo.md 登记为规划入口（二选一）。 期次 **P1** — 已选后者：README 项目状态段登记 todo.md 为规划入口，`9a9e923f`
- [ ] **集群级黑盒回归链**（testing 章"最小 cluster 回归"）：login → 实体创建 → Cell 切换 → BaseApp 崩溃恢复 → DB digest 的端到端一键冒烟，现无任何脚本/框架。
  验收：最小链可在 CI 或本机一键运行并断言终态。 期次 **P3**
- [ ] **Windows CI runner**：无（依赖 R3）。 期次 **P3**

## 维护规则

- 每完成一项：勾选并附 commit hash；新事项按 B/C 档入账并给验收口径与期次。
- 不许把设计稿/分析章记成已完成；架构章的"源码验证重点"只是验收建议来源。
- 每次巡检（patrol-*）更新本账并单独 commit。
