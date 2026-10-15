# 迁移与覆盖率现状

<div class="arch-hero">

本页是 Python 3.13 迁移与覆盖率补强工程的**单一现状仪表**：当前推进到哪、各模块覆盖到多少、哪些面已登记为不可达。逐批的完整过程、代码证据与陷阱记录在 [Python 3.13 迁移全量台账](/python-313-migration)，本页只回答“现在是什么状态”。

</div>

## 当前状态

<div class="decision-table">

| 事项 | 状态 | 说明 |
|------|------|------|
| 嵌入式解释器 | ✅ Python 2.7 → **3.13.16** | 3.13.15 迁移完成后，2026-10-12 同系列安全补丁升至 3.13.16（全链构建 + 冒烟 + 全量单测复验） |
| 全量单测门禁 | ✅ 21 模块 / 1505 用例全绿 | 2026-10-15 实测（批次36 `CellApp::reloadScript` 序列抽成头内联 `ReloadScript::planReload` + `EntityMessageHandler::handleMessage` 处置梯抽成 `classifyEntityMessage`，cellapp_test 12→27；fantasydemo 占位资产树 + 生成脚本 + 加载冒烟 entitydef_test 375→380；批次35 cellapp 实体消息 EntityID/SpaceID 关联接线 cellapp_test 8→12；批次34 `entitydef/method_description.cpp` 纯助手族直驱 entitydef_test 370→375；批次33 热重载回滚链 supersede 语义直驱 entitydef_test 366→370；批次32 entity_description 流/访问器/暴露消息号直驱 entitydef_test 359→366；批次31 entity_description MD5 族直驱 entitydef_test 348→359；批次30 `network/machine_guard.cpp` 直驱 network_test 229→256；批次29 `entitydef/entity_description.cpp` 直驱 entitydef_test 318→348；日志关联字段跨进程承载腿后 1397） |
| 覆盖率补强工程 | ✅ 批次 1–32 已完成（批次 33–35 为语义证据 lane） | 批次35 观测面 lane：cellapp 实体消息 EntityID/SpaceID 关联接线落地（`EntityMessageLogScope` 头内联 RAII 载体 + `EntityMessageHandler::handleMessage` 4 参重载外沿接线，补 channelID 之上的实体与空间两维；spaceID 取 `isDestroyed()` 守卫避免 destroy 后裸解引用 pSpace_；调用点不编入 cellapp_test 故不可测，wrapper 语义由新建 `test_entity_message_log_scope.cpp` 4 用例直驱，cellapp_test 8→12）；批次34 `entitydef/method_description.cpp` 纯助手族 70% → **72%**（精准插桩 329/466→337/466，既有 TU 追加 5 用例直驱 extractSourceEntityID/convertKeywordArgs/areValidReturnValues/createDictFromTuple；实测坑位：ScriptObject::operator== 是指针等价，值断言走 c_str/asLong）；批次33 热重载回滚链 `EntityMethodDescriptions::supersede`/`EntityDescription::supersede` 语义直驱（entitydef_test 366→370，改名 `old_` 前缀后 find 旧名断、下标/exposed 派发活、重复 supersede 前缀叠加、supersede 后 MD5 漂移）+ F7 失败模式入账（回滚后旧描述仍 superseded，按名查找断而 msgID 派发活，全仓无 un-supersede）；批次32 `entitydef/entity_description.cpp` 70% → **89%**（精准插桩 468/663→591/663，既有 TU 追加 7 用例直驱流写侧访客族/读侧/访问器/setExposedMsgIDs/checkMethods；实测坑位：clientServerProperties_ 按流尺寸稳定排序、NUM_PASSES=4）；批次31 `entitydef/entity_description.cpp` 64% → **70%**（精准插桩 424/663→468/663，既有 TU 追加 11 用例直驱 MD5 族——addToMD5/addPersistentPropertiesToMD5 双摘要面，L36 热重载设计的 schema 版本原语；实测坑位：BWResource::openSection 先查 DataSectionCensus，重写 .def 后重解析须先 purgeAll 驱逐）；批次30 `network/machine_guard.cpp` 28% → **87%**（精准插桩 668/764，新建 `test_machine_guard_messages.cpp` 27 用例；TESTING.md 表值 553/774 28% 为 09-28 全量口径）——MGM 16+1 消息类全量 write/create 往返 + c_str 直驱，ProcessMessage 额外块前向兼容跳过与欠声明报错、matches 六参逐位、ReplyHandler 16 型分发 + 未知型默认臂 + 基类 16 默认落 onUnhandledMsg；残余 96 行 UDP 套接字面（sendto/三 sendAndRecv/sendAndRecvFromEndpointAddr）+ typeStr UNKNOWN 默认臂 + seq ticker 回绕臂；批次29 `entitydef/entity_description.cpp` 0% → **64%**（精准插桩 424/663，新建 `test_entity_description.cpp` 30 用例；TESTING.md 原受限判定「需 EntityType/EntityDef XML 装载机全套」推翻——parse 仅依赖 BWResource 单例与 fixture .def 树）；批次28 双收口 `resmgr/bwresource.cpp`（精准口径 322/746 43% → 464/746 **62%**，新建 17 用例）与 `entitydef/method_description.cpp`（精准口径 153/466 32% → 329/466 **70%**，新建 33 用例）；平台行为与源码级隐患登记不修；缺口 top 清单落 [TESTING.md](../../TESTING.md) |
| 下批首位候选 | ⏳ `entitydef/method_description.cpp` 残余 129 行 | 纯助手族已直驱（批次34，70%→72%）；剩余 PyDeferredResponse/handleException/网络发送路径维持批次28 受限分类（需活通道/两路调用上下文）；`entitydef/entity_description.cpp` 残余 72 行为错误/警告臂与 watcher 面；`moo/image.ipp` 589 行维持受限（el7 混合服务端构建 lib/moo 只编 node.o/png.o，image.ipp 无实例） |

</div>

## 模块覆盖率基线（2026-10-01 插桩实跑，批次 22 两阶段复测；批次23 起文件级数字走精准插桩口径，模块级全量值待下轮复测）

gcovr 全量聚合值。客户端大件（moo / physics2 / server / chunk）接近零并非“没人测”，而是结构性的：21 个单测宿主全部来自服务端构建，这些模块的执行路径基本不在单测进程里。

<div class="decision-table">

| 模块 | 覆盖率 | 解读 |
|------|--------|------|
| script | 92% | 脚本抽象层，覆盖率最高的模块（批次1 起主攻） |
| math | 68% | 几何与数学工具 |
| pyscript | 68% | 嵌入解释器封装，批次4–9 六连批收口 |
| network | 66% | Mercury 网络层，批次10–18 主攻区 |
| resmgr | 58% | 资源管理器 |
| cstdmf | **54%** | 基础设施（调试、内存、流、watcher 族；批次22 逐类判定 51% → 54%） |
| entitydef | 43% | 类型契约；装载机依赖重 |
| terrain | 34% | 地形（含 gcovr 记录异常项，见台账批次18） |
| connection | 24% | 服务端连接面；`server_connection.cpp`（990 行 0%）维持不可测登记拖底 |
| moo | 13% | 客户端渲染支撑，服务端单测不触 |
| physics2 | **76%** | 客户端物理；批次20 收口 `quad_tree.ipp` 漏列激活（61% → 76%） |
| server | 13% | 服务端骨架，需完整 app 装配 |
| chunk | 3% | 客户端 chunk 体系，需 ChunkSpace 装载机 |

</div>

文件级亮点：`entitydef/entity_description.cpp` 70% → **89%**（精准插桩口径 468/663 → 591/663 实例行，批次32；既有 TU 追加 7 用例直驱四块：addDictionaryToStream 的 AddToStreamMapVisitor（缺键落默认值不报错、错误类型值换默认后整体失败、null mapping 前置拒绝）、addAttributesToStream 的 AddToStreamAttributeVisitor（缺属性即失败、NULL 对象前置拒绝）与 addToStream 助手（NUM_PASSES=4 逐 pass dataSizes 与流总长精确对账：owner(BASE)→pass0、nick(ALL_CLIENTS)→pass2、hp(CELL_PUBLIC)→pass3、pass1 base+client 无属性贡献 0）、readStreamToSection（流→新建 XML section 往返）、readTaggedClientStreamToDict（count u8 + 逐项 [clientServerFullIndex u8 + 属性流数据]）、pIdentifier/isTempProperty/clientServerProperty(Count)（Identifier 属性须同时 Persistent:true）、setExposedMsgIDs（唯一暴露方法取 range 首槽 msgID=10、未暴露方法 -1 哨兵读作 255）、checkMethods（warnOnMissing 双臂）；**实测坑位**：clientServerProperties_ 经 allocateClientServerFullIndexes 按流尺寸稳定排序（定长升序在前、变长升序在后，clientServerProperty(n) 的 n 非 def 顺序，tagged 流 index 亦然）、MemoryOStream 用 addBlob 无 append、XMLSection 的 asString/asInt protected 须经 DataSectionPtr 的 readString/readInt、ScriptString 取值 c_str；残余 72 行为 parse 错误/警告臂与 watcher 面，见台账 §7.32）；`entitydef/entity_description.cpp` 64% → **70%**（精准插桩口径 424/663 → 468/663 实例行，批次31；既有 TU `test_entity_description.cpp` 追加 11 用例直驱 **MD5 族**——addToMD5 与 addPersistentPropertiesToMD5 双摘要面，正是 L36 热重载设计指定的 schema 版本原语：确定性（同字节重解析双摘要相等）、CELL_PUBLIC 属性对双摘要零贡献（连类型替换 UINT16→FLOAT32 都不动）、client-server 属性只进全量摘要（clientServerFullIndex + DataDescription::addToMD5）、BASE+Persistent 属性只进持久摘要、非持久属性对持久摘要零贡献（与空 def 同摘要）、未暴露 base 方法零贡献（与无 base 方法同摘要——exposed 计数器只对入选方法递增，非暴露方法不挤位）、`<Exposed/>` 暴露 base/cell 方法即入摘要（flags_ 变化 + 摘要收录）、client 方法无暴露门控（ClientMethods 带 `<Exposed/>` 恒拒绝，方法一旦存在即进摘要）、暴露方法顺序敏感（legacyExposedIndex 随位置计数，交换两暴露方法即变摘要）、client-server 属性顺序敏感（clientServerFullIndex 按 def 顺序编号，交换两 ALL_CLIENTS 属性即变摘要而持久摘要不动）；**实测坑位**：`BWResource::openSection` 先查 `DataSectionCensus` 再碰文件系统（bwresource.cpp:614-625），同一路径重写 .def 后重解析**必须**先 `BWResource::instance().purgeAll()` 驱逐否则读旧段；`CHECK` 宏展开为 `result_`/`m_name` 成员、自由助手函数只能用 `MF_VERIFY`；`MD5::Digest` 默认构造不初始化字节、须走 `Digest(MD5&)` 或 `operator=`；残余 195 行登记：parseProperties 覆盖/override 臂、LoDLevels/Volatile/Implements/Components 解析面、addSectionToStream 流写侧、supersede 迁移面、watcher 面等，见台账 §7.31）；`network/machine_guard.cpp` 28% → **87%**（精准插桩口径 668/764 实例行，批次30；新建 `test_machine_guard_messages.cpp` 27 用例直驱 MGM 消息编解码面：16+1 消息类全量 write/create 往返 + c_str、ProcessMessage 额外块前向兼容跳过/欠声明报错、matches 六参逐位、UserMessage 双 param 门控与 init/getConfFilename、ReplyHandler 16 型分发矩阵 + 未知型默认臂 + 基类 16 默认 on*Message 全落 onUnhandledMsg；实测坑位——CppUnitLite2 CHECK_EQUAL(char*,char*) 比指针须 strcmp 化、精准插桩须重打包 libnetwork.a + 链接显式 --coverage、gcda 落编译期 -o 路径；残余 96 行登记：sendto/三 sendAndRecv/sendAndRecvFromEndpointAddr UDP 套接字面 83 行、typeStr UNKNOWN 默认臂 12 行、seq ticker 回绕臂 1 行，见台账 §7.30）；`entitydef/entity_description.cpp` 0% → **64%**（精准插桩口径 424/663 实例行，批次29；TESTING.md 原受限判定「需 EntityType/EntityDef XML 装载机全套」推翻——`EntityDescription::parse` 全程仅依赖 BWResource 单例与 scripts/entity_defs 下的 fixture .def 树，从不触 EntityType；新建 `test_entity_description.cpp` 30 用例直驱：parse 最小形态/标量属性 Flags 各位与 Persistent/缺 .def 失败、BaseMethods/CellMethods/ClientMethods 三列表与 `<Exposed/>`（ClientMethods 带 Exposed 恒拒绝）、Parent 递归继承与缺父失败、Volatile 优先级平方与 yaw≥pitch≥roll 校验、LoDLevels 标签匹配 detailLevel、Distribution 三 decider 位与 ClientName 弃用告警、无 Client 脚本时 ClientMethods 拒绝、TempProperties、Components 装载/缺 def 失败/重名失败（实体自有属性先于组件属性）、parseService 的 Methods→base 列表映射与强制非持久化、Implements 接口属性、checkExposedMethods 的 proxy/非 proxy 四象限、getDataDomains 全映射（含 ONLY_OTHER_CLIENT_DATA/ONLY_PERSISTENT_DATA 附标）、shouldSkipPass 的 EXACT_MATCH 精确匹配矩阵与非精确按位臂、shouldConsiderData 各 pass 过滤与 ONLY_PERSISTENT_DATA、addSectionToStream→readStreamToDict 流往返与 addSectionToDictionary 直写 dict；**实测坑位**：`EntityDescription::parse` 第二参 componentName 生产恒传空串（entity_description_map.cpp:362），传实体名会把全部属性标记为 componentised、fullName 带前缀，addSectionToStream 按 fullName 找值落默认值 0；残余 239 行登记：readStreamToSection/readTaggedClientStreamToDict/addAttributesToStream/addDictionaryToStream 网络流读侧、MD5 族（addToMD5/addPersistentPropertiesToMD5）、setExposedMsgIDs、checkMethods、pIdentifier/isTempProperty/clientServerProperty 族访问器、getDataDomains 两个 CRITICAL_MSG 不可达臂（CLIENT_ENTITY_CELL_CREATION/CLIENT_ENTITY_LOD_DATA）与析构/模板实例暗区，见台账 §7.29）；`resmgr/bwresource.cpp` 43% → **62%**（精准插桩口径 322/746 → 464/746 实例行，批次28；新建 `test_bwresource_paths.cpp` 17 用例直驱路径格式化族、路径列表访问、addPath/delPath、purge/purgeAll 与 DataSectionCensus 联动、modification monitor 族、文件类型查询、modifiedTime/isFileOlder、resolveToAbsolutePath；平台行为四件钉死——addPath(-1) 默认追加、addSubPaths 预分配空串污染、ensurePathExists 的 '/'→'\\' Windows-only、isFileOlder 1e7 单位错配致非零阈值在 Unix 恒 inert；残余 282 行分类登记，见台账 §7.28）；`entitydef/method_description.cpp` 32% → **70%**（精准插桩口径 153/466 → 329/466 实例行，批次28；新建 `test_method_description_gaps.cpp` 33 用例直驱 parse 全选项、exposed 组件位、ExposedMethodMessageRange 算术、流尺寸、server/client 流、md5、clientSafety、toString 唯一可达警告点、areValid 族、getArgsAsTuple exposed 路径、callMethod 的 exec-then-eval；登记五件不修只记——returnValuesStreamSize 死声明、无 BWTwoWay 模块、Mercury::MessageID uint8 回绕、DEFAULT_VARIABLE_LENGTH_HEADER_SIZE=1、MAILBOX → CLIENT_UNUSABLE；残余 137 行分类登记，见台账 §7.28）；`pyscript/pywatcher.cpp` 43.5% → **93.4%**（精准插桩口径 312/717 → 670/717 实例行，批次27；新建 `test_pywatcher_gaps.cpp` 22 用例直驱 PyObjectWatcher 特化分发、SimplePythonWatcher 全操作面（生产树零实例化的公共类）、PyAccessorWatcher 三错误族与 set 失败臂、PyFunctionWatcher 实参/返回值/坏载荷/deferred 全分支与模块级错误臂；残余 47 行全数分类——putPyObjectOnStream 失败臂经唯一调用方 finishSetFromStream 忽略返回值而结构性死亡、STRING 提取器自描述不可失败、GetItem/PyTuple_New 边界臂、Py3 迁移死臂（:1766/:1870）、PySequence_Check 内建全过 PyMapping_Check 所致的序列分支不可达、需活 stdout 钩子臂与 fini 后窗口臂；三处源码级隐患登记不修：visitChildren 映射臂对 list/tuple/str/range 必 Py_DECREF(NULL)（:1934-1938，gdb SIGSEGV 实证）、getPyObjectWatcherData 孤代理 str MF_ASSERT 中止（:757-763）、py_addFunctionWatcher comment 形参不可省（watcher.cpp :546 std::string(NULL) 抛 logic_error）；**方法论教训**：SimplePythonWatcher 缺子路径与孤代理 traceback 会遗留 pending Python 异常使后续 PyImport_ImportModule 静默失败——假绿形态之一，对策 PyErr_Clear + fixture CHECK 化 + 失败即打印，见台账 §7.27）；`resmgr/datasection.cpp` 19.3% → **99.7%**（精准插桩口径 169/876 → 873/876 实例行，批次26；新建 `test_datasection.cpp` 21 用例直驱迭代器协议、标量/宽字符/blob/binary/向量/矩阵单复数读写、基类 as*/set* 默认与拒绝面、createAppropriateSection 五臂、经 BinSection::save 的 splitSaveAsFileName 三臂、setWatcherValues、copy/copySections/compare/binaryCompare 全差异臂与 sanitise 族；复数读写的 20 条缺失父守卫用「DirSection 挂不存在基路径」一次驱动且零文件副作用；唯一残余 :90-93 为迭代器 operator* 空子回退臂——回退体经基类 childSectionName 的 `openChild(index)->sectionName()` 在同场景 NULL 解引用，gdb SIGSEGV 实证，登记为到达即崩的结构性死臂兼源码级隐患，见台账 §7.26）；`pyscript/script_math.cpp` 92.3% → **97.0%**（精准插桩口径 1000/1083 → 1050/1083 实例行，批次25；新建 `test_script_math_gaps.cpp` 10 用例直驱通用 PyVector number 协议、原地运算符与富比较、参数校验回退、z/yaw/pitch 存取、单操作数 product/combiner 分支与 Vector4Shader 两拒绝臂；残余 33 行全数分类——13 行 Windows-only 构造性死区〔ProviderStore 矩阵半边，唯一 autoTick 的 MatrixProvider 子类 MatrixAnimation 整体 `#if defined( _WIN32 )`〕+ 3 行 safeSetVector 死臂 + 8 行 pyGet_x/y/z/w 死成员 + 9 行宏展开点幻影行，见台账 §7.25；表值 655/1920 属全量插桩口径撤出 TODO 表）；`cstdmf/smartpointer.hpp` 84/91 → **117/118 测试 TU 视角**（批次24；此前无专属测试，新建 `test_smartpointer.cpp` 29 用例直驱两类引用计数基类、自由函数族与 ConstSmartPointer/SmartPointer 全操作面；唯一残余 `:121` 为基析构空体结构性死臂）；`cstdmf/profiler.cpp` 6% → **94%**（1155/1222 行，批次23；红门禁提交 `dc5cd043` 的 13 失败用例逐根因修复 + 385→398 用例；残余 67 行不可测臂逐条登记，见台账 §7.23）；`cstdmf/watcher.hpp` 14% → **22%**（943 → 1545 行覆盖，+602 实例行；批次22 逐类可测性判定 + 39 用例，两阶段全量插桩同口径，BEFORE 干净复测与批次 20 快照一致；总行 6414→6943 为新测试 TU 实例宇宙扩容；残余为生产 TU 从不执行的实例 + 死臂登记）；`cstdmf/fixed_sized_allocator.cpp` 0% → **99%**（286/289，批次21：既有测试组被 `ENABLE_FIXED_SIZED_POOL_ALLOCATOR` 平台宏守卫恒 0，新建 12 用例直驱；残余 3 行为 findPool 排序 shift 死臂）；`physics2/quad_tree.ipp` 0% → 98%（测试 TU 视角 860/876，批次20 Makefile.rules 漏列修复激活）；`physics2/bsp.cpp` 0% → 91%（批次19 死门激活）；`network/tcp_bundle.cpp` 达 100%（批次16 收口）；`network/machine_guard.cpp` 28%（bwmachined 面不可达，批次17 登记）；批次18 派测的 `event_poller.cpp` 45% → 57%（其余缺口为 Linux 死代码的 SelectPoller/PollPoller，175 行结构性登记）。

## 批次时间线

<div class="decision-table">

| 批次 | 日期 | 主对象 |
|------|------|--------|
| 1 | 2026-09-26 | lib/pyscript |
| 2–3 | 2026-09-26 | lib/connection |
| 4–9 | 2026-09-26 | lib/pyscript（残留面 / script_math / 基础设施 / 回溯打印器 / 零覆盖文件 / 收尾） |
| 10–11 | 2026-09-26 | lib/connection（零覆盖面 / filter/login 面） |
| 12–13 | 2026-09-27 | connection 挑战/replay 面 + cstdmf/math/resmgr/db 零覆盖；replay 线索证伪 |
| 14 | 2026-09-27 | encryption_filter 流式加密面 + 零覆盖清单可测性判定 |
| 15 | 2026-09-27 | file_stream 零覆盖 |
| 16 | 2026-09-27 | tcp_bundle 线格式整层 |
| 17 | 2026-09-27 | machine_guard MGM 编解码面 |
| 18 | 2026-09-28 | 全量覆盖率实跑基线 + event_poller 轮询器面 |
| 19 | 2026-09-28 | bsp.cpp 死门激活（MF_SERVER 守卫移除 + 14 用例） |
| 20 | 2026-09-30 | quad_tree.ipp 激活（Makefile.rules 漏列修复 + 22 用例 + 四死函数登记） |
| 21 | 2026-09-30 | fixed_sized_allocator 直驱（平台宏死测试实证 + 12 用例，0%→99%） |
| 22 | 2026-10-01 | watcher.hpp 逐类可测性判定（39 用例：流协议/容器族/解引用/工厂/root 全局面，14%→22%） |
| 23 | 2026-10-12 | profiler.cpp 收口（红门禁 13 用例修复 + CHECK(true) 换计数钩子实断言 + 孤儿测试复活：scene +2 / network +3，6%→94%）+ Python 3.13.15→3.13.16 |
| 24 | 2026-10-13 | smartpointer.hpp 直驱收口（此前无专属测试，29 用例，84/91→117/118 测试 TU 视角） |
| 25 | 2026-10-13 | script_math.cpp 收口（精准口径复判 + 10 用例，92.3%→97.0%；ProviderStore 矩阵半边 Windows-only 死区取证） |
| 26 | 2026-10-13 | datasection.cpp 直驱收口（此前无专属测试，21 用例，19.3%→99.7%；operator* 回退臂到达即崩死臂登记） |
| 27 | 2026-10-13 | pywatcher.cpp 收口（精准口径复判 312/717 → 670/717 93.4% + 22 用例；残余 47 行全数分类；三处源码级隐患登记；pending 异常毒化假绿形态取证与对策） |
| 28 | 2026-10-13 | bwresource.cpp + method_description.cpp 双收口（精准口径 322/746 → 464/746 62% + 153/466 → 329/466 70%；新建 50 用例；平台行为四件钉死、登记五件不修） |
| 29 | 2026-10-15 | entity_description.cpp 直驱（受限判定推翻——parse 仅需 BWResource + fixture .def 树；新建 test_entity_description.cpp 30 用例，0%→64% 精准口径 424/663；残余 239 行网络流读侧/MD5/访问器登记） |
| 30 | 2026-10-15 | machine_guard.cpp 直驱（MGM 消息编解码面不依赖 bwmachined——16+1 消息类自持 wire 格式；新建 test_machine_guard_messages.cpp 27 用例，28%→87% 精准口径 668/764；残余 96 行 UDP 套接字面 + typeStr UNKNOWN 臂 + seq ticker 回绕臂） |
| 31 | 2026-10-15 | entity_description.cpp MD5 族直驱（既有 TU 追加 11 用例，348→359，64%→70% 精准口径 424/663→468/663；双摘要三分区、暴露门控与计数、client-serverFullIndex/legacyExposedIndex 顺序敏感性全断言；重写 .def 后重解析须先 purgeAll——DataSectionCensus 先于文件系统） |
| 32 | 2026-10-15 | entity_description.cpp 流/访问器/暴露消息号直驱（既有 TU 追加 7 用例，359→366，70%→89% 精准口径 468/663→591/663；NUM_PASSES=4 逐 pass dataSizes 对账、clientServerProperties_ 按流尺寸稳定排序、tagged client 流格式、Identifier 须 Persistent、setExposedMsgIDs range 首槽、checkMethods 双臂） |

</div>

用例总量演进：批次14 后 1061 → 批次15 后 1071 → 批次16 后 1083 → 批次17 后 1099 → 批次18 后 1113 → 批次19 后 1127 → 批次20 后 1151 → 批次21 后 1163 → 批次22 后 1202 → 批次23 后 **1220**（全量 21 模块门禁全绿；批次23 净增 +18 = cstdmf profiler +13 + scene 孤儿复活 +2 + network 孤儿复活 +3；假绿修复均为原位改写断言，不增减用例数） → 批次24 后 **1249**（cstdmf smartpointer 直驱 +29） → 批次25 后 **1259**（pyscript script_math 缺口直驱 +10） → 批次26 后 **1280**（resmgr datasection 直驱 +21） → 批次27 后 **1302**（pyscript pywatcher 缺口直驱 +22） → 批次28 后 **1352**（resmgr bwresource 路径面 +17 + entitydef method_description 缺口直驱 +33；全量 21 模块门禁全绿） → 2026-10-14 故障注入批次后 **1355**（network/unit_test/test_request_failure.cpp +3：hang peer 请求超时 REASON_TIMER_EXPIRED、通道丢失 REASON_CHANNEL_LOST、正控回声；请求失败路径首次断言级覆盖，此前 handleException 仅记日志）→ 2026-10-15 日志关联字段跨进程承载腿后 **1397** → 批次29 后 **1427**（`entitydef/entity_description.cpp` 直驱 +30，entitydef_test 318→348；全量 21 模块门禁全绿） → 批次30 后 **1454**（`network/machine_guard.cpp` 直驱 +27，network_test 229→256；全量 21 模块门禁全绿） → 批次31 后 **1465**（`entitydef/entity_description.cpp` MD5 族直驱 +11，entitydef_test 348→359；全量 21 模块门禁全绿） → 批次32 后 **1472**（`entitydef/entity_description.cpp` 流/访问器/暴露消息号直驱 +7，entitydef_test 359→366；全量 21 模块门禁全绿） → 批次33 后 **1476**（热重载回滚链 supersede 语义直驱 +4，entitydef_test 366→370；全量 21 模块门禁全绿） → 批次34 后 **1481**（method_description 纯助手族直驱 +5，entitydef_test 370→375；全量 21 模块门禁全绿） → 批次35 后 **1485**（cellapp 实体消息 EntityID/SpaceID 关联接线 +4，cellapp_test 8→12；全量 21 模块门禁全绿） → 批次36 后 **1505**（fantasydemo 占位资产树 + 生成脚本 + 5 冒烟用例，entitydef_test 375→380；`ReloadScript::planReload` 热重载动作序 +7、`classifyEntityMessage` 实体消息路由决策表 +8，cellapp_test 12→27；全量 21 模块门禁全绿）。

## 不可达 / 受限登记摘要

逐臂判定的完整记录见台账各批次；这里是 0% 大文件的结论摘要：

- `connection/server_connection.cpp`（990 行）— **维持不可测**：真登录管线 + TCPChannel 状态机（批次14/16 口径）。
- `connection/message_handlers.hpp`（538 行）— **结构性死代码**：服务端 app 实例化的模板 handler 表，单测无人实例化。
- `moo/image.ipp`（589 行）— 受限：客户端图像合成面。
- `chunk/chunk_space.cpp`（506 行）— 受限：需 ChunkSpace + chunk 装载机。
- `connection/replay_controller.cpp`（495 行）— 受限：录像/回放文件管线（线格式已批次12 测）。
- `network/logger_endpoint.cpp` / `logger_message_forwarder.cpp` / `watcher_nub.cpp` — 维持受限登记（真消息日志管线、UDP 绑 machined 标准端口，批次17）。
- `terrain/terrain2/terrain_height_map2.cpp`（755 行）— gcovr 对该记录丢失文件名列（空名条目），未逐臂判定。
- `physics2/quad_tree.ipp` 死函数族 — **结构性死代码**（批次20 三重取证）：`QuadTree::del`（calculateQTRange 少传 origin_）、`QuadTree::testPoint`（调不存在的 `QuadTreeNode::testPoint`）、`print`/`printQTNode`（`node.elements()` 不存在）、`countAt`（int 实参传 Quad 形参）——从未实例化，一实例化即编译错；生产侧 chunk 的 `pChunkTree_` 是 HullTree，对 ObstacleTree 只用 addToRoot。

已收口（移出登记）：`entitydef/entity_description.cpp` MD5 族与流/访问器/暴露消息号面（64% → **70%** → **89%** 精准口径 424/663→468/663→591/663，批次31+32 直驱收口——addToMD5/addPersistentPropertiesToMD5 双摘要面全断言：双摘要三分区、暴露门控与计数、clientServerFullIndex/legacyExposedIndex 顺序敏感性；残余 195 行为 parseProperties 覆盖/override 臂、LoDLevels/Volatile/Implements/Components 解析面、流写侧与 supersede 迁移面等，见台账 §7.31；`moo/image.ipp` 589 行维持受限——el7 混合服务端构建 lib/moo 只编 node.o/png.o、image.ipp 无实例）；`network/machine_guard.cpp`（28% → **87%** 精准口径 668/764，批次30 直驱收口；新建 `test_machine_guard_messages.cpp` 27 用例，MGM 16+1 消息类 wire 往返全断言；残余 96 行——UDP 套接字面 83 行（sendto/三 sendAndRecv/sendAndRecvFromEndpointAddr，需活 socket + 绑 PORT_MACHINED=20018 回环，引入环境依赖故登记不测）+ typeStr UNKNOWN 默认臂 12 行 + seq ticker 回绕臂 1 行，见台账 §7.30）；`pyscript/pywatcher.cpp`（43.5% → **93.4%** 精准口径，312/717 → 670/717 实例行，批次27 直驱收口；新建 `test_pywatcher_gaps.cpp` 22 用例，残余 47 行全数分类登记（putPyObjectOnStream 失败臂结构性死代码、Py3 迁移死臂、序列分支不可达、需活钩子/fini 后窗口臂），三处源码级隐患登记不修，pending 异常毒化假绿形态取证，见台账 §7.27）；`resmgr/datasection.cpp`（19.3% → **99.7%** 精准口径，169/876 → 873/876 实例行，批次26 直驱收口；新建 `test_datasection.cpp` 21 用例，残余 3 行为迭代器 operator* 空子回退臂——到达即崩的结构性死臂兼源码级隐患登记，见台账 §7.26）；`pyscript/script_math.cpp`（92.3% → **97.0%** 精准口径，1000/1083 → 1050/1083 实例行，批次25 复判后直驱；新建 `test_script_math_gaps.cpp` 10 用例，残余 33 行全数分类：13 行 Windows-only 构造性死区〔ProviderStore 矩阵半边——唯一 autoTick 的 MatrixProvider 子类 MatrixAnimation 整体 `#if defined( _WIN32 )`，档案内符号数 0〕、3 行 safeSetVector 死臂、8 行 pyGet_x/y/z/w 死成员、9 行宏展开点幻影行，见台账 §7.25）；`cstdmf/smartpointer.hpp`（84/91 → **117/118** 测试 TU 视角，批次24 直驱收口；唯一残余 `:121` 基析构空体结构性死臂，见台账 §7.24）；`cstdmf/watcher.hpp`（14% → **22%**，批次22 逐类可测性判定；39 用例收编含并行会话追加 3 用例审读——SafeWatcher 用例的非 NULL base 在直驱 DataWatcher 引用成员语义下 `&value + &value` 非规范地址必 SIGSEGV，修正后收编；rootWatcher/fini 死臂、`_XBOX360` 分支与 uint64 提取器缺 else 缺陷登记，见台账 §7.22）；`cstdmf/fixed_sized_allocator.cpp`（289 行，0% → **99%**，批次21 平台宏死测试直驱；根因是既有 test_allocate.cpp 两组用例整体被 `ENABLE_FIXED_SIZED_POOL_ALLOCATOR` 守卫、el7 服务端构建恒 0——“有测试却 0%”第三例；残余 3 行为 findPool 排序 shift 结构性死臂，登记 `getNumPoolItemsForSize` 未命中读 `allocSizes_[-1]` 越界隐患，见台账 §7.21）；`physics2/quad_tree.ipp`（0% → **98%** 测试 TU 视角，批次20 漏列激活；残余 16 行为断言/日志失败臂 + 上述死函数族）；`physics2/bsp.cpp`（778 行，0% → **91%**，批次19 死门激活；残余 67 行为构造校验/加载错误臂，逐段登记见台账 §7.19）。缺口 top 全清单见 [TESTING.md](../../TESTING.md) TODO 段。

## 口径与边界

- 覆盖率为 **gcovr 8.6 插桩全量实跑**值（`user_shouldBuildCodeCoverage=1`，21 模块测试全跑完、498 个 .gcda 聚合），不是估算。嵌 Python 三模块的可用前提：树重建后显式 `make python_install`（`bw-run-all-unit-tests` 链路不触发安装，台账 §7.19）。批次21 的 `fixed_sized_allocator.cpp` 文件级数字来自**精准插桩口径**（只重编 cstdmf TU 拿 -fprofile-arcs，普通 .cpp 单 TU 分母与全量一致、精确可比；模板头分母随插桩 TU 集缩放故模块表维持批次18/20 全量插桩值，台账 §7.21）。
- 台账中部分批次的门禁措辞为等效口径：本仓无 ctest、无独立覆盖率门禁脚本，等效标准 = `bw-run-all-unit-tests` 21 模块全绿 + gcovr 聚合成功。
- 覆盖率数字会随后续批次继续变动，本页与台账同步更新；冲突时以台账为准。

延伸阅读：[Python 升级路线图](/upgrade-plan/python-upgrade-plan)（原规划决策记录）· [Python C API 迁移](/upgrade-plan/python-c-api-guide) · [第三方依赖升级](/upgrade-plan/third-party-deps) · [全量台账](/python-313-migration)
