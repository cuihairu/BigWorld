# BigWorld 内嵌 Python 2.7.3 → 3.13.15 迁移

> 任务：把 BigWorld 集成的 Python 从 2.7.3 升级到 3.13.15（源码树整换 + 补丁重移植 + C-API/脚本全量迁移）。
> 分支：dev（所有提交仅在 dev）。

## 1. 基线事实

- 原内嵌解释器：`programming/bigworld/third_party/python` = CPython 2.7.3 源码树（README、configure.ac 佐证），git 跟踪 4480 个文件。
- BigWorld 对 CPython 的魔改清单：`third_party/python/bw_changes.txt`（2.7 版，7 类补丁）。
- C++ 绑定面：62 个文件直接 `#include <Python.h>`；`PyString_*` 428 处、`PyInt_*` 197 处、`PyClass_/PyCObject_/PyInstance_` 10 处。
- 服务端/工具脚本是 Python 2 语法（含构建工具脚本 `build/make/platform_info.py` 等）。
- 引擎侧对 BWHooks 的实际使用：`lib/pyscript/script.cpp` 只注册 `ignoreAllocsBegin/End` 两个钩子，malloc/free/realloc 钩子在源码中被注释（`script.cpp:352` 附近）。

## 2. 补丁处置表（2.7 → 3.13）

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
