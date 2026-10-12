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

测试环境陷阱：network_test 的 ConfigTest 依赖主机内核参数
`net.core.rmem_max ≥ 16MB`（`wmem_max/wmem_default ≥ 1MB`），主机重启会回落
4MB 并报 "Insufficient recv buffer to run tests"，用 sysctl 恢复即可（非持久）。

不在构建内的测试文件（"文件存在≠参编"）：`cstdmf/unit_test/test_object_pool.cpp`
——object_pool.hpp:343 调 `LookUpTable::reserve()`，本树 lookup_table.hpp 无此
成员，无法编译（cstdmf Makefile.rules 内注释登记）；`scene/unit_test` 的
`test_blend_transform`（Windows 专用，规则内注释）。

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
| connection/server_connection.cpp | 990/990 | 0% | **维持不可测**（真登录管线 + TCPChannel 状态机，批次14/16） |
| entitydef/entity_description.cpp | 665/665 | 0% | 受限：需 EntityType/EntityDef XML 装载机全套 |
| moo/image.ipp | 589/589 | 0% | 受限：客户端图像合成面 |
| network/machine_guard.cpp | 553/774 | 28% | bwmachined 面不可达部分维持登记（批次17） |
| connection/message_handlers.hpp | 538/538 | 0% | **结构性死代码**（模板 handler 表无人实例化） |
| chunk/chunk_space.cpp | 506/506 | 0% | 受限：需 ChunkSpace + chunk 装载机 |
| connection/replay_controller.cpp | 495/495 | 0% | 受限：录像/回放文件管线（线格式已批次12 测） |
| pyscript/pywatcher.cpp | 416/730 | 43% | 候选（下批首位） |
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
`network/event_poller.cpp` 57%（批次 18，余量 175 行为 Linux 死代码登记）；
`cstdmf/profiler.cpp` 6%→**94%**（1155/1222 行，批次 23；红门禁提交 `dc5cd043` 13 用例逐根因修复——
getAsString 不驱动 statistics watcher 数据填充、EVENT_COUNTER 断言方向错、空帧字母序断言、可见性门控/守卫条目兄弟污染、counter 落在错帧、
时序型断言在 -O2+OS 抢注下不可控（换确定性比较器与逐模式重录帧）、join 后槽位已清 等七处；
385→398 用例全绿；文件级数字来自精准插桩口径（只重编 cstdmf TU，同批次 21），模块级全量值待下轮复测；
残余 67 行登记为不可测臂：`SortByNamePtr` 比较器（BW_UNUSED 死代码）、HitchDetector 全族、MF_SERVER 排除族、
ENABLE_PER_CORE_PROFILER=0 面、`processEntriesTask_` 不 delete、jsonDump else-if 停臂（force 下不可达）、
tick stall WARNING、frameNumber 竞态、END mismatch 健康态 ASSERT、rootWatcher fini 悬垂、`doBackgroundTask` !historyFile_ 竞态，见台账 §7.23）；
`cstdmf/smartpointer.hpp` 84/91→**117/118 测试 TU 视角**（批次 24；此前无专属测试，覆盖全部来自 watcher/circular_queue/memory_debug 顺带使用；新建
`test_smartpointer.cpp` 29 用例直驱 ReferenceCount（含第二基类多继承 delete this）/SafeReferenceCount（destroy() 虚钩子恰好一次 + 4 线程原子锤击）/自由函数族/ConstSmartPointer·SmartPointer 全操作面与派生→基转换；唯一残余 :121 `~ReferenceCount()` 空体为结构性死臂——构造受保护故基析构永不为最外层析构；表值 679/1519 为 2026-09-28 全量插桩多 TU 实例事件聚合口径，精准口径不可直接换算，撤出本表；零计数偷取构造的 MF_ASSERT_DEV 臂经
hasDevelopmentAssertions(false) 非致命覆盖；另钉死 ConstSmartPointer 在非 _XBOX 构建无模板拷贝构造（基指针只能经显式 get() 共享），见台账 §7.24）；
`pyscript/script_math.cpp` 92.3%→**97.0%**（精准插桩口径 1000/1083 → 1050/1083 实例行，批次 25；分母为
单 TU 实例宇宙，表值 655/1920 属 2026-09-28 全量插桩多 TU 聚合、不可直接换算，撤出本表——批次5 的
23 用例在多 TU 分母下显示 65%，复判后真实暗区仅 83 行；新建 `test_script_math_gaps.cpp` 10 用例直驱
原型测试从未触达的行：通用 `PyVector<V>` number 协议的一元负/正与标量左乘（Vector3 自带乘法特化，
自家测试永不触达共享体）、Vector2/4 原地运算符与各运算符不可转换实参的 NotImplemented 臂、富比较
GT/LE/GE 臂与 Vector2/4 分发槽、distSqrTo/flatDistSqrTo/flatDistTo/setPitchYaw 实参校验回退、z setter
与 yaw/pitch getter（原型只写过 x/y/w、没读过 yaw/pitch）、MatrixProduct/Vector4Product 单操作数分支、
Vector4Combiner 缺操作数早退、look-at 近 +Y 上向量回退、Vector4Shader 用户寄存器上限与非临时 provider
作输出寄存器两拒绝臂；残余 33 行全数分类登记：13 行 Windows-only 构造性死区（ProviderStore 矩阵半边
——唯一 autoTick 构造的 MatrixProvider 子类 MatrixAnimation 整体包在 `#if defined( _WIN32 )`，档案内
MatrixAnimation 符数 0；Vector4 半边有 Vector4Animation 触达）、3 行 safeSetVector 死臂（setVector 恒返
true）、8 行死成员（pyGet_x/y/z/w 被 `PyVector::pyGetAttribute` 单字符截获取代后永不调用的历史体）、
9 行宏归属幻影行（`PY_RW_ATTRIBUTE_DECLARE`/`PY_SCRIPT_CONVERTERS` 展开点——访问器体实际生成于
pyobject_plus.hpp 头内行、执行归属头行，.cpp 展开点行恒 0，对应读写已由测试触达），见台账 §7.25）。
`resmgr/datasection.cpp` 19.3%→**99.7%**（精准插桩口径 169/876 → 873/876 实例行，批次 26；此前无专属
测试，覆盖全靠 bwresource/xml_section/zip_section 等宿主测试顺带；表值 600/878 属 2026-09-28 全量插桩
口径、不可直接换算，撤出本表；新建 `test_datasection.cpp` 21 用例直驱：迭代器全协议（默认构造/前增/
后增旧位拷贝/==/!=/解引用/tag）、getIndex·childSectionName·openFirstSection 双臂、openSection 空/深/
尾斜杠/单标创建、openSections 平铺与嵌套、deleteSection/deleteSections、16 型标量读写往返+缺失默认、
wideString/blob/binary 往返（binary 需 BinSection 宿主——XML 子持基类 setBinary 拒写，恰覆盖基类
:1973-1976）、Vector2/3/4/Matrix34 往返、复数读 11 + 复数写 11 的缺失父守卫族（DirSection 挂不存在
基路径使 writer 侧 newSection/makeDirectory 失败，一次驱动 20 条 `if(!pSection)` 守卫且零文件副作用）、
基类 as* 15 式默认回显 + 基类 set* 全拒（BinSection 宿主）、createAppropriateSection 五臂（合法 XML/
copy 保护/非法 XML 回落 BinSection/allowModifyData=true 拒绝/creator 短路）、splitSaveAsFileName 经
BinSection::save 三臂（protected 直测不可达；zip 父臂以 purgeAll 前后包夹防缓存污染 test_zip_section）、
setWatcherValues 空路径/成功/dot→space 重试/空值静默、copy（Bin→Bin 短路 + modifyCurrent 两义 + XML→XML
全树 compare==0）、copySections tagged/all 合并覆盖、compare/binaryCompare 全差异臂、sanitise 族基类
恒等；残余 3 行（:90-93）= 迭代器 operator* 空子回退臂，**登记为到达即崩的结构性死臂**——回退体调用
`childSectionName( index_ )`，基类实现 `openChild(index)->sectionName()` 在同场景恰为 NULL 解引用
（datasection.cpp:168，gdb SIGSEGV 实证），属源码级隐患登记不修，见台账 §7.26；
下批首位候选 `pyscript/pywatcher.cpp`）；
