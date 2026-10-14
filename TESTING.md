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
成员，无法编译（`lib/cstdmf/unit_test/Makefile.rules` 内注释登记）；
`lib/math/unit_test/test_blend_transform.cpp`（Windows 专用，规则内注释）。

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
| moo/image.ipp | 589/589 | 0% | 受限：客户端图像合成面 |
| network/machine_guard.cpp | 553/774 | 28% | bwmachined 面不可达部分维持登记（批次17） |
| connection/message_handlers.hpp | 538/538 | 0% | **结构性死代码**（模板 handler 表无人实例化） |
| chunk/chunk_space.cpp | 506/506 | 0% | 受限：需 ChunkSpace + chunk 装载机 |
| connection/replay_controller.cpp | 495/495 | 0% | 受限：录像/回放文件管线（线格式已批次12 测） |
| chunk/chunk_boundary.cpp | 403/403 | 0% | 受限：同 chunk 体系 |
| network/logger_endpoint.cpp | 398/398 | 0% | 维持受限登记（真消息日志管线，批次17） |
| resmgr/bwresource.cpp | 282/746 | 62% | 批次 28 实测（精准插桩 464/746；386/746 为 09-28 全量口径） |
| entitydef/entity_description.cpp | 239/663 | 64% | 批次 29 实测（精准插桩 424/663；0% 为 09-28 全量口径，受限判定推翻） |
| network/logger_message_forwarder.cpp | 335/335 | 0% | 维持受限登记（批次17） |
| entitydef/method_description.cpp | 137/466 | 70% | 批次 28 实测（精准插桩 329/466；323/478 为 09-28 全量口径） |
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
`pyscript/pywatcher.cpp` 43.5%→**93.4%**（精准插桩口径 312/717 → 670/717 实例行，批次 27；表值
416/730 属 2026-09-28 全量插桩口径、不可直接换算，撤出本表；新建 `test_pywatcher_gaps.cpp` 22 用例
直驱原型测试从未触达的面：PyObjectWatcher 的 mapping/sequence 特化 watcher 非空路径分发与恒假
addChild、SimplePythonWatcher 全操作面（dict/实例/bytes/str/int/缺子/递归/visitChildren 与
stopper 语义，公共类但生产树零实例化）、PyAccessorWatcher 流路径（getAsStream __doc__ 臂、getter
抛错/str() 抛错/str() 孤代理三错误族、setFromStream 载荷拒绝与 setter 失败/置后重取失败臂）、
PyFunctionWatcher（INT/UINT/FLOAT/BOOL/UNKNOWN 实参提取、四类坏载荷尺寸拒绝臂、返回值编码
int/大整数/float/bool/bytes/对象/deferred 三态、BadDeferred 接线失败打印臂、stdio 钩子与调用失败）、
py_setWatcher/py_addWatcher/py_addFunctionWatcher 错误臂（含 comment 省略即 logic_error 的
不可省略面）；残余 47 行全数分类登记：putPyObjectOnStream 的 FLOAT/BOOL/STRING 失败臂+default 臂+
getPyObjectWatcherData NULL 臂（:629-739 唯一调用方 finishSetFromStream :1252-1268 忽略返回值，
失败臂结构性死代码）、STRING 提取器 break 臂 :616（提取器自描述不可失败）、GetItem/PyTuple_New
边界臂（:1186/:1191/:1207/:1338/:1340，序列长度同包刚读出）、Py3 迁移死臂（:1766/:1870 非 unicode
str() 回退——PyObject_Str 在 Py3 恒返 str 或 NULL，NULL 则在未检查的 PyUnicode_Check 即崩）、
结构性不可达序列分支（:1683-1696 pythonChildBase、:1943-1946 visitChildren、:1971-1975 索引标签——
PySequence_Check 内建全部同时过 PyMapping_Check）、需活 stdout 钩子的臂（:1300/:1393/:1395-1396）、
fini 后窗口臂（:845/:847）；登记源码级隐患三处不修只记：SimplePythonWatcher::visitChildren 映射臂
对 list/tuple/str/range 必走 PyMapping_Keys→NULL→Py_DECREF(NULL)（:1934-1938，gdb SIGSEGV :1936
实证）、getPyObjectWatcherData 的 PyUnicode 分支 MF_ASSERT 孤代理 str 即中止（:757-763）、
py_addFunctionWatcher 的 comment 形参"可选"实不可省（PyFunctionWatcher 先于 init() 构造，
watcher.cpp :546 std::string(NULL) 抛 logic_error）；**本批方法论教训**：SimplePythonWatcher 的缺子
路径（pythonChildBase 末段 PyObject_GetAttrString）与孤代理 traceback 打印会遗留 pending
Python 异常，之后所有 PyImport_ImportModule 静默失败——假绿形态之一（用例静默跳过仍报绿），
对策=每个此类用例后 PyErr_Clear + fixture 取回全部 CHECK 化 + fixtureModule 失败即打印并清除；
批次前段一度把 :1775-1791 判为"gcov 内联归属怪癖"，实为该毒化所致的静默跳过，结论已撤回，
见台账 §7.27；下批首位候选 `resmgr/bwresource.cpp`，次选 `entitydef/method_description.cpp`）；
`resmgr/bwresource.cpp` 43%→**62%**（精准插桩口径 322/746 → 464/746 实例行，批次 28；表值
386/746 48% 属 2026-09-28 全量插桩口径、不可直接换算；新建 `test_bwresource_paths.cpp` 17 用例直驱：
路径格式化族（getFilename/getExtension/removeExtension/changeExtension/pathIsRelative、formatPath
空串臂、getFilePath 的 dirname 平台语义 #ifdef 双臂、getCurrentDirectory 非 Windows TODO 空串臂）、
路径列表（getDefaultPath/越界空串臂/getPathAsCommandLine/path 缓冲区出入参）、addPath 默认追加 +
index 0 头插 + delPath 三连还原、purge/purgeAll 与 DataSectionCensus 联动、save 非 census 拒绝臂、
watchAccessFromCallingThread 与 RAII holder 双向、modification monitor 族（监听器增删/使能/flush/
ignoreFileModification 一次性与永久两臂）、文件类型查询（res 相对名 vs fileAbsolutelyExists 原生
绝对路径双轨）、modifiedTime/isFileOlder 用 utime 回拨做确定性比较、resolveToAbsolutePath
FT_FILE/FT_DIRECTORY/FT_NOT_FOUND 与 res 相对解析）；平台行为经测试注释钉死四件：addPath(-1)
默认追加而非前置（-1 转 size_t 后越过范围检查落到 size()）、addSubPaths 的
`STRINGVECTOR newPaths( paths.size() )` 预分配空串污染路径列表（源码级隐患登记不修）、
ensurePathExists/ensureAbsolutePathExists 先把 '/'→'\\' 再 mkdir（Windows-only 行为）、
isFileOlder 把 mustBeOlderThanSecs 乘 1e7（Windows 100ns 单位）而 unix_file_system.cpp:119 存的
是 st_mtime 秒 → 非零阈值在 Unix 恒 inert（用例只测 0 阈值并注释说明）；残余 282 行分类登记：
impl 初始化族（188-484）、purge 递归（551-564）、searchPathsForFile/searchForFile（1179-1378）、
EDITOR_ENABLED 死区（1389-1392/1493-1579）、ensurePathExists 族（1886-1974）、addPaths/addSubPaths
族（2054-2213）、fixCommonRootPath（2285-2353）、monitor 包装（2558-2640）、
overrideAppDirectory/dissolveFilename（2702-2883），以 el7 服务端构建的结构性不可达面为主；
见台账 §7.28）；
`entitydef/method_description.cpp` 32%→**70%**（精准插桩口径 153/466 → 329/466 实例行，批次 28；
表值 323/478 属 2026-09-28 全量插桩口径、不可直接换算；新建 `test_method_description_gaps.cpp`
33 用例直驱：parse 默认值（replayExposureLevel OTHER_CLIENTS、priority FLT_MAX 哨兵）、exposed
组件位（OWN/ALL/BASE/CLIENT/BOGUS 各臂）、replay 暴露三级、detailDistance 平方换算、old-style
`<Arg>` 子节点、returnValues 与 NOT_A_TYPE 失败臂、arg 失败回卷、exposure 访问器与 internalIndex、
exposed msgID（NULL range → index 2 且 msgID uint8 回绕 255 注释；range(10,19)：5 exposed 且
exposedID 3 → msgID 13 sub -1、12 exposed 且 exposedID 10 → msgID 19 sub 1 的
ExposedMethodMessageRange 算术）、addSubMessageID 0 vs 1 字节、流尺寸（定长 4 / exposed
+sizeof(EntityID) / STRING 与子索引 -1）、add_to_stream/server/client（exposed 前置 EntityID 42；
client 面需先 setExposedMsgID）、returnValues 流往返、md5（同 parse 同摘要、异 args、异 legacy
index）、clientSafety（INT32 → 0、MAILBOX → CLIENT_UNUSABLE 位）、shouldRecord、toString
（MemberDescription protected / MethodDescription private → 经 VariableLengthHeaderSize≠default
+ setExposedMsgID 的唯一可达警告点驱动）、components、is_signature_equal、p_watcher 单例、
createErrorObject（环境无关 exists 断言——无 BWTwoWay 模块恒返 null）、areValidArgs（exposed CELL
firstOrdinaryArg=1 / None source 合法 / 字符串 source 非法）、areValidReturnValues、
extractSourceEntityID（plain 不变 / exposed 剥 123 / None → NULL_ENTITY_ID / 空 tuple → null）、
convertKeywordArgs、createDictFromTuple、getArgsAsTuple（valid / 空 → null / 多余数据 CHEAT null /
exposed：流只含 args、sourceID 作形参 42 → (42,5)——消息头在上游已剥）、
createReturnValuesFromStream、returnValuesOrFailure（成功 uint8 0 / 失败 uint8 1 + 错误名 + 空串 /
pickled 分支以 !pickled.empty() 守卫）、callMethod 与 isMethodHandledBy（exec 定义类 + eval 建实例
——Py_file_input 返 None，须 exec-then-eval）；登记五件不修只记：returnValuesStreamSize 在
method_description.hpp:257 声明但全仓无定义（死声明）、全仓无 BWTwoWay 模块 → staticInitTwoWay
失败、Mercury::MessageID 为 uint8 故 -1 哨兵读作 255、DEFAULT_VARIABLE_LENGTH_HEADER_SIZE = 1
（network/interface_element.hpp:75）、MAILBOX → UNSUPPORTED_TYPE → clientSafety
CLIENT_UNUSABLE(0x2)；残余 137 行分类登记：PyDeferredResponse 族（58-135）、copy/赋值（254-268）、
createErrorObject 带 func 臂（288-298）、parse 缺 name（347-348）、object-with-id 分支（636-646）、
handleException 族（747-798）、callMethod 错误/returnValues 分支（850-923）、网络发送路径
（970-1071）、2 参 createArgsFromStream（1163/1166）、:1216；见台账 §7.28）；

`entitydef/entity_description.cpp` 0%→**64%**（精准插桩口径 424/663 实例行，批次 29；表值
665/665 0% 的受限判定「需 EntityType/EntityDef XML 装载机全套」**推翻**——`EntityDescription::parse`
全程仅依赖 BWResource 单例与 scripts/entity_defs 下 fixture .def 树，从不触 EntityType；entitydef
harness 每 TEST_F 重建 BWResource/Script，故 `BWResource::addPath` 须每用例调用、fixture 文件按
pid 唯一目录 /tmp/bwentitydef_<pid> 写入一次；新建 `test_entity_description.cpp` 30 用例直驱：
parse 最小形态、标量属性 Flags 各位（CELL_PUBLIC 幽灵掉/OWN_CLIENT+Persistent/ALL_CLIENTS
other-client 事件戳索引）、缺 .def 与坏 Flags 失败、BaseMethods/CellMethods/ClientMethods 三
列表与 `<Exposed/>`（空 `<Exposed/>` 恒 OWN_CLIENT、ClientMethods 带 Exposed 恒拒绝）、Parent
递归继承与缺父失败、Volatile 优先级平方与 yaw≥pitch≥roll 校验、LoDLevels 标签匹配 detailLevel
与失败臂、Distribution 三 decider 位、ClientName 弃用告警、无 Client 脚本时 ClientMethods 拒绝、
TempProperties、Components 装载/缺 def/重名失败（实体自有属性先于组件属性）、parseService 的
Methods→base 列表映射与属性拒绝、Implements 接口属性、checkExposedMethods 的 proxy/非 proxy
四象限、getDataDomains 全映射、shouldSkipPass 的 EXACT_MATCH 精确匹配矩阵与非精确按位臂、
shouldConsiderData 各 pass 过滤、addSectionToStream→readStreamToDict 流往返、
addSectionToDictionary 直写 dict；**实测坑位**：`parse` 第二参 componentName 生产恒传空串
（entity_description_map.cpp:362），传实体名会把全部属性标记为 componentised、fullName 带前缀
「实体名.属性名」，addSectionToStream 按 fullName 找值落默认值 0、getItem("属性名") 必空——
流往返用例用 gdb+PyObject_Print 实证后修正传参；方法 fixture 的 `<baseExposed/>` 是名为
baseExposed 的非暴露方法，暴露须 `<baseExposed><Exposed/></baseExposed>`；残余 239 行分类登记：
readStreamToSection/readTaggedClientStreamToDict/addAttributesToStream/addDictionaryToStream
网络流读侧、MD5 族（addToMD5/addPersistentPropertiesToMD5）、setExposedMsgIDs、checkMethods、
pIdentifier/isTempProperty/clientServerProperty 族访问器、getDataDomains 两个 CRITICAL_MSG
不可达臂（CLIENT_ENTITY_CELL_CREATION/CLIENT_ENTITY_LOD_DATA）、parseProperties 属性覆盖臂与
ClientServerPropertiesSortHelper 模板实例暗区，见台账 §7.29）；
