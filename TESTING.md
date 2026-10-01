# TESTING — 单测与覆盖率工作台

单测入口、覆盖率口径与缺口 TODO 的单一落点。逐批过程记录见
[docs/python-313-migration.md §7](docs/python-313-migration.md)，
现状仪表见 [docs/upgrade-plan/migration-status.md](docs/upgrade-plan/migration-status.md)。

> 本文件为覆盖率巡检批次 19 新建：任务口径中的 “TESTING/todo 落点” 在仓库中
> 并不存在，按“新建于仓库根”处理。

## 测试入口

```sh
# 全量构建 + 21 模块单测（当前门禁口径）
cd programming && env VCPKG_ROOT=/home/cui/vcpkg make -s -j12 -rR bw-run-all-unit-tests

# 单模块复跑（二进制在仓库根 game/，必须从各组件 unit_test/ 目录用 --root 运行）
cd programming/bigworld/lib/<组件>/unit_test
/game/bin/server/el7/unit_tests/<组件>_test --root <仓库根>
```

嵌 Python 的三个模块（entitydef/pyscript/script）依赖 `game/` 下的 CPython
标准库安装：`bw-run-all-unit-tests` 链路**不会**触发安装（`python_install`
只挂在 make all 链的 `third-party-libraries` 目标下，`user_shouldInstallPython`
仅控制该挂接），树重建后需显式补一次：

```sh
cd programming && env VCPKG_ROOT=/home/cui/vcpkg make -s python_install   # sentinel 幂等
```

## 覆盖率口径

`user_shouldBuildCodeCoverage=1` 加 `-fprofile-arcs -ftest-coverage`，
**必须 `rm -rf programming/bigworld/build/el7` 全量重编**后跑完 21 模块
（产出 ~498 个 .gcda），再 gcovr 聚合：

```sh
gcovr -r /home/cui/workspaces/BigWorld/programming/bigworld --txt -o /tmp/cov.txt \
  --gcov-ignore-parse-errors suspicious_hits.warn_once_per_file \
  --gcov-ignore-parse-errors negative_hits.warn_once_per_file \
  --gcov-ignore-errors output_error \
  --filter '/home/cui/workspaces/BigWorld/programming/bigworld/lib/'
```

跑完须无桩重建恢复（不带覆盖率变量全量重编+重跑门禁）。gcc#68080 的可疑
命中/负命中两形态加 gcov 输出缺失（SanityCheckError，批次 20 起）共三形态
均需容错参数。聚合前确认对象树静止：并发会话的重编会按 TU 删除 gcda，
中途聚合必得残缺数据。

门禁有效性核验（防"测试重跑、树没重建"的假门禁）：重建命令显式
`cd <绝对路径> && pwd` 自证、rm 用绝对路径；跑完后对象树必须**无 gcda**
（`find build/el7 -name '*.gcda' | wc -l` 为 0）、抽查对象无 gcov 符号
（`nm <某>.o | grep -c gcov` 为 0）。

多代理共享树的两个变体（批次 20 实录）：① **无 rm 续跑**——静默探测
（无并发 make 持续 20s 才发车）+ 对象 mtime 代龄全部晚于当日 rm 时刻，
与 gcov 符数/gcda 构成四重等效证据；② 第三方配置态被并发 configure
互踩（config.log 交错、pyconfig.h 全 undef、libpython 编译确定性三连败
且报 `SIZEOF_VOID_P` 未声明）时，定向 `rm -rf build/el7/third_party/python`
重配即恢复，勿动 BigWorld 对象树。

## 模块覆盖率（2026-10-01 批次 22 插桩实跑；两阶段干净复测，仅 cstdmf 实质变动，其余模块与批次 18/20 值一致）

| 模块 | 覆盖率 | 模块 | 覆盖率 |
|------|--------|------|--------|
| script | 91% | resmgr | 58% |
| math | 70% | **cstdmf** | **54%（上轮 51%）** |
| pyscript | 68% | entitydef | 43% |
| db | 67% | scene / sqlite / terrain | 42% / 39% / 34% |
| network | 66% | connection | 24% |
| physics2 | 76% | moo / server / chunk | 13% / 13% / 3% |

## TODO — 缺口 top 清单（未覆盖行数降序，2026-09-28 口径）

已判定不可测/受限的面维持登记（不重复派测）；其余为候选池，按序派测。

| 源文件 | 缺口/总行 | 覆盖率 | 状态 |
|--------|-----------|--------|------|
| chunk/chunk.cpp | 1215/1228 | 1% | 受限：需 ChunkSpace + chunk 装载机 |
| cstdmf/profiler.cpp | 1148/1224 | 6% | 候选（profiler 开关面可测性待判定） |
| connection/server_connection.cpp | 990/990 | 0% | **维持不可测**（真登录管线 + TCPChannel 状态机，批次14/16） |
| cstdmf/smartpointer.hpp | 679/1519 | 55% | 候选（残余为 watch/调试面） |
| entitydef/entity_description.cpp | 665/665 | 0% | 受限：需 EntityType/EntityDef XML 装载机全套 |
| pyscript/script_math.cpp | 655/1920 | 65% | 批次5 后残余；候选（逐函数复判） |
| resmgr/datasection.cpp | 600/878 | 31% | 候选 |
| moo/image.ipp | 589/589 | 0% | 受限：客户端图像合成面 |
| network/machine_guard.cpp | 553/774 | 28% | bwmachined 面不可达部分维持登记（批次17） |
| connection/message_handlers.hpp | 538/538 | 0% | **结构性死代码**（模板 handler 表无人实例化） |
| chunk/chunk_space.cpp | 506/506 | 0% | 受限：需 ChunkSpace + chunk 装载机 |
| connection/replay_controller.cpp | 495/495 | 0% | 受限：录像/回放文件管线（线格式已批次12 测） |
| pyscript/pywatcher.cpp | 416/730 | 43% | 候选 |
| chunk/chunk_boundary.cpp | 403/403 | 0% | 受限：同 chunk 体系 |
| network/logger_endpoint.cpp | 398/398 | 0% | 维持受限登记（真消息日志管线，批次17） |
| resmgr/bwresource.cpp | 386/746 | 48% | 候选 |
| network/logger_message_forwarder.cpp | 335/335 | 0% | 维持受限登记（批次17） |
| entitydef/method_description.cpp | 323/478 | 32% | 候选 |
| server/python_server.cpp | 298/298 | 0% | 受限：需完整 app 装配 |

收口记录（不再在列）：`cstdmf/watcher.hpp` 14%→**22%**（943→1545 行覆盖、+602 实例行，缺口
5471→5398，批次 22；两阶段干净全量插桩同口径，BEFORE 复测 6414/943/14% 与批次 20 快照一致——
旧基线的混沌窗口疑点解除；总行 6414→6943 的 +529 为新测试 TU 带入的模板实例宇宙扩容，口径同
quad_tree.ipp）。逐类判定收口：39 用例（36 本批新写 + 3 并行会话追加审读收编，其中 SafeWatcher
用例修正非 NULL base 必崩缺陷后收编）驱动 流格式函数族/基类默认实现/Sequence/Map/Data/ReadOnly/
Member/Func/Dereference/SmartPtrDeref/ContainerBounce/Absolute/Freeze/rootWatcher 全局面与
makeWatcher·makeNonRefWatcher 工厂重载；残余分母为生产 TU 从不执行的模板实例 + 死臂登记：
rootWatcher() 创建臂与 fini() removeChild 臂结构性死代码（rootWatcherInternal 恒保证
g_pRootWatcher 非 NULL 的两步推演）、hasRootWatcher() false 臂仅进程早期/fini 后窗口可达、
`_XBOX360` 平台宏分支、MF_ASSERT 中止臂；另登记 uint64 提取器 upcast 块缺 else 的源码级缺陷
（size==4 先读 4 字节再无条件读 8 字节必失败，测试注释钉死现状，不修只记），见台账 §7.22；
`cstdmf/fixed_sized_allocator.cpp` 0%→**99%**（286/289 行，批次 21；根因是既有测试组整体被
`ENABLE_FIXED_SIZED_POOL_ALLOCATOR` 平台宏守卫——el7 服务端构建恒 0（config.hpp:123），新建
`test_fixed_sized_allocator.cpp` 12 用例直驱类本身；残余 3 行为 findPool 排序 shift 结构性死臂
（autoPools 单调建档下 :757 恒 false），另登记 `getNumPoolItemsForSize` 未命中即读
`allocSizes_[-1]` 的源码级越界隐患，见台账 §7.21）；
`physics2/quad_tree.ipp` 0%→**98%**（测试 TU 视角
860/876，批次 20；全仓模板实例聚合口径 68%，低值来自生产 TU 从不执行的
实例化。根因是 test_quadtree.cpp 从未进 Makefile.rules cxxSource——文件
存在≠参编第二例；2 休眠用例复活 + 22 新用例；残余 16 行为 MF_ASSERT/
dprintf 失败日志臂、池扩容臂与 radius 法向翻转小臂，登记不硬凑；
`QuadTree::del`/`testPoint`/`print` 族/`countAt` 四处从未实例化的编译级
死代码维持登记）；
`physics2/bsp.cpp` 0%→**91%**（711/778 行，批次 19，
MF_SERVER 死门激活 + 14 用例）；`network/tcp_bundle.cpp` 100%（批次 16）；
`network/event_poller.cpp` 57%（批次 18，余量 175 行为 Linux 死代码登记）。
