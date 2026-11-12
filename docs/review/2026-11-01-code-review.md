# 代码深审报告（2026-11-01）

> 范围：`programming/bigworld/`（lib/、server/、client/、tools/、examples/）＋ `scripts/` ＋ `.github/workflows/`，首方 C++/Python 共 5221 个源文件。vendor 树（`third_party/`、`build/`、`node_modules/`）不计入结论。
> 方法：六路并行为主——①安全静态扫描；②核心库隐藏 bug 猎捕；③server 热重载/脚本绑定猎捕；④引用计数与所有权审计；⑤baseapp/dbapp/loginapp 迁移面复核；⑥lib/server、tools、examples 迁移面复核。每条结论均在 `file:line` 取证，CONFIRMED 与 SUSPECTED 分列。
> 严重度口径：P0 远程未授权 RCE／内存破坏；P1 远程可触发崩溃或越界写、或 Py3 迁移导致的确定性功能回归；P2 需内部/管理端口/构造输入才触发，或资源泄漏；P3 纵深防御缺口或平台受限。
> 修正方向：**源码为准**，先改码再改文档。

---

## 一、总账

| 桶 | 发现数 | 已修 | 剩余 | 去向 |
|---|---|---|---|---|
| P0 | 0 | 0 | 0 | — |
| P1 | 7 | 7 | 0 | 本报告 §2 |
| P2 | 26 | 0 | 26 | `todo.md` §「代码深审遗留」 |
| P3 | 4 | 0 | 4 | `todo.md` §「代码深审遗留」 |
| 误报已排除 | 1 | — | — | `dump_controllers_extra.cpp:99`（见 §3） |
| **合计确认** | **37** | **7** | **30** | — |

另有 6 条 SUSPECTED（竞态/脚本依赖/平台受限），计入上表 P2/P3，标注「疑」。

**门禁实测（2026-11-02）**：`make bw-run-all-unit-tests` 21 模块全绿（cstdmf 452 / entitydef 387 / network 256 / pyscript 151 / cellapp 41 / math 44 / server 5 / script 18 …），共 1521+ 用例，0 失败。

---

## 二、P1（7 条，全部已修）

### P1-1 · `PYTHON` 成员属性反序列化 = 远程 RCE — `lib/entitydef/data_types/python_data_type.cpp:77`
`pickle.loads()` 无白名单地解码网络字节，`__reduce__`/`GLOBAL` 可执行任意 Python。可达链逐跳确认：客户端→服务端方法调用 `server/baseapp/base.cpp:1316`、`server/cellapp/entity.cpp:5522` → `lib/entitydef/method_description.cpp:1149/1166` → `python_data_type.cpp:77`；属性更新 `server/cellapp/entity.cpp:5839`、幽灵复制 `entity.cpp:1800` → 同一 `unpickle`。同类还有 `lib/entitydef/data_types/user_data_type.cpp:152`（把解码委托给游戏脚本）与 `lib/entitydef/method_description.cpp:1197`（服务端间错误参数）。
**已修** `9d6d4b38`：受限 unpickler 覆写 `find_class()` 仅放行纯数据全局名。

### P1-2 · 变长序列「先分配后校验」→ 内存耗尽 — `lib/entitydef/data_types/sequence_data_type.cpp:110-149`
变长 `ARRAY`/`TUPLE` 的计数从流里读入 `varSize_`（`readPackedInt()` 上限 `0xFFFFFF`），随即 `startSequence(sink, varSize_)` 分配 ~128–256 MB 容器，**之后**才检查 `remainingLength()`。几条并发小报文即可耗尽 cellapp 内存。可达：`entity.cpp:5839`（`setOwnedProperty`）、`method_args.cpp:334`。另 `varSize_ * elementSize` 乘积可溢 `int`。
**已修** `431f9daf`：校验前置到 `startSequence` 之前；定长分支改用 `remaining / elementSize < varSize_` 的除法比较，消除溢出。`entitydef_test` 387 用例绿。

### P1-3 · 内部方法描述符解引用前未判空 — `server/cellapp/entity.cpp:5503`
`runMethodHelper` 的 `else`（内部方法）分支先调 `internalMethod(methodID)`（越界时**带日志返回 NULL**，`lib/entitydef/entity_method_descriptions.cpp:271-282`），紧接着 `pMethodDescription->isExposed()`；NULL 检查在下方 `:5509`。暴露方法分支（`:5479`）已正确判空，内部分支是复制粘贴漏项。触发：任一服务端同伴发送方法序号 ≥ 该类型内部 cell 方法数 → cellapp 段错误（可远程触发的 DoS）。
**已修** `2804083b`。

### P1-4 · `im_func`/`func_code` 在 Python 3 不存在 — `server/cellapp/entity_type.cpp:108-116`
迁移引入的功能回归。路径 `{"__init__","im_func","func_code","co_argcount"|"co_flags"}` 在 3.13 必然失败，`getNestedValue` 返回 false，`argCount`/`flags` 恒为 0 → `expectsNearbyEntity_` **恒为 false**。后果（`entity.cpp:1916-1935`）：`callScriptInit` 恒以**空**元组调用 `__init__`，声明 `__init__(self, nearbyEntity)`（必填）的实体类型全部 `TypeError` 且从不初始化；声明 `nearby=None` 的则静默丢掉该参数。实测核对 Py3.13：`cls.__init__` 是 `function` 类型（`hasattr(__func__)=False`），`__code__.co_argcount` 路径可用。
**已修** `70293dec`：改为 `{"__init__","__code__","co_argcount"|"co_flags"}`。`cellapp_test` 41 用例绿。

### P1-5 · `PySequence_Size` 返回 -1 未检查 → 越界写 — `lib/pyscript/stl_to_py.cpp:100-107`（另 `254-262`、`438-446`）
`PySequence_Check` 只要求 `sq_item`，故「有 `__getitem__` 无 `__len__`」的对象通过检查却使 `PySequence_Size` 返回 -1（带 TypeError）。`PyList_New(szA + (-1))` 只分配 `szA-1` 槽，随后 `PyList_SET_ITEM`（宏，**无**边界检查）写第 `szA-1` 号槽 → `ob_item` 堆越界。`:255` 与 `:438` 同型（负数喂给 `holder_.insertRange`）。
**已修** `061e85a1`：三处均在取值后判 `szB < 0` / `sz < 0` 提前返回（异常已由 `PySequence_Size` 置好）；`concat` 另补 `PyList_New` NULL 判。`pyscript_test` 151 用例绿。

### P1-6 · `PyUnicode_GET_LENGTH` 按码点当字节用 — `lib/server/py_services_map.cpp:32-33`（另 `server/baseapp/data_downloads.cpp:432,436,537`、`lib/script/py_script_output_writer.cpp:122-123`）
`PyUnicode_AsUTF8` 返回 NUL 结尾 UTF-8，其**字节**长度 ≥ 码点数；`assign(ptr, 码点数)` 截断非 ASCII（`"héllo"` → 5 字节拷入，丢末尾 `o`）。Py2 用 `PyString_Size`（字节数）故为迁移引入回归。次生：含孤立代理项时 `AsUTF8` 返回 NULL → `assign(NULL, len)` UB。
**已修** `dc50fae4`：五处统一 `PyUnicode_AsUTF8AndSize`；`data_downloads.cpp` 新增 `utf8Size_` 成员承载该字节数。`server`/`script`/`baseapp` 构建与单测绿。

### P1-7 · `PyObject_Str` 结果未判空 — `lib/pyscript/pywatcher.cpp:1754`、`:1856`
`PyUnicode_Check(PyObject_Str(...))` 直接解引用；`__str__` 抛异常时结果为 NULL，`PyUnicode_Check(NULL)` 读 `((PyObject*)NULL)->ob_type` → 段错误。触发：任一已注册对象的 `__str__` 抛异常且读取该 watcher 路径。
**已修** `098fd73f`：判 NULL 后 `PyErr_Clear()` 并回退空串；顺带补上 `else` 分支缺失的 `Py_DECREF`。

---

## 三、已排除的误报

- **`examples/cellapp_extension/dump_controllers_extra.cpp:99`**（一处报告称「`Py_INCREF(pCallback_)` 泄漏」）——**误报，未修**。`Script::ask` 在 `lib/pyscript/script.cpp:1191-1192` 对 `pFunction` 与 `pArgs` 各做一次 `Py_XDECREF`，即它**接管所有权**；`:99` 的 `Py_INCREF` 与之配平，`PyTuple_Pack` 的元组引用同样被消费。两路审计结论冲突，已据 `script.cpp:1129-1192` 实读裁定。

---

## 四、遗留 P2（26 条，已登记 `todo.md`）

**引用计数/所有权**
- `lib/pyscript/script.cpp:107,133,192` — `PyFrame_GetCode()` 返回**强**引用却从不 `Py_DECREF`（`pushStack`/`updateStackTop`/`PythonProfiler::handleStack`）。注：该泄漏目前是**承重**的（保住交给 `StackTracker::push` 的裸 `name`/`filename` 指针），裸修会引入 use-after-free，修复须同时改为拷贝字符串。
- `lib/pyscript/pywatcher.cpp:1202-1233` — `PyFunctionWatcher::init` 在**所有**路径泄漏 `pName`/`pType`（`PySequence_GetItem` 新引用）。
- `lib/pyscript/script.hpp:508-532`（`getNestedValue`） — 每次 `PyObject_GetAttrString` 泄漏，终引用也未释放；4 元路径每次漏 4 引用，`EntityType` 构造调两次 → 每次热重载每类型 8 引用。
- `lib/pyscript/py_traceback.cpp:593-594` — `PySys_SetObject` INCREF 不 steal，`PyCFunction_New` 结果一次性泄漏。

**热重载/迁移**
- `server/cellapp/entity_type.cpp:381` — `reloadScript` 在 `importEntityClass` 失败（pClass 空）时仍 `setPyType(newRef())`，把该类型活着的 `pPyType_` 抹成 NULL → 此后 `newEntity()` 恒返回 NULL。
- `server/cellapp/entity_type.ipp:73-100`（疑）— 持 `PyDict_GetItemString` 借用的 `sys` 模块跨 `PyDict_Clear(pModules)`，理论上可被 DECREF 到零。
- `server/cellapp/entity_type.cpp:420`（疑）— `s_curTypes_ = s_newTypes_` 无锁改写，而 `getType()`/`getDescription()` 并发读取。
- `server/cellapp/cellapp.cpp:2450-2455`（疑）— `MIGRATE_ENTITIES` 遍历 `Entity::population()` 期间 `onMigrate` 钩子可建/销实体，迭代器失效。
- `server/cellapp/buffered_ghost_message_factory.cpp:104-108`（疑）— `MF_ASSERT(pEntity)` 在 release 为 no-op，`pEntity` 为 NULL 时解引用。

**错误处理/静默失败**
- `lib/pyscript/script.cpp:1547-1551` — `setData(PyObject*, bool&)` 的 `PyLong_AsLong` 溢出错误被忽略，留 OverflowError 挂起。
- `lib/pyscript/py_logging.cpp:52,68`（疑）— `PyUnicode_AsUTF8` 结果未判空即传 `LogMsg`/`metaAsJSON`。
- `lib/script/py_script_object.hpp:1417-1418`、`lib/pyscript/script.cpp:1555-1556`、`lib/pyscript/pickler.cpp:149-150`（疑）— 同族 NULL/`-1` 未检查。
- `server/cellapp/entity.cpp:5311-5331` — `callBaseMethod` 对未知方法序号既不报错也不 `data.finish()`（对比 `callClientMethod` `:5367-5372` 有日志）。
- `server/baseapp/data_downloads.cpp:557`（疑）— `StringDataDownloadFactory::onCreate` 现要求 `PyUnicode_Check`，Py2.7 下可过的 `bytes` 二进制负载现被 `TypeError` 拒绝。

**网络/反序列化（内存与边界）**
- `lib/network/watcher_nub.cpp:448,464,482,499`（另 `646-680`）— watcher 协议 `wdm->count` 无界循环，`astr += strlen(astr)+1` 越界读（管理端口，常 localhost-only）。
- `lib/entitydef/data_type.cpp:178-180` — `retrieve()` 在 `len > BS_BUFF_MAX_SIZE` 且超长时返回 NULL，`new MemoryIStream(NULL, len)` 随后 `readPackedInt` 解引用 `*(uint8*)NULL`。
- `lib/network/udp_channel.cpp:530-541` — 损坏通道状态流上 `Packet::createFromStream` 恒返回 NULL 而 `numToReceive` 不递减 → 死循环。
- `lib/network/fragmented_bundle.cpp:162-173` — `Packet::createFromStream` NULL 未判即 `new FragmentedBundle(pPacket.get())`。
- `lib/cstdmf/network_stats.cpp:149-156` — `strstr(...)+2` 解引用发生在 NULL 守卫**之前**。

**逻辑**
- `lib/entitydef/data_type.cpp:744` — `const_iterator` 拷贝构造把 `size_( other.index_ )`，`index_`≠`size_` 时取到错误元素尺寸（当前潜伏）。
- `server/cellapp/entity_migration_plan.hpp:249` — `advance()` 令 `rollbackNeeded = state.migrated > 0`，首个实体失败且 `migrated==0` 时半迁移态不回滚。

**安全纵深**
- `lib/network/zip_stream.cpp:135-146` — `origLen` 由对端声明（~2^32）后 `new unsigned char[origLen]`，无上限。
- `lib/network/udp_bundle_processor.cpp:338,551` — `new char[dataLength_]` 取声明报文长，越界发现前已分配。
- `lib/db_storage_mysql/db_config.cpp:48-49` — 编译期兜底凭据 `username="bigworld"`/`password="bigworld"`。
- `lib/db_storage_mysql/mysql_database.cpp:568` — 实体类型名裸拼进 `FROM tbl_` 表名（无引用/校验）。
- `lib/db_storage_mysql/table_meta_data.cpp:120` — `SHOW INDEX FROM ` << tableName。
- `lib/cstdmf/binary_stream.hpp:353,407` — `operator>>(BW::vector&)` 与 `TYPEDEF_STREAMING_VECTOR` 无条件 `reserve(attacker_max)`。

## 五、遗留 P3（4 条）

- `lib/cstdmf/log_msg.cpp:558` — `printf(buffer)` 用报文文本当格式串（**仅 `PLAYSTATION3` 分支**，Linux/Windows 不可达）。
- `lib/entitydef/data_types/sequence_data_type.cpp` 原「先分配后校验」的 DoS 面（已随 P1-2 一并消除，留档）。
- `examples/cellapp_extension/dump_controllers_extra.cpp:99` — 误报，留档防重开。
- `programming/bigworld/tools/*` C++ 仍含 Py2 专属 `PyString_*`/`PyInt_*` 调用，故这些 tool 目标未对 3.13 构建（非缺陷，属覆盖面说明）。

## 六、扫描已清（无真实发现，为审计诚实列明）

- **Shell 注入**：全部 exec 点均用 argv 数组、无 shell —— `lib/server/child_process.cpp:324`（`execv`）、`lib/db_storage_mysql/table_synchroniser.cpp:243`（`execvp`）、`server/tools/bwmachined/linux_machine_guard.cpp:452`（`execv`）、`server/tools/snapshot_helper/snapshot_helper.cpp`（`execl`）。范围内无 `system()`/`popen()`/`sh -c`/`shell=True`。Windows `CreateProcess` 调用方（`lib/cstdmf/restart.cpp:64`、`lib/server/service.cpp:1083`）不启动 `cmd.exe`。Python 侧 `subprocess` 均用 list argv（`lib/build/p4revision.py:61,89`、`lib/build/svnrevision.py:11`）。
- **Python 侧 eval/exec/反序列化**：首方 Python 无 `eval()`/`exec()`/`pickle.loads`/`yaml.load`。
- **路径穿越**：无可网络触达者。`bwmachined` 进程启动消息拦截 `..`、`commands/_helpers` 并白名单校验配置（`server/tools/bwmachined/bwmachined.cpp:1784-1818`）；`server/baseapp/data_downloads.cpp:578` 取服务端脚本给出的路径而非客户端输入；resmgr XML 解析为手写、无 DOCTYPE/实体支持（无 XXE）。
- **连接串/API key/私钥/密钥类机密**：首方代码与 `.github/workflows/*.yml`、`scripts/` 均无令牌（仅 `deploy-docs.yml:17` 的 `id-token: write` OIDC，属标准配置）。
- **其余格式串**：所有 `printf`/`sprintf`/`ERROR_MSG` 均为字面量格式串。
- **已核实安全的候选**（不报）：`MemoryIStream::retrieve` 负长路径不可达（`readPackedInt` 上限 `0xFFFFFF` 恒正）；`WatcherConnection::recvSize` 有 `WN_PACKET_SIZE_TCP` 边界；`global_bases.cpp:314` `PyObject_RichCompareBool` 三态收敛正确；`auto_ptr`→`unique_ptr` 迁移所有权交接均 `.get()`+`.release()`（或 `std::move`）保语义。