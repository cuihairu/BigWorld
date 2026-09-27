# BigWorld 内嵌 Python 2.7.3 [3.13.15 迁移]

> 任务：把 BigWorld 集成的 Python 从 2.7.3 升级到 3.13.15（源码树整换 + 补丁重移植 + C-API/脚本全量迁移）。
> 分支：dev（所有提交仅在 dev）。

## 1. 基线事实

- 原内嵌解释器：`programming/bigworld/third_party/python` = CPython 2.7.3 源码树（README、configure.ac 佐证），git 跟踪 4480 个文件。
- BigWorld 对 CPython 的魔改清单：`third_party/python/bw_changes.txt`（2.7 版，7 类补丁）。
- C++ 绑定面：62 个文件直接 `#include <Python.h>`；`PyString_*` 428 处、`PyInt_*` 197 处、`PyClass_/PyCObject_/PyInstance_` 10 处。
- 服务端/工具脚本是 Python 2 语法（含构建工具脚本 `build/make/platform_info.py` 等）。
- 引擎侧对 BWHooks 的实际使用：`lib/pyscript/script.cpp` 只注册 `ignoreAllocsBegin/End` 两个钩子，malloc/free/realloc 钩子在源码中被注释（`script.cpp:352` 附近）。

## 2. 补丁处置表（2.7 [3.13]

| # | 2.7 补丁 | 处置 | 3.13 落地方式 |
|---|---|---|---|
| 1 | BWHooks 内存分配钩子（obmalloc.c/pymem.h `#define malloc/realloc/free`；Makefile/CMake 加 bwhooks.o） | **替代（官方接口）** | `Include/bwhooks.h` + `Python/bwhooks.c` 原样保留（新增 `BW_Py_calloc`，`PyMemAllocatorEx` 需要）；`Makefile.pre.in` PYTHON_OBJS 加 `Python/bwhooks.o`。运行时由 `lib/pyscript/script.cpp` 在 `Py_Initialize()` 前调用官方 `PyMem_SetAllocator(PYMEM_DOMAIN_RAW, ...)`（转发 BW_Py_malloc/realloc/calloc/free）+ `PyObject_SetArenaAllocator`（覆盖 pymalloc arena），**不再侵入 obmalloc.c/pymem.h**。`_hashopenssl.c` 的 `BW_Py_memoryTrackingIgnoreBegin/End` 区间保留（移植到 3.13 的 `hashlib_init_constructors()`，2.7 的 `INIT_CONSTRUCTOR_CONSTANTS` 在 3.13 多阶段初始化中的对应点） |
| 2 | CMake 静态编译链接 python（CMake/ 目录、PC/dl_nt.c ActCtx 桩） | **废弃** | 2.7 的 CMake/libpython 是 BigWorld 自加目录，3.13 树不存在；Linux 用 configure+make（见 §3）。Windows 静态配置如需要须基于 3.13 PCbuild 重建 |
| 3 | VS2012 工程文件 | **废弃** | 工具链过时；3.13 自带 PCbuild（VS2022+） |
| 4 | setup.py 摘除 ssl/crypto 引用 | **被取代** | 3.12+ 扩展模块改由 `Modules/Setup.stdlib` + configure 驱动；`--with-openssl` 即正确姿势。当前 vendored OpenSSL 1.0.0d 低于 3.13 最低要求（1.1.1），故 `_ssl/_hashlib` 被 configure 软禁用（详见 §5 风险 R1） |
| 5 | 老版 sqlite3 兼容修补 | **废弃** | 被修补的代码在 3.x 不存在；本机 `_sqlite3` 用系统 sqlite 正常构建 |
| 6 | Parser/asdl_c.py 行尾修补 | **废弃** | 3.13 树无此问题 |
| 7 | 官方补丁 #17547（gcc 4.8 -Wformat）+ VS2015 兼容（timemodule/posixmodule 桩） | **废弃** | 上游早已修复/工具链过时 |

## 3. 构建系统改动

- `build/make/third_party_python.mak`：
  - `BW_PYTHON_VERSION := 3.13`（`libpython3.13.a`、`bwpython3.13` 目标名自动派生）
  - configure 选项：删 `--enable-unicode=ucs4`（3.3+ 移除）；加 `--disable-test-modules`；`--with-openssl` 以注释形式就位待 OpenSSL 升级
  - 共享模块：3.12+ 由 `Modules/Setup.stdlib` 驱动、产物落在构建目录根且带 `EXT_SUFFIX` 全名；`pythonSharedMods` 列表换成 3.13 实际产物集（54 个，剔除 stdlib 移除项与可选系统依赖项），复制规则与存在性断言机制不变
  - `modulesWhichNeedSymbols` 置空（`_ssl.so/_hashlib.so` 暂不存在），预生成符号表保持不变使进程链接行为与 2.7 一致
  - libpython 的 OpenSSL 依赖与解释器 whole-archive `-lbwssl -lbwcrypto` 注入暂时摘除（无 _ssl 时无意义且 OpenSSL 1.0.0d 在 gcc15/C23 下无法编译），OpenSSL 升级后恢复
- `build/make/discover_python.sh`：python2.4 → python3（优先 `python3.13-config`）
- `build/xcode/build-python.sh`：版本 3.13、去 ucs4、`make all` 替代失效的 `make sharedmods`
- `build/make/Makefile`：`third_party_python_modules.mak`（Twisted 11 / SQLAlchemy 0.6 / Zope 2.11 等 Py2 时代包安装）暂缓启用，包升级后恢复
- 构建工具脚本 Py3 化：`platform_info.py`（print + Debian→el7 平台等效映射）、`invoke_without_jobserver.py`（print/iteritems）、`check_eol_style.py`（print/except 语法）
- 补齐 git 执行位：`build/make/*.sh *.py`、`third_party/openssl/Configure`
- `build/make/third_party_openssl.mak`：拷贝后 `chmod -R +w` → `u+rwX`（保留脚本执行位）

批次 3 期间构建系统的追加改动（gcc 15 / glibc 新环境踩坑）：

- `common_footer_config.mak`：全 BW 静态库用 `-Wl,--start-group/--end-group` 包裹（单遍链接器不回溯，替代原 Havok 特判）
- `third_party_openssl.mak`：configure 加 `-std=gnu89`（C23 `bool` 关键字与 openssl 0.9.8 参数名冲突）；configure 后 `sed -i 's/-DTERMIO\b/-DTERMIOS/g'`（termio.h 被现代 glibc 移除，源码同步在 `ui_openssl.c`/`read_pwd.c` 加 TERMIOS 条件）；删除 `.INTERMEDIATE` 声明（stamp 被反复清理导致 openssl 全量重建 + 并发构建写坏 libcrypto.a）
- `server/Makefile.rules`：cellappmgr 提前到 tools 之前（规避 message_logger 的 `useMongoDB := 1` 全局变量泄漏进其链接行）
- `platform_el7.mak`：恢复 mongodb 探测并以 `ifeq` 包住
- `discover_python.sh`：`[ == ]` → `[ = ]`（/bin/sh=dash 下 bashism 曾致探测静默失败，错误文本混入链接行）

### 3.1 运行期 Python 初始化修复（单测全绿的前置条件）

单测从「pyscript/script/entitydef/baseapp 批量失败」到全绿，靠的是两个与 3.13 无关、却在 3.13 移植激活 Debian 主机构建后才暴露的缺口：

1. **`bw_site.py` 源码发行包缺失**。`Script::init()` 先置 `Py_NoSiteFlag = 1`（跳过 stdlib site.py），再从 `scripts/common` 强制 `import bw_site`，失败即整个初始化失败（`script.cpp` 末段）。这个模块只存在于二进制 RPM 的 res 树里，源码包从未携带（git 全历史无此文件）。现随 pyscript 源码跟踪（`lib/pyscript/bw_site.py`，注释性空模块，不做路径操作——sys.path 由引擎自建），并经 `third_party_python.mak` 新增规则安装到 `game/res/bigworld/scripts/common/`。
2. **`PlatformInfo::str()` 无 Debian 回退，lib-dynload 目录名不匹配**。构建侧 `platform_info.py` 已映射 Debian→`el7`，但运行侧 `lib/cstdmf/bw_platform_info.cpp` 只认 `/etc/redhat-release`，在 Ubuntu 上返回 `unknown`。于是 `Script::init()` 组出的 sys.path 是 `lib-dynload-unknown`（实际目录 `lib-dynload-el7`），全部 C 扩展 stdlib 模块（`_struct`、`_pickle`、`zlib`……）导入失败 → `import pickle` 失败 → `Pickler::init()` 失败 → "Could not initialise Script module"。诊断手段：bw_site.py 是引擎导入的普通 Python 文件，可临时在其中把 `sys.path`/`traceback` 写到 `/tmp` 文件取证（BW 日志被 DebugFilter 过滤、`PyErr_Print()` 输出被 ScriptOutputWriter 重定向，均不可见）。修复：`/etc/debian_version` 存在且无 redhat-release 时同样返回 `el7`，与构建侧 `DEBIAN_EQUIVALENT_PLATFORM` 对齐；`cstdmf` 单测补 `PlatformInfo_debianEquivalentPlatform`。附带收益：`bin/server/<platform>` 等运行期二进制定位逻辑同步恢复一致。

## 4. BWHooks 官方接口方案（替代 #define 重定向）

2.7 的机制是在 obmalloc.c/pymem.h 里 `#define malloc BW_Py_malloc` 等，把 CPython 内部所有裸 malloc 兜进钩子层。3.13 采用官方嵌入接口等价实现：

1. `PyMem_SetAllocator(PYMEM_DOMAIN_RAW, &allocator)`：raw 域是 PyMem/PyObject 全部分配的根基，四个入口全部转发 `BW_Py_malloc/realloc/calloc/free`（钩子表为空时直通 libc，与 2.7 行为逐字节一致，保留 size==0→1 语义）。
2. `PyObject_SetArenaAllocator`：pymalloc arena 分配走钩子层（对应 2.7 在 obmalloc.c 里的 #define）。
3. 两项都在 `Script::init()` 中、`Py_Initialize()` 之前安装——官方要求 raw 域只能在初始化前设置。
4. `_hashlib` 构造常量表的 ignore 区间照旧（引擎内存统计不重复计数）。

## 5. 剩余风险

| # | 风险 | 影响 | 计划 |
|---|---|---|---|
| R1 | vendored OpenSSL 1.0.0d：低于 3.13 最低要求（1.1.1），且在 gcc 15（默认 C23）下无法编译（`bool` 关键字冲突） | `_ssl/_hashlib` 缺席 lib-dynload；依赖 py ssl 的工具受限 | **已解决**（见 [vcpkg-migration.md](vcpkg-migration.md)）：OpenSSL 3.6 经 vcpkg 引入后恢复 `--with-openssl`、共享模块列表、symbols 采集与 libpython 依赖。落地时另发现两个必须一并处理的点：① CPython 用 `-fvisibility=hidden` 编译自身对象，`BW_Py_*` 钩子符号在 `python.exe` 里是 STB_LOCAL，`-export-dynamic` 导不出去，`_hashlib.so` 导入失败并被 3.12+ 的 `check_extension_modules.py` 重命名成 `_hashlib_failed*.so` —— 已给 `bwhooks.h` 的声明加 `visibility("default")`，并用 `-Wl,-u,...` 强制把 `Python/bwhooks.o` 拉进解释器；② 2.7 靠 `obmalloc.c` 里的 `#define malloc` 引用钩子从而把 `bwhooks.o` 拉进链接，3.13 改用官方 `PyMem_SetAllocator` 后 libpython 内部不再引用钩子，这个引用关系没有了。 |
| R2 | `third_party/python_modules` 全部为 Py2 生态包（Twisted 11、SQLAlchemy 0.6、Zope 2.11、oursql 等） | 集群监控/日志工具等运行时 Python 服务不可用 | 单独升级各包到 Py3 兼容版本后再启用 mak；不在编译验证关键路径 |
| R3 | Windows 构建（PCbuild/VS 工程）未迁移 | Windows 侧无法构建 | 后续批次 |
| R4 | 客户端/大文件资源（FantasyDemo 等）未在 Linux 验证范围 | — | 按批次推进 |

## 6. 批次与提交

| 批次 | 内容 | 验证 | 提交 |
|---|---|---|---|
| 1 | 换源 3.13.15 + 补丁重移植 + 构建系统 + 工具脚本 Py3 化 | `make libbwpython3.13` 全链通过：libbwpython3.13.a + python.exe + 57 个共享模块 + lib-dynload/Lib 拷贝 + 动态模块 smoke | `3260bdd9` |
| 2 | lib/pyscript 移植 | libpyscript 编译通过 | `ee940e96` |
| 3 | 引擎其余 C++ 按目录移植（基础库/游戏库/pyscript 残留/common/server 主程序/tools 六组，含 `build(python)` 构建系统组） | 九个 server 主程序 + tools（bots/message_logger/_bwlog.so/bw_profile/bwmachined）全部编译链接通过；cellapp/baseappmgr/loginapp/dbappmgr/reviver 冒烟（启动至无 bw.xml 预期退出，无 Traceback/段错误）；活代码 Py2 API 残留清零 | `ef48c2e5`（构建+三方库）、`871a6589`（基础库）、`bd8ff4cc`（游戏库）、`57b3f1e4`（pyscript/script）、`f86028c6`（common+server）、`7f3cf5ce`（tools） |
| 4 | 服务端/工具 Python 脚本 2to3（bw_internal 181 + examples 15 + build 散点 + res_packer + eg_tcpechoserver，共 204 文件） | py_compile 全量 204/204（迁移前基线 65 失败）；手工修复 simplejson/encoder.py 的 Py2 局部绑定 hack；活代码 Py2 API 残留清零 | `b3bd5fd7` |
| 5 | R1 收尾（OpenSSL 经 vcpkg 3.6 升级后恢复 `_ssl`/`_hashlib`）+ 单测可跑通 | `make user_shouldInstallPython=1 python_install` 通过：59 个共享模块 + stdlib 安装完成，`_hashlib` 不再被重命名；`make bw-unit-tests` 22 个可执行文件全部链接通过 | 见 vcpkg-migration.md（本次未提交，见该文档 §6.1） |
| 6 | 运行期初始化修复：补 `bw_site.py`（源码包缺失，随 pyscript 跟踪 + mak 安装规则）、`bw_platform_info.cpp` Debian→el7 回退对齐构建侧 | 全量单测（21 个模块）无失败；详见 §3.1 | `41fafefc` |
| 7 | 覆盖率基线测量（插桩构建 + 全量单测 + gcovr，详见 §7） | 插桩套件与恢复正常构建后的复验套件均 21 模块 0 失败 | 本次提交（dev） |

---

*命中的 C-API 统计与补丁清单复核于 2026-09-24；CPython 源码 tag v3.13.15。*

## 7. 单测与覆盖率基线（2026-09-26）

全量单测：21 个模块、856 用例、0 失败（`make bw-run-all-unit-tests`）。

覆盖率基线（`user_shouldBuildCodeCoverage=1` 插桩跑全部单测后 gcovr 汇总，统计 `lib/` + `server/`、剔除 unit_test 与 third_party）：

- 总计：行覆盖 **35.2%**（26,556/75,477），分支覆盖 20.1%（27,063/134,406）。
- 较强：`lib/script` 88.9%、`server/`（组件主程序）82.1%、`lib/network` 55.7%、`lib/resmgr` 49.8%、`lib/entitydef` 43.0%、`lib/cstdmf` 39.2%。
- 薄弱：`lib/chunk` 3.8%、`lib/terrain` 2.5%、`lib/physics2` 6.9%（客户端/资源侧子系统，服务端单测基本不触及）、`lib/server` 8.9%、`lib/pyscript` 20.2%。

说明：本次迁移触碰的代码（pyscript/script 绑定层、watcher、pickler、ECDSA/zip/sqlite 封装、平台信息等）均有新增针对性测试覆盖；100% 覆盖率对整个引擎是长期目标，短期性价比最高的补强点是 `lib/pyscript` 与 `lib/connection`（迁移改动密集且可测）。覆盖率周期脚本留存于会话记录：删 `build/el7/obj` 下 `*.o` → `user_shouldBuildCodeCoverage=1 make bw-run-all-unit-tests` → `gcovr --gcov-ignore-errors=all --gcov-ignore-parse-errors=all`（必须加这两个参数，否则 gcov 工作目录缺失的 gcda 会让 gcovr 整体失败）→ 恢复正常构建并复验全绿。

### 7.1 覆盖率补强批次 1：lib/pyscript（2026-09-26）

pyscript_test 新增 14 个用例（24→38，全量 21 模块 856→870 全绿）：

- `test_pywatcher.cpp`（+ `res/test_pywatcher.py` 辅助模块）：BigWorld.getWatcher/setWatcher/getWatcherDir、addWatcher（PyAccessorWatcher 读写回路 + 非 callable 报错）、delWatcher（含 ValueError 路径）、addFunctionWatcher（手工构造 WATCHER_TYPE_TUPLE 流驱动 PyFunctionWatcher，验证回调副作用）、PyObjectWatcher（str() 渲染、setFromString 求值 Python 源码且不做类型强转、SET2 字符串载荷回路、getAsStream）。
- `test_py_data_section.cpp`（+ `res/test_py_data_section.xml`）：结构（keys/has_key/len/下标/child/childName/values/items）、类型化读取（string/int/float/bool/int64/vector2/3/4、复数读取、默认值、as* 访问器）、写入与变更（write* 回路、write 按类型分发、createSection/createSectionFromString/deleteSection/copy）、ResMgr.DataSection() 工厂。
- `test_stl_refs.cpp`：PySequenceSTL/PyMappingSTL（size/迭代/引用读写/拷贝语义）、PyObjectPtrRefSimple（引用计数语义）、PyObjectWatcher 在 list/dict 上的目录行为。

附带修复：`py_data_section.cpp` `createSection` 在 Linux 上父目录名被截断一个字符——`BWUtil::getFilePath`（dirname）不返回尾部分隔符，旧代码无条件 `substr(0, len-1)` 把 `"a/b"` 的父目录削成 `"a"` 的前缀；改为仅在分隔符实际存在时剥离（裸名产生的 `"."` 清空，行为不变）。

备注：network_test 的 ConfigTest 依赖主机内核参数 `net.core.rmem_max ≥ 16MB`（`wmem_max/wmem_default ≥ 1MB`），主机重启后会回落默认值 4MB 导致环境性失败，按测试输出用 sysctl 恢复即可。已发现的预存引擎局限（本次未修）：`PyMappingSTL` 迭代器的按键查找在 `py_to_stl.hpp` 中被注释掉（只做指针同一性比较的 std::find），经 watcher 路径按键查 Python dict 成员永远无法命中，目录枚举不受影响。

### 7.2 覆盖率补强批次 2：lib/connection（2026-09-26）

connection 库此前没有任何单测宿主，本次把 `lib/network/unit_test` 的 `dependsOn` 增加 `connection`，network_test 新增 8 个用例（82→92；文件 `test_login_challenge.cpp`，全量 21 模块 870→880）：

- `LoginChallengeFactories`：默认注册表（delay/fail/cuckoo_cycle 三种）、createChallenge 缺名报错、deregister 后 registerDefaultFactories 恢复、自定义工厂注册与挑战回路。
- delay 挑战：挑战/响应浮点流回路 + 篡改时长校验失败；用 `configureFactories` 注入 0.05s 短时长避免默认 1s 真实 sleep 拖慢套件。测试用最小 `LoginChallengeConfig` 桩（纯虚接口，getDouble/getChild）驱动 configure，免依赖 resmgr。
- fail 挑战：流方法恒真、readResponseFromStream 恒假。
- `configureFactories`：cuckoo easiness 非法（≤0 或 >100）时对应工厂被注销且整体报失败、其余工厂保留；无子配置时全部跳过 configure。
- `CuckooCycleLoginChallengeFactory`：默认 50%、setter 钳位 [0,100]、configure 缺项保留现值、越界拒绝、pWatcher（protected，经测试子类暴露仅验存在）。
- cuckoo 挑战流：17 字符随机 hex 前缀 + 8 字节 maxNonce 的写读回路、重序列化逐字节一致。
- `readResponseFromStream` 失败路径：空流（key 读不出）、外部 key（前缀不匹配）、proof 长度 ≠ 168 字节、proof 非法（全零 nonce 非递增，verify() 拒绝）。
- 正向 PoW 回路：客户端 `writeResponseToStream` 挖矿 → 服务端 `readResponseFromStream` 验证 42-cycle 通过。

坑：挖矿 easiness 不能为了"更快找到解"调到 100——加载率越高，`Cuckoo::path` 越容易超过 MAXPATHLEN，BW_CHANGES 版 worker 直接放弃整次尝试，`writeResponseToStream` 换 key 无限重试，测试表现为挂死（实测 10 分钟无解）。50%（出厂默认）是实测可行点：首个可解 key 通常在百余次迭代内出现，整个挖矿用例 ~15s。

勘误：§7.2 中"17 字符随机 hex 前缀"不准确。前缀是随机 key 的 `"%.16llx:"` 渲染——最多 16 个 hex 位、零填充到至少 2 位、外加冒号，实际长度 3..17 浮动。断言固定长度（如 26 字节挑战流）是概率性通过的，批次 3 已改为动态探测 `readPackedInt` 长度后断言区间。

### 7.3 覆盖率补强批次 3：lib/connection 其余可测面（2026-09-26）

network_test 新增 19 个用例（92→111，3 个新测试文件；全量 21 模块 880→899）：

- `test_replay_header.cpp`（11 用例）：`ReplayHeader` 写读回路（streamSize/calculateStreamSize 一致、digest/频率/时间戳/numTicks/签名长度全字段还原）、签名篡改检测（翻签名字节即报错）、协议版本不匹配前置拒绝（"version mismatch"）、不验签读取时 16 字节签名原样 transfer 给调用方、`checkSufficientLength` 恰好/差一字节/不足以容纳签名长度字段三态、numTicks 字段偏移；`ClientServerProtocolVersion` 流回路（4 字节、"2.9.0" 渲染）与 `supports()` 逐分量相等语义；`ReplayMetaData` 集合行为（增删改查、覆盖不增长）、签名块读回路+篡改检测、不验签读取报告块长并 transfer 签名。
- `test_replay_tick_loader.cpp`（5 用例）：手工构造最小 replay 文件（签名 header + 签名 metadata 块 + 签名 tick 块）驱动后台任务：READ_HEADER 报 header+首 tick 游戏时间、APPEND 区间取回链表（[1,3) 两 tick 且 pStart->pNext()==pEnd）、文件缺失 → ERROR_FILE_MISSING、坏验签 key → ERROR_FILE_CORRUPTED、请求排队簿记与 onListenerDestroyed。
- `test_server_finder.cpp`（3 用例）：`ServerInfo` 值对象、`ServerProbeHandler` 键值对探测回路（handleMessage 逐对回调 onKeyValue → onSuccess → onFinished，用不自杀的测试子类记录）、handleException → onFailure → onFinished。

构建：network_test 的 `dependsOn` 追加 `resmgr`——libnetwork 的 `compression_stream.cpp` 引用 `DataSection`（initCompressionType），tick loader 测试把该目标文件拉进了链接。

本批三个关键事实（都曾是测试的"错误预期"或崩溃源，记录为引擎行为）：

1. **`ReplayTickLoader` 必须堆分配**。它继承 `SafeReferenceCount`，任务持有 `ReplayTickLoaderPtr` 强引用；`TaskManager::tick()` 收割已完成任务时任务析构、引用归零即 `delete this`。栈上实例会被 delete 栈地址，glibc 报 "double free or corruption (out)" 直接 abort（gdb 回溯定位到 `ReplayTickLoader_readHeader`）。引擎自身（ReplayController）就是 `new ReplayTickLoader` + 成员 SmartPointer，测试照做即可。
2. **`appendString` ≠ 追加裸字节**：它先 `writePackedInt( length )` 再 `addBlob`（带长度前缀，无结束符）。签名、cuckoo proof、tick 载荷这类裸字节必须用 `addBlob`，否则每处多出一个长度前缀字节——本批三个测试文件（含批次 2 的 proof 构造）都踩过。附带教训：长度错时 `readResponseFromStream` 走的是"长度不符"分支而非"验证失败"分支，测试通过了但没测到想测的路径。
3. **`ReplayMetaData` 每次读取要新 scheme 实例**：元数据读取用 `ChecksumIStream( shouldReset=false )`，不重置 scheme 的 MD5 状态；复用写侧 scheme 读数据会在脏状态上累加导致 verify 恒败。

坏验签 key 的真实行为：`ReplayChecksumScheme::create` 返回的 ChainedChecksumScheme（SHA+EC）不采纳 EC 子方案的错误状态，`isGood()` 仍为真，坏 PEM key 不会在 `addData` 早期走 `ERROR_KEY_ERROR`，而是拖到 header 签名校验时以 `ERROR_FILE_CORRUPTED` + "Failed to read header: Malformed signature on stream" 呈现（"Verifying key error: " 前缀只进 reader 内部 `lastError()`，该路径不使用；且 task 侧 `addData==false` 分支最终覆盖监听回调里的错误类型）。

`loginapp_login_request_protocol` 评估后主动跳过：其可测行为是 Mercury 消息编解码 + LoginHandler 协作，需要活的 NetworkInterface 与完整登录回合，纯流面已由本批协议版本/元数据用例覆盖；为 ~1 个集成测试引入整套 Mercury 联网环境性价比过低，留待有集成测试宿主时再补。

### 7.4 覆盖率补强批次 4：lib/pyscript 残留面（2026-09-26）

pyscript_test 新增 13 个用例（38→51，3 个新测试文件 + 1 个 XML fixture；全量 21 模块 899→912）：

- `test_resource_table.cpp`（4 用例）：`ResourceTable` 映射导航（`PyObject_Size`/`at()` 正负索引/越界 ValueError、`sub()` 按 key、`PyObject_GetItem` 整数与字符串双路、浮点 key 留错、`keyOfIndex`/`indexOfKey` 哨兵值）、census 身份（同资源两次 `New` 同一对象）、value 结构体类型猜测（int/float/bool/string 属性与 `get()` 缺省回退、根表无 `<value>` 时全落缺省、AttributeError、子表 value 继承链）、`link()`/`unlink()`（link 立即回调表本体、非 callable TypeError、带 updateFn 的 New 只挂不调、New 的 TypeError/ValueError 路径）。注意 ResourceTable/ResMgr 已是弃用接口，仅按现状行为锁定。
- `test_script_events.cpp`（4 用例）：`ScriptEventList`（level 升序稳定插入、triggerEvent 逐监听器回填结果列表、坏监听器 → 返回 false 且补 None、remove 语义、空列表 trigger 为真）、`ScriptEvents` 注册表（未知事件全路径拒绝、`triggerTwoEvents` 一参两发、`clear()` 连事件类型一起删）、`initFromPersonality`（以 `__main__` 充当 personality 模块，有名函数注册、无名事件类型空跑）、`BigWorld.addEventListener/removeEventListener` 模块函数（单例驱动、非 callable TypeError、未知事件 ValueError、二次 remove ValueError）。
- `test_stl_to_py.cpp`（5 用例）：`PySTLSequence` 读路径（len/逐项/越界 IndexError/contains 类型不匹配静默假/concat/repeat 产生普通 list 且不动底层数组/`length` 属性/repr 渲染成 list 样式）、写路径（`x[i]=v` 的 erase+insertRange+insert+commit 全回路、`x+=y`/`x*=n` 原位变异且返回同一对象、`x*=0` 清空）、失败插入整组 cancel（`+=` 中途坏项取消、向量原样、序列可继续读）、只读 holder 三路写拒绝（均 TypeError）、`Script::setData` 整体覆写（list 覆写、同 holder 赋值 no-op、非序列 TypeError、只读拒绝、坏列表整组取消）。

本批修复了三个真实引擎缺陷（均已最小化修复并保留注释）：

1. **`ScriptEventList::remove` 死循环**（script_events.cpp）：原循环不推进迭代器，删除"非首元素"的监听器会原地打转。发布版从未暴露是因为 C++ 里无副作用的无限循环是未定义行为，GCC 直接把循环优化没了——退化成"只看首元素"的 remove，语义错而不崩。修复即补上 `++iter`。
2. **`ResTblStruct::pyGetAttribute` 哨兵崩溃**（resource_table.cpp）：属性miss时把 `(PyObject*)-1` 当 not-found 哨兵传给 `get()`，而 `get()` 的缺省路径会 `Py_INCREF(defVal)`——沿父链查到底必崩。改为先沿父链探测成员是否存在、存在才调 `get()`，miss 落回 `PyObjectPlus::pyGetAttribute`（报 AttributeError）。
3. **`ResourceTable::New` census 探测 UB**（resource_table.cpp）：原代码在未初始化栈缓冲上伪造 ResourceTable、只清零 8 字节 `pSect_` 指针的低 4 位就拿去查 set——64 位上 `DataSectionPtr` 析构会对垃圾指针 decref。改为新增 `findInCensus()` 静态成员做一次真实指针扫描，命中才 incref 返回。

本批两条测试方法论：

1. **待决 Python 异常会跨 CppUnitLite2 用例泄漏**：某用例结束时不 `PyErr_Clear()`，下一个用例的 `PyRun_String` 会带着上一个用例的异常直接失败（且无 traceback——Python stderr 走 BW 压制的输出钩子，非 `-v` 不可见），表现为"下游用例监听器为 NULL → `Script::ask(NULL)` 段错误/`MF_ASSERT` abort"。查错类型的 helper 必须"消费式"取异常（`PyErr_Fetch` 后不再 restore），每处产生错误的断言后紧跟 `PyErr_Clear()`。
2. **`PySequence_GetItem/SetItem` 在协议层归一化负索引**：CPython 先取 `sq_length` 把负下标加上长度再调 `sq_item`/`sq_ass_item`，槽位实现本身仍拒绝负数——测试负索引行为要在协议入口断言，而不是假设槽位语义穿透。

### 7.5 覆盖率补强批次 5：lib/pyscript script_math + 脚本工具面（2026-09-26）

pyscript_test 新增 28 个用例（51→79；全量 21 模块 912→940）：

- `test_script_utilities.cpp`（5 用例）：`PyImportPaths`（非 res 路径插入序去重、分隔符可配、`append()` 保持他方路径在后；res 路径按 BWResource 展开成多条、重复添加不增长；`pathAsObject` 产出 Python list、空集出空表）、`PythonInputSubstituter`（模块缺函数时原行返回且异常待决、非 callable/抛错/返回非串一律空串、正常展开、无 personality 模块原样返回）、`PyLogging` 八个 `BigWorld.log*` 模块函数（(category, message, metaData) 三元组、metaData 可 None 或 JSON 串、参数个数/类型不符 TypeError）。
- `test_script_math.cpp`（23 用例，接管自并行会话的半成品后按引擎实测行为全部重写）：`Math.Matrix` 工厂/元素/乘法求逆/applyPoint/lookAt/投影、Euler 属性回路、`__getstate__`/`__setstate__` 字节回路、Vector2/3/4 运算与方法、引用语义与只读、`Vector4Basic`/`Vector4Product`、LFO/Morph/Animation/Translation/Distance/Swizzle/Combiner/MatrixAdaptor、`Vector4Shader` 寄存器机（13 个 op 全覆盖 + 指令序 + 跨 shader 寄存器喂入）。

本批修复了四个真实引擎缺陷（均已最小化修复并保留 BIGWORLD_BEGIN(3.13 migration) 注释）：

1. **`Script::getRetData` 对智能指针返回类型解析到 `getData( const bool )`**（script.hpp）：`IsValidRetData<>::getData` 里的 `Script::getData( data )` 是限定调用（不走 ADL），且在该模板的定义处只看得到 script.hpp 内建的 bool/int/Vector3 等重载——`PY_SCRIPT_CONVERTERS()` 为各类型生成的 `getData( ConstSmartPointer<CLASS> )` 声明在其后，从不参与重载决议。SmartPointer 的 safe-bool 运算符于是成了唯一可行候选：所有以 RETDATA 声明且返回 `SmartPointer<CLASS>` 的函数都把 `Py_True`/`Py_False` 发给 Python。本批实证受害者是 `Math.getRegister()`（返回 True，随后每个 `addOp` 的 Vector4Provider 形参都报 "argument 2 must be set to a Vector4Provider or None"）。修复：新增 `getRetData( const SmartPointer<T>& )` 偏特化重载，直接走内建 `getData( const PyObject* )`（同一对象 incref、NULL 映 None），凭偏序关系自然胜出。
2. **`PyMatrix::__getstate__` 产 str 触发 UTF-8 校验崩**（script_math.cpp）：原来经 `Script::getData( BW::string )` 把矩阵原始字节构造成 str，3.13 对其做 UTF-8 校验直接 UnicodeDecodeError（`__setstate__` 早已迁成要求 bytes）。改 `PyBytes_FromStringAndSize`，与 PyVector 的 state 方法对齐。
3. **`PyVector` 三种宽度共用 `tp_compare` 声称"一切皆相等"**（script_math.cpp）：旧比较器对任何不匹配返回 0，`Vector3(1,2,3) == 5`、`Vector3 == Vector4` 均为 True。换成 `tp_richcompare`：同宽度按字典序六算子，异类型返回 NotImplemented（`==` 落 False、排序比较按 Py3 约定 TypeError），旧 `tp_compare` 经迁移漏斗 `LegacyCompareAdapter` 的通道随之撤销。
4. **`Vector3.setPitchYaw` 实参数错时返回 NULL 未设异常**（script_math.cpp）：解释器把无异常的 NULL 当系统错误（SystemError）上报。补 TypeError。附带修正 `addOp` 文档串里的 op 编号表（6 ADD 曾被错标成别的名字）。

本批确立的引擎事实（都是测试预期与实现的偏差来源）：

1. **Vector4Provider 强制转换在属性与方法两条路上语义一致**：都过 `Vector4Provider::coerce`——四元组/Vector4 会被快照成新 `Vector4Basic`；活提供者（LFO、Register、Product 等）转换失败后原样透传、保持引用语义；None 复位为空。shader 寄存器喂入由此保持"活"语义（寄存器内容后写覆盖前读）。寄存器本身无 Python 属性可写（`Vector4Register` 不暴露 value），常量得经 `Vector4Product` 单源直通构造。
2. **`Matrix::lookAt` 产视图矩阵**：第三行是 −position·(Right/Up/Direction)，位于 (0,0,−10) 朝 +z 看的观察者 translation.z 读作 +10。`Vector4MatrixAdaptor` 的 X_ROTATE 是绕 X 的俯仰（pitch），Euler 提取在 |pitch|≥90° 退化（roll 出 ±π）；角度属性报主值区间，4 弧度 roll 读作 4−2π——旋转语义不确定时直接断言矩阵元素（BigWorld 行主序，`setRotateX` 存 m[1][2]=+sin）。
3. **无 source 的 MatrixAdaptor 让接收矩阵原样保留**：`Math.Matrix(adaptor)` 新建的是全零矩阵，determinant 为 0（不是单位阵）。
4. **`Vector4Morph` 的 time 钳制链**：time 钳在 [0.0001, duration]，写 duration 会把已有 time 经钳制重写；`target()` setter 直接把 time 清 0（换目标即回起点）。

方法论（接 7.4）：

1. **引擎侧 fprintf 取证**：BW 的 DebugFilter 压掉 ERROR_MSG、`PyErr_Print()` 输出被 ScriptOutputWriter 重定向，均不可见；在怀疑的 C++ 强制转换函数里临时 `fprintf( stderr, ... )` 打 `ob_type->tp_name` 是最短的实证路径（本批靠它 3 分钟定位"reg 竟是 bool"，随后才顺藤摸到 getRetData 重载决议缺陷）。
2. **`checkRaises`/`checkSucceeds` 必须走 `Py_file_input`**：表达式模式无法执行赋值语句（全数报假 SyntaxError），属性 setter 与方法调用的错误路径都要以语句块形式运行；表达式取值仍用 `Py_eval_input` 的 `checkTrue`/`evalFloat`/`evalString`。
3. **跨 C++ 单测的寄存器是全局共享状态**：`Vector4Shader` 的 63 个临时寄存器是进程级单例，多个用例先后写同一寄存器会互相污染——依赖寄存器初值的断言要么先 MOVE 覆写、要么换没用过的寄存器号。

### 7.6 覆盖率补强批次 6：lib/pyscript 脚本基础设施（2026-09-26）

pyscript_test 新增 15 个用例（79→94；全量 21 模块 940→955），全部落在 `test_script_infra.cpp`：

- `KeywordParser`（6 用例）：全量提取 + 缺省删除（ALL_FOUND、值经 `Script::setData` 落到输出变量、字典清空）、部分缺失与空/NULL 字典（SOME_MISSING 只相对已注册关键字、缺失项输出保持缺省）、坏值 EXCEPTION_RAISED + TypeError 带关键字名、未注册 key 默认忽略（既不提取也不删除、子集解析仍 ALL_FOUND）、`allowOtherArguments=false` 下报字典序第一个未注册残留 key（精确到报文文本）、`shouldRemove=false` 字典原样可重复解析、bytes key 报 `"<non-string key>"` 而非崩溃。
- `Script::ask`（3 用例）：调用成功并返回新引用（fn/args 引用恒被偷走，调用后仅剩调用方自己的引用计数）、四条错误路径（非 callable/非 tuple 报 TypeError 带前缀、NULL fn + 不允许 → ValueError、NULL fn + 允许 → 无异常且顺手清掉残留异常）、`printException` 两态（false 时异常留在待决区、true 时打印进 BW 日志并吞掉）。
- `Script::runString`（1 用例）：表达式模式求值返回值、语句模式（printResult=true → Py_single_input）接受赋值并返回 None 且变量落 `__main__`、eval 模式拒语句、坏语法 SyntaxError 待决。
- 标量转换器（1 用例）：`setData(bool)` 收 int 与大小写不敏感的 "true"/"false" 字符串、其余 TypeError；`setData(int)` 收 PyLong 与截断 float、字符串拒绝。
- `Pickler`（4 用例）：协议 2 回路（首字节 0x80/0x02、unicode/bytes/容器结构原样还原）、垃圾与空载荷落 `FailedUnpickle` 替身且 `pickle(替身)` 原样吐回原始字节、NULL ScriptObject 拒绝、`finalise`/`init` 生命周期可逆。

本批引擎修复（1 项，附带并行会话的成果并补齐测试验收）：

1. **`KeywordParser::parse` 非字符串 key 崩溃**（keyword_parser.hpp）：严格模式检查未知关键字时把 `PyUnicode_AsUTF8( key )` 直接喂给 `BW::string` 构造——bytes/int key 下该函数返回 NULL，构造即未定义行为。修复后仅对 str key 做 UTF-8 解析，非串 key 视为未知关键字，报文显示 `Invalid keyword argument: "<non-string key>"`。

本批确立的引擎事实：

1. **`KeywordParser` 的语义都以"已注册关键字"为基准**：`SOME_MISSING` 只在字典缺已注册 key 时出现；`shouldRemove` 只删已注册项（未注册 key 留在字典里，继续参与后续严格检查）；严格模式报的是 `PyDict_Next`（插入序）里第一个未通过检查的残留 key。
2. **`Script::ask` 默认 `printException=true`**：异常会被打印进 BW 日志然后吞掉（`PyErr_PrintEx` 消费 + 显式 `PyErr_Clear`）——要在测试里检查待决异常必须显式传 false。`ask` 无论成败都偷走 fn/args 的引用。
3. **`Script::runString` 第二参无默认值**，false → `Py_eval_input`（仅表达式、返回值），true → `Py_single_input`（接受语句、交互式回显、返回 None）。
4. **Pickler 的替身语义**：`unpickle` 失败不报错，返回持有原始字节的 `FailedUnpickle` 对象；对替身再 `pickle` 走透传分支原样吐回。pickle 流是二进制（PROTO 头 0x80 非 UTF-8），unpickle 侧必须以 bytes 传入——批次前迁移已把 "s#" 改 "y#"，本批的回路用例即是该修复的回归锁。

协作事故记录：本批期间一个并行会话在同一工作树上持续覆写 `test_pickler.cpp`（其内容引用不存在的 API，无法编译）。处置：`test_pickler` 移出构建清单、该文件不入库；Pickler 用例改并入并行会话未触碰的 `test_script_infra.cpp`；其 `keyword_parser.hpp` 修复经审阅属实后收录并补测试。教训：**有并行会话同时改树时，提交前必须以 `git status` 快照为准逐文件核对，构建产物只信任自己刚构建的那一份**。

### 7.7 覆盖率补强批次 7：lib/pyscript 回溯打印器（2026-09-26）

pyscript_test 新增 7 个用例（94→101；全量 21 模块 955→962），全部落在 `test_py_traceback.cpp`（+ fixture `res/test_py_traceback_mod.py`）。`py_traceback.cpp` 此前只有被间接踩到的路径，源码行渲染、缓冲增长、异步补读三条主线都没有直接断言，本批为其补齐并顺带修出两个真实缺陷。

- `PyTraceback_initExceptionHook_installsHook`：`Script::initExceptionHook` 把 `sys.excepthook` 换成引擎的 `printTraceBack`。
- `PyTraceback_rendersFramesOfNestedCall`：三级嵌套调用（`run_nested` → `_outer_boom` → `_inner_boom`）的完整帧链渲染——每帧的 `File "…", line N, in <name>` 头、去缩进后补四个空格的源码行、行末的 `ValueError: tb boom`；`<string>` 帧（fixture 模块是绝对路径，snippet 是相对名）拿不到源码，必须只有头没有源码行。fixture 行号由文件尾部常量钉死（15/19/23/27），改动 fixture 需同步改断言。
- `PyTraceback_missingSourceFile_omitsLine`：帧指向不存在的文件时，头照常打印、源码行省略。
- `PyTraceback_longSourceLine_growsBuffer`：`outputFrame()` 的缓冲初值 256 字节，fixture 里 354 列的源码行触发翻倍增长，断言整行（354 列、含 "padding padding padding"）无截断落地。
- `PyTraceback_nonTracebackArg_fallsBack`：第三参不是 traceback（传 42）时整条交给 `PyErr_Display`，输出里不出现引擎的 "Traceback (most recent call last):" 前缀但异常照常可见。
- `PyTraceback_wrongArgCount_returnsNone`：参数个数不对时引擎侧报错并清掉，hook 仍返回 None 而不是往调用方抛。
- `PyTraceback_blockedRead_registersWithDispatcher`：源码文件是 fifo（无写端时 open/read 都不阻塞），引擎读到 EAGAIN → 把 fd 注册进 `EventDispatcher` → 测试喂数据 → `processOnce(false)` 后补出源码行。构建侧为此给 unit_test 的 `Makefile.rules` 加 `dependsOn += network`（`Mercury::EventDispatcher`）。

捕获机制：把 `sys.stderr` 换成一个只有 `write`/`flush` 的 Python 对象——`PySys_WriteStderr` 走 `sysmodule.c: sys_write`、`PyErr_Display` 走 `pythonrun.c`，两者都写 **sys.stderr 对象**，于是引擎整块输出落到捕获串里；析构时还原。注意 stdlib 自己的 `_print_exception_bltin` 还会再渲染一遍异常（带 caret 的那段），断言一律用 `find`/`!= npos` 而非全等。

本批引擎修复（2 项）：

1. **`TraceBack::tb_lineno` 在 3.13 恒为 -1**：`_PyTraceBack_FromFrame()` 现在只记 `tb_lasti`（字节码偏移），行号由 `tb_lineno_get` 惰性 `addr2Line` 解析；直接读字段永远得到 -1，`isAtDesiredLineNum()` 永不命中 → **任何帧都不会打印源码行**（帧头照常）。修复：新增 `TraceBack::lineNo()`，用 `PyFrame_GetCode` + `PyCode_Addr2Line( code, tb_lasti )` 现算，替换 `isAtDesiredLineNum`、`ERROR_MSG`、`outputFrame` 三处直读。
2. **绝对 `co_filename` 被 `getAbsolutePath` 加前缀**：`startFrame` 原本无条件走 `MultiFileSystem::getAbsolutePath`，它会给名字挂上第一个 res 路径前缀——而源码文件名来自 import 机制（`sys.path` 条目 + 模块名），服务端 res 路径本身是绝对的，所以拼出的路径不存在，open 失败被静默吞掉。修复：`BWUtil::isAbsolutePath` 为真时直接用原名。sys.path 条目形如 `<resPath>/.`，所以 fixture 的 `co_filename` 是 `…/unit_test/res/./test_py_traceback_mod.py`（含 `./` 但绝对），这正是修复 2 覆盖的形态；`<string>` 帧是相对名、走 getAbsolutePath 必失败，构成修复 1 之外的对照组。

本批确立的取证（都曾是测试的错误预期）：

1. **无写端的 fifo 上 `read()` 返回 0（EOF），不是 `EAGAIN`**——异步分支根本触发不了；且此时再用 `O_WRONLY|O_NONBLOCK` 去开写端会直接 `ENXIO`。正确做法是先 `open(fifo, O_RDWR|O_NONBLOCK)` 自己持住写端再调 hook（既不会 EOF，也不会 ENXIO），喂数据时从同一个 fd 写。
2. **`linecache` 会去读帧的源文件**：`PyErr_Display` 回落渲染时若 `__traceback__` 还挂着，会对 fifo 再开一个读端等数据，与引擎自己的读端抢数据（可能永久挂住）。用例显式 `value.__traceback__ = None` 切断这条路径，只留引擎侧的单读者。
3. 测试里产生待决异常后必须消费式取错或紧跟 `PyErr_Clear`，否则会毒化下一用例的 `PyRun_String`（本批 7 例反复踩到）。

分工记录：本批与批次 6（§7.6）由两个并行会话在同一工作树上分头进行——批次 6 认领 pickler + keyword_parser，本批只认领 `py_traceback`，互不触碰对方文件；`test_pickler.cpp` 依批次 6 的决定保持 untracked 不入库。提交前以 `git status` 逐文件核对，本批只含 `test_py_traceback.cpp`、`res/test_py_traceback_mod.py`、`Makefile.rules`、`py_traceback.cpp`、本文档。

### 7.8 覆盖率补强批次 8：lib/pyscript 四个零覆盖文件（2026-09-26）

pyscript_test 新增 14 个用例（101→115；全量 21 模块 962→976），全部追加进批次 6 建立的 `test_script_infra.cpp`，覆盖此前完全零测试的四个编译产物：`res_mgr_script.cpp`、`personality.cpp`、`py_debug_message_file_logger.cpp`、`py_factory_method_link.cpp`。另有两文件确认根本不在 Linux 构建里：`py_memory_log.cpp`（`log_malloc` 声明 `size_t`/定义 `unsigned int` 在 x86_64 上是重声明冲突，未入构建）与 `automation.cpp`（`LPCTSTR`/`bw_wtoutf8` Windows 专用），无法也无需测试。

**ResMgr 模块（6 例）**：`isFile`/`isDir` 直查资源系统；`openSection` 命中返回 DataSection、缺资源返回 None；`purge`（含递归）后可重开；`ResMgr.root` 属性。三个曾判错的行为：① `openSection(id, True)` 在中间目录缺失时会**真的在第一个 res 路径（即源码 unit_test/res）里建出目录**并返回新 section——用例结尾用 `remove()` 清理，防止污染源码树；② makeNew 挂在**已有文件**路径下（`xxx.xml/sub/child.xml`）则成功返回文件 DataSection 的虚拟子节点、不落盘——"Could not make new section" ValueError 只在父节点不可建时出现；③ `save()` 未缓存资源抛 `OSError: Save of %s failed`（RETOK 的 false 转 raise；Py3 的 IOError 已是 OSError 别名，`type(e).__name__` 拼出的是 "OSError"）。`resolveToAbsolutePath` 命中→绝对路径、缺失相对→挂第一个 res 路径、缺失绝对→原样返回；`localise` 对未知 key 按 `RETURN_PARAM_IF_NOT_EXISTING` 原样回串，非字符串参数 TypeError。

**Personality（2 例）**：import 失败留下空实例且异常被 `ScriptErrorPrint` 消费；import 成功后二次 import 走 WARNING 并返回同一实例；`getMember` 单名/双名（弃用名回退）；`callOnInit` 把 isReload 传给 `onInit` 并注册 FiniTimeJob（进程退出时才会触发 `onFini`，fixture 模块须存活到那时——留在 `sys.modules` 即可）。

**PyDebugMessageFileLogger（5 例）**：`BigWorld.FileLogger(...)` 工厂的默认值（append、空 category、enable False 可读写）与显式配置（severities 元组/单串两形态、sources、openMode）；非法参数矩阵（缺 fileName→TypeError、severities/sources 非元组非串→TypeError、未知名→ValueError、openMode 非 a/w→ValueError；`None` 表示保留 ALL）；`defaultLoggers()` 空表；C++ 侧 `ConfigCreatedFileLoggers` 容量上限（MAX_FILE_LOGGERS=5，满后拒收，越界下标返回 NULL）。两个事实：`config()` 对 category 做 **tolower 归一**（cstdmf:315，过滤大小写不敏感）；severities/sources 属性回读为 `bitsToString` 的 **`;` 连接串**。注意 FileLogger 测试的 fileName 一律指向 `/tmp`——`enable=True` 会真的开文件，相对名会在测试 CWD（unit_tests 目录）落盘。

**PyFactoryMethodLink（1 例）**：引擎用法是**静态初始化期构造**——其基类 `Script::InitTimeJob` 在 `Script::init` 之后构造会触发 CRITICAL（"constructed after script init time!"）直接 abort，测试里临时构造必炸。用例改为文件作用域静态实例 + 静态类型字段初始化器（复刻 `PyVarObject_HEAD_INIT`：引用计数 1、元类型 `&PyType_Type`，字段须在 Script::init 跑 job 前备好），测试体只验证 Script::init 已把类型以限定名发布到模块（tp_name 变 `module.method`，这正是 PyDataSection 的 tp_name 显示为 **"ResMgr.DataSection"** 的机制）、`fini()` 恢复原名且二次 fini 无操作。

**链接器坑（ResMgr 模块整体缺失的根因）**：测试对 res_mgr_script.o 没有任何 C++ 符号引用（只经 Python 字符串访问），静态库根本不会把它拉进可执行文件——ResMgr 模块在测试进程里不存在，`__import__('ResMgr')` 直接失败。该文件唯一非静态符号是 `ResMgr_token`，用 `volatile int g = ResMgr_token;` 强制收敛档案成员后模块才可用。

**跨用例状态污染事故（本批最重要的教训）**：`PythonInputSubstituter::substitute(line, NULL, fn)` 在模块为 NULL 时**回落到 `Personality::instance()`**（python_input_substituter.cpp:28）。批次 5 的用例以"无 personality 模块"为前提断言 NULL 分支；本批导入 personality 后该前提失效——回落路径 getattr 我的 fixture 模块的 'expand' 失败，**AttributeError 挂起**，毒化下一个用例（PyLogging）的 `PyImport_ImportModule("BigWorld")`，表现为一个与肇事者毫无表面关联的确定性失败（gdb 断点 + `PyErr_GetRaisedException()` 才定位到）。修复：更新批次 5 该断言块的注释并在其后 `PyErr_Clear()`。结论：**Personality 单例是进程级全局状态，导入它的测试会改变其后所有用例的引擎行为路径**，认领 Personality 测试时必须全局排查 `Personality::instance()` 的消费者（目前仅 input_substituter 一处）。

**CppUnitLite2 陷阱**：`CHECK_EQUAL( const char *, const char * )` 比较的是**指针**——内容相同的字面量与 `PyUnicode_AsUTF8()` 结果必不相等，报错还显示为"expected: 'x' but was: 'x'"。凡与运行时字符串比较，要么 `strcmp(...)==0`，要么包成 `BW::string`。

**并行会话碰撞实录（批次 6 之后第二轮）**：① 被打断的构建在编译中途被杀，留下 **0 字节 .o 且时间戳与源文件同秒**——make 判定"最新"永远跳过重编，二进制静默缺 30 个用例（86 vs 115），`nm` 查不到新测试符号是判别特征；对策是 `touch` 源文件强制重编。② opencode 在同一 obj 目录并发构建/跑全量套件，期间二进制被对方重链成中间态；对策是提交前自查 `ps` 确认无并发 make，且**只相信自己刚构建并立即运行的二进制**。③ opencode 同期提交了它的批次 7（debf3038，py_traceback 7 例），并在我迭代期间给 `py_debug_message_file_logger.cpp` 补了 `MAX_FILE_LOGGERS` 的类外定义、调整了 lib Makefile.rules 源序——均为其未提交改动，本批不触碰。

提交范围：`test_script_infra.cpp`（+14 例与上述修复）、`test_script_utilities.cpp`（回落分支断言更新）、本文档。opencode 的未提交改动（`py_debug_message_file_logger.cpp`、lib `Makefile.rules`、`test_script_events.cpp`、`test_stl_to_py.cpp`、`test_pickler.cpp`）一律不收。

### 7.9 覆盖率补强批次 9：lib/pyscript 收尾（2026-09-26）

先收录并复验了并行会话（opencode）的遗留未提交改动（`1331bf28`）：`MAX_FILE_LOGGERS` 类外定义（ODR 正解）、lib Makefile.rules 源序调整、`runPython` 失败分支简化为 `PyErr_Print()`、`test_stl_to_py.cpp` 四处 `PyErr_Clear()` 补漏；`test_pickler.cpp` 继续保持不入库。

pyscript_test 新增 3 个用例（115→118；全量 976→979）：

- `test_stl_to_py.cpp` +2：`PySTLSequence_operatorArgumentTypes`（非序列参数走 `PyNumber_Add`（类型机制把 sq_concat 接进 nb_add）与 `PySequence_Concat`/`InPlaceConcat` C API 双路命中引擎自己的 "Argument to + / += must be a sequence" TypeError、向量不动）、`PySTLSequence_subscriptAndDeletion`（下标读/写/删、负索引归一、非整数与 slice key 拒绝、C-API 切片入口同样拒绝）。
- `test_script_events.cpp` +1：`ScriptEvents_secondInstanceKeepsFirst`（二实例构造/析构双 WARNING，模块函数始终路由首实例，二实例容器与注册表互不干扰）。
- `test_script_utilities.cpp`：PyLogging 用例补 metaData **空串**分支（与 None 同走普通 write 路径，仅非空串才当 JSON 载荷）——py_logCommon 最后一个未覆盖分支。

本批修出两个真实缺陷：

1. **`del seq[i]` 在 PySTLSequence 上必然崩溃**（stl_to_py.cpp）：映射协议把删除表达为 `sq_ass_item(i, NULL)`，旧代码无条件 `holder_.insert(pItem)` → `Script::setData(NULL, …)` → `PyLong_Check(NULL)` 段错误。修复：`pySeq_ass_item` 识别 NULL pItem，单独走 `erase(index, index+1)+commit`（holder 契约允许"单 erase 无 insertRange"的 commit 组）。
2. **序列型类型整体丢失脚本下标能力（迁移回归）**（pyobject_plus.hpp/.cpp）：Py2 的 `PyObject_GetItem/SetItem/DelItem` 对无 `tp_as_mapping` 的类型有 int-key 回退到 `sq_item/sq_ass_item`（脚本可 `seq[i]`）；Py3 删除了该回退——`PySTLSequence` 在 3.13 下 `seq[i]`/`seq[i]=v`/`del seq[i]` 全部 TypeError（"'X' object is not subscriptable"）。修复：新增共享 `PyTypeObjectUtil::seqMappingMethods`（`mp_subscript`/`mp_ass_subscript` 经类型自身 sq 槽派发、负索引归一、NULL 值转删除），`PY_TYPEOBJECT_SPECIALISE_SEQ` 自动接线 tp_as_mapping。影响面：Linux 构建内 WITH_SEQUENCE 用户只有 PySTLSequence 一个（另一用户 gizmo/item_view 是客户端库、不在 Linux 构建）；无类型同时自带 mapping 表。stl_to_py.cpp 顶部的旧迁移注释（"the type machinery synthesises x[i:j]"）与 3.13 源码不符（abstract.c 三个 Slice 入口对无 mp 实现的类型一律 type_error），已更正：**切片在本类型上彻底不可用**，可用面是整数键下标。

本批确立的引擎/协议事实：

1. **CPython 3.13 无 mp_subscript 的类型没有任何切片合成回路**：`PySequence_GetSlice/SetSlice/DelSlice`（abstract.c:1905/1993/2016）只走 `mp_subscript/mp_ass_subscript`，否则直接 `"'%.200s' object is unsliceable"` 类 type_error；解释器层 `x[i]` 同样需要 mp_subscript。sq_item/sq_ass_item 仍被迭代、`PySequence_GetItem/SetItem` C API 与 `PyObject_Size`（len 走 sq_length）使用。
2. **`PyNumber_Add` 能到 sq_concat 的非序列参数分支**：类型机制（typeobject.c add_operators）把 sq_concat/sq_repeat 包进 nb_add/nb_multiply 派发，所以 `x + 42` 由引擎槽位报自己的错；`PySequence_Concat`/`InPlaceConcat` 则无条件直调槽位。
3. **ScriptEvents 单例是"首实例获胜"**：二实例构造只 WARNING 不接管 `g_pInstance`，析构时非单例也只 WARNING 不清槽——模块函数全程路由首实例，两实例的事件容器完全独立。

方法论（接 7.8）：待测协议行为先读 vendored CPython 源码再写断言（abstract.c 的三个 Slice 入口直接推翻了引擎旧注释与我的第一版测试预期——测试跑出来的段错误/NULL 是"断言写错"的第一信号，gdb 回溯+源码比对十分钟内定位）；对 NULL 返回值做 `PyList_Check` 这类解引用断言前必须先 `CHECK(p != NULL)` 短路，否则断言失败本身变崩溃。

提交范围：`stl_to_py.cpp`（删除分支修复+注释更正）、`pyobject_plus.hpp/.cpp`（共享下标表）、`test_stl_to_py.cpp`、`test_script_events.cpp`、`test_script_utilities.cpp`、本文档。

### 7.10 覆盖率补强批次 10：lib/connection 零覆盖可测面（2026-09-26）

本批转向 `lib/connection`。三个目标文件经核实全部可在 Linux 构建里可测，**无需替换**（批次 2/3 的做法照抄：宿主 `lib/network/unit_test`，其 `dependsOn` 已含 `connection`）：`log_on_params.cpp`（114 行）、`data_download.cpp`（74 行）、`filter_helper.cpp`（50 行），合计 238 行此前被全仓库 `test_*.cpp` 零引用。

network_test 新增 7 个用例（111→118；全量 21 模块 979→986）：

- `test_log_on_params.cpp` +4：`LogOnParams_plainRoundTrip`（明文回路：三个字符串/16 字节摘要/4 字节 nonce 全部回读一致、`remainingLength()` 归零、flags 回 HAS_ALL；并用"同一对象写两次"证明 nonce 随机但逐字节透传）、`LogOnParams_flagControl`（写侧 flags 决定可选字段：无 HAS_DIGEST 时线上恰少 16 字节、读侧 flags 为 0 且摘要保持空；默认构造的空串也能回路）、`LogOnParams_encoderRouting`（挂 encoder 时 encrypt/decrypt 各恰好被调一次、透传 encoder 保持明文布局逐字节相同、encoder 失败时 addToStream/readFromStream 都返回 false）、`LogOnParams_streamingOperators`（`stream << out` / `in >> back` 走同一条非加密路径）。
- `test_data_download.cpp` +2：`DataDownload_segmentsAndCompletion`（`complete()` = 末段与描述**同时**到齐：只有段不行、只有末段不行、描述后补也算完成；描述与 id 同存，`size()` 计段数）、`DataDownload_writeOrderIsInsertionOrder`（乱序 seq 的三段写出仍是插入序，见下方"行为事实 1"）。
- `test_filter_helper.cpp` +1：`FilterHelper_forwardsToEnvironment`（5 个转发面逐个验参数顺序、出参双向传递、返回值保真，含 `pGroundNormal == NULL` 的调用形态）。

三个文件均达到完整行/分支覆盖（析构回收段与描述、`insert` 的 isLast 真假两支、`write` 的 `MF_ASSERT_DEV(complete())` 前置、`FilterHelper` 构造与四个转发面）。

**本批没有修出引擎缺陷**——这三处是纯管道代码（序列化转发、列表持有、虚调用转发），7 个用例首跑全红但**七处失败全在测试侧**，引擎行为与断言预期一致。这与批次 8/9（pyscript 连出两个真实缺陷）形成对照：越靠近"纯转发"的文件，越可能只产出事实而非缺陷。据此记下两条行为事实：

1. **`DataDownload::insert` 的注释与实现不符**（data_download.cpp:25-27 写 "Insert the segment into this record in a sorted fashion"，实现只是 `push_back`）：`DownloadSegment::seq()` 被携带但**从不参与排序或校验**，`write()` 因此按**到达序**拼接。唯一生产调用方 `ServerConnection::onDataDownload`（server_connection.cpp:2907）的分段消息走可靠有序通道，到达序恰与 seq 序一致，故当前不是活跃缺陷；测试用 seq 为 1/0/2 的乱序插入把这一事实钉住。
2. **`LogOnParams` 的线上布局是 `flags | username | password | encryptionKey | [digest] | nonce`**，其中每个 `BW::string` 占 **1 字节 packed length + 载荷**（`appendString`，<255 的串前缀恒 1 字节），故 35 字节的完整消息 = 1 + (1+4) + (1+4) + (1+3) + 16 + 4。摘要分支由**写侧 flags 参数**而非对象自身状态决定（`PASS_THRU=0xFF` 时才回落到 `flags_`）。

方法论（本批新增的 C++ 测试基础设施陷阱，共四条）：

1. **`MemoryIStream` 只借指针，`MemoryOStream::retrieve()` 又是破坏式的**（推进 `pRead_`）——于是"helper 里 `return new MemoryIStream( localOStream.retrieve( n ), n )`"是双重陷阱：helper 一返回局部 `MemoryOStream` 就析构，缓冲随即悬垂；而在同一个输出流上第二次 `retrieve()` 拿到的是已耗尽的流（表现为"字符串读出来是空的"，而不是崩溃）。对策两条：需要跨函数持有时把字节 `new char[n]` 拷出、用一个持有 `delete[]` 的 `MemoryIStream` 子类包住（本批两个文件的 `OwnedIStream`）；同一段字节要既比较又建流，就先取一次指针存进局部变量复用。这条坑的迷惑性在于**失败表现是"值不对"而非崩溃**，`CHECK` 报错完全指不到根因。
2. **CppUnitLite2 的 `CHECK_EQUAL` 会 ODR-use 类内 `static const` 整型常量**，若该常量没有类外定义就链接失败（`LogOnParams::HAS_ALL`、`MD5::Digest::NUM_BYTES` 都是 `enum`/整型常量且无 out-of-line definition）；`CHECK_EQUAL( const char*, const char*)` 同样比指针。两条都靠"先拷进局部变量再断言"绕开（`const int HAS_ALL = LogOnParams::HAS_ALL;`），已在两个新测试文件里各留注释。
3. 待测类若把转发面设成 `protected`（`FilterHelper` 五个方法全是，真实 filter 在 `input()/output()` 内部调用），测试子类要补 public 包装再转发——这正好顺带验证"protected 只影响可见性、不影响派发"；`FilterHelper` 构造器同为 `protected`（类是抽象的），子类化是唯一入口。
4. **`Direction3D( const Vector3 & )` 的分量映射与 `Vector3` 不同序**：`v[0]→roll`、`v[1]→pitch`、`v[2]→yaw`，所以断言构造结果要读 `.yaw` 而不是 `.z`。
5. **"消耗流"与"读流长"不能写在同一个实参表里**（`7820caf9` 之后的补修）：`MemoryIStream in( stream.retrieve( stream.size() ), stream.size() )` 看着无害，实则两个实参的求值顺序在 C++ 里**未指定**；GCC 从右向左求值，第二个 `size()` 抢在 `retrieve()` 之前跑，所以一直是绿的，换个求值顺序的实现就会拿到 `length == 0` 的空流、`readFromStream` 直接失败。这类隐患的共同点是"当前编译器恰好正确"，所以新写测试里凡是要先取长度再建流，一律先把长度存进局部变量（`const int n = stream.size();`），别指望求值顺序。

提交范围：`test_log_on_params.cpp`、`test_data_download.cpp`、`test_filter_helper.cpp`、`unit_test/Makefile.rules`、`unit_test/CMakeLists.txt`（三个文件同批登记进两处构建描述，`nm` 确认 7 个用例符号确实编进二进制）、本文档。`test_pickler.cpp` 继续保持 untracked 不入库。

下一批候选（同样零引用、行数适中、Linux 构建可测）：`movement_filter.cpp`（124）、`login_request_protocol.cpp`（93）、`login_challenge_task.cpp`（81）。`replay_data_file_reader.cpp`（1166）、`login_handler.cpp`（653）、`smart_server_connection.cpp`（362）体量大且依赖 res 树/真实连接，可测性需另行评估。（更正：本条原把 `replay_checksum_scheme.cpp` 也列为零覆盖，**这是错的**——当时的扫描按文件 basename 做的区分大小写匹配，而实际引用用的是类名 `ReplayChecksumScheme`；`server/baseapp/unit_test/test_recording.cpp` 早已覆盖它（签名回路、错公钥拒绝、手工核对三处）。批次 11 已按类名重扫纠正。）

### 7.11 覆盖率补强批次 11：lib/connection filter/login 面（2026-09-26）

承接 7.10 的候选清单（已按上面那条更正剔除 `replay_checksum_scheme.cpp`），本批三个文件全部可测，无需替换：`movement_filter.cpp`（124 行）、`login_challenge_task.cpp`（81 行）、`login_request_protocol.cpp`（93 行）。零覆盖的判定这次改用**类名**扫描（`MovementFilter` / `LoginChallengeTask` / `LoginRequestProtocol`），避免 7.10 那种 basename 漏判。

network_test 新增 8 个用例（118→126；全量 21 模块 986→994）：

- `test_movement_filter.cpp` +4：`MovementFilter_copyStateUsesTryCopyState`（子类 `tryCopyState` 返回 true 时**立即返回**，`input` 一次都不被调）、`MovementFilter_copyStateReplaysLastInput`（回退路径：源 filter 的 last input 被原样重放成目标的首次 input，time/spaceID/vehicleID/position/positionError/direction 六项逐一比对）、`MovementFilter_copyStateWithNoLastInput`（源无可用样本时回退是空操作）、`MovementFilter_forwardsToEnvironment`（四个环境转发面 + 可选 ground normal）。
- `test_login_challenge_task.cpp` +3：`LoginChallengeTask_performWritesResponse`（perform 跑一次挑战、翻转 isFinished、响应留在 `data()`、耗时被记录）、`LoginChallengeTask_performIsNotRepeatable`（二次 perform 被拒：挑战不再被调用、`data()` 长度与耗时都不变）、`LoginChallengeTask_disassociateDropsHandler`。
- `test_login_request_protocol.cpp` +1：`LoginRequestProtocol_singletons`（两个访问器都是进程级单例、重复调用同一对象、两者互不相同且 `appName()` 分别是 LoginApp/BaseApp、释放全部本地句柄后再取仍存活）。

本批修出一个**真实缺陷**（潜伏型，由新测试触发链接面变化才暴露）：

**`test_channel_interfaces.hpp` 的 fixture 接口占了生产命名空间 `BW::ClientInterface` / `BW::ServerInterface`**。`BEGIN_MERCURY_INTERFACE` 的"定义"变体会在头文件里生成 `Mercury::InterfaceMinder gMinder( "..." )` 这个**有实体的命名空间级对象**（外加三个非 inline 的自由函数）。该 fixture 头此前只有 `test_channel.cpp` 一个 TU 包含，而 network_test 从不链入 `libconnection` 的 `server_connection.o`，所以两处定义碰不到一起；本批新增的 `test_login_challenge_task.cpp` 一旦构造 `LoginHandler` 就把 `server_connection.o` 拖进链接图，于是 `gMinder` 与三个 `registerWith*` 立刻 multiple definition。修复：把 fixture 接口改名 `ServerChannelTestInterface` / `ClientChannelTestInterface`（`test_channel.cpp` 内 14 处引用同步更新），这正是同目录 `test_tcp_channels_interfaces.hpp` 早就采用的 `TCPChannels*Interface` 惯例——只有这个更老的 fixture 头没跟上。顺带确认其余四个 fixture 头（flood/fragment/mangle/tcp_channels）命名都不侵占生产名，无需处理。

本批确立的引擎/协议事实：

1. **`tryCopyState()` 在 `MovementFilter` 里是 private virtual，但派生类照样能覆写**——访问控制不阻止虚函数覆写，基类 `copyState()` 经 vtable 派发。测试正是靠这一点分别构造"接管"与"回退"两条路径。
2. **`LoginChallengeTask::perform()` 的重复调用保护在单测里完全不可见**：拒绝分支只发一条 `ERROR_MSG`，而 network_test 的 DebugFilter 会吞掉 ERROR_MSG；可观测的等价证据是"挑战没被再调一次、`data()` 长度与耗时都没动"。
3. **两个 BackgroundTask 覆写与 `onAttemptFailed` 本批不覆盖，原因是依赖链而非偷懒**：`doBackgroundTask`/`doMainThreadTask` 需要真实 `TaskManager` 驱动并回调 `LoginHandler::onLoginChallengeCompleted()`（会牵动登录状态机与 ServerConnection）；`LoginRequestProtocol::onAttemptFailed` 需要构造完整 `LoginRequest`，而它要求一个 `LoginRequestTransport &`，后者（`login_request_transport.cpp`，488 行）本身依赖真实 ServerConnection。这三处的可测性留待后续批次。
4. **`LoginHandler` 在单测里必须堆分配且带非 `NOT_SET` 的状态**：`SafeReferenceCount` 的构造函数把计数置 **0**（不是 1），而 `LoginChallengeTask` 会用 `LoginHandlerPtr` 持有一份引用——若 handler 是栈对象，引用归零时 `release()` 会去 `delete` 一个栈对象。同时析构函数在 `!isDone_` 时会 WARNING 并调 `finish()`，而 `finish()` 会去碰 NULL 的 `pServerConnection`；传 `LogOnStatus::LOGGED_ON` 之类的状态即可让 `isDone_` 为真、绕开这条路径。

方法论（接 7.10）：**新增一个"看起来无害"的测试可能改变链接图，从而引爆早已存在的 ODR 冲突**。本批的教训是给 fixture 接口起名时先扫一遍生产命名空间——`ClientInterface`/`ServerInterface` 这种名字在测试语境下"读起来很自然"，恰恰因为自然才危险；正确做法是像 `TCPChannels*` 那样带模块前缀。另外，`CppUnitLite2` 的 `CHECK_EQUAL` 指针比较陷阱（7.10 记过）在 `appName()` 返回 `const char *` 时又撞上一次，改用 `strcmp(...) == 0`。

提交范围：`test_movement_filter.cpp`、`test_login_challenge_task.cpp`、`test_login_request_protocol.cpp`、`test_filter_interfaces.hpp`（新增：把 `test_filter_helper.cpp` 里的 `TestFilterEnvironment`/`TestFilter` 提到共享头，并追加 `TestMovementFilter` 替身；沿用同目录 `test_channel_interfaces.hpp`/`test_flood_interfaces.hpp` 的 `*_interfaces.hpp` 惯例，避免两个文件各写一份 80 行环境桩）、`test_filter_helper.cpp`（改为包含共享头）、`test_channel_interfaces.hpp` + `test_channel.cpp`（命名空间冲突修复）、`unit_test/Makefile.rules`、`unit_test/CMakeLists.txt`、本文档。

### 7.12 覆盖率补强批次 12：connection 挑战/replay 面 + cstdmf/math/resmgr/db 零覆盖（2026-09-27）

多线并进的批次：lib/connection 侧把 `replay_metadata.cpp` 的用例从 `test_replay_header.cpp` 迁出成独立文件并补 4 个新用例、给 login challenge 工厂面（`login_challenge_factory.cpp`/`cuckoo_cycle_login_challenge_factory.cpp`）补测；另外四个模块各扫出零覆盖文件补测。用例数变化：network 126→139、cstdmf 322→334、math 38→44、resmgr 38→50、db 3→11，全量 21 模块 994→1045。

network_test（宿主仍是 lib/network/unit_test，`dependsOn` 已含 connection）：

- `test_replay_metadata.cpp`（新，7 用例）：`_collection`/`_signedRoundTrip` 自 `test_replay_header.cpp` 迁入（旧文件同步删除该节，否则 registrer 符号 multiple definition），新增 `_rejectsTamperedBlock`（改一个 value 字节、不动任何长度字段，签名必须抓到）、`_unverifiedRead`（`init(signatureLength)` 的不校验读：签名原样交还、内容照读）、`_rejectsMalformedBlock`（头部声称 1000 字节 → "Insufficient data for meta-data"）、`_checkSufficientLength`（静态长度检查：完整块、差一字节、连头都没有三态）、`_swap`。
- `test_login_challenge_factories.cpp`（新，6 用例）+ `test_login_challenge_interfaces.hpp`（新，`TestChallengeConfig` 提为共享 fixture，cuckoo 文件同用）：`_defaultRegistrations`（delay/fail/cuckoo_cycle 三个内建、未知名拒绝）、`_registerAndDeregister`（计数工厂注册/注销/重复注册替换）、`_configuresDelayChallenge`（configure→create→线上 float 往返）、`_rejectsBadDelay`（见缺陷 1）、`_alwaysFailChallenge`（不读流的 true/false 应答对）、`_addWatchers`（每个工厂一个目录节点）。
- `test_cuckoo_cycle_login_challenge.cpp`（新，3 用例）：`Factory_easiness`（setter 钳位 0..100、configure 区间拒绝——见缺陷 2）、`challengeStream`（1 长度字节 + 16 个补零 hex + ':' + uint64 = 26 字节，对端读取后 `writeChallengeToStream` 逐字节还原）、`responseVerifies`（真实解一次 PoW：响应 = key(prefix+attempt) + 42×4B nonce；换了前缀的挑战、改动一个 nonce 的响应都必须拒）。

cstdmf 334（+12）：`test_ansi_allocator.cpp` +4（allocate/reallocate 保内容/allocateAligned 各对齐档位/非 `sizeof(void*)` 倍数对齐拒绝/debugReport·onThreadFinish 空操作）、`test_bw_safe_allocatable.cpp` +2（类 operator new/delete 与 new[]/delete[] 全生命周期计数）、`test_bw_hash.cpp` +4（FNV-1a 64 位向量先用手写 python 核算再写断言；确定性/敏感性/hashCombine/pair 特化）、`test_debug_message_source.cpp` +2（已知源枚举 + 越界回落 "unknown"）。math 44（+6）：`test_linear_lut.cpp`（段内插值与段缓存、data() 按 x 排序、退化表与单点 BC、上下边界条件、访问器）。resmgr 50（+12）：`test_bdiff.cpp` +5、`test_xml_special_chars.cpp` +7。db 11（+8）：`test_db_config.cpp`——`BWConfig::hijack()` 装入内存 XML、经 `db_config.cpp` 的文件级 `pTopLevelConfig` 指针发布自建块（`extern` 引用，析构还原），覆盖读取/缺省/`postInit` 校验（port>65535、空库名拒绝；numConnections/maxSpaceDataSize 零值钳一）、废弃名拒绝、`secondsToTicks` 舍入与下限、`maxCommitPeriodInTicks`、`get()` 单例。

本批修出的真实缺陷（4 处，全部引擎侧）：

1. **`DelayLoginChallengeFactory::configure` 先赋值后校验**：`duration_(config.getDouble(...))` 落进成员之后才判 `<= 0` 拒绝——配置被拒后工厂仍带着 duration=-1 存活，`create()` 产出带毒挑战。修法：读入局部、校验通过才提交。
2. **`CuckooCycleLoginChallengeFactory::configure` 同一模式**：easiness 越界（0 或 >100）被拒时成员已被覆写，easiness=0 意味着客户端要搜遍整个 nonce 空间。同修法。两例均由新用例的第一条"拒绝后应保留前值"断言钉住（`LoginChallengeFactories::configureFactories` 对失败工厂是整员注销，容器层不受毒化——只有直接 configure 的调用方受害）。
3. **`ReplayMetaData::clear()` 漏清 `streamSize_`**（并行会话修，本批复验）：clear 后 `streamSize()` 仍报旧值，而 `addToStream()` 写的是这个陈旧长度——读侧按它取数据必错。`_collection` 用例的 clear 后断言钉住。
4. **`linear_lut.cpp` 在 server 构建里是孤儿**：mak 侧 `lib/math/Makefile.rules` 的 cxxSource 没有它，CMake 侧它被圈在 `IF(NOT BW_IS_SERVER)` 里，只有 Android.mk 编——于是 server 端 `math_test` 链接 `test_linear_lut` 直接 undefined reference。修法：mak 的 cxxSource 加 `linear_lut`（静态库成员选择惰性，server 无人引用则零影响），CMake 把它挪进主源列表，两种构建、两个配置对称。顺带发现 **mak 的 unit_test 链接不做传递闭包**：链接行就是 `--start-group -l<dependsOn 平铺列表> --end-group`，db/unit_test 的 dependsOn 没列 math，`interface_element.o` 引 `EMA::calculateBiasFromNumSamples` 即 undefined（CMake 侧 writer 已列 math）——补 `math` 一行。

本批确立的引擎/协议事实：

1. **测试二进制里 `MemoryIStream` 读越界不是错误标志而是进程 abort**：`memory_stream.ipp` 的 `retrieve()` 用 `IF_NOT_MF_ASSERT_DEV`，dev 断言直通 `LogMsg::linuxAssertAndAbort`（SIGABRT，exit 134）；只有 release 构建才落到 `error_` 标志。推论：**凡是"畸形长度字段→读更多字节"的协议分支（`data >> string` 先读 packed 长度再按它取）在单测里不可测 malformed 路径**——本批为此删掉两处用例内断言（cuckoo `readChallengeFromStream` 的 "ab" 截断流、replay `_rejectsMalformedBlock` 的"块内字符串越界"段），文件内留注释说明。`_rejectsMalformedBlock` 保留的"头部声称 1000"段安全，因为生产在 `readFromStream` 入口就有 `streamSize_ > remainingLength` 前置防护。
2. **`ChecksumScheme` 是有状态对象，`shouldReset=false` 约定下同一 scheme 二次 addToStream 会互相污染签名**：`ReplayMetaData::addToStream` 的 ChecksumOStream 用 `shouldReset=false`（scheme 允许喂更大的流），digest 从上一状态继续——同一 meta 连写两次，第二块的签名是"两段 payload 之和"，单块验证方必拒。本批 `signedRoundTrip` 的"块自带签名"探针因此必须对**孪生对象**跑，不能复用 writer。读侧同源：`readFromStream` 同样不 reset（旧用例已注释过"每次读要新 scheme"）。**待核实线索**：生产 `replay_data_file_reader.cpp:675` 循环读多块时共享同一个 `pChecksumScheme_` 且不 reset（`verifyFromStream` 成功路径也不 reset）——多块文件是否真会撞这颗雷，留给下一批（正是候选清单里的文件）。
3. **`LinearLUT` 下界 BC_WRAP 实际不回绕**（`_lowerBoundaryConditions` 钉住不修）：C `fmod` 保留被除数符号，x<x0 时"回绕"落点仍在 ≤x0，穿第一段 lerp 过去，-2.5 与 -0.5 读值相同；上界 BC_WRAP 是真回绕（2.5→0.5）。另记一次**自查教训**：插值断言的期望值必须按 `(yb-ya)*(x-a)/(b-a)+ya` 重算再写，本批初稿把 slope=4 段的 1.5 处脑算成 5，三连红。
4. **bdiff 缺陷钉而不修**（`bdiff_excessLiteralFlushDropsOneByte`）：溢出字面量收尾把 `lastMatch` 置 `i+1` 而不是 `i`，多吐场景丢一字节；`performDiff` 此时返回 false，调用方须放弃 patch——行为可钉、修复需评估历史 patch 兼容性。
5. **XmlSpecialChars 解析器怪癖钉而不修**（+7 用例）：`&#0;` 截断输出、`&lt` 缺分号也参与折叠等，用例按实际行为断言并逐条注释。
6. **测试配置对象必须用 SmartPointer 持有**：network_test `setCrashOnLeak(true)`，裸 `new TestChallengeConfig` 在进程退出时 abort（与事实 1 同为 134，gdb `bt` 才能分辨）；`LoginChallengeConfigPtr pRoot(pRootConfig)` 收口（`setChild` 只在派生类上，先裸指针装配再入 ptr）。`SafeReferenceCount` 计数从 0 起，禁栈分配（批次 11 已记，本批在工厂用例上再次踩到）。

方法论与并行干扰（本批网络部分被干扰 6 轮才收口）：并行会话以"构建→pkill→跑"循环工作，其 `pkill -f network_test` 按整条命令行匹配，连包装 shell 一起杀；对策是把刚链好的二进制 `cp` 成不含库名字面量的路径（`/tmp/p12nw13`）再跑。空日志 + 134 有两种截然不同的成因（leak 检查 abort vs dev 断言 abort），一律 `gdb -batch -ex run -ex bt` 分辨，不猜。构建中断留 0 字节 .o、并发重链把产物写成半文件（秒退 exit 1）的旧坑本批再验：`rm` 重链 + `make && ls && run` 一条链。archive 竞态（库重编与依赖它的测试重链并发）表现为随机 undefined，串行重链即恢复。**本批中途出现第三方 `git stash -u`**：19 个跟踪文件的改动一度整体消失——`git checkout stash@{0} -- <显式路径清单>` 恢复、不 pop 不 drop（留给发起方），恢复后逐文件 `git diff HEAD --stat` + 关键点 grep 核对。

提交范围：引擎侧 `login_challenge_factory.cpp`、`cuckoo_cycle_login_challenge_factory.cpp`、`replay_metadata.hpp`、`lib/math/Makefile.rules`、`lib/math/CMakeLists.txt`；测试侧 network 的 `test_replay_metadata.cpp`/`test_login_challenge_factories.cpp`/`test_cuckoo_cycle_login_challenge.cpp`/`test_login_challenge_interfaces.hpp`（新）、`test_replay_header.cpp`（删迁出节）、`test_data_download.cpp`/`test_log_on_params.cpp`（§7.10 方法论第 5 条的求值顺序加固）；cstdmf 四个新测试、math 的 `test_linear_lut.cpp`、resmgr 的 `test_bdiff.cpp`/`test_xml_special_chars.cpp`、db 的 `test_db_config.cpp`；五组件 `unit_test/Makefile.rules` + `unit_test/CMakeLists.txt` 双描述登记（db 另补 resmgr/math 依赖）；本文档。`test_pickler.cpp` 与各 `unit_test/xmldataresource.xml`（DataResource 用例的工作目录产物）继续保持 untracked 不入库。

### 7.13 覆盖率补强批次 13：§7.12 replay 线索证伪 + avatar_filter_helper 零覆盖（2026-09-27）

**§7.12 待核实线索的结论：证伪（不是缺陷，是签名链设计）**。`replay_data_file_reader.cpp` 多块读共享 `pChecksumScheme_` 不会污染签名，因为每个验证块读之前都走 `primeChecksumScheme()`：先 `pChecksumScheme_->reset()`，再 `readBlob( lastSignature_ ... )` 把**上一块的签名**回喂进 digest 当种子（`readMetaData()` 与 `readNextChunk()` 两个调用点都如此；`readHeader()` 自己 `reset()`）。即每块签名 = digest(上一块签名, 本块数据)，块与块 chaining，天然防重排/删块。`server/baseapp/unit_test/test_recording.cpp` 的 `ReplayWriter_MultiTickSigning` 早已手工复现过这套 reset+reseed 链并逐块 `verify()` 通过——它是写侧类（`ReplayDataFileWriter` 在 server/baseapp，不在 lib）配套的读写闭环测试，reader 本体由 `ReplayDataFileReader_BasicReading`/`_NoKey` 覆盖。批次12 事实2 的"同 scheme 连写互相污染"只在**绕过 primeChecksumScheme 直接复用 scheme** 时成立（如 `ReplayMetaData::addToStream` 连写两次），生产 reader 路径不在此列。零覆盖清单修正：`replay_data_file_reader.cpp`（1166L）并非零覆盖——baseapp `test_recording.cpp` 已覆盖其主路径，`replay_checksum_scheme.cpp` 同理（EC 密钥夹具也在那个文件里）。

**覆盖率缺口 sweep（本批派测依据）**：对 lib/connection 与 lib/network 逐文件查测试引用，零覆盖大文件为——connection：`avatar_filter_helper.cpp`(833L)、`login_request.cpp`(341L)、`login_request_transport.cpp`(488L)、`server_connection.cpp`(2971L)、`smart_server_connection.cpp`(362L)；network：`machine_guard.cpp`(1481L)、`event_poller.cpp`(1238L)、`logger_endpoint.cpp`(1120L)、`watcher_nub.cpp`(678L)、`packet_sender.cpp`(440L)、`encryption_filter.cpp`(391L)、`tcp_bundle.cpp`(369L) 等。派测 `avatar_filter_helper`：connection 中最大的可测纯逻辑零覆盖文件（其余多依赖真实 nub/TaskManager/EC 密钥链）、server 构建主源列表（mak cxxSource 第 8 行 + CMake 主列表，可被 network_test 链接）、批次11 的 `test_filter_helper.cpp` 模式直接沿用。

**新增 `test_avatar_filter_helper.cpp`（10 用例，network_test 139→149）**：`_initialState`（无 input 时 getLastInput false 且出参不动、-2000 哨兵种子、latency=帧数×0.01、isActive 静态往返）、`_firstInputSeedsHistory`（首输入重播种全环 0.01s 递减、onGround 经环境解析）、`_staleInputIgnored`（早于/等于最新时间戳的输入丢弃、新输入压环首）、`_resetDiscardsHistory`（reset 后 getLastInput false、下个输入重播种而非追加）、`_extractWalksInputLine`（共线历史 waypoint 收敛后插值精确 1.0/1.5/2.0/2.5、速度 1.0、yaw 中点插值、roll 恒 0、space/vehicle 透传）、`_extrapolationStandsStill`（越过最新输入后位置冻结、速度归零、不爬行）、`_inactivePassthrough`（isActive(false) 即 DumbFilter：任意时间返回最新输入、零速）、`_outputLatencyController`（稳态 1s 输入流下 ideal=2.5s 精确推导：锚点 105 = 101..107 的 5/7 处；首调 dTime 巨大一步贴满 ideal，outputTime=105.0；收敛后延迟保持、提取点 1:1 前滑）、`_resetKeepsStaleIdealLatency`（钉住，见下）、`_copyIsIndependent`（拷贝构造/赋值全环复制、副本新输入不回写源、自赋值 no-op）。

**引擎事实与钉住不修**：

1. **reset() 清 `latency_` 不清 `idealLatency_`（钉住不修）**：`resetStoredInputs()` 重置 latency_ 但 idealLatency_ 保留旧值，而 `output()` 只在 `inputCount_ ≥ 2` 时重算 ideal（`maxLatencyFrame = min(inputCount_,8)-1 > 0` 才进）——复位后首个 output 仍按陈旧 ideal 拉延迟（实测 0.52s 而非 0.1s 下限，`_resetKeepsStaleIdealLatency` 钉住）。修复涉及传送/切空间后客户端移动手感的回归评估，超出测试批次范围；调用方（AvatarFilter）如需干净状态应在 reset 后自行处理或后续单独修。
2. **`Direction3D( const Vector3 & )` 的分量顺序是 (roll, pitch, yaw)**（`network/basictypes.hpp:242`）——本批夹具按 (yaw,pitch,roll) 写，当场红两处（0.75 vs 0.9、1.5 vs 0.9），修夹具非引擎。与批次11 `test_filter_interfaces.hpp` 的用法一致，复用时先看定义。
3. **航点时间永不碰撞的排序不变量**：chooseNextWaypoint 只在请求时间越过当前头 waypoint 时触发，且输入时间戳严格递增保证 nextInput.time_ > current.time_，故 extract 的插值比例分母恒非零、proportion ∈ [0,1]，无除零/外插 NaN 路径（手工推演 + `_extractWalksInputLine`/`_extrapolationStandsStill` 钉住）。
4. **`getStoredInput` 只有 const 重载是 public**（非 const 版 protected）——测试检视一律经 const 引用；`Vector3` 无 `fuzzyEqual`；CppUnitLite2 的 CHECK 宏只能在 TEST 体内展开（依赖 result_/m_name 成员），共享断言须改为返回差异描述的普通函数。
5. **`AvatarFilterSettings` 四个静态 + `s_isActive_` 是进程级全局**：测试用 RAII 保存/恢复，防止用例顺序耦合；watcher 由文件级 `s_avf_initer` 静态注册（"Client Settings/Filters/…"），无重名冲突。

方法论：`timeout 560` 差点误伤整模块跑（批次12 基线 334s，机器忙时 >10 分钟，且 stdout 全缓冲零字节日志无从判断进度）——长跑一律后台无 timeout + `ps -C <bin>` 看 CPU TIME 确认在推进，跑完读 exit 文件。pkill 规避沿用批次12：二进制先 `cp` 成无库名字面量路径（`/tmp/p13afh2`）再跑。

提交范围：`lib/network/unit_test/test_avatar_filter_helper.cpp`（新）、`lib/network/unit_test/Makefile.rules` + `CMakeLists.txt`（双描述登记）、本文档。用例数：network 139→149；全量 1045→1055。`test_pickler.cpp` 与各 `unit_test/xmldataresource.xml` 继续保持 untracked 不入库。

### 7.14 覆盖率补强批次 14：encryption_filter 流式加密面 + 零覆盖清单可测性判定（2026-09-27）

**清单逐项可测性判定**（§7.13 末尾的"剩余零覆盖大文件"）：

| 文件 | 行数 | 判定 | 理由 |
|---|---|---|---|
| `lib/network/encryption_filter.cpp` | 391 | **本批派测** | 流式 API（encryptStream/decryptStream）公开且仅依赖 BlockCipherPtr，密码链逻辑可用恒等密码精确验证 |
| `lib/connection/login_request.cpp` + `login_request_transport.cpp` | 829 | 不可单测，登记 | 需真实 TaskManager/Channel/NetworkInterface 依赖链（批次11 已登记同类） |
| `lib/network/machine_guard.cpp` | 1481 | 不可单测，登记 | machined 守护进程面（fork/信号/进程间注册） |
| `lib/network/event_poller.cpp` | 1238 | 不可单测，登记 | epoll/kqueue/select 事件循环，需真实 fd 生态 |
| `lib/network/logger_endpoint.cpp` | 1120 | 不可单测，登记 | 日志守护 socket 协议端 |
| `lib/network/watcher_nub.cpp` | 678 | 不可单测，登记 | watcher UDP nub，需 NetworkInterface |
| `lib/connection/server_connection.cpp` / `smart_server_connection.cpp` | 3333 | 不可单测，登记 | 需完整 Mercury nub + 接口注册栈（批次11 记过链接陷阱） |
| `lib/network/file_stream.cpp` | 354 | 可测，**下批候选** | 自足 FILE* 包装（FileStream: MemoryOStream + open/commit/seek/stat/remove + 静态打开文件登记表），纯 I/O 逻辑含量较低故让位本批 |

**派测文件的源码分析**（断言全部由此推导，不凑数）：

*密码链（`encryption_filter.cpp:266-344` encrypt、`:350-386` decrypt）*：无 IV 的类 CBC。`combineBlocks` 是逐字节 XOR（`block_cipher.ipp`，x86_64 有 8 字节字快速路径）。加密第 i 块：先 `combineBlocks( src+i, pPrevBlock, dest+i )`（与**明文**上一块异或），再 `encryptBlock` 原地加密——即 **C₀ = E(P₀)，Cᵢ = E(Pᵢ ⊕ Pᵢ₋₁)**；首块 memcpy 直拷。解密严格逆链：`decryptBlock` 后再与已解出的**明文**上一块异或，in-place 安全（prev 已还原）。注释明说链的用途：防不同包密文块重组重放。推论（协议事实，记录不修）：首块无链保护，同 key 同首块内容密文可预测（ECB 弱点仍在 C₀）。

*分帧（`send()` :51-113、`recv()` :119-201）*：尾部追加 `ENCRYPTION_MAGIC = 0xdeadbeef`(4B) + wastage(1B)；`wastage = ((BS - ((len+1) % BS)) % BS) + 1`，len = payload+4——保证加密总长为 BS 整数倍且 wastage ≥ 1（永不写覆原始数据）。整段加密进**新包**（原包不动）。`recv()` 解密 in-place（注释：对 Blowfish 安全，其他算法未必），四个 corrupted 出口：非整块长度 / magic 不符 / wastage > BS / footer 越界，均走 `stats().incCorruptedPackets()` + `REASON_CORRUPTED_PACKET`；通过后 `shrink( footerSize )` 交回 `PacketFilter::recv` → `receiver.processFilteredPacket`。空 key 两侧直接 `REASON_GENERAL_NETWORK`。

*可测性边界*：`PacketSender`/`PacketReceiver` 是**非虚具体类**（`packet_sender.hpp:36` 构造需 Endpoint/RequestManager/EventDispatcher/OnceOffSender/SendingStats/PacketLossParameters 六个引用；`packet_receiver.hpp:41` 需 Endpoint/NetworkInterface），无法桩替——**send/recv 包级分帧路径（含 4 个 corrupted 出口）本批不可测，登记**，留待 Mercury nub 级集成测试。

**新增 `test_encryption_filter.cpp`（6 用例，network_test 149→155）**：`_streamRoundTrip`（Blowfish 20B→24B 补齐、明文流原地增长至块边界、解密还原且 4 字节零填充尾精确验证）、`_streamRoundTripExactMultiple`（16B 整块无补齐往返）、`_nullCipherRevealsChain`（NullCipher(8) 恒等密码把链算术摊开：C₀=P₀ 逐字节、C₁=P₁⊕P₀ 逐字节、解密折回；**NullCipher 空 key 只封包级 API 不封流 API**）、`_decryptStreamRejectsNonMultiple`（10B 密文拒解；reserve 先于 decrypt、失败路径输出流仍增长——事实钉）、`_blockRearrangementBreaksChain`（交换两密文块：首块独立还原、次块变垃圾——链属性钉）、`_maxSpareSize`（= BS + 4B magic + 200B MTU 余量：Blowfish 212、NullCipher(16) 220）。

**本批修出的真实缺陷（1 处，由既有用例的偶发失败暴露）**：`cuckoo_cycle_login_challenge_factory.cpp:362` 的挑战前缀十六进制位宽写错——`stream.width( sizeof( prefixValue ) / 8 * 2 )` 实际求值 8/8×2 = **2** 而非 16，`fill('0')`/`std::right` 的补零装置形同虚设：随机 uint64 顶层半字节为 0 时（概率 1/16）前缀只有 15 个 hex 位，线上前缀在 17/16 字节间抖动（整包 26/25 字节）。批次12 的 `CuckooCycleLoginChallenge_challengeStream` 断言 26 字节 + 16 hex + ':'@17，因此带着 1/16 的隐发失败率跑了两个批次才踩中。修法：`stream.width( sizeof( prefixValue ) * 2 )`（16 位十六进制恒定）。读侧 `>> BW::string` 按打包长度读，新旧格式互通；`verify` 的 `key.find( prefix_ )` 不受长度影响。既有用例即回归钉。

**链自愈语义（源码分析修正本批初稿断言）**：交换两个密文块后解密 out₀ = D(C₁) = P₁⊕P₀（打花），但 out₁ = D(C₀)⊕out₀ = P₀⊕P₁⊕P₀ = **P₁ 精确复原**——XOR 链只打花移动区域的**首块**，随后逐块自愈；N 块场景下交换 C₀/C₁ 只毁前两块且仅 out₀ 变垃圾。因此**排列检测实际由 recv() 的尾部 magic 落位检查完成**（任何块移动都会让 0xdeadbeef 错位→corrupted），链本身的贡献是"保证至少打花头部"。`_blockRearrangementBreaksChain` 按此精确断言（首块 ≠P₀、次块 =P₁、整体 ≠明文），初稿"次块变垃圾"的想当然已修正。

**引擎事实**：`SymmetricBlockCipher::BLOCK_SIZE = 64bit = 8B`（静态断言保证，`symmetric_block_cipher.cpp:97`）；`BlockCipher::Key = BW::string`；`readableKey()` 静态 1024B 缓冲返回十六进制串（非线程安全，测试未用仅记录）。

**覆盖率注记**：本批未跑 §7 基线法插桩（删 obj → `user_shouldBuildCodeCoverage=1` 全量重建 → gcovr → 恢复重建，需数小时），覆盖率变化按测试面如实描述：`encryption_filter.cpp` 的流式 API（encryptStream/decryptStream/maxSpareSize/key + Blowfish 与 NullCipher 两条密码链、非整块拒绝路径）由本批 6 用例从零覆盖变为已覆盖；包级 send/recv 分帧（4 个 corrupted 出口）因 PacketSender/PacketReceiver 不可桩替仍为零覆盖（登记见上）。工具链钩子确认存在：`common_footer_config.mak:357` 读 `user_shouldBuildCodeCoverage`（全 mak 树无内置赋值，纯用户 make 变量）。

方法论：沿用批次12/13 的二进制改名跑法与后台无 timeout 套路，无新坑。另：本批窗口内按用户指示完成 logo 替换（根 `logo.svg` + `docs/public/logo.svg` 替换原 2048×1024 PNG，README 与 VitePress config 同步改引），独立成提交、不与测试批次混合。

提交范围：`lib/network/unit_test/test_encryption_filter.cpp`（新）、`lib/connection/cuckoo_cycle_login_challenge_factory.cpp`（位宽缺陷修复）、`lib/network/unit_test/Makefile.rules` + `CMakeLists.txt`、本文档；logo 提交另列。用例数：network 149→155；全量 1055→1061。

### 7.15 覆盖率补强批次 15：file_stream 零覆盖（2026-09-27）

**派测文件**：`lib/network/file_stream.cpp`（354L + 头 89L + ipp），批次14 判定表点名的下批候选。生产用户是 message_logger 三件套与 bwmachined（`server/tools/`），本身编入 libnetwork（`Makefile.rules:32`），network_test 天然可链。无任何既有测试引用（`grep` unit_test 全目录零命中），确认零覆盖。

**可测性判定：可测**。自足 FILE* 包装（构造即 fopen、析构即 close+提交），无 nub/TaskManager 依赖；静态 LRU 句柄表虽是 protected，但 C++ 允许派生类访问基类 protected 静态成员——测试用 `ProbeFileStream : public FileStream` 的静态访问器（`openFileCount()`/`maxOpenFiles()`）精确观察驱逐行为，不用桩、不碰生产代码。

**源码分析**（断言全部由此推导）：

*双面流语义*：`FileStream : public MemoryOStream`——写路径是内存流（`addBlob` 进缓冲，`commit()` 才 `fwrite`+`fflush` 并 `reset()` 清缓冲），读路径是**磁盘流**（`retrieve(int)` 是虚覆盖，从 FILE* `fread` 进独立读缓冲，与内存缓冲无关）。`length()` 明确只报磁盘尺寸、不计未提交内存（头文件 119-121 行注释自述）——用例钉住"commit 前 length()==0、补写后仍不变"。

*LRU 句柄管理（`open()` :261-311）*：进程级 `s_openFiles_` 链表，`MAX_OPEN_FILES=20`；三条分支——队首快路径直接返回、已开但不在队首先 `remove()` 再重压队首（:271-275）、真开（fopen + 恢复 `offset_` :289-297）+ 压队首 + 超限关掉队尾（:305-308）。**失败路径不入队**（fopen 失败 return 先于 push_front，:280-284）——用例钉住计数不变。20 个洪水流恰好把最老流驱逐（第 20 次压入时 size 21>20 触发关闭 back），驱逐流下次 `retrieve` 透明重开并 `fseek` 恢复原位置——"rb" 流位置无损续读是设计语义，用例精确钉住（40B 文件读 10B → 驱逐 → 再读得 [10,20) 字节）。

*错误语义（`strerror()` :52-62）*：errno 非零走 libc 串、零走 `errorMsg_`。`retrieve()` 在 fread 前 `errno=0`，短读（fread 因 EOF 返短）不设 errno → `strerror()` 精确回落到源码自带的 "Couldn't read desired number of bytes from disk"——用例钉住该确切串（这依赖"短读不设 errno"的 libc 事实，与源码的 errno=0 前置共同成立）。

*风险注记（源码推论，未断言）*：真开分支用保存的 `mode_` 再 fopen——"wb" 流被驱逐后重开会**先截断文件**再恢复 offset（fopen "w" 语义），写型句柄跨驱逐有丢数据面；message_logger 实际用法（追加/读日志）不踩此路。记录不修。

**新增 `test_file_stream.cpp`（10 用例，network_test 155→165）**：`_commitWritesToDisk`（commit 前后 length/size 与磁盘内容逐字节）、`_destructorAutoCommits`（close 对非空缓冲自动提交，:324-327）、`_readBackGrowsReadBuf`（INIT_READ_BUF_SIZE=128 → retrieve(200) 精确扩到 200）、`_shortReadFlagsError`（error/good 翻转 + strerror 确切串）、`_tellSeekLengthStat`（seek/tell/SEEK_END/未提交不计长/stat 一致）、`_openFailureFlagsError`（坏路径：tell/seek/length/stat 全 -1、retrieve 返回非 NULL 读缓冲、失败不入队）、`_lruQueueCap`（25 流恒 ≤20、销毁全清零）、`_lruEvictionRestoresPosition`（驱逐后续读位置精确恢复）、`_alternatingStreamsStayDistinct`（双流交错读写各归各，驱动 remove-and-repush 分支）、`_writeThenReadSameStream`（"w+b" 单句柄 commit→seek(0)→读回，驱动 setMode 的 ANSI 读写交错 fseek 分支 :246-250）。

**陷阱复用**：`INIT_READ_BUF_SIZE`/`MAX_OPEN_FILES` 均无类外定义——按批次10 教训先拷局部再 `CHECK_EQUAL`（避免 ODR-use 链接失败）；临时文件 `/tmp/fs_test_<pid>_<tag>.bin` 进测先清、出测必清，pid 防并行会话撞车。

**覆盖率注记**：同批次14 口径，未跑插桩基线（需数小时全量重建）。`file_stream.cpp` 全部 11 个方法（ctor/dtor/strerror/tell/seek/length/commit/retrieve/stat/setMode/open/close/remove）+ ipp 的 good/error 由 10 用例从零覆盖变为已覆盖；仅 commit 的 fwrite 短写失败臂（需盘满注入）未覆盖，登记。用例数：network 155→165；全量 1061→1071（21 模块全绿）。

提交范围：`lib/network/unit_test/test_file_stream.cpp`（新）、`lib/network/unit_test/Makefile.rules` + `CMakeLists.txt`、本文档。
### 7.16 覆盖率补强批次 16：tcp_bundle 线格式整层 + login_request 二次可测性判定（2026-09-27）

**巡检派发与判定**：批次14 判定表点名的两候选 `lib/network/tcp_bundle.cpp`（369L）与 `lib/connection/login_request.cpp`（341L）做二次可测性细查：

- **login_request：登记不可测**（理由同批次14 判定表口径，无新发现可翻案）。纯协议面可测部分（time 预算算术、finish 生命周期、pChannel wiring）极薄；全部失败路径——`onTransportConnect` 超时臂、`onTransportFailed`、`handleException`、`onChannelGone`——落到**非虚** `LoginRequestProtocol::onAttemptFailed`（login_request_protocol.cpp:72-86）→ `loginHandler.onRequestFailed`，冒烟必须先有真 `LoginHandler` 实例，而 `pProtocol_` 是 private、仅 `start()` 设置，`start()` 一调用即引爆 `makeRequest`/`sendNextRequest` 全套真实机制（要真 transport、真 protocol 应答流）。`#define private public` 无仓库先例，弃。
- **tcp_bundle：派测**。整层 TCP 线格式编码器此前零直接断言（`test_compresslength.cpp` 只测 UDPBundle 侧的 compressLength 契约），纯编码面最大。

**源码分析核心事实（含首跑失败自证的端序发现）**：

1. **BigWorld 线序=小端**（binary_stream.ipp:100-103 注释自述："BigWorld uses little-endian over the network"；BW_HTONS/BW_HTONL 仅 `_BIG_ENDIAN` 构建做字节交换，x86 即恒等宏）。因此 compressLength 的 width2/width4 "HTONS/HTONL" 字段实测按小端落盘（258→字节 02 01；4→04 00 00 00）。**本批首跑 25 失败中的"请求 offset 槽谜团"根因即此**：把 BW_HTONL 当标准大端网络序、用 NBO 读回必然不符（offset 5 落盘 05 00 00 00，NBO 读回 0x05000000）。
2. TCPBundle 帧几何：ctor 先 `reserve(Flags=1)` 再 `reserve(Offset=4)` → frame size 5、`frameStartOffset_=4`；`size() = frame + msg - frameStartOffset_` → 新鲜 bundle size()==1（4B 请求偏移保留头对外隐形）；`data() = frame + frameStartOffset_` 是唯一合法观察窗（pFrameData_/frameStartOffset_ 均 private，派生类不可达）。
3. 非请求帧 wire = `[flags][id][len?][payload]`，size()=1+msg 总长——**flags 字节计入 size()**（首跑多数失败源于漏计此字节）。
4. compressLength（interface_element.cpp:283-409）：FIXED 长度不符走 CRITICAL_MSG abort（进程级中止，不可测臂）；VARIABLE width1 内联 `len<0xff`（254 为内联上限）/ width2 / width3 BW_PACK3 / width4；oversize → 0xff 填充 + 返 -1；负长返 -1（长度由流尺寸推导恒 ≥0，不可达）。
5. 逃逸路径（finaliseCurrentMessage :255-275）：compressLength 返 -1 → transfer(id+len 字段) + `*pFrameData_ << uint32(length)`（裸 operator<< 主机序写，与内联字段同为小端、只在 BE 主机上显示差异）+ transfer 余下。
6. 请求链（startRequest :71-115 / setNextRequestOffset :123-152 / doFinalise :281-302）：startRequest 把 frameStartOffset_ 4→0（flags 字节自 frame[4] 下移到 frame[0]，原保留头位置被首个请求 offset 覆写）、置 FLAG_HAS_REQUESTS(0x01)；offset=size()（首请求=5）；首请求 offset 写 frame[1..4]，后续第 N 个写第 N-1 个请求的 replyIDOffset+4 处（覆写 -1 占位）；每请求 msg=[id][len][ReplyID 0x12345678 占位 4B][nextOffset 0xFFFFFFFF 占位 4B][payload]，payload 长度扣 8（currentMessagePayloadLength :327-330）；doFinalise 经 ReplyIDOrOffset union hack 把存储的偏移解析为 frame 基址真指针（防 MemoryOStream 重分配使指针失效）。
7. startReply：REPLY 元素=("Reply", 0xFF, VARIABLE, 4) → `[flags][FF][u32 4 小端][replyID 4B host]`。

**环境工程**：TCPBundle 构造即需真 TCPChannel；`maxSegmentSize()` 活读 TCP_INFO 的 snd_mss——未连接 socket mss=0 会让 numDataUnits/freeBytesInLastDataUnit 对 0 取模（除零臂，只能以已建立连接测）。环回对搭建：listener socket/bind(0)/listen(5) + 阻塞 connect/accept（无需泵 dispatcher），服务端 `TCPChannel( iface, *pAccepted, true )`。生命周期：~Channel MF_ASSERT(isDestroyed_) → 必须 `destroy()`（decRef→0→delete，且 channel 析构接管 accepted endpoint 所有权）；rig 析构按 pChannel_ 是否生成分叉释放。

**新增 `test_tcp_bundle.cpp`（13 用例，network_test 164→177 实测 "177 tests run"）**：`_freshState`（size()==1 隐形保留头+flags 字节）、`_fixedLengthMessageFrame`（FIXED 无长度字段）、`_variableLengthWidth1`（[id][len 1B][payload]）、`_variableLengthWidth1Boundary`（254=内联上限）、`_variableLengthWidth2WireOrder`（258→02 01，钉死小端）、`_variableLengthWidth3Pack3`（300→2C 01 00，钉死 BW_PACK3 臂）、`_oversizeEscape`（300B→[id][FF][u32 300][payload]，size 307）、`_startReplyFrame`、`_firstRequestFrame`（offset 槽=5、ReplyID 占位、链尾 sentinel、doFinalise 解析 pReplyID==frame+7）、`_requestChain`（offset 链 5→17、占位覆写、双 ReplyOrder 位置 7/19）、`_clearResets`（重建帧复用）、`_numDataUnitsArithmetic`（真 mss：恰好一段 / +1 滚两段；numDataUnits/freeBytesInLastDataUnit 首次直接断言）。

**迭代取证实录**：首跑 25 失败全部为测试侧断言算术错（生产代码零缺陷），报错值可逐字节解码自证：77055=0x00012CFF=FF 2C 01 00（0xff 逃逸 + LE 300）、67108864=0x04000000（LE 4 被 NBO 误读）、width2 首跑"缺失"的那行恰证 wire[2]==0x02（LE 低位在前）。两处系统性错因：非请求帧漏计 flags 字节（+1 偏移）、把 BW_HTONL 当真大端序。

**陷阱复用与新增**：rig 构造器内不放 CHECK 宏（CppUnitLite2 宏只在 TEST 体内展开），改守卫链 + `isCreated()` 每用例首行断言；FLAG_HAS_REQUESTS 无类外定义，先拷局部再 CHECK_EQUAL（批次10 教训）；**新增**：`CHECK_EQUAL( uint8, uint8 )` 按 char 打印字节——0x11/0x00 等控制字节在日志里显示为两个相同的空串，误导排查，一律 `int(...)` 转型后再断言（fixed/fresh 两处字节断言即曾如此）。

**覆盖率注记**：同批次14/15 口径，未跑插桩基线。tcp_bundle.cpp 全部方法（ctor/dtor/startMessage/startRequest/setNextRequestOffset/startReply/finaliseCurrentMessage/doFinalise/clear/size/reserve/data/flags/newMessage/currentMessagePayloadLength/tcpChannel）+ compressLength 的 width1/2/3/4/oversize 臂由 13 用例直接覆盖；登记不可测臂：FIXED 长度不符 CRITICAL abort（进程终止）、compressLength 负长臂（长度恒 ≥0）、width 越界 default 臂（CRITICAL abort）、未连接 socket 的 mss=0 除零臂。用例数：network_test 164→177（实测 "177 tests run"）；全量 21 模块全绿、实测合计 **1083**（此前的"全量 1071"计数含同一 ±1 漂移——network 基线实为 164 而非 165，本批起以实测 run 计数为准）。

提交范围：`lib/network/unit_test/test_tcp_bundle.cpp`（新）、`lib/network/unit_test/Makefile.rules` + `CMakeLists.txt`、本文档。
### 7.17 覆盖率补强批次 17：machine_guard MGM 编解码面（2026-09-27）

**派测判定（批次14 口径，五候选逐个）**：

- `machine_guard.cpp`（1481L）：**可测，本批派测**。MGMPacket/MachineGuardMessage 家族是纯流编解码（wire 布局、首字节分派、错误回退），零 nub/TaskManager/EC 依赖；sendto/sendAndRecv 网络臂与 bwmachined 进程管理臂（fork/exec/signal，`machined` 专属）不硬凑。
- `event_poller.cpp`（1238L）：**可测（下批候选）**。`EventPoller::create()` 无参工厂 + fd 注册表（registerRead/WriteFileDescriptor 只需 fd + InputNotificationHandler 接口实现），maxFD 算术可直测。
- `logger_endpoint.cpp`（1120L）：**受限登记**。LoggerEndpoint 需要 LoggerMessageForwarder（MessageLogger 消息面）+ TCP 端点状态机 + dispatcher 定时器臂，冒烟必须真消息日志管线。
- `watcher_nub.cpp`（678L）：**受限登记**。WatcherNub 无参构造但 init/收包走 UDP 绑定 machined 标准端口 + watcher 协议路由，协议面窄。
- `packet_sender.cpp`（440L）：**受限登记**。六件套构造（Endpoint/RequestManager/OnceOffSender/SendingStats/PacketLossParameters/EventDispatcher），send() 主体需 UDPBundle/UDPChannel 协同。

**源码分析核心事实**：

1. MGMPacket wire = `[flags(1B)][buddy(u32 LE)]` + 每消息 `[len(u16 LE)]+[消息体]`（BW_HTONS 在 x86 恒等，批次16 端序口径）；write 超 32KB 返 false（碎片化未实现的告警臂）；`s_buddy_` 是进程级静态，非 BROADCAST 时覆写所有 write 的 buddy 字段。
2. `MachineGuardMessage::create` peek 首字节分派 16 种具体消息 + UnknownMessage 兜底；peek 空流安全置 error → 返 NULL（MemoryIStream::peek 无断言，与 retrieve 的 MF_ASSERT_DEV 越界 abort 不同——**这是"空流可测、截断体不可测"的分界**）。
3. 基类头 = `[message(1B)][flags(1B)][seq(u16)]`，seq_ 是 **private**（读回对象只能经 write 回观察——read 侧 seqSent_ 为 false，write 原样发出；第二次 write 走 refreshSeq 生产路径）；wire flags 含 MESSAGE_NOT_UNDERSTOOD(0x2)/OUTGOING(0x1)。
4. UnknownMessage：body 原样吞进 data_，write 回 = 头 + appendString（writePackedInt(\<255 为 1B) + blob），即"echo + 长度前缀 + NOT_UNDERSTOOD 置位"。
5. ProcessMessage 读段 = 头 + param/category/uid(均 1B)+uid(u16)+pid/port/id(u16)+name(packed string)，extra 前向兼容段（extraData 计数 + 未知字节 skip）；**构造默认 username_(getUsername())/pid_(getpid())**，legacy 无 extra body 不触碰这些默认。
6. typeStr() switch **缺 ERROR_MESSAGE case** → ErrorMessage 报 "** UNKNOWN **"（日志可读性小疵，记录不修）；readExtra 的 `is.retrieve(extraData)` 遇恶意 extraData 会触发 retrieve 断言 abort——单测不可造此畸形。

**新增 `test_machine_guard.cpp`（16 用例，network_test 177→193）**：`MGM_packetEmptyRoundTrip`（5B 头+stagger 旗标）、`_packetMessageRoundTrip`（QIM 往返）、`_packetLengthPrefixLittleEndian`、`_packetTruncatedTailFlagsError`（hasError_ 显式臂）、`_packetZeroLenMessageDropped`（空 msgstream→create NULL→静默丢弃）、`_packetSetBuddyOverrides`（静态覆写+恢复 BROADCAST）、`_packetOversizeReturnsFalse`（33000B→write false，33016B 逐字节账）、`MGM_createByFirstByteDispatch`（手工 wire→QIM，write 回观察 seq）、`_createUnknownEchoes`（NOT_UNDERSTOOD+packed-len echo）、`_createEmptyStreamReturnsNull`、`_queryInterfaceDefaults`（typeStr 映射）、`_processMessageFullRoundTrip`（含 extra 段与 c_str 格式串）、`_processMessageLegacyBodyWithoutExtra`（readExtra remaining==0 臂+默认保留）、`_errorMessageRoundTrip`、`_refreshSeqAdvances`（两次 write 观察刷新）、`_typeStrFallbacks`。

**陷阱复用与新增**：字节断言一律 `int()` 转型（批次16）；手工 wire 的字节数值按 LE 语义解读（[01][02] 读回 0x0201 而非 0x0102，首跑 2 失败之一）；类内 enum（PACKET_STAGGER_REPLIES 等）无 ODR 问题，static const MAX_SIZE 未用；MF_USE_ASSERTS 下 retrieve 越界=abort，"截断 body 回退 UnknownMessage"臂（create 的 is.error() 分支）在单测构建不可达，登记；进程级静态（s_buddy_）用后必须恢复，防毒化后续用例。

**覆盖率注记**：同前批口径，未跑插桩。MGMPacket 全方法（read/write/append/shouldStaggerReply/hasError/dtor/setBuddy）+ MachineGuardMessage 基类流读写/refreshSeq/create 两入口/typeStr/c_str + QueryInterface/Process/ErrorMessage 与 UnknownMessage 的 impl/extra 臂由 16 用例直接覆盖；登记不可测臂：create 截断回退（retrieve 断言 abort 在先）、readExtra 恶意 extraData、sendto/sendAndRecv 真网络臂、machined 进程管理面。用例数：network_test 177→193（实测 "193 tests run" 无失败）；全量 21 模块实测合计见提交信息（批次16 实测 1083 + 16 = 1099）。

提交范围：`lib/network/unit_test/test_machine_guard.cpp`（新）、`lib/network/unit_test/Makefile.rules` + `CMakeLists.txt`、本文档。
