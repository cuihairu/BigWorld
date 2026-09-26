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
