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
| network/machine_guard.cpp | 96/764 | 87% | 批次 30 实测（精准插桩 668/764；28% 为 09-28 全量口径，bwmachined 面之外已直驱） |
| connection/message_handlers.hpp | 538/538 | 0% | **结构性死代码**（模板 handler 表无人实例化） |
| chunk/chunk_space.cpp | 506/506 | 0% | 受限：需 ChunkSpace + chunk 装载机 |
| connection/replay_controller.cpp | 495/495 | 0% | 受限：录像/回放文件管线（线格式已批次12 测） |
| chunk/chunk_boundary.cpp | 403/403 | 0% | 受限：同 chunk 体系 |
| network/logger_endpoint.cpp | 398/398 | 0% | 维持受限登记（真消息日志管线，批次17） |
| resmgr/bwresource.cpp | 282/746 | 62% | 批次 28 实测（精准插桩 464/746；386/746 为 09-28 全量口径） |
| entitydef/entity_description.cpp | 72/663 | 89% | 批次 32 实测（精准插桩 591/663；195/663 为批次 31 口径） |
| network/logger_message_forwarder.cpp | 335/335 | 0% | 维持受限登记（批次17） |
| entitydef/method_description.cpp | 129/466 | 72% | 批次 34 实测（精准插桩 337/466；329/466 为批次 28 口径） |
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

`network/machine_guard.cpp` 28%→**87%**（精准插桩口径 668/764 实例行，批次 30；表值 553/774
28% 为 09-28 全量口径）。批次17 的「bwmachined 面不可达」判定**部分推翻**——MGM 消息编解码面不
依赖 bwmachined，16+1 个消息类自持 wire 格式（基线 [message u8][flags u8][seq lo][seq hi]，
`MachineGuardMessage::create` 按 message_ 字节 new 子类），写侧 readImpl/writeImpl 与读侧配对即可
直驱。新建 `test_machine_guard_messages.cpp` 27 用例（network_test 229→256）：HighPrecision/Whole
两套 InterfaceStats 位宽往返 + c_str、ProcessMessage 全字段往返含额外块（版本三元 + interfaceVersion
+ username + defDigest）、额外块前向兼容跳过（声明 +4 并追写 4 字节 dummy → consume 干净、
remainingLength 0）、欠声明报错（声明 1 → 字段吃完 → is.error()）、matches 六参（category/uid/
pid/port/id/name）逐位断言且未置位忽略、categoryToString 三臂、ProcessStats、Listener 的
RegBirthL/RegDeathL 标签臂、Signal 的 setControlledShutdown=SIGUSR1/setKill=SIGINT/
setHardKill=SIGQUIT、Create 与 CreateWithArgs（args 列表与空列表）、Tags、User 的 PARAM_CHECK_
COREDUMPS/PARAM_GET_VERSION 双门控 + init(struct passwd&) + getConfFilename、Reset、Pid、
MachinedAnnounce 的 birth count / death addr union、MachinePlatform、UnknownMessage 回显（0x55 +
MESSAGE_NOT_UNDERSTOOD + body 保留）、ReplyHandler 16 型分发矩阵 + 未知型默认臂 + 基类 16 默认
on*Message 全落 onUnhandledMsg（返回 true 保持处理）。**实测坑位**：CppUnitLite2 的
CHECK_EQUAL(const char*, const char*) **比较指针**（45 处 assert 须 strcmp 化，否则同文不同址报
"expected: 'x' but was: 'x'"）；`MachineGuardMessage::write` 非 const（roundTrip 助手取非 const
引用）；matches 的 query 需自带与 self 等值的字段（其 ctor 用 getUserId()/mf_getpid() 播种真实值）；
精准插桩在 libnetwork.a 场景须**重打包静态库**（instrumented obj 复制进 build 树 obj 后须
`make libnetwork` 重归档，否则链上旧库）；g++ 链接须**显式 --coverage**（否则 __gcov_exit/
__gcov_merge_add 未定义）；gcda 落在**编译期 -o 路径**（/tmp 暂存目录）而非 build 树 obj 目录，
gcovr 以该目录为根。残余 96 行分类登记：sendto / sendAndRecv(Endpoint&) / sendAndRecv(srcip,
destaddr) / sendAndRecvFromEndpointAddr 的 UDP 套接字面 83 行（需活 socket + 绑 PORT_MACHINED=
20018 回环，引入环境依赖故登记不测）、typeStr 的 `** UNKNOWN **` 默认臂 12 行、seq ticker 回绕臂
（s_seqTicker_ 达 0xffff 后 ++）1 行；见台账 §7.30。

`entitydef/entity_description.cpp` 64%→**70%**（精准插桩口径 424/663→468/663 实例行，批次 31；
表值 239/663→195/663）。批次 29 的「需 EntityType/EntityType XML 套件」判定已推翻（EntityDescription
默认构造 + parse 仅需 BWResource 打开 .def 段），本批直驱其 **MD5 族**——addToMD5 与
addPersistentPropertiesToMD5 两个 schema 摘要面，正是 L36 热重载设计指定的 schema 版本原语。
既有 TU `test_entity_description.cpp` 追加 11 用例（entitydef_test 348→359）：确定性（同字节
重解析双摘要相等）、CELL_PUBLIC 属性对双摘要零贡献（连类型替换都不动）、client-server 属性只进
全量摘要、BASE+Persistent 属性只进持久摘要、非持久属性对持久摘要零贡献（与空 def 同摘要）、
未暴露 base 方法零贡献（与无 base 方法同摘要）、`<Exposed/>` 暴露 base/cell 方法即入摘要、
client 方法无暴露门控（`<Exposed/>` 在 ClientMethods 恒拒绝）、暴露方法顺序敏感（legacyExposedIndex
随位置计数）、client-server 属性顺序敏感（clientServerFullIndex 按 def 顺序编号）。**实测坑位**：
`BWResource::openSection` 先查 `DataSectionCensus` 再碰文件系统（bwresource.cpp:614-625），同一路径
重写 .def 后重解析**必须**先 `BWResource::instance().purgeAll()` 驱逐，否则读到旧段；
`CHECK` 宏展开为 `result_`/`m_name` 成员，自由助手函数里只能用 `MF_VERIFY`；`MD5::Digest` 默认
构造不初始化字节，须走 `Digest(MD5&)` 或 `operator=`。残余 195 行分类登记：parseProperties 覆盖/
override 臂、LoDLevels/Volatile/Implements/Components 解析面、addSectionToStream 流写侧、
supersede 迁移面、watcher 面等，见台账 §7.31。

`entitydef/entity_description.cpp` 70%→**89%**（精准插桩口径 468/663→591/663 实例行，批次 32；
表值 195/663→72/663）。批次 31 收口的 MD5 族之外，本批直驱其**流写侧访客族 + 读侧 + 访问器 + 暴露
消息号**四块：addDictionaryToStream（AddToStreamMapVisitor：缺键落默认值不报错）、
addAttributesToStream（AddToStreamAttributeVisitor：缺属性即失败、NULL 对象前置拒绝、错误类型值
换默认值后整体失败）、addToStream 助手逐 pass 数据尺寸（**NUM_PASSES=4**：pass0 base、pass1
base+client、pass2 cell+client、pass3 cell，PASS_FILTER 按 isBaseData×isClientServerData 二维过滤；
owner(BASE)→pass0、nick(ALL_CLIENTS)→pass2、hp(CELL_PUBLIC)→pass3，dataSizes 逐 pass 尺寸与流总长
精确对账）、readStreamToSection（流→新建 XML section 往返）、readTaggedClientStreamToDict
（count u8 + 逐项 [clientServerFullIndex u8 + 属性流数据]，other-client 与 own-client 数据均落
dict）、pIdentifier/isTempProperty/clientServerProperty(Count) 访问器（Identifier 属性须同时
Persistent:true——parse 对 Indexed 默认真、非持久 Indexed 直接 parse 失败）、setExposedMsgIDs
（唯一暴露方法取 range 首槽 msgID=10，未暴露方法保留 -1 哨兵读作 255）、checkMethods
（warnOnMissing 双臂，Python 类缺方法即 false）。既有 TU 追加 7 用例（entitydef_test 359→366）。
**实测坑位**：① `clientServerProperties_` 经 `allocateClientServerFullIndexes` **按流尺寸稳定排序**
（定长升序在前、变长按名义尺寸升序在后，entity_description.cpp:828-876）——clientServerProperty(n)
的 n 不是 def 顺序，tagged 流的 index 也用排序后序号；② NUM_PASSES=4 而非 3，dataSizes 数组须开
4 且 numDataSizes=4 才与流总长对账（numDataSizes=3 时 MF_ASSERT_DEV 放行 actualPass-1 但第 4 pass
不记录）；③ `MemoryOStream` 追加原始字节用 `addBlob`（无 append）；④ `XMLSection` 的
asString/asInt 是 protected，须经基类 DataSectionPtr 用 `readString/readInt`；⑤ ScriptString 取值
用 `c_str()`（无 asChar）。残余 72 行分类登记：parse 错误/警告臂、LoD/Volatile 缺省臂、
parseProperties 覆盖与 clientSafety 臂、parseMethods 错误臂、checkExposedMethods 臂、访客
MF_EXIT/边界臂、watcher 面，见台账 §7.32。

**批次 33（2026-10-15）——热重载回滚链 supersede 语义直驱**（todo C 档 reload 测试承载切片，
服务 B 档热重载生产化的失败模式证据）。本批不动覆盖率口径：`EntityMethodDescriptions::supersede`
（`entity_method_descriptions.cpp:218-231`，清名重建 map + 全部方法改名 `old_` 前缀）与
`EntityDescription::supersede`（BASE/CELL 分派、CLIENT 仅警告）在批次 29 的 smoke 用例中已过线，
但其**功能效应**（按名查找断、下标访问活）从未断言。既有 TU 追加 4 用例（entitydef_test 366→370，
全套 1472→1476）：
1. `EntityDescription_supersedeRenamesCellMethods`——supersede(CELL) 后 `find("cellTick")`→NULL、
   `find("old_cellTick")` 可达且与 `internalMethod(0)` 同对象、`exposedMethod(0)` 仍解析（名字带
   old_ 前缀）→ **按 msgID 的暴露派发不受影响**；base/client 列不动。
2. `EntityDescription_supersedeRenamesBaseMethods`——supersede(BASE) 同语义，cell/client 列不动。
3. `EntityDescription_supersedeTwiceStacksPrefixes`——重复 supersede 前缀叠加（`old_old_`），
   印证 `EntityType::old` 首设守卫的必要性。
4. `EntityDescription_supersedeChangesMd5`——supersede 后 addToMD5 漂移（暴露方法改名进 digest），
   **schema digest 必须在 supersede 前采集**；无属性实体 persistent digest 不变（只含实体名）。
顺带产出 **F7** 入热重载设计稿：`Entity::migrate`（`entity.cpp:4267`）在 `__class__` 尝试前先经
`EntityType::old`（`entity_type.cpp:168-185`）对旧描述无条件 supersede，`__class__` 失败回滚
`pEntityType_ = pOldType`（`entity.cpp:4287`）后全仓无 un-supersede——回滚"成功"的实体按名查找
全断（`mailbox.cpp:122/240` findMethod→NULL）而 msgID 派发仍活，半残实体。见台账 §7.33。

**批次 34（2026-10-15）——method_description.cpp 纯助手族直驱**（server→server 调用约定与关键字
参数/返回值面）。既有 TU `test_method_description.cpp` 追加 5 用例（entitydef_test 370→375，全套
1476→1481），精准插桩 329/466 70%→**337/466 72%**：
1. `MethodDescription_extractSourceEntityIDIntAndNone`——暴露 CELL 方法的源实体 id 提取：int 首参
   直取 + 元组去头；None 首参记 NULL_ENTITY_ID（两者都返回去头后的元组）。
2. `MethodDescription_extractSourceEntityIDObjectWithId`——首参非 int 非 None 时从 `.id` 属性取
   （exec 类 + 实例）；无 `.id` 属性报错返回空元组。
3. `MethodDescription_extractSourceEntityIDPassthroughAndEmpty`——BASE 方法（或非暴露）原样透传
   且 sourceEntityID 不动；暴露 CELL 方法零参元组报错。
4. `MethodDescription_convertKeywordArgs`——位置前缀原样拷贝、命名尾槽按名从 kwargs 补齐、名字
   缺失/总数不符报错（kwargs 按个数计数、按尾参名取值——注意不是"剩余 kwargs 任意分配"）。
5. `MethodDescription_returnValuesHelpers`——areValidReturnValues 按 <ReturnValues> 节校验元数与
   逐元素类型（错元数/错类型均 false）；createDictFromTuple 产出以声明返回名为键的 dict。
**实测坑位**：① `ScriptObject::operator==` 是**指针等价**（py_script_object.hpp:596），值断言必须
走 `ScriptString::check` + `c_str()` strcmp / `ScriptInt::check` + `asLong()`；② ScriptDict 无
`size()`（只有 next/getItem/setItem），断言用逐键 getItem；③ `<ReturnValues>` 节用命名子元素
（`<name> STRING </name>`，MethodArgs::parse 旧样式同名惯例）。残余 129 行维持批次 28 分类
（PyDeferredResponse 58-135、staticInitTwoWay 错误臂 254-298、parse 空 name 臂 347-348（XML
段名不可为空、不可驱动）、handleException 族 747-798、callMethod 错误臂 850-923、网络发送
970-1071、:1163/1166/:1216），见台账 §7.34。

**批次 35（2026-10-15）——cellapp 实体消息 EntityID/SpaceID 关联接线**（观测面 lane，结构化日志
关联字段的最后一块生产接线）。新增头内联载体 `EntityMessageLogScope`
（`server/cellapp/entity_message_log_scope.hpp`：构造即 `LogCorrelation::set("entityID")`、
`setSpaceID` 补 "spaceID"，析构经 `ScopedLogCorrelation` 恢复进入前上下文；不可复制不可移动，
须就地构造）并接线进 `EntityMessageHandler::handleMessage` 4 参重载
（`server/cellapp/message_handlers.cpp`，findEntity 之后、分支之前）——3 参输入侧重载与缓冲
重放都汇入该重载，一处接线覆盖两条路径。新建 `cellapp/unit_test/test_entity_message_log_scope.cpp`
4 用例（cellapp_test 8→12，全套 1481→1485）：
1. `EntityMessageLogScope_EntityIDOnly`——未知/已销毁实体路径：只设 entityID，无 spaceID，退出后
   上下文清空。
2. `EntityMessageLogScope_EntityIDAndSpaceID`——活实体路径：entityID + spaceID 两字段可读回。
3. `EntityMessageLogScope_RestoresOuterContext`——与 `UDPBundleProcessor` 设的 channelID 叠加
   （三字段并存），退出后 channelID 原样保留——scope 只恢复进入前快照，不重置他人字段。
4. `EntityMessageLogScope_NestedScopes`——嵌套 scope（如转发消息）替换 entityID、保留外层
   spaceID，退出后外层字段逐层恢复。
**实测坑位**：① 调用点不可达——`message_handlers.cpp` 不编入 cellapp_test（Makefile.rules
只编低耦合 TU），运行时接线验证需 Entity 运行时（同 C 档承载缺口），本批只直驱 wrapper 语义
核心；② `Entity::spaceID()`（entity.cpp:6116）裸解引用 `pSpace_` 无 NULL 检查，而 destroy 后实体
仍在 population 可 findEntity、pSpace_ 已 NULL（entity.cpp:2959 setDestroyed → :2964 removeEntity →
:2965 pSpace_=NULL），故接线必须带 `pEntity != NULL && !pEntity->isDestroyed()` 守卫，不能直取；
③ `LogCorrelation` 上限 8 字段（超出静默丢弃，log_meta_data.cpp:268 WARNING），channelID +
entityID + spaceID 占 3 个，余量充足。见台账 §7.35。

**批次 36（2026-10-15）——热重载/实体消息路由语义体抽出 + fantasydemo 资产树落地冒烟**
（R1015F，C 档可抽语义体 lane + R4 资产 lane）。三块：
1. **reload 序列成数据**：`CellApp::reloadScript`（cellapp.cpp:2322）的分支策略与顺序抽到头内联
   纯函数 `ReloadScript::planReload`（`server/cellapp/reload_script_plan.hpp`），
   `reloadScript` 保留 head 内联（swap 解释器 + 两次加载）、从 swap-back 起由计划驱动。
   新建 `cellapp/unit_test/test_reload_script_plan.cpp` 7 用例：成功序 10 步全序（销毁在迁移前、
   cleanup 收尾）、脚本级重载成功尾与全量重载相同（只有 defs 加载不同）、全量重载 defs 失败
   5 步无 recover、脚本级重载 defs 失败 6 步带 recover、UDO 失败两模式走失败尾无迁移链、
   defs 失败后 UDO 结果被忽略（两输入同计划）、8 种结果组合计数公式与数组容量。
2. **实体消息处置梯成数据**：`EntityMessageHandler::handleMessage` 4 参重载的处置梯
   （原 message_handlers.cpp 的 isForDestroyedGhost 块 + else-if 梯）抽到头内联纯函数
   `classifyEntityMessage`（`server/cellapp/entity_message_routing.hpp`），
   `handleMessage` 填 `EntityMessageRoutingState` 后 `switch` 处置、副作用留在调用点。
   新建 `cellapp/unit_test/test_entity_message_routing.cpp` 8 用例：还原为 real 的 ghost 消息丢弃、
   顺序正确的 ghost 直派、乱序 ghost 缓冲、ghost 消息实体缺失（无排队=已销毁→丢、有排队→缓冲）、
   子序列起点逃生口（两次不判已销毁、同一状态去掉逃生口即丢）、REAL_ONLY 无 real 转发（缺失与
   ghost 两形态，real 则直派）、WITNESS_ONLY 无 witness 丢且转发臂先于无-witness 臂
   （缺失实体/ghost 两形态都不落到见证检查）、next-tick 延后需 flag 与 pending 同时成立。
   cellapp_test 12→27。
3. **R4 fantasydemo 占位资产树落地 + 加载冒烟**：`programming/fantasydemo/res/`
   21 文件（server/bw.xml 自足无 parentFile、scripts/entities.xml 5 实体、scripts/db.xml
   登录映射 + 一行 Account/Avatar、entity_defs/{alias.xml + 5 个 .def}、
   scripts/{base,cell,client}/*.py 12 个实体脚本），由
   `scripts/generate-fantasydemo-assets.py` 可重建（仓内程序化生成、无第三方资产、无美术代餐）。
   新建 `entitydef/unit_test/test_fantasydemo_assets.cpp` 5 用例：全资产经 BWResource 可开、
   5 个 .def 经 `EntityDescription::parse` 解析、defs 内容计数（Account 2 属性/1 客户端方法、
   Avatar 4 属性/2 客户端方法/1 暴露 base 方法/带 Identifier、Guard 1 属性、Space 无属性）、
   12 个实体脚本 `Py_CompileString` 语法编译（不执行——import BigWorld 只在活引擎里有）、
   entities.xml 与 db.xml 交叉一致（bw.xml 不含 parentFile）。entitydef_test 375→380，
   全套 1485→1505。

**实测坑位**：① `XMLSection` 读取器把首个标签前的文本当元素属性列表——
`scripts/generate-fantasydemo-assets.py` 起初在 `.def`/`.xml` 前写 `# Generated by ...` 行，
5 个 .def 全部解析成空（propertyCount=0、parse 静默为真），生成器改为把标记作为
`<!-- ... -->` 注释插在根元素内首行；② `BWUtil::executableDirectory()` 与 cwd 的深度不固定
（make 链 5 层、cmake 样板按 WORKING_DIRECTORY 跑源码目录 6 层），冒烟测试用「从可执行目录
逐级上溯直到 `programming/fantasydemo/res` 存在，否则回退 cwd」解析，不写死深度；
③ `CHECK` 宏依赖测试方法的 `m_name`，不能放在普通静态帮助函数里
（`CppUnitLite2/src/Test.h:111` 直接引用 `m_name`）；④ `-Werror=narrowing` 拒绝
`{ full, defs, udo }` int→bool 聚合初始化，须显式 `!= 0`。见台账 §7.36。

**批次 37（2026-10-15）——CI 根构建期探针（零测试改动轮）**
（R1015G，L38/L39 Windows 面 lane）。本轮无任何测试/C++ 改动，全部改动在
`.github/workflows/ci-windows.yml`：
1. 新增 job `root build (R3 probe, manual)`：job 级 `if: github.event_name ==
   'workflow_dispatch'`——约 1h 的全 superproject 构建不随 push/pr 跑（CI 经济模型），
   仅手动 `gh workflow run ci-windows.yml --ref dev` 触发；timeout 120 min、
   continue-on-error、configure + build log 与 ExternalProject 子 log 上传 7 天。
   构建命令 `cmake --build build/win_root_build --config Release --parallel 4`（全目标、
   依赖序，首个失败定位阻塞清单头部；`bwpython313` ExternalProject 天然排最前）。
   这是 libpython 承载注释里"构建期行为待首次真实构建判定"三项（get_externals 网络克隆、
   CPython PCbuild 对 MSVC 19.51、全 superproject 首次 MSVC 编译）的判定入口。
2. `root-configure-probe` job 名去掉过时的 "expected red"（configure 自 `1bdb68b5`
   起绿，每次 push 记录的是"仍然绿"），注释同步改为常态化描述。
3. 同轮完成 C 档 cellapp scaffold 成本判定（未实现，见 todo）：`entity_type.o` 的
   链接传递闭包 = `entity.cpp` 实体运行时整链，非单轮切片。
4. 首轮 dispatch 实测修掉一个并发缺陷：workflow 级 `concurrency: ci-windows-${{ github.ref
   }}` + cancel-in-progress 使 probe dispatch **取消了同 ref 在跑的 push 门禁腿**
   （run 38744999297 被 38745028827 取消）。改为 job 级三组并发（msvc 门禁 /
   configure probe 各自 cancel-in-progress；root-build probe 组 cancel-in-progress:
   false，不中途杀构建、后续 dispatch 排队），workflow 级组删除——push 门禁与手动
   probe 从此互不取消。
5. 首轮 dispatch 实测暴露第一个构建期阻塞（run 38745028827，probe job 51 秒）：
   `error MSB8013: This project doesn't contain the Configuration and Platform
   combination of Release|x64 [ZERO_CHECK.vcxproj]`——根 `CMakeLists.txt:22` FORCE 把
   `CMAKE_CONFIGURATION_TYPES` 定为 `Debug;Hybrid`，生成的工程不含 Release 配置，
   `--config Release` 对任何目标必败。构建步骤改为 `--config Hybrid`（根自身的规范
   配置；libpython ExternalProject 仍 `build.bat -c Release` 编 pythoncore，不受影响）。
6. 二轮 dispatch（run 38745701681）越过 MSB8013 进入真实编译，暴露两个构建期阻塞头：
   ① **cstdmf 腿在 superproject 模式回归（已修）**——`critical_error_handler.cpp` 编
   `critical_message_box.hpp` 时 `HWND/WNDPROC/CALLBACK` 未声明（C2065/C2923）：standalone
   门禁因 `PCH_SUPPORT=1` 让 `pch.hpp` 文本注入 `cstdmf_windows.hpp`（windows.h），而根
   server 配置关 `PCH_SUPPORT`（workflow 头注预记的偏差首次实际咬人）。修复 =
   `critical_message_box.hpp` 自带 `#include "cstdmf_windows.hpp"`（`concurrency.hpp` /
   `debug_exception_filter.hpp` 同款仓内 idiom，Windows 外空展开零行为差）；Linux 侧选择性
   门禁 cstdmf_test 451 用例 0 失败，superproject 侧证明由下一轮 dispatch 出。
   ② **`examples/cellapp_extension` 全 TU `C1083: cellapp/entity.hpp 等找不到`（未动）**
   ——example 目标（`BW_ADD_LIBRARY ... MODULE`，自身无 INCLUDE_DIRS）没把 `server/` 父目录
   带上（`cellapp/xxx.hpp` 形态依赖 `programming/bigworld/server` 在 include 路径），根
   include 布局如何供给是构建系统设计件，记录为下一个阻塞头。
7. 三轮 dispatch（run 38747050836）实证：首轮的 critical_message_box 修复在 superproject
   模式**生效**（HWND/WNDPROC 错误消失），阻塞清单推进为四类：
   ① cstdmf 残余 PCH 文本依赖（已修其二）：`message_box.hpp`（自用 HWND/WNDPROC/CALLBACK，
   与 critical_message_box 同款修法）与 `callstack_windows.cpp`（`<DbgHelp.h>` 前补
   `cstdmf_windows.hpp`）；**仍未修**：`memory_debug.hpp:169` `timeval firstTime`（MSVC 无
   `<sys/time.h>`，timeval 载体须在 MSVC 侧验证后定）与 `launch_windows_process.cpp`
   （GetCurrentThreadId + Profiler::EntryBuffer 成员级联，需 MSVC 侧复核根因）；
   ② `examples/cellapp_extension` include 布局（同上，未动）；
   ③ `dbapp_extensions/bwengine_mysql+xml` `C1083: d3dx9math.h`——math 头的 D3DX 类型经
   server 模块传递编译，根构建同样需要 DXSDK：探针 job 补了与 math 门禁腿相同的
   `Microsoft.DXSDK.D3DX` 9.29.952.8 拉取步（DXSDK_DIR → GITHUB_ENV）；
   ④ **libpython ExternalProject 首次真实执行并走得很远**：`get_externals.bat` 网络克隆、
   `_freeze_module.exe` 构建、AST/ frozen 模块再生成全部通过，倒在
   `regen.targets:69`——`generate_token.py rst` 读 `Doc/library/token.rst` 报
   `FileNotFoundError`（vendored CPython 3.13.16 树不带 `Doc/`，而
   `_TokenOutputs` 含 `Doc\library\token-list.inc`，缺输出即触发再生成）。
   **已修**：从上游 cpython 3.13 分支 vendor `Doc/library/token.rst` + `token-list.inc`
   两文件进 `third_party/python/`，本地用仓内脚本实跑
   `generate_token.py rst Grammar/Tokens Doc/library/token-list.inc` 通过且
   token-list.inc 无 diff（证明 vendored `Grammar/Tokens` 与上游一致）；MSVC 侧证明由
   下一轮 dispatch 出。
8. 四轮 dispatch（run 38749824237）实证：②d3dx9math C1083 **消失**（DXSDK 步生效，但
   bwengine_mysql/xml 倒在更深的 C2059/C2332 `'struct': missing tag name` 解析错 +
   C2220 warning-as-error——根构建带 /WX，是新的下一层阻塞头）；①的 message_box 与
   callstack_windows 修复**生效**（错误表消失），cstdmf 残余仍是 `memory_debug.hpp
   timeval` 与 `launch_windows_process.cpp`（`profiler.hpp:729/736` 的
   `level_`/EntryBuffer 级联）。**libpython 大步前进**：token.rst vendoring 生效
   （regen 过），CPython 3.13.16 在 MSVC 19.51 上编完 pythoncore 与扩展模块
   （`_sqlite3.pyd` 等），倒在 `_hashlib.pyd` 链接：
   `BW_Py_memoryTrackingIgnoreBegin/End` LNK2001——**根因是载体设计缺陷**：BW 钩子
   编在 libpython CMake 目标的独立 obj 里，而 PCbuild 的扩展模块只链 `python313.lib`
   （pythoncore 不含 bwhooks.c，钩子也不在导入库里）；Linux 轨用
   `-u,BW_Py_memoryTrackingIgnore*` 把钩子强制拉进每个消费方，Windows 侧没有等价物。
   修法属设计件（pythoncore 打进钩子并导出 / Directory.Build.targets 注入 ClCompile /
   给 PCbuild 扩展补链），下一轮定。**这是本探针首轮系列最深的一手数据：解释器本体在
   MSVC 19.51 上是可编译的。**
门禁：`critical_message_box.hpp` / `message_box.hpp` / `callstack_windows.cpp` 为
include-only 改动 → 选择性 `bw-run-test-cstdmf` 451 用例 0 失败（两轮同结果）；
third_party/python 两文件为本轮新增非测试改动。
门禁：CI 配置改动 → 全量套件 `make -s -rR bw-run-all-unit-tests`，
21 模块 / 1505 用例 / 0 失败（与批次 36 同数——本轮无测试改动），
YAML 经解析校验（3 jobs / 各自 steps 与 job 级 if 正确）。见台账 §7.37。

**批次 38（2026-10-16）——根构建探针 #5/#6：钩子导出落地 + 首次真实编译普查与五族修复**
（R1016A，L38 Windows lane 续；本轮无测试改动）。三段：
1. `67099235`（windows build carriers + hooks 导出）：`PCbuild/Directory.Build.targets`
   把 `bwhooks.c` 注入 pythoncore 的 ClCompile 并加导出——`_hashlib.pyd` LNK2001 的
   设计件落地（PCbuild 扩展只链 `python313.lib`，钩子进 pythoncore 后经导入库自然
   可达，Linux 轨行为不变）；同轮 8 处载体修（examples/cellapp_extension CMakeLists
   include 路径、cstdmf_init.cpp、launch_windows_process.hpp、memory_debug timeval、
   endpoint.hpp、event_dispatcher.hpp、backup_hash）。探针 #5（run 38762634821）
   configure 倒在 `No SOURCES given to target: libpython`（sourceless STATIC）→
   `f45598be` 将 libpython 定为 IMPORTED 目标指向 pythoncore 导入库后 configure 绿。
2. 探针 #6（run 38770875378）：configure 绿，根 superproject 首次真实 MSVC 编译——
   448 错误 / 10 项目（server 379、pyscript 18、bwengine_mysql 11、cstdmf 11、
   network 10、cellapp_extension 7、resmgr 5、bwengine_xml 3、script 3、math 1）。
   五大族定位：①pyconfig.h C1083×38（Python.h 含 pyconfig.h，PCbuild 的 IntDir 生成
   目录不在 include 面）；②`interface` 宏级联（站点 `time_keeper.hpp:17` /
   `db_engine_creator.hpp:24` 等——SDK `objbase.h` `#define interface struct` 把
   interface 形参重写成 struct，后续成员解析级联成 C2065/C4430/C2332/C2144；仓内
   `#define interface` 零处、`ole2.h` 零包含、HRESULT/IUnknown 用点仅 lib/model 与
   lib/terrain，已核）；③zlib.h×4（resmgr/network 消费 vendored zip 而其头目录不在
   include 面）；④`signal_set.hpp` sigset 族（直接 81 + signal_processor.cpp 下游
   16——MSVC CRT 的 `<signal.h>` 只有 signal/raise/sig_atomic_t）；⑤C2220 /WX 十处
   （留待下一轮）。
3. 本轮修复（9 文件，全部非测试面）：`BWStandardProject.cmake` 三项——python
   Include/PC 与 vendored `third_party/zip` 入 include、pyconfig 生成目录按
   `Include/patchlevel.h` 推导版本（CPython 升版不指向过期归档；plain
   INCLUDE_DIRECTORIES 非存在性检查、目录未生成时无害，IMPORTED 接口 include 会在
   generate 期硬查故不可放）、`-DWIN32_LEAN_AND_MEAN` 全局（`cstdmf_windows.hpp`
   同款 define；`Winsock2.h` 自含 windows.h，逐头排序不可靠）；两份 `signal_set.hpp`
   POSIX 全守卫（`pthread_sigmask` 无 Windows 等价，Blocker 语义须设计决定非翻译，
   留待）；`debug.hpp` 自载 `cstdmf_windows.hpp`（MF_DEBUGPRINT_NOALLOC 的
   OutputDebugStringA）；`memory_debug.cpp` `<sys/time.h>` 收 POSIX（前轮遗留缺口）；
   `endpoint.cpp` SIGPIPE StaticIniter 守卫（MSVC 无 sigaction/SIG_IGN）；
   `background_file_writer.cpp` fchmod/chmod 两块守卫（Windows 臂降级 WARNING）；
   `address_resolver.cpp` include 分置（Winsock2 供给 getaddrinfo/inet_ntop/hostent，
   函数体共享）；`third_party/python/CMakeLists.txt` 去重复的 pyconfig 推导（收敛到
   BWStandardProject 单点）。前轮已修错误复查全数消失：launch_windows_process 级联、
   memory_debug.hpp C3646、cstdmf_init C2065、backup_hash C4267、cellapp_extension
   之 cellapp/*.hpp C1083。
门禁：构建系统文件改动（BWStandardProject.cmake 触及全部模块编译面）→ 全量
`make -s -rR bw-run-all-unit-tests`：21 模块 / 1505 用例 / 0 失败（EXIT=0，与批次
36/37 同数——本轮无测试改动）；CMake 改动另经本地 3.5 最小工程双验证（Linux 解析
通过 + 强制 MSVC 实测：pyconfig 目录推导出 `PCbuild/obj/313amd64_Release/pythoncore`、
zlib 目录与 WIN32_LEAN_AND_MEAN 定义在 COMPILE_DEFINITIONS）。MSVC 侧证明由探针
#7（已派发）出。进度并入 todo.md L38（R3 行）。

**批次 39（2026-10-16）——探针 #7 普查解码与洋葱下一层五族修复（R1016D）**
（R1016D，L38 Windows lane 续；本轮无测试改动）。探针 #7（run 38785120808，
commit `69623139`）configure 绿后全量编译 649 错——**旧五族全数证实已修**：
pyconfig.h C1083 38→7（残余是构建顺序竞态非 include 缺失）、zlib.h→0、
signal_set→0、`interface` 宏级联 146→32（全局 lean 未覆盖的残留路由待
/showIncludes 定位）、cellapp 的 cellapp/*.hpp C1083→0。649>448 是洋葱效应：
旧层塌掉后暴露更深层。**本轮修五族（10 文件，目标 ~305/649）**：
1. **C4005 ×238（自伤）**：`69623139` 加的命令行 `-DWIN32_LEAN_AND_MEAN` 与
   `cstdmf_windows.hpp:5` 的裸 `#define` 在 /WX 下相撞——每 TU 一个 C4005 宏重
   定义。修：该 define 收进 `#ifndef` 守卫（模块级门禁不带此 flag 仍依赖它，
   故不可删）。
2. **C7555 ×37（语言级）**：make 轨 `-std=c++23`（platform_el7.mak:6）而 MSVC
   无 std flag 时默认 c++14，pyscript 指定初始化器全灭。修：
   `BWCompilerAndLinkerOptions.cmake` 全配置 `/std:c++20`（MSVC 最接近的稳定
   档；完整 c++23 需 19.5x 的 /std:c++latest preview 面）。
3. **C4996 ×13（弃用告警即 error）**：① Berkeley 地址族 ×10（inet_ntoa×9 /
   gethostbyname×2 / gethostbyaddr / inet_addr）——`BWStandardProject.cmake` 加
   SDK 自带的 `_WINSOCK_DEPRECATED_NO_WARNINGS` 弃用豁免（调用点转
   inet_ntop/getaddrinfo 记入 Windows lane 台账）；② pyscript `script.cpp`
   Py_FrozenFlag/Py_NoSiteFlag/Py_IgnoreEnvironmentFlag ×3（CPython 3.13 标
   Py_DEPRECATED，gcc 仅告警、MSVC /WX 成 error）——局部 `#pragma
   warning(disable:4996)` 包住三行（迁移到 PyConfig 属行为级改动，留 3.13
   迁移台账）。
4. **cellapp_extension pyconfig ×7（构建顺序竞态）**：MODULE 目标不链 libpython
   故不继承对 bwpython313 ExternalProject 的依赖，其 TU 在 externals 克隆前就
   开编。修：该 CMakeLists `ADD_DEPENDENCIES( cellapp_extension bwpython313 )`。
5. **POSIX 头 C1083 ×5**：`bwconfig.cpp`（pwd.h 收 POSIX，Windows 臂改读
   `USERNAME` 环境变量，与 pw_name 查询同效）；`child_process.cpp` 整 TU
   `#if !defined(_WIN32)` 收拢（fork/exec/waitpid 无 Windows 翻译，仓内无人
   包其头文件）；`nat_config.cpp` include 分置 Winsock2 + inet_addr 改
   inet_pton 助手（in_addr_t 返回型平台不一 + 弃用，保留 INADDR_NONE 哨兵语义）；
   `server_app.cpp`（dirent.h/sys/resource.h 收 POSIX——使用点已在
   `#ifdef _WIN32` 两臂内）；`server_info.cpp`（unistd.h 收 POSIX）。
本地验证：五文件 + `cstdmf_windows.hpp`/`script.cpp` 以 make 轨精确旗标
`g++ -fsyntax-only -std=c++23 ...` 全过（SYNTAX_EXIT=0）；CMake 改动经本地
3.5 最小工程强制 MSVC 验证（`COMPILER_FLAGS: .../std:c++20...` 与
`_WINSOCK_DEPRECATED_NO_WARNINGS` 均落 COMPILE_DEFINITIONS；须先
`include(BWMacros)` 再 `BWStandardProject`，"Unsupported compiler" 行为良性）。
门禁：构建系统文件改动 → 全量 `make -s -rR bw-run-all-unit-tests`：
21 模块 / 1505 用例 / 0 失败（EXIT=0，与批次 38 同数——本轮无测试改动）。
探针 #8 待派发。**下一层已知头**：`interface` 残留 ×32 站点/约 9 server TU
（BW 侧 include 链已排查干净，疑 rpcndr.h 经 lean 不挡的链抵达，需
/showIncludes 探针定路由）；转换告警 ~15 站点（C4267/C4244：balance_config、
bdiff、ema、id_client、py_services_map、python_server×6、services_map、
script_timers）；Berkeley 族调用点转换（现由豁免压制）。

**批次 40（2026-10-16）——探针 #8 普查解码与两大主族修复（R1017）**

探针 #8（run 38795984158，`7358401d`，8m13s，922 错误）全部分族。
R1016D 五族证实全灭（C4005=0、C7555=0、C4996=0、C1083 仅剩 cellapp_extension
closest_triangle 1、C2220=8）；总数 649→922 为洋葱效应——C4005 噪声消失后
显出两大主族：

- **interface 宏级联（约 420 行）路由闭合（无需 /showIncludes）**：include
  闭包脚本证实 `network/basictypes.hpp → math/vector3.hpp → math/xp_math.hpp
  → d3dx9math.h（DXSDK）→ d3dx9.h → d3d9.h（Windows SDK，不在 DXSDK 包内）
  → COM 头直接抵达 rpcndr.h` 的 `#define interface struct`。WIN32_LEAN_AND_MEAN
  不经此路（windows.h 不是载体，全局 lean 已生效）。全部报错站点都是名为
  interface 的标识符（ServerApp::interface()、TimeKeeper 构造参数、
  db_engine_creator::interface()、createImpl 参数、signal 站点解析级联）。
  修法：xp_math.hpp 在 DirectX include 块后 `#undef interface`——rpcndr.h
  的 include guard 保证该 TU 内不会重定义，一次 undef 即持久。
- **std::allocator C++20 成员移除（432 行，43 个实例化）**：
  DebugStlAllocator 的 allocate/deallocate 签名用
  `std::allocator<T>::pointer/size_type` 拼写——C++20 已移除（MSVC C2039
  硬错；libstdc++ 仅留弃用扩展，Linux 轨无感）。测试先行：
  test_memory_debug.cpp 新增 MemoryDebug_testDebugStlAllocator
  （allocate/construct/destroy/deallocate 直测 + std::vector 容器用法），
  再把签名改为类自身 typedef（全仓唯一 std::allocator 使用点，grep 证实）。
- **转换告警族（C4267/C4244/C4312/C4311/C4302/C4065）20 站点全修**：
  size_t→int/uint/uint32/ushort、double→float、Py_ssize_t→int、time_t→int、
  ScriptList::size_type→int 的 static_cast；int32→void* 经 intptr_t
  （script_timers×2）；HANDLE→intptr_t（service _open_osfhandle）；
  SOCKET→int（python_server accept）。entity_app C4065（switch 只有
  default）实为 SIGQUIT 未定义的解析级联，随信号宏修复消失；
  time_keeper/updatables 的 C4430/C4244 同理为解析级联。
- **小族**：ws2tcpip.h 补 include（inet_pton/getaddrinfo/inet_ntop 居所，
  address_resolver/nat_config，修 R1016D 自身 C3861 残留）；A/W 分裂
  （全局 UNICODE 下 plugin_library 钉 A 版 API、util.hpp exePath
  GetModuleFileNameA、bwresource 规范化 _fullpath + exeDir 反斜杠）；
  POSIX 守卫（updatables uint→stdmf include、py_traceback
  _open/_read/_close+ssize_t+io.h、config_reader Windows getline 助手
  （fgets+realloc 增长，同 NULL/-1 EOF 契约）、manager_app_gateway
  Sleep(delay/1000)、table_synchroniser 整 TU Windows 桩（沿
  child_process.cpp 先例）、signal 族（common.hpp 25 个 POSIX 信号号
  compile-compat 宏 + signal_set.hpp Windows 位图 Set（无 operator
  sigset_t*，Blocker 保持 POSIX-only）+ signal_processor 四处平台分裂：
  signal() 注册 / dispatch 去 Blocker / waitForSignals stub /
  signalNumberToString SIGRT 门控）、script_math 两阶段查找（stl_to_py.hpp
  模板定义点绑定的 Script::setData 对 pair<float,MatrixProviderPtr> 的
  前置声明——/permissive- 下定义点之后的重载不再可见）、endpoint
  if_nameindex 桩 const char*、service shellapi.h + intptr_t、
  cellapp_extension CMakeLists bwpython313 ADD_DEPENDENCIES + 根/服务器
  include 路径（probe #7 轮））。
- **自身回归截获**：bwresource 规范化块最初把 POSIX canonicalize_file_name
  臂整个替换成 _fullpath（Linux g++ 语法检查报 _MAX_PATH/_fullpath 未
  声明）——恢复为平台分裂臂（_MAX_CANONICAL_PATH = _MAX_PATH / PATH_MAX）。

门禁：全量 `make -s -rR bw-run-all-unit-tests`：21 模块 / 1505 用例 /
0 失败（EXIT=0）。新测试位于 ENABLE_MEMORY_DEBUG 守卫内（与既有
MemoryDebug_testCounts/testSmartPointerCounts 同境；hybrid server 轨该开关
经 config.hpp 公式求值为 0，故三者在门禁二进制中均不注册、计数不变）；
守卫开启态（+ -DENABLE_MEMTRACKER）经 make 轨精确旗标语法检查验证
（SYNTAX_EXIT=0）。改动 26 个 TU/头全部过 make 轨精确旗标语法检查。
探针 #9 待派发。**下一层已知头**：两大主族修复的真实效果由探针 #9 判定；
Berkeley 族调用点转换（现由 _WINSOCK_DEPRECATED_NO_WARNINGS 豁免压制）；
bwmachined/linux_machine_guard.cpp（signal(SIGCHLD)）与
server/cellapp/LeakTracer.cpp（裸 windows.h TU）为下轮整 TU 守卫候选。

**批次 41（2026-10-16）——探针 #9 普查解码、interface 宏改为按文件局部作用域（R1018）**

探针 #9（run 38810084412，`732813a9`，7m59s，307 错误）：922→307 腰斩，R1017
旧族全灭（无 C2039 allocator 洪泛、无 C2332/C2187 interface 声明错误）。分族：
C2146×109、C4430×90、C2378×44、C2371×37、C3861×4、C2664×3、C2275×2、C2143×2、
C2065×2、C2059×2、C2039×2、C1003×2、C2665×1、C2498×1、C2447×1；告警 C4244×1、
C4229×1。

本轮主修：interface 宏作用域整体反转。

- **R1017 的修法被证伪**：xp_math.hpp 在 DirectX include 块后的 TU 级
  `#undef interface` 引发 resmgr 191 错 + cellapp_extension 104 错，全落在 SDK
  COM 头内部（shobjidl.h 170、shlwapi.h、d3d10.h）——rpcndr.h 的 include guard
  保证该宏 undef 后不再重定义，于是其后的每个 SDK COM 头都失去 interface。
  宏是硬二分：SDK COM 头（d3d9.h 自己的 DECLARE_INTERFACEForward、shlobj/
  shobjidl、shlwapi、d3d10）在其解析时刻要求宏在场，BW 代码要求宏缺席。
  重命名出界（624 处精确 token / 61 处 `.interface()` 调用 / 175 文件）。
- **改为按文件局部作用域**：每个以 interface 为标识符的 BW 文件自带
  `#if defined( _WIN32 )` 内的 `#pragma push_macro( "interface" )` +
  `#undef interface`（压栈再摘除），文件末 `#pragma pop_macro` 还原——只在本
  文件被解析期间关宏，其后的 SDK COM 头恢复宏在场。宏缺席因此与各文件的
  include 顺序无关。
- **xp_math.hpp 的 TU 级 undef 已回退**（-11 行），不再承载宏策略。
- **覆盖范围从「探针报错文件」扩大到全量普查**：以正确注释/字符串剥离器
  重扫 lib/server/common（排除 third_party、client_integration），得 95 个持
  独立 interface token 的文件，93 个已包（xp_math.hpp 本身不含该 token；
  render_context.ipp 无 include 且由 render_context.cpp 的包覆盖）。
  旧启发式剥离器被字符串内的 `/*` 误判为块注释起点而卡在注释态、整行清空，
  95→10 假阴性——这直接导致前两轮漏包 18 个文件与一次基于错误清单的重建。
- **包内顺序的两条硬规则**（本轮反复踩坑后确立）：push 必须落在文件**首个
  include 连续块的末尾**——落在「首个 include 之后」会让后续 include（如
  math/vector3.hpp 经 xp_math 抵达 d3d9.h）在宏缺席下解析；pop 必须落在
  头文件 guard 的**最后一个 `#endif` 之前**（否则二次包含双重 pop）。
- **包内后位 include 的 SDK 首达性验证**：20 个文件的包区间内仍有 include
  （CODE_INLINE 的 .ipp、interface 消息头、network 的 Linux-only 头）。
  include 闭包脚本（先按相对包含者解析、再根目录回退）证实 20/20 的
  d3dx9math.h（及 d3d9.h/d3d9types.h/d3d10.h）**都在 push 之前已抵达**，
  其 include guard 使包内再次包含成为空操作——SDK 头恰在宏在场时被解析。
- 结构自检（93 文件，0 违例）：push/pop 各一且 push 在 pop 前；push 先于
  首个 interface token；#if/#endif 计数与 HEAD 逐文件相同且嵌套深度归零；
  无 SDK include 直落包内；末行保留换行。中途两次重建脚本各自引入过
  「push 块只剩 pragma 行、#if 成为孤立开括号」与「pop 吃掉 guard 收口」
  两类损坏，最终改为从 HEAD 内容重建（HEAD 无任何 wrapper）才干净收敛。

门禁：全量 `make -s -rR bw-run-all-unit-tests`：21 模块 / 1505 用例 /
0 失败（EXIT=0，与批次 40 同数——本轮无测试改动）。语法检查：非门禁模块的
10 个 TU（entitydef/data_types、connection/baseapp_login_request_protocol、
network/network_interface、network/packet_receiver、network/endpoint、
baseapp/initial_connection_filter、cellapp/entity_type、loginapp/loginapp、
transfer_db/transfer_db、bwengine_mysql/mysql_engine_creator）PASS；另 10 个
FAIL 均为 Linux 既有面且与本轮改动无因果（lib/moo 两 TU 需 d3d9.h、
machined_utils/watcher_nub/bwmachined 的枚举位运算触发 -Werror、
database_tool_app/logger 的 `-DMF_CONFIG` 取值需在字符串宏内展开）——插入块
整体位于 `#if defined( _WIN32 )` 内，Linux 预处理即整块丢弃。

**下一层已知头**：两大族修复的真实效果由探针 #10 判定；service.cpp
wsprintfW/lstrlenW（char 缓冲需 A 版）与 CommandLineToArgvW 第二参需 int*；
server_app.cpp kill/ctime_r 无 Windows 版；plugin_library
`_WIN32_FIND_DATAA::cFileName`（R1017 误拼 cFileNameA，MSVC 结构体无 A 后缀
字段）；nat_config inet_pton 与 address_resolver inet_ntop 的 ws2tcpip include
生效性；smartpointer C2665 ConstSmartPointer<BW::Vector4Provider> 构造；
shared_data C2220；C4244×1/C4229×1。

**批次 42（2026-10-16）——探针 #10 普查解码、wrapper 错位根因修复与残余单点批（R1019）**

探针 #10（run 38822218035，`7c7006a7`，job `continue-on-error`，conclusion
无意义、以 log artifact 判读）：307→176。分族 C4430×46、C2065×33、C3646×26、
C3861×19、LNK2019×16、C2143×11、C2039×6、C2664×4、C2238×4、C2059×4、
C2440×3、LNK1120×2、C2504×2、C2332×2、C2220×2、C2146×2、C2144×2、C4596×1、
C2761×1、C2665×1、C2334×1、C2338×1；按工程 cellapp_extension 140、
lib/server 32、bwengine_xml 10、bwengine_mysql 8、network 3、pyscript 1、
cstdmf 1。

本轮主修：R1018 的 wrapper 插入脚本存在**错位根因**——「首个 include 连续
块」遍历器把 `#if`/`#endif` 也当连续块成员，7/93 个文件的 push 落进了
Windows 不活跃条件内（server_app.cpp 在 `#if !defined( _WIN32 )`、moo_dx.cpp
在 `#ifdef ENABLE_DX_TRACKER`、packet_receiver.cpp 在
`#if defined( PLAYSTATION3 )`、cellapp/cellapp.cpp 在
`#ifdef MF_USE_VALGRIND`、cellappmgr/cellapp.cpp 在 `#ifndef CODE_INLINE`、
dbapp.cpp 在 `#ifdef BUILD_PY_URLREQUEST`、add_to_cellappmgr_helper.hpp 的 pop
紧贴 push）：Windows 下 `#undef` 不执行（宏整文件存活、interface 站点全灭）
而单独 pop 触发 C4602——与探针 #10 的 server_app.cpp(104) C2332/C2447 和
packet_receiver.cpp(1444) C4602 完全对上。修复：push 重定位到**无条件**
include 之后（逐一验证 SDK 首达性：前序 include 已经
xp_math.hpp→d3dx9math.h→d3d9.h 抵达，include guard 使包内再入为空操作），
pop 统一移到文件尾；server_app.cpp 的修复脚本中途还把 pop 落进了
numFileDescriptorsOpen 函数体（`#ifndef _WIN32` 支路内的 Windows 死代码），
已纠正至 EOF。全量复核：93 文件 0 真实违例（2 个 flag 均为检查器误报：
bwtracer.cpp 的 push 在 `#ifndef _WIN32` 的 `#else` 即 Windows 支路；
watcher_nub.cpp 的 push 在 `#if ENABLE_WATCHERS`=1 的活跃块，移动反而违反
SDK 首达性故不动）；#if/#endif 计数与 HEAD 逐文件全等。

批内其余修复（探针 #10 残余单点）：

- plugin_library.cpp：`_WIN32_FIND_DATAA` 字段是 `cFileName`（R1017 拼成
  cFileNameA，MSVC 结构体无 A 后缀字段）。
- service.cpp 三处：CommandLineToArgvW 第二参要 `int*`（局部 int 再回写
  dwArgc）；SERVICE_DESCRIPTION::lpDescription 在 UNICODE 下是 LPWSTR（静态
  TCHAR 数组承载 "BigWorld Daemon"）；control_handler 的缓冲改 TCHAR
  （wsprintf/lstrlen 的 W 映射）。
- server_app.cpp：`ctime_r`→`ctime_s`（`#if defined( _WIN32 )` 分支）；
  `kill( getpid(), sig )`→`raise( sig )`（C99 等价、MSVC 声明）。
- shared_data.cpp：`PyDict_Size` 返回 Py_ssize_t，显式 static_cast 收窄
  （/WX 下 C4244→C2220）。
- script_math.cpp：补 `Script::getData` 前置声明（R1017 只声明了 setData，
  探针 #10 的 C2665 正是 stl_to_py.ipp:42 的 getData——两阶段查找下 MSVC
  只见 Vector4Animation_Keyframe 重载，其 pair 元素转换
  MatrixProviderPtr→Vector4ProviderPtr 不存在）。
- BWStandardProject.cmake：`_WIN32_WINNT` 0x0501→0x0601（NTDDI 同步
  0x06010000）。inet_pton/inet_ntop 在 ws2tcpip.h 是 `>=0x0600` 门控，XP 钉
  死使 nat_config.cpp/address_resolver.cpp 的调用 C3861；VS 2026 工具集无
  XP 能力、CPython 3.13 自身要求 8.1+，钉死本就虚构。pyconfig.h 仅在
  `Py_BUILD_CORE*` 下且 `#ifndef` 包裹地自定这些宏（server 侧 TU 不受
  影响，无 C4005 相撞面）；openssl/mongodb 等 third_party 的引用皆
  `#ifndef` 自守卫。
- memory_debug.cpp：两张 map 的 allocator value_type 非常量键
  （`pair<CallstackAddressType,...>`→`pair<const ...>`）——C++20
  [container.alloc.reqmts]/5 分配器一致性（MSVC 静态断言 C2338，
  libstdc++ 静默容忍，门禁腿因此漏过；其余 unordered_map 用法
  `pair<const void* const,...>` 形异但 MSVC 对 unordered 无此断言，不动）。

门禁：全量 `make -s -rR bw-run-all-unit-tests`：21 模块 / 1505 用例 /
0 失败（EXIT=0，与批次 41 同数——本轮无测试改动）。包装文件复核：93 文件
`#if`-栈追踪器 0 真实违例；8 个改动文件 #if/#endif 计数与 HEAD 全等。

**下一层已知头（卡点）**：cellapp_extension 140 错为 MSVC-only——Linux 以
同宏集（MF_SERVER + 全套 Windows 宏面模拟）`g++ -E` 的 703 头闭包不含任何
moo 客户端头（仅 moo/reload.hpp），Windows 分支扫描（直接 `#if _WIN32` 与
间接 EXT_MATH/DIRECTX_MATH/D3D9 宏）亦 0 命中，base_texture.hpp 仍被 MSVC
解析——本机不可解释，需探针加 `cl /showIncludes` 或 `/P` 判定真实 include
路由；bwengine_xml(10)/bwengine_mysql(8) 的 LNK2019×16 为 server.lib 未产
出的级联，预计随 server 腿转绿消失。

**批次 43（2026-10-16）——Linux CI 两条从未绿腿根因修复 + 探针 #11 普查（R2PATROL 第 2 轮）**

一、entitydef 腿（自 workflow 建立起从未绿，7 连红）：**根因实证**——
`game/` 整目录被 `.gitignore:17` 排除（0 tracked files），而
`BaseResMgrUnitTestHarness`（`lib/unit_test_lib/base_resmgr_unit_test_harness.cpp:66-77`）
按 `--root` 拼 `<root>/game/res/bigworld` 进 BWResource 路径；`Script::init`
（`lib/pyscript/script.cpp:411`）经 `PyImportPaths` 把 `scripts/common`、
`scripts/common/Lib`、`scripts/server_common/lib-dynload-<平台>` 挂上 sys.path
（`script.cpp:460-490`），stdlib 纯 Py 源之外**还要 dlopen 59 个 CPython 扩展
模块**（`pickle`→`_pickle`、`struct` 等即时命中）——该树由 make 链
`third_party_python.mak` 的 `python_library` 目标装配（Lib→`scripts/common/Lib`、
`lib/pyscript/bw_site.py`→`scripts/common/bw_site.py`、构建树 `Modules/*.so`
59 个→`scripts/server_common/lib-dynload-el7`），cmake 样板从不装配。**最小
充分集二分实证**（`--res` 直指工作副本后逐组件删除）：common/Lib +
bw_site.py + lib-dynload-*.so 三者必需；site-packages（common/server_common
两侧）与 __pycache__ 均可删（entitydef_test 不触及）。本地复现：藏
`game/`→78 失败（与 CI 签名一致），恢复→380 全绿。**修复（`7d28b832`）**：
`cmake/samples/python` 的 ExternalProject 改 `make all`（python.exe+共享模块，
3.12+ 的 `check_extension_modules.py` 会 dlopen 校验并改名失败模块，故同时
移植 make 链三 sed 中的两 sed——TERMNAM undef 与 LINKFORSHARED
whole-archive openssl + `-u,BW_Py_memoryTrackingIgnore*`；sed 表达式在
ExternalProject 生成串里不能带反斜杠引用，TERMNAM 按宏展开为三条、
LINKFORSHARED 定界符换 `|`）；新增 `install_res.cmake` 装配
`python-res/game/res/bigworld/scripts` 三件套（59 .so 全数 glob，排除
`_failed` 改名）；`cmake/samples/entitydef` 的 add_test 从 `--root` 换
`--res "<unit_test/res>:<python-res 树>"`。验证：本地 cmake 重配全量重建后，
**藏 `game/`（=CI 干净检出等价）ctest 380/380 绿、恢复后仍绿**；python 样板
头注"entitydef_test 从不 dlopen lib-dynload"的旧论断被二分证伪并改写。

二、network 腿（同 7 连红）：`test_config.cpp:20` ConfigTest 断言
`setBufferSize(SO_RCVBUF, 16777216)`，`Endpoint::setBufferSize`
（`endpoint.cpp:604`）判 `getBufferSize >= size`；内核对 SO_RCVBUF 读取值
×2 且钳到 `net.core.rmem_max`，GH ubuntu runner 默认 212992 → 需
`rmem_max >= 8MiB`。环境前置而非代码缺陷。修复（`6415b0fb`）：ci.yml Test
步前加条件步（matrix=network 时）`sysctl -w rmem_max=16777216
wmem_max/wmem_default=1048576`。

三、探针 #11（run 38837937153，`cc80f752`，58m46s）：job FAILURE（run 级
"success" 为 continue-on-error 所致，判读以 job/log 为准）。普查 **141 C 错 +
2 LNK1120**，按工程 cellapp_extension 140（首错 `base_texture.hpp(89,15)`
C2039 'Colour'，与 #10 同起点）+ cstdmf 1（**memory_debug.cpp 第三张 map**：
Key=`const char*`，分配器须 `pair<const char * const, V>`；批次 42 只修了两张
`void*` 键 map——`const void*` 键在 #11 里通过、`const char*` 键漏网，修
`aeb0494e`，门禁 21/1505/0）+ bwengine_xml/mysql 各 9/7 unresolved（含
`bw_new`/`LogMsg`——**模块 DLL 零链接列表**；批次 42 的"server.lib 级联"
假设反证：#11 server.lib 已产出仍报）。修 `74d39171`：两 DLL 补
`BW_TARGET_LINK_LIBRARIES`（镜像 dbapp 链接集减 db）；mysql 五组件
（bwengine_mysql/db_storage_mysql/sync_db/clear_auto_load/consolidate_dbs）
按 make 链 `SYSTEM_HAS_MYSQL` 语义 `FIND_PATH mysql/mysql.h` 无 dev 即
`RETURN()` 跳过（Windows runner 无 MySQL 且无 in-tree/manifest 客户端）。
另记：MSBuild `-m:4` 遇失败即停调度新项目，chunk/scene/terrain/entitydef/
dbapp/db_storage_mysql 从未进入 #11 日志——**#11 是阻塞清单的头、非全量**，
三修后探针 #12（38852284641）推进下一层；cellapp_extension 卡点不变
（Linux `g++ -H` 七宏集闭包零 moo 头、umbra 空文件、moo/reload.hpp MF_SERVER
自足），`69d49d87` 给探针加了 `CL=/showIncludes` 单 TU 诊断步与 log 上传，
待 #12 artifact 定位真实 include 路由。

### 批次 44（2026-10-16，R1020）：探针 #12 决定性普查——cellapp_extension 百错根因闭合

**Linux CI 双腿闭合（run 38852265405，HEAD 69d49d87）**：`network cmake sample` 与
`entitydef cmake sample` 首次全绿（sysctl 步 + python res 树修复生效；entitydef 腿含
CPython `make all` 全量构建）。ci.yml 自诞生起的两条红腿闭合。

**探针 #12（run 38852284641，6m02s，job FAILURE）**：错误数 141→1699 但结构全同——
cstdmf C2338 修复解锁 MSBuild 调度，chunk（11 TU）/entitydef/connection/moo/
cellapp_extension（8 TU）首次真实编译，每 TU 均以 `base_texture.hpp(89)` 起步 140 错。
TU 归属经错误档 `(compiling source file ...)` 精确解析确认。

**决定性证据（`CL=/showIncludes` 单 TU 链，`cmake-showincludes-cellapp.log`）**：
`bounce_grenade_extra.cpp → common/closest_triangle.hpp → chunk_obstacle.hpp →
model/model.hpp → moo/visual.hpp → moo/managed_texture.hpp → moo/base_texture.hpp(89)`。
同 TU 内 `chunk_obstacle.hpp:7 #if !defined(MF_SERVER)` 的 romp include 未触发
（romp 头全链 0 次）⇒ MF_SERVER 已定义；而 `chunk_obstacle.hpp:12
#if ENABLE_RELOAD_MODEL` 触发 ⇒ 宏体为真。两者矛盾的唯一解释：**MSVC 传统预处理器
把宏展开产生的 `defined()` 按未定义求值**（标准明文 UB；GCC/Clang 就地求值故 Linux
无恙）。config.hpp 全部 12 个宏体含 `defined()` 的 ENABLE_* 均受累：
`ENABLE_MEMORY_DEBUG` 错误为 1 ⇒ `memory_debug.cpp`（整体在 `#if ENABLE_MEMORY_DEBUG`
内，Linux 下空 TU）被编译 ⇒ `<map>` C2338 复现即独立旁证。

**修复（config.hpp，语义真值表逐一保留）**：PROFILER/HITCH_DETECTION/ASSET_PIPE/
MINI_DUMP/MEMORY_DEBUG/STACK_TRACKER/ALLOCATOR_STATISTICS/RELOAD_MODEL/
FIXED_SIZED_POOL_ALLOCATOR/FIXED_SIZED_POOL_STATISTICS/
REFERENCE_COUNT_THREADING_DEBUG/TRANSFORM_VALIDATION 十二宏全部重写——`defined()`
判定上提为真实 `#if`，宏体成为纯表达式（`#if` 与真实代码两用）；补
`FORCE_ENABLE_MEMORY_DEBUG`/`FORCE_ENABLE_ALLOCATOR_STATISTICS` 的 `#ifndef 0` 默认
（原仅 `ENABLE_MEMTRACKER` 块定义、普通构建下是未定义标识符，`#if` 合法但真实代码
非法）。`BWCLIENT_AS_PYTHON_MODULE`/`BW_EXPORTER`/`BW_EMBEDDED` 不可加默认值
（`defined()` 调用点遍布 allocator.cpp/ned_allocator.hpp/bwresource.cpp 等），
改为在宏体的 `#if` 条件里取值。

**测试先行**：`test_config_values.cpp`（static_assert 钉服务端契约
ENABLE_RELOAD_MODEL==0 等六项 + TEST 运行时镜像）在旧 config.hpp 下 GCC 直接拒绝
编译（宏体 `defined()` 进真实代码即错）——天然红；修复后 make 轨 cstdmf_test
451→452 全绿。教训：含 `defined()` 的宏体连「用作 C++ 表达式」都做不到，
MSVC 更会静默错值——真值表进 `#if`、宏体进表达式是唯一两全结构。

**#12 首见的 Windows 端口洞（真码层，非宏）**：`mailbox_base.cpp:12 <arpa/inet.h>`
（TU 内零 arpa/inet 符号使用，vestigial）、`replay_header.cpp:7,68 <sys/time.h>+
gettimeofday`（仅取 tv.tv_sec，秒分辨率足够）、`integer_data_type.cpp:53 ssize_t`
（→ `intptr`，与 PRIzd 三平台精确匹配：POSIX long/_WIN64 __int64/_WIN32 int）。
`moo/png.cpp:10 png.h C1083`：png 已迁 vcpkg（make 轨 `third_party_vcpkg.mak`），
CMake 根的 libpng 映射仍指 VS2003 工程（`BWConfiguration_server.cmake:98
third_party/png/projects/visualc71`）——Windows 根构建的 libpng 接线为独立设计件。
探针 root build 步补 `_CL_=/WX-`（`BWCompilerAndLinkerOptions.cmake` 的 /WX 追加在
命令行后，_CL_ 再后置故 /WX- 胜出；C4267 转换告警类清除，与模块腿偏差一致）。

另记：push run 38852265330 的 cstdmf 门禁腿被探针 #12 dispatch 经并发组取消
（skipped 腿仍占组），已 `gh run rerun --failed` 重跑。

### 批次 45（2026-10-17）：探针 #13→#15 三层递降——message_logger MSVC 移植与 MODULE 链接层

**探针 #13（run 38871089923）→ 1699→23 C + 227 LNK**：批次 44 的 defined() 修复
在 Windows 上得到验证，真码层错误全部暴露。

**批次 `13eed37d`（全量门禁 21 模块 / 1506 用例 / 0 失败后提交）**：
- `message_logger/posix_compat.hpp`（新）：目录枚举（opendir/readdir/closedir/
  scandir——FindFirstFileA 承载，scandir 比较器仅占签名、调用方按段起始时间自排）、
  unlink/getcwd/dirname/rmdir/mkdir(0777)/gettimeofday（FILETIME 1970 纪元换算）/
  localtime_r/gmtime_r（_s 变参序相反，wrapper 归位）/getline（NULL/0 起步、保换行、
  调用方 free——全部按 POSIX 语义）。
- 26 处 include 平台化（`#if defined( _WIN32 )` + shim / `#else` 原 POSIX 树）；
  复检新抓三漏网：根 `metadata.cpp` vestigial unistd（删）、根 `hostnames_validator.cpp`
  与 `mongodb/hostnames.cpp` socket 块换轨（前者 inet_ntoa/gethostbyaddr 在用，
  非 vestigial）。
- `BWStandardProject.cmake`：vendored png 1.6.2 头目录全局 include（同 zip 先例）——
  png 卡点以最简路径闭合，`lib/moo/png.cpp:10 png.h C1083` 探针 #14 实证已清；
  `cellapp`/`bots` 链接表 `png`→`libpng`、`pysics2`→`physics2` 笔误修复。
- `cellapp_extension` 显式链接表（MODULE 目标零符号容忍，对齐 cellapp 链接集 +
  libpython）。
- `bw_profile.cpp` `access/W_OK`→`_access/2`（`_W_OK` 被 nonstdc guard 挡，
  C2065，探针 #14 实证）。
- `CMakeLists.message_logger.txt`：mongodb 后端按 make 链同款探针门控
  （`FIND_PATH(mongo/client/dbclient.h)`，缺即弃源不定义 `HAS_MONGODB_SUPPORT`；
  原文件无条件编 mongodb TUs，Windows runner 必 C1083）。

**探针 #14（run 38883400603）→ 20 条**：regex.h×11（`query_params.hpp:28`，_bwlog）、
mlutil stat/geteuid×6、`_W_OK`×1、re2.lib LNK1181、python3.13.lib LNK1181。

**批次 `cc2fe51d`**：posix_compat.hpp 增 std::regex 承载的 regex shim（工具只问
"是否匹配"，regmatch_t 全免）；`BWConfiguration_server.cmake` 补 `re2 third_party/re2`
（vendored 树自带仓内定制 CMakeLists，漏配而已）；`python${MAJOR}${MINOR}.lib`
（`python3.13.lib` 带点拼写必 LNK1181，PCbuild 契约无点）。

**探针 #15（run 38985715421）→ 1 C + 326 LNK（bwengine_xml 117 / _bw_profile 29 /
cellapp_extension 180）**，全部修复在 **`a7003cdc`**（门禁 21/1506/0 后提交）：
- C2664 `hostnames.cpp:49`：Winsock `gethostbyaddr` 首参 `const char*`（POSIX 为
  `const void*`）——加 `(const char*)` 转换（双向兼容）；`hostnames_validator.cpp:187`
  同款 latent 修（其所在 exe 探针 #15 未编到，属预防）。
- **cstdmf server 配置三缺口**（`IF( NOT BW_IS_SERVER )` 源表在 Linux make 轨成立、
  Windows server 根构建即炸）：`memory_load.cpp`（log_msg.obj 的
  win32GenerateCriticalMsg 需要）、`ned_allocator.cpp`（allocator.cpp:40
  `#define SystemAllocator BW::NedAllocator` 无守卫）、`critical_message_box.cpp`
  （critical_error_handler.cpp 的 Windows ask() 需要）——server+Windows 分支补齐，
  nedalloc 链接同扩（`BWConfiguration_server.cmake` 增 vendored `nedalloc` 目标）。
- `_bw_profile`/`bwengine_xml` MODULE 目标直链 `libpython`（本轨 Python 符号从
  不经宿主进程加载期解析，必须自带 import 归档）。
- 探针 workflow 增「Dump module link lines」步（vcxproj 的
  `<AdditionalDependencies>` 全量落盘 `cmake-linkdeps-root.log`）：/v:m 控制台
  永不回显 link.exe 命令行，libpython 是否真达 MODULE 链接行此前不可判定
  ——探针 #16 以此给硬证。

**余留实证卡点（cellapp_extension 180 LNK 中的宿主符号族）**：`Entity::id/
position/isReal`、`Controller::*`、`g_entityMovementCallback`（file:line：
`examples/cellapp_extension/speed_extra.cpp:296/327`、`speed_controller.cpp:96`）
是 cellapp 可执行体独有符号——Linux `.so` 扩展靠加载期解析（宿主进程内即有），
Windows MODULE（DLL）链接期必须全解析，需 cellapp 侧 `__declspec(dllexport)`
导出故事（引擎现无 exe 导出机制）。解锁条件 = 为 cellapp 宿主符号定导出方案
（`.def`/dllexport 宏/改托管接口三选一），属独立设计件；Py/cstdmf 符号族
（同目标内）探针 #16 由 linkdeps 判定后另行收敛。

### 批次 46（2026-10-18）：exe 车道全绿——探针 #16→#31 递降至 11/11

**范围**：root build 探针加 exe 车道（`c2d8a8c7`：`cmake --build --target` 逐目标
lane，11 个可执行 + `cmake-build-root-exes.log` 落盘）起，至 **探针 #31（run
39065790752，`d3a2336a`）11/11 全绿** 收口。每轮落地前本地门
`bw-run-all-unit-tests` 21 模块全 result 0 / EXIT=0。

**清点口径**：`cmake-build-root-exes.log` 按 MSBuild banner（`^MSBuild version`）
分 11 节，一节一 lane（sec01 cellapp … sec11 message_logger）；该 job
`continue-on-error: true`（ci-windows.yml:277）使 run 级 conclusion 恒 success，
真绿只认分节清点。

**推进轨迹（分节清点）**：#26（`a52dcbb3`）5/11 → #27（`1ec3a0a8`）7 →
#28（`6b3a4cf3`）9 → #29（`b395ed75`）10 → #30（`42108b2c`）10 →
#31（`d3a2336a`）**11/11**（0 错，11 个 .exe 全产出；余 3 处 LNK4098
LIBCMT defaultlib 冲突警告，cellapp/baseapp/dbapp，非错误）。

**终点五批修复类**（file:line 与错误形态见 todo.md R3 条目同轮追记）：

1. **DEFINE_INTERFACE_HERE/DEFINE_SERVER_HERE 双重定义病**：接口头的两路
   全量定义在 make 链靠「归档成员已定义不拉」活下来，MSVC 把两 obj 无条件
   放上链接行 → LNK2005（dbapp ×40、baseapp ×211）。修法 = exe 源表删本地
   `*_interface.cpp` 留 .hpp；dbappmgr 的保留（其 message_handlers.cpp 只对
   dbappmgr_interface.hpp 做 DEFINE_SERVER_HERE）。
2. **bwservice.hpp 服务入口三定义**：Windows 臂把 usage/bwNarrowToTCHAR/
   全局 `__cdecl main` 放头文件 out-of-line，双 TU 包含即 LNK2005 'main'。
   `BW_NO_SERVICE_MAIN` opt-out；首版整段守卫误把 `BW_END_NAMESPACE`
   一起编译掉（C2888/C2039 ×105，自省回归），终版两段守卫夹命名空间收口。
   教训：**抑制守卫绝不可跨 namespace close**。
3. **resmgr/network 族补链**：dbappmgr/transfer_db/message_logger 的
   `BW_TARGET_LINK_LIBRARIES` 缺 resmgr（BWResource ctor/dtor/init/getPath/
   getPathNum/fileExists/openSection/purge、DataSection::openSection/
   readString/writeString、DataSectionIterator、Singleton<BWResource>::
   s_pInstance）与 network（WatcherNub、LoggerMessageForwarder、
   Mercury::EventDispatcher、MachineDaemon::queryForInternalInterface、
   Endpoint::convertAddress、FileStream::error、Logger::init、
   MachineGuardMessage）；make 链 Makefile 同表逐条佐证。
4. **MSVC base vtable 非 pure 槽**：IChildProcess 唯一非纯虚
   `onExecFailure` 的定义 TU（child_process.cpp）整 TU `#if !defined(_WIN32)`
   编译掉，构造派生对象即 LNK2001（base vtable 照发）——改头内联。
5. **POSIX 面收口**：strerror_r→strerror_s（errno_t 返回，逗号表达式递
   buffer）、link(2)→CreateHardLinkA（参数序相反，posix_compat.hpp）、
   mkdtemp shim（glibc 式 _mkdir 探测）、transfer_db consolidate CD_* 宏
   （_open/_O_RDONLY|_O_BINARY——text 模式会毁 sqlite 字节；ssize_t→int）、
   snapshot.cpp 整 TU Windows 桩（fork/exec + rsync 未移植，同 Consolidator
   先例，init/transfer* 报不支持）、sqlite_database statfs 块 POSIX 守卫
   （CIFS 超级块魔数属 Unix 挂载面，Windows 恒 true）、server_app
   fileDescriptorLimit→_getmaxstdio、baseapp usleep→Sleep、
   g_timingMethod/RDTSC 守卫。

**余留（均已知设计件，不入增量）**：cellapp_extension 宿主符号族
（stage-1 ALL build 的 LNK——exe 导出机制 .def/dllexport/改托管接口三选一）；
3 处 LNK4098 LIBCMT 警告（cosmetic，后续 /NODEFAULTLIB 评估）。

### 批次 47（2026-10-18）：L39 exe 打包——产品车道落地（探针 #32 实测 + daily Windows 车道）

**探针 #32（run 39075113978，`14ebd63a`）**：exe 车道 11/11 复绿（0 实错，
11 个 .exe 全产出），首个产品 artifact `ci-windows-root-products-*` 实测
定盘——11 .exe + Hybrid .dll（_bwlog / bwengine_xml / _bw_profile）+
python313.dll（7.2 MB）+ 39 个 .pyd 全数在列；cellapp_extension.dll 缺席
（已知设计件，manifest 记录）；stdlib in-repo 2338 文件。构建 18m41s、
job 全程 20m20s（比首估 ~1h 低三倍，CI 经济无忧）。

**布局决策（源码证据）**：全仓无 `Py_SetPythonHome`/`Py_SetPath` 调用 →
解释器以 python313.dll 所在目录为 home、期望旁边 `Lib/`（PCbuild 自身
布局）→ **扁平 zip**：exes + BW Python 模块 dll + python313.dll + *.pyd +
`Lib/` 同目录自足。bin/python 分离会让全部 import 失败。

**共享脚本化（两条车道同一路径）**：
- `scripts/build-windows-server.ps1`：configure + 11 exe 车道逐目标构建
  （单次 msbuild 在首个失败工程即停，会把其余 10 个藏起来——run
  39013952226 教训）；`-SkipConfigure` 供探针保留自己的 configure/stage-1
  分节与日志契约。
- `scripts/package-daily-windows.ps1`：收产物 + SHA256 manifest（头部记
  toolset/UCRT 假设/stdlib 计数/cellapp_extension 缺席原因）+ zip；版本串
  镜像 `package-daily.sh`（vcpkg.json "version" + `+daily<UTC 日期>.<短
  sha>`），写 `out/pkg-version.txt` 供 artifact 命名。
- 调用方：daily-build.yml `windows-products` job（交付面）与 ci-windows.yml
  探针 job（验证车）——探针绿即交付面绿，无第二套逻辑。

**daily-build.yml `windows-products`**：windows-latest、timeout 90、48h
新鲜度门与 Linux 车道一致（workflow_dispatch 无条件）、**无
continue-on-error**（构建红即 job 红）、upload-artifact 14 天保留
（`bigworld-daily-<ver>-windows-x64`），无 tag 无 release（发版红线）。

**本轮写后即审修掉的三个脚本 bug（未经 runner，探针 #33 判定）**：
manifest 哈希循环的管道输出没有收进 `$manifest` 数组（manifest 只剩头部）、
相对 `$stage` 对绝对 `FullName` 做 `Substring` 会切错（改 `$stage` 建目录后
即转绝对）、`$PyBin` 参数定义未用（改为从 `$BaseDir` 推导）。

**余留**：探针 #33 验证脚本改造后的同路径（含 manifest 哈希行回归）；
daily 车道首跑实证；bundle 运行时冒烟（exe 启动级，需 Windows 侧）未做。

**探针 #33 判定（2026-10-20，run 39087537135，`31ce396c`）——三处修复全部实证**：
exe 车道 11/11 复绿（0 实错、11 个 .exe）；manifest 2387 条 SHA256 行
（哈希收进数组 ✓）、相对路径无乱切（`Lib\__future__.py` 形态，Substring
转绝对 ✓）、头部完整（version `14.4.1+daily20261018.31ce396c` / toolset /
UCRT / stdlib 2338 / cellapp_extension 缺席说明）、`Lib\` 2338 条 +
python313.dll + 11 .exe + _bwlog/bwengine_xml/_bw_profile.dll + 34 pyd 全数
在列、zip 名带版本串、zip 内 MANIFEST.txt 在位；**哈希交叉验证**：zip 内
python313.dll 实算 SHA256 == manifest 行（`8ECEBB59…771A`）。2448 行 artifact
总账 = 2338(stdlib) + 49(bundle 顶面文件) + 头部。
（注：cron 记录曾写「39 pyd」，实测 34——以 artifact 为准。）

**daily 车道首跑已触发（run 39350418532，`31ce396c`，workflow_dispatch）**：
Linux 车道（build + 21 模块门 + tar.gz/deb/rpm）与 Windows 车道
（`windows-products`）并行，产物 artifact 14 天保留，无 tag 无 release；
首跑双红双修，见第 49 批。

## 第 48 批（2026-10-20）：bundle 运行时冒烟步 + gate17 环境取证

**ci-windows.yml「Smoke the products bundle」步**（Upload 之后，
`if: always()`，pwsh）：解开 zip 后两项窄断言——
1. `LoadLibraryW(python313.dll)`（P/Invoke kernel32）非零，证明解释器
   导入链在 bundle 内自足；失败打 Win32 错误码。
2. `baseappmgr.exe` 起进程：退出码 `-1073741515`
   （`0xC0000135` STATUS_DLL_NOT_FOUND）判 FAIL（bundle 缺 DLL）；其余
   退出码（配置类报错）是数据不是失败；20s 不退则 kill 并报告不判死。
不做完整 boot（无 res/、无 bw.xml）——冒烟只验"装得上、起得过 loader"。

**gate17（CI 文件改动 → 全量套）**：20/21 绿，network_test 1 失败。
取证：`test_config.cpp(20)` 报 `SO_RCVBUF` 16MB 设置失败，机器
`net.core.rmem_max=4MB`（重启丢失此前手工 sysctl）。按测试自己文档的
前置条件恢复并写入 `/etc/sysctl.conf`（带标注，幂等）后单模块复跑
network_test：256 tests, 0 failures。判定：环境前置条件缺失，与本次
改动无关；等效 21/21。探针 #34 负责冒烟步本身的 runner 侧实证。**探针 #34 判定（run 39357396757）——冒烟步绿**：LoadLibraryW 过；
baseappmgr 20s 存活（kill，PASS-with-note：DLL 图解析完整，设计即报告
不判死）；版本串 `14.4.1+daily20261020.68c2c1f1`。同跑回归：exe 车道
11/11（逐节 0 真错、目标集与 #33 全同、3× LNK4098 装饰性）；stage-1
53 错全落 `cellapp_extension.vcxproj`（已知设计件）。

## 第 49 批（2026-10-20）：daily 首跑判决（双红双修）+ 验证车道盲区入账

**首跑判决（run 39350418532，head `31ce396c`，workflow_dispatch，
2026-10-20 08:54）**：双 job 双红，各一处根因——
- **Windows `Windows server products`**：红在 freshness 门 step——脚本
  原样抄 Linux 车道的 POSIX shell，windows-latest 默认 pwsh，
  **ParserError 在解析期即死**（连 workflow_dispatch 短路分支都没跑到）。
  修复：该步补 `shell: bash`。
- **Linux `build-test-package`**：红在 Build step——
  `message_handlers.cpp:163` `enumeration value 'DISPOSITION_COUNT'
  not handled in switch [-Werror=switch]`（CI gcc-14）。批次 36 引入的
  **潜伏可移植性缺陷**：`DISPOSITION_COUNT` 全仓零消费方（测试矩阵另有
  数法），是枚举里的死哨兵；生产 switch 无 default 臂，老 gcc 不查枚举
  穷尽性，故从未暴露。修复：删哨兵枚举值（switch 穷尽性保护反而更纯——
  将来新增 disposition 漏 case 仍会被 -Wswitch 抓）。

**本地复现与验证（gcc 15.2.0，比 CI 的 gcc-14 更严）**：复现
`make cellapp`（生产二进制目标）→ `message_handlers.cpp:163:16` 逐字节
同错误；修后同目标链接绿（`[BIN] [hybrid] cellapp`）；全量单测门 18：
21 模块全 `result 0`（cellapp_test 路由测试同编该头，删前已核全仓零
引用）。

**验证车道盲区入账**：本地门禁链只有 `bw-run-all-unit-tests`（21 个
测试目标），**从不编译生产二进制 TU**——`message_handlers.cpp` 不在
任何测试目标源清单里，批次 36 后所有本地 gate 全绿而完整 make 自
10-15 起就是红的；daily 车道是唯一能抓它的防线，首跑即抓到。两车道
互补：单测门管语义、daily 全量 make 管编译可移植性。

**再验（run 39369088298，head `98fd9b19`，workflow_dispatch）——双
车道全绿，L39 收口**：
- **Linux `build-test-package`**：绿，39m56s——apt 依赖 + vcpkg pinned
  baseline + 全量 make + 21 模块门 + tar.gz/deb/rpm 打包，artifact
  `bigworld-daily-14.4.1+daily20261020.98fd9b19-linux-x86_64`
  （2.68 GB）。批次 36 埋、daily 首跑抓出的 `message_handlers.cpp:163`
  缺陷在 CI gcc-14 下复测通过。
- **Windows `Windows server products`**：绿，19m0s——freshness 门
  （`shell: bash` 修复生效）、D3DX NuGet + jsoncpp vcpkg + 共享 ps1
  脚本 + exe 车道 + zip/manifest 打包，artifact
  `bigworld-daily-14.4.1+daily20261020.98fd9b19-windows-x64`
  （30.7 MB）。产物命名与探针 #33（`…daily20261018.31ce396c`）同构：
  同一生成脚本 `package-daily-windows.ps1`、同一 `bigworld-daily-<ver>-windows-x64`
  模式，版本串随 head 更新。
- **产物内容实证（artifact 解包，与探针 #33 同法）**：artifact 内恰
  两件套——`bigworld-windows-server-14.4.1+daily20261020.98fd9b19.zip`
  （31.2 MB）+ `products-manifest.txt`；内层 zip **11 个 exe**
  （baseapp / baseappmgr / bots / cellapp / cellappmgr / dbapp /
  dbappmgr / loginapp / message_logger / reviver / transfer_db）与 exe
  车道 11/11 同集，**pyd 34 个**（#33 修正后的数，非早期误记 39）；
  manifest 2397 行、头部版本串 `14.4.1+daily20261020.98fd9b19` /
  commit `98fd9b19` / MSVC 19.51 VS 18 / Hybrid / UCRT 说明齐；
  `python313.dll` 解包实算 SHA256 `b0829916…0636` 与 manifest 行同值
  （交叉验证通过）。
- 产物仅 workflow artifact（14 天保留），无 tag、无 release（红线）。
  todo.md L39（exe 打包）勾账关闭，链 `14ebd63a`→`31ce396c`→
  `68c2c1f1`→`98fd9b19`。

### 批次 50（2026-10-20，PATROL1020-1434）：F8 收窄并测试化——DataType 边界的 reload 重置语义

（批次 48/49 为 exe/daily 车道收尾，记录并入上节批次 47 续，无新增单测；
单测编号跳至 50 与测试文件头注一致。）

**新增 4 用例（`42ec3b48`，entitydef_test 380→384）**：
`lib/entitydef/unit_test/test_data_type_reload_reset.cpp`，make/CMake 双轨
注册（Makefile.rules、CMakeLists.txt 同步补 TU）。

**F8 真值收窄**：原记录「失败回滚无半迁移状态」在 DataType 层钉死——
`SingletonMap = set< SingletonPtr >`，`SingletonPtr` 只包裸指针、
`operator<` 按 `(*pInst_) < (*me.pInst_)` **值比较**、**非所有权**
（`lib/entitydef/data_type.hpp:497-507`）；`findOrAddType` 在 map 为 NULL
时懒重分配全新 map（`lib/entitydef/data_type.cpp:316`）。结论：全量 reload
失败路径清掉的是**索引**而非**实例**，DataType 层自愈、无悬垂、无半迁移
状态；真正未重建的是 entitydef 描述/类型注册表（`EntityType::s_newTypes_`
残缺、`s_curTypes_` 未换），cellapp 本地，仍卡 C 档 L48 承载。

**4 用例钉的三条不变量**：
1. 值等类型段经单例索引坍缩到同一实例、不同参数各归各（去重契约）；
2. reset 后索引懒重建、去重重新生效（两次等段坍缩到新实例）；
3. reset 释放索引不释放实例——调用方持有的 DataType 在 reset 后仍可寻址、
   `streamSize()` 不变（非所有权，失败 reload 留下的是可存活对象）。

**承载约束**：`findOrAddType`/`initAliases`/`s_aliases_`/`s_singletonMap_`
全 private（data_type.hpp 访问块），用例只走公开面——`buildDataType`
（尾部即 `findOrAddType`，`data_type.cpp:247-295`）驱动索引，reset 用
公开静态 `clearStaticsForReload()`。基类 `operator<` 只比
`pMetaDataType_` 指针、派生类比参数（sequence 比size_/dbLen、string 比
defaultValue_），去重按值敏感——用例参数取 ARRAY size 差一档，保证
「等段坍缩 / 异段分立」两侧都断言得到。

**门禁**：entitydef_test 384/0；全量 `bw-run-all-unit-tests` 21 模块全
`result 0`，**1506→1510**（合计 1510 用例逐模块累加实测），无 tag、无
release（红线）。
