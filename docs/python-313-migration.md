# BigWorld 内嵌 Python 2.7.3 [3.13.15 迁移；2026-10-12 升 3.13.16]

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
- `build/make/Makefile`：`third_party_python_modules.mak` 重新启用——包已整体换成 Py3 兼容版本（Twisted 24.7 / SQLAlchemy 1.4.54 / zope.interface 7 / Pympler 1.0 / pika 1.3.2 / msgpack 1.1.0，另含 setuptools、typing_extensions、attrs、constantly、incremental、Automat、hyperlink、idna），安装目标改为 `game/res/bigworld/scripts/{common,server_common}/site-packages`；验收脚本 `scripts/python-modules-smoke-import.py`（tarball 源树 + 安装后目录两相 import 冒烟）。oursql 无 Py3 移植、目标暂缓（见剩余风险 R2）
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
| R2 | `third_party/python_modules` 全部为 Py2 生态包（Twisted 11、SQLAlchemy 0.6、Zope 2.11、oursql 等） | 集群监控/日志工具等运行时 Python 服务不可用 | **已解决**：14 个包换成 Py3 兼容版（Twisted 24.7 / SQLAlchemy 1.4.54 / zope.interface 7 / Pympler 1.0 / pika 1.3.2 / msgpack 1.1.0 + setuptools / typing_extensions / attrs / constantly / incremental / Automat / hyperlink / idna），`Makefile:351` 的 include 已恢复启用。遗留子项：oursql 是 Py2 C 扩展、无 Py3 移植，需另选 Py3 MySQL 连接器后才能恢复其目标；`Imaging-1.1.7`/`jpegsrc.v6b` 是 C 扩展树，服务 Windows `tools/` 轨，不在本次替换范围 |
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
| 8 | 安全补丁升级 3.13.15 → 3.13.16（3.13 系列安全维护版本；vendored 树按 v3.13.16 git tag 重同步，裁剪面/行尾约定保持，Makefile.pre.in 与 _hashopenssl.c 补丁原样重放） | `make libbwpython3.13` 全链通过 + 全量单测 21 模块全绿 | 见本次提交（dev） |
| 9 | `third_party/python_modules` 整体换 Py3 兼容版本并重新启用安装规则（R2 收尾）：删 7 个 Py2 包，vendor 14 个 Py3 源（Twisted 24.7 / SQLAlchemy 1.4.54 / zope.interface 7 / Pympler 1.0 / pika 1.3.2 / msgpack 1.1.0 + setuptools / typing_extensions / attrs / constantly / incremental / Automat / hyperlink / idna）；`third_party_python_modules.mak` 重写为每包单规则（tar 解包 → cp 到 site-packages → .pth 标记）；`Makefile:351` include 恢复启用；新增验收脚本 `scripts/python-modules-smoke-import.py` | ① 14 个包 tar 解包后逐一 import 全通过；② `make -rR python_*` 安装后 `game/res/bigworld/scripts/{common,server_common}/site-packages` 下 15 个模块逐一 import 全通过（内嵌 3.13.16）；③ 全量单测 21 模块 / 1355 用例 / 0 失败 | 见本次提交（dev） |
| 10 | vendored CPython 3.13 的 CMake 样板 `cmake/samples/python`（CMake 轨前置，见 vcpkg-migration §3.4）：`ExternalProject` 包住 configure+make（选项逐字取自 `third_party_python.mak`），只出 `libpython3.13.a`，拷 `Lib/` 作 stdlib，导出 `BWPython::Python`（GLOBAL），构建门禁核对 8 个 BWHooks 符号 | ① `scripts/build-python-cmake.sh` 构建通过（`libpython3.13.a` 66M）；② `check_symbols.cmake` 确认 8 个 `BW_Py_*` 符号齐全、归档含 `bwhooks.o`；③ 作为 entitydef CMake 样板的子目录被实际拉起并跑出 318 用例全绿 | 见本次提交（dev） |

---

*命中的 C-API 统计与补丁清单复核于 2026-09-24；CPython 源码 tag v3.13.15。2026-10-12 升至 tag v3.13.16（3.13 系列安全维护版，上游 .15→.16 无 C-API 变更面），vendored 树与 v3.13.16 git tag 内容逐一核对（EOL 不敏感比对 0 mismatch），BW 补丁集仅 2 文件且原样重放成功；上游本版删除了 Lib/test/memory_watchdog.py、Lib/idlelib/idle_test/test_zzdummy_user.py、Misc/mypy 部分 mypy 存根，vendored 树随之同步。*

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

### 7.18 覆盖率补强批次 18：全量覆盖率实跑 + event_poller 轮询器面（2026-09-28）

**本批首次实跑插桩覆盖率**（此前批次注记均为"未跑插桩"）：`user_shouldBuildCodeCoverage=1` 强制 `rm -rf obj` 全量重编 21 模块测试并跑完（496 个 .gcda），gcovr 8.6 聚合。任务模板中"ctest 全绿、覆盖率门禁 RC=0"为模板化措辞——本仓无 ctest、无独立覆盖率门禁脚本；等效口径 = `bw-run-all-unit-tests` 21 模块全绿（RC=0）+ gcovr 聚合成功（RC=0），两者均实测达成。

**lib/ 模块级覆盖率基线（插桩全量跑，gcovr）**：network 65%、pyscript 67%、script 91%、math 67%、resmgr 57%、cstdmf 50%、entitydef 42%、connection 24%、terrain 34%、moo 13%、physics2 12%、server 12%、chunk 3%（客户端大件近零：单测宿主是服务端构建，chunk/moo/physics2/server 的执行路径基本不在 21 个单测里）。tcp_bundle.cpp 100%（批次16 收口）、machine_guard.cpp 28%（bwmachined 面不可达，批次17 登记）。

**lib/ 0% 大文件判定表（行数=可执行行，按缺口排）**：

- `connection/server_connection.cpp`（990L，0%）：**维持不可测登记**（批次14/16 口径：真登录管线 + TCPChannel 状态机）。
- `physics2/bsp.cpp`（778L，0%）：**候选，未派测**。BSP 树构建/查询疑似纯几何可测，本批按台账序列派 event_poller，留作下批首位候选（判定做实需通读 778 行）。
- `entitydef/entity_description.cpp`（665L，0%）：受限。需 EntityType/EntityDef XML 装载机全套。
- `moo/image.ipp`（589L，0%）：受限。客户端图像合成面，服务端单测不触 moo 运行时。
- `connection/message_handlers.hpp`（538L，0%）：结构性死代码（服务端 app 实例化的模板 handler 表，单测无人实例化）。
- `chunk/chunk_space.cpp`（506L，0%）：受限。需 ChunkSpace + chunk 装载机。
- `connection/replay_controller.cpp`（495L，0%）：受限。录像/回放文件管线（replay_metadata 线格式已批次12 测）。
- `network/logger_endpoint.cpp`（398L）/`logger_message_forwarder.cpp`（335L）：**维持受限登记**（批次17：真消息日志管线）。
- `network/watcher_nub.cpp`（248L）：**维持受限登记**（批次17：UDP 绑 machined 标准端口）。
- `terrain/terrain2/terrain_height_map2.cpp`（755L，0%）：terrain 近零区成员；gcovr 对该记录丢失文件名列（空名条目），按字母序邻居归属，未逐臂判定。

**派测 `event_poller.cpp`（台账批次17 既定下批候选；基线 45%，257 可执行行）**：三个具体 Poller（SelectPoller :296/PollPoller :685/EPoller :998）全在 .cpp 内，测试只能经 `EventPoller::create()` 工厂 + 基类接口；Linux（HAS_EPOLL）→ EPoller。双通道策略：StubPoller 探针子类（public 转发 protected 面）测基类机制，create() 真 EPoller + pipe() 驱动平台行为。

**新增 `test_event_poller.cpp`（14 用例，network_test 193→207）**：`EventPoller_registerMapsTrackDoRegisterResult`（doRegister 失败不落 map——源码实证先 doRegister 后 `fdReadHandlers_[fd]=`）、`_maxFDAcrossMaps`（跨读/写两表，空→-1）、`_triggersDispatchToRegisteredHandler`、`_triggerErrorArms`（读 handler 优先认领→不回退；不认领→回退 input 且 triggerError 仍返 true；仅写 handler 时错误路由到写；无 handler→false）、`_isReadyForShutdownConjunction`（全注册 handler 就绪合取）、`_selfHandlerRunsPollPass`（EventPoller 自身是 InputNotificationHandler：handleInputNotification→processPendingEvents(0) 返 0；getFileDescriptor 基类默认 -1）、`_inputHandlerEntryTriggerCounting`（各 10 万次输入/错误触发过 INFORM_COUNT=100000 INFO 臂）、`_createReturnsUsablePoller`（EPoller epfd≥0）、`_pollDispatchesReadEvent`（写 1 字节→epoll_wait(0) 返 1→排空后返 0）、`_pollDispatchesWriteEvent`（pipe 写端恒可写）、`_bothDirectionsAndSelectiveRemoval`（同 fd 读+写=EPOLL_CTL_MOD；摘读兴趣后数据不再上报）、`_epollDeregisterUnknownFdFails`（epoll_ctl 失败臂）、`_epollRegisterBadFdFails`（非 ENOENT→ERROR 臂+不落 map）、`_pollHangupRoutesToErrorHandler`（关写端→EPOLLHUP→triggerError：未认领错误回退 input，errorCalls=inputCalls=1）。

**覆盖率对账（gcovr，补测前→后）**：event_poller.cpp 45%→**57%**（116→147 行），event_poller.hpp 68%→**85%**。after 快照口径：精准插桩（仅重编 event_poller 两个 TU，混合树重链）单跑 network_test 一轮——before 的 45% 来自全量 21 模块跑，故 after 略保守。剩余缺口逐臂登记：**:302-670（175 行）SelectPoller/PollPoller = Linux 构建死代码**（create() 只实例化 EPoller，两类不可达，结构性登记）；:1098 ENOENT→WARNING 臂（两个失败用例实测走的都是 EBADF→ERROR 臂：fd 9999 未打开得 EBADF，fd -1 得 EINVAL/EBADF——ENOENT 需"已打开但未注册"的 fd，下批一行可补）；:1037-1038 EPoller::doDeregisterForWrite 包装行（基类层已由 StubPoller 测）；hpp :71-83 访问器 6 行（调用实测在跑但 gcov 归属被内联吞并的怪癖）；:67,69,96 琐碎残留。

**陷阱新增（本批实录）**：

1. **lib/network 的接口类在 `Mercury::` 命名空间而非 BW**（event_poller.hpp/interfaces.hpp 自带 namespace Mercury）——测试文件 BW_BEGIN_NAMESPACE 内裸写 `InputNotificationHandler` 编译失败，必须 `Mercury::` 限定。
2. **`using Base::f;` 会把 private 同名重载一并拖进作用域**：EventPoller::maxFD 有 protected 实例版 + private static `maxFD(const FDHandlers&)`，public 段 using 声明直接编译错；改派生类转发方法（`exposedMaxFD()`）靠重载解析只选中实例版。
3. **本构建对 `warn_unused_result` 开 -Werror**：`pipe()` 返回值必须检查，否则 cc1plus 直接拒绝。
4. **管道 fd 默认阻塞：`while (read(...) > 0) {}` 排空循环在写端未关时永久挂死**（读空后 read 阻塞等数据而非返 0）——首轮跑 207 卡死在 anon_pipe_read 即此因；明确只写了 N 字节就单次 read(N)。症状取证：ps wchan=anon_pipe_read + CPU TIME 停涨。
5. **epoll 默认水平触发：注册读事件后再次 poll 前必须排空数据**，否则事件重复上报（并行会话代修的一处，正确采纳）。
6. gcovr 8.6 实操：`--txt-summarized` 不存在（用 `--txt`）；`-r` 必须指源码根且对象树在根下（`-r obj目录` 会把 lib/ 源全过滤成 "All coverage data is filtered out"）；gcc#68080 可疑命中需 `--gcov-ignore-parse-errors suspicious_hits.warn_once_per_file`（bit_reader/bit_writer/memory_stream.ipp 热点行触发）。
7. **精准插桩法**（对象树被并行重建为无桩后的 after 快照手段）：`rm` 指定 .o → 带 `user_shouldBuildCodeCoverage=1` 重编→只有缺失 TU 拿到 -fprofile-arcs，混合树重链可跑可出 gcda；跑完删桩重编恢复无桩。
8. **并行会话三连击实录**：① 插桩 gcda 全被其 `rm -rf obj` 清掉（496 个）；② 两份构建清单各被复制入一行重复 `test_machine_guard` 条目（make 变量重复项无害但污染 diff）；③ 批次17 的 test_machine_guard.cpp 被重写为 readNBO16/32"网络字节序"helper——**该端序模型正是批次16 用 25 个失败证伪的**（BW 线序=小端，binary_stream.ipp:100-103），按"破坏性残留还原"预案 `git checkout HEAD` 还原并清除重复行，不纳入本批提交。

**门禁记录**：插桩全量 21 模块跑（496 gcda 产出时）全绿；精准插桩单模块 207 绿；最终无桩全量 21 模块门禁结果见提交信息（本批后 network_test 193→207，全量合计 1099+14=1113，以实测为准）。

提交范围：`lib/network/unit_test/test_event_poller.cpp`（新）、`lib/network/unit_test/Makefile.rules` + `CMakeLists.txt`（各 +1 行）、本文档。

### 7.19 覆盖率补强批次 19：bsp.cpp 死门激活 + physics2 面（2026-09-28）

**根因闭环（批次18 首位候选）**：`lib/physics2/bsp.cpp`（778 行）0% 的原因不在物理——`test_bsptree.cpp` 整文件包在 `#ifndef MF_SERVER` 里（上游注释 "unusual linkage issues to be fixed later"），服务端单测二进制里编译为空，3 个休眠用例从未在服务端跑过。核实 bsp.cpp 现依赖面仅 physics2/cstdmf/math/resmgr、`unit_test/Makefile.rules` 的 cxxSource 与 dependsOn 早已挂好 → 判定"链接问题"为上游历史包袱，移除死门即可激活。本批 `test_bsptree.cpp` 完整重写：复活 3 个休眠用例（两个浮点断言按 `-Werror=float-equal` 口径改 `CHECK(closeTo(a,b))` 容差比较——unit_test_lib 无 CHECK_CLOSE）+ 新增 11 个用例，physics2_test 6→20。

**14 个用例的可测面**：构造/查询不变量（单三角形树的 contains/intersects/visitor）、射线未中与 maxDistance 截断、visitor 否决后找到更远命中、三角形重叠查询、**强制分区树**（11 瓦片 >MAX_TRIANGLES_PER_NODE=10 → 根分割；分割面 15 个随机候选导致树形状非确定，断言只基于不变量）、save/load 现行格式往返（含结构尺寸校验头）、坏 magic 拒载、**legacy 三节点树手工 blob**（NodeRecord 逐字段写：u8 flags(0x02 FRONT/0x04 BACK) + PlaneEq(16) + u16 计数与 refs；TriRecord 40 字节静态断言）、空树 save/load、remapFlags 模映射、`notInBsp` 三角形排除、WorldTriangle 扫掠平移。BSP_FILE 版本常量 LEGACY=0/CUR=2/INVALID=0xFF、MAGIC=0x505342。

**prism 扫掠语义陷阱（4 断言失败实录）**：`WorldTriangle::intersects(triangle, offset)` 测的是**扫掠壳边界穿越面**（worldtri.cpp:295-367）：offset 对另三角形面近似平行 → bruteCheck；另三角形三顶点过本面分数 vd 全 <0（面之前）或全 ≥1（面之后）→ 直接 false，部分出界才转 bruteCheck。首轮用"零平移已重叠"与"z 向穿越"几何全判 false——零平移使 `unitVector()` 退化、z 向扫掠方向躺在 mover 自身平面内（prism 体积为零），**containment 式几何按实现就该 false**。修正后用例 5 臂：近距下扫 1 单位（brute 路径命中）、20 单位远穿越、向上 miss、沿树三角形法向 8 单位扫掠（vd∈(0,1) 快路径命中）、法向远离（快路径拒绝）。要义：**offset 必须离开 mover 自身平面且真穿越目标面，非退化 prism**。

**覆盖率复跑（沿用批次18 口径）**：`rm -rf bigworld/build/el7` + `user_shouldBuildCodeCoverage=1` 全量重编 21 模块跑完（498 gcda）+ gcovr 8.6 聚合。**gcc#68080 第二形态首现**：smartpointer.hpp:610 负命中（NegativeHits），批次18 只需 suspicious_hits 容错，本批起两个都要（`--gcov-ignore-parse-errors` 可重复传参）。

**encodings 三连败真根因（跨会话二次踩坑后闭环）**：entitydef/pyscript/script 三模块 "Fatal Python error: Failed to import encodings" 的机制不是环境变量没生效——`env user_shouldInstallPython=1` 确实进了 make（-pn 可见），该变量只把 `python_install` 挂进 `BW_THIRD_PARTY`，而 `python_install` 目标只被 `third-party-libraries`（make all 链 `depsForRuleAll` 成员）引用，**`bw-run-all-unit-tests` 只依赖 `BW_RUN_UNIT_TESTS`（各测试二进制），链接用的 libpython 在构建树、不依赖 game/ 安装副本 → 安装目标在单测链路中永不可达**。树重建后正确操作：显式 `make python_install`（`bwsentinel_postinstall.txt` 幂等哨兵）。已固化进 TESTING.md 测试入口节。

**覆盖率对账（gcovr，补测前→后）**：模块级 physics2 **12%→61%**（2356 行基盘）；文件级 `bsp.cpp` 0%→**91%**（711/778）。bsp.cpp 剩余 67 缺口行分布：:282-424 区（BSPConstructor 内部校验臂，需畸形树输入，结构性登记）、:1297-1319/:1378-1379（版本探测/加载错误臂）、:1581-1596（legacy 子树回溯残余臂）、:1705-1710（remapFlags 未映射值臂）、:1933-1934/:1997-1999（射线遍历边界臂）——均直击成本高或需构造畸形输入，不硬凑。`worldtri.cpp` 85%（156 行；`test_worldtri.cpp` 既有 100%）。新增 `physics2/quad_tree.ipp`（299 行 0%）为下批首位候选：BSP 邻接、纯几何、依赖面同 bsp.cpp。

**缺口 top 清单落点**：任务口径 "落 TESTING/todo 对应段"——仓库根无 TESTING.md，**新建**，含测试入口、覆盖率口径（含 python_install 前置与 gcovr 双容错参数）、模块表、缺口 top 文件清单（22 项，逐项标注状态：不可测/受限/候选）与收口记录。

**并行会话实录（本批第三次）**：插桩树 497 gcda 被并行 `rm -rf` 全灭后，放弃定点补编（混合树 undefined `__gcov_*` 老坑），排干后干净全量重建；重编 test_bsptree.cpp 后旧 gcda checksum 不匹配（"overwriting an existing profile data with a different checksum"）—— checksum 冲突即"必须干净重建"的直接证据。

**假门禁事故（本批新陷阱，入档防复发）**：首轮"无桩门禁"21 绿 exit 0，但对象树核验拆穿——`bsp.o` mtime 仍是插桩轮时间戳且带 323 个 gcov 符号、498 个 gcda 在"无桩"构建后重新出现。根因：后台任务继承会话 cwd，而会话 cwd 已被中途 cd 漂到仓库根（无 Makefile，`make` 在此必报 "No rule"——真实执行路径是从组件子目录起的 make，相对路径 `rm -rf bigworld/build/el7` 打在子目录下静默 no-op），于是"门禁"只是**在原封未动的插桩树上原样重跑测试**。教训两条：① 构建类后台命令必须显式 `cd <绝对路径> && pwd` 自证 + rm 用绝对路径；② **"21 绿 + exit 0" 不等于"门禁跑了"**——门禁后必须核验 `find -name '*.o' -newer <起点标记>` 与对象内 gcov 符号数（无桩应为 0）。

**门禁记录**：插桩轮 18 模块绿（706 用例）+ python_install 补装后 entitydef 18/pyscript 118/script 285 补跑绿，合计 **1127**（1113+14）；无桩全量终局门禁（绝对路径 rm + pwd 自证 + 对象树 gcov 符数核验）结果见提交信息（21 模块全绿为提交前置）。

提交范围：`lib/physics2/unit_test/test_bsptree.cpp`（重写）、`TESTING.md`（新）、`docs/upgrade-plan/migration-status.md`、本文档。

### 7.20 覆盖率补强批次 20：quad_tree.ipp 激活 + Makefile.rules 漏列修复（2026-09-30）

**根因闭环（批次 19 首位候选）**：`lib/physics2/quad_tree.ipp`（299 行 0%）的根因与 bsp.cpp 的 MF_SERVER 死门同型——"文件存在≠参编"第二例：`test_quadtree.cpp` 文件存在、CMakeLists.txt 早已列入，但 `unit_test/Makefile.rules` 的 `cxxSource` 漏列，GNU 构建链的单测二进制里它根本不存在，2 个休眠用例（QuadTreeMemPool_usage/QuadTree_usage）从未参编。补列一行后复活，另新增 22 个用例（本会话 12 + 并行 opencode 会话 10:44 追加 10，追加块逐用例审读通过后收编），physics2_test 20→44。

**用例面**：QTCoord clipMin/clipMax/offset/findChild 与 QTRange fills/isValid/inQuad/clip 谓词面；越界元素 add 跳过（节点不增长）；BL 象限链 childRef 五层走查（根 0 元素、兄弟 INVALID_NODE、叶恰 1 元素、countNodes=6）；traverse 四方向扫掠（dirType 0/1/2/3 全覆盖，各恰 1 命中）；轴平行线 x/y 退化臂（isZero 分支，交叉项不串扰）；radius 半带宽（离线 43 单位：r50 中/r10 不中）；大元素跨越全树驻根；removeFromRoot 二次删除返 false；QuadTreeNode 元素池批保留量与 swapLastRemove 语义、del 桩钉死；跨四象限/两象限 addElement 递归分区（fills→直挂 else 递归四象限臂）；零长线退化（过点访链、离点 0 命中）；百元素池增长与交换删除压力。

**`-Werror=float-equal` 口径**：文件级 `closeTo(a,b,eps=1e-4f)`（fabsf）；既有 `CHECK_EQUAL(static_cast<float>(i),…)` 改 int 等价比较（i<2^24 float 精确无损），比容差更严格且不损失断言力。

**四处编译级死代码（登记不硬凑）**：`QuadTree::del`（calculateQTRange 少传 origin_，3 参 vs 4 参）、`QuadTree::testPoint`（公有面调不存在的 `QuadTreeNode::testPoint`）、`print`/`printQTNode`（`node.elements()` 访问器不存在，一实例化即编译错）、`countAt`（int 实参传 `getChildRef` 的 Quad 形参，且无其他调用者）——四处全部从未实例化：生产侧 chunk 的 `pChunkTree_` 是 HullTree 非 QuadTree，chunk 对 ObstacleTree 只用 addToRoot；`QuadTreeNode::del` 保留上游 "TODO: Implement this!" 桩原样，但用例钉死其 `return false` 行为防静默回归。

**rmem_max 环境重置事件（非测试妥协）**：network_test 的 `test_config.cpp:20` 断言 `SO_RCVBUF` 可设 16MB——断言失败文本本身就是配置说明书；09-28→09-30 间系统 `net.core.rmem_max` 被重置回 4194304，`sudo -n sysctl -w net.core.rmem_max=16777216 net.core.wmem_max=1048576 net.core.wmem_default=1048576` 修复后 207 用例补跑全绿。这是环境修复（测试文件自带的要求），不是放宽断言。

**gcovr 第三形态容错（gcc#68080 家族）**：聚合报 `SanityCheckError: Output file /home/avatar_filter_helper##<hash>.gcov.json.gz doesn't exist but no error from GCOV detected`——gcov 对个别 TU 不产出输出文件且 stderr 无错，gcovr 的存在性自检拒绝继续。read.py:934 显示 `--gcov-ignore-errors output_error` 恰好过滤"期望输出不存在"的文件集，聚合通过。至此三形态参数齐备：suspicious_hits + negative_hits（parse-errors）+ output_error（ignore-errors）。

**并行会话实录（方法论教训）**：① 聚合期间并发会话重编插桩树，gcc 按 TU 删除旧 gcda，499→63→0，聚合数据残缺——单树多代理必须串行化，聚合前确认对象树静止（本批放弃自建插桩轮，改搭并行会话插桩 run 的顺风车）；② 并行会话 10:44:46 向测试文件追加 10 用例后 32 秒即重跑全套——4 空格缩进与本文件 tab 风格可辨 provenance，审读要点：断言保守、无浮点等值违规、死代码登记口径一致；③ 其 run#2 疑似工具超时被杀即起 run#3——跨代理的树占用窗口不可预期，关键轮次的结果必须自己留日志；④ 终局门禁首跑中止于多模块 `.d` 写入时目录消失（当日先归为"无人认领的瞬时干扰"，下午进程取证确认为并行会话的连环 make，见下段）；⑤ 12:02 并行会话将本会话的 `collectTraversal` 辅助模板化（意图：服务全文件实例）但漏 `typename`，被终局门禁拦下后补 1 行修复——两个等价遍历辅助模板并存，列为下批去重候选；其对账修正（用例总量 1149→1151：1127 基数里 physics2 仅 20，复活+新增后 44，Δ=+24 含 2 个休眠用例）经本会话复核采纳。

**终局门禁三连败与并发 configure 互踩（新增陷阱档，2026-09-30 12:40–15:00）**：无桩门禁 #1 中止于 entitydef/db 模块 `.d` 目录写入时消失；#2 拦下 12:02 漏 typename（单行修复，单 TU 复验编译过）；#3 `chunk_overlapper.o` 在编译（日志 :28004）与归档（:43899）之间消失——三败同族，下午取证确认：并行 opencode 会话 13:26/13:56/14:04 连环拉起 `make bw-run-all-unit-tests`（各因工具超时被 SIGTERM，留下部分树），期间还向同树写入 ~201 个外来 gcda droppings。对策改"**静默探测**（无并发 make 持续 20s 才发车）→ **无 rm 续跑**（沿用当日全新无桩代的对象，四重证据替代 rm 全量）"，发车后又揭出第二类**确定性**故障：`libpython3.13.a` 三连败于 `SIZEOF_VOID_P` 未声明/`long must be 4 or 8`/`Require native threads`。取证：`config.log` 内**两个 configure 并发交错**（同名 conftest.c 被对方删除报 `No such file`、exit 77 中止与 exit 0 交错写同一日志、`config.status` 丢失全部 SIZEOF 烘焙值），第二来源为野生 `./configure --enable-optimizations` 调用——`pyconfig.h` 从被污染的 confdefs 生成全 undef 模板（60KB 与 `.in` 仅差头注释与 undef 包裹格式），`ac_cv_sizeof_long=8` 等 cache 值正常却烘不进头。处置：定向 `rm -rf build/el7/third_party/python`（BigWorld 对象树不动），make 配方单进程重配（test_symlink/test_exec 探针 → 全参数 configure → TMPNAM/LINKFORSHARED/Py_DEBUG 三段 sed）恢复。教训：**树上"没有 make"≠"没有 configure"**，第三方配置态损坏的特征是编译错误确定性复现（非文件消失类），修复入口是被撞坏的生成头而非源码。

**覆盖率对账（gcovr，补测前→后）**：文件级 `quad_tree.ipp` 0%→**98%**（测试 TU 视角 860/876；全仓模板实例聚合口径 68%——总行数 299→886 是测试 TU 参编后模板实例化集合扩大的口径变化，低值来自生产 TU 从不执行的实例）；模块级 physics2 61%→**76%**（math 顺带 68→70，聚合轮差）。残余 16 行（测试 TU 视角）：MF_ASSERT 校验臂 ×4 行 ×3 实例（断言未触发即测试未失败的证明）、dprintf 越界日志臂 ×2、TestQuadTreeItem 实例的元素池扩容臂 :66、radius 法向翻转小臂 :437——全部登记不硬凑。

**门禁记录**：插桩轮数据取自并行会话 run#3 全量插桩 run（gcda 全集 499，本会话即时快照聚合成功，EXIT=0）。无桩终局门禁实跑（15:08–15:24，自有日志 `/tmp/pcov20_gate4.log`）：静默探测确认无并发 make 后**无 rm 续跑** 14:04 无桩代——`cd && pwd` 自证 + 抽样对象 gcov 符数 0 + gcda 0 + mtime 代龄四重证据替代 rm 全量（三连败取证与 python 重配见上段）；**GATE_EXIT=0**：21 模块 result 全 0、**1151 用例全绿**（physics2_test **44**、network 207、pyscript 118、script 285），跑后 gcda=0、`test_quadtree.o`/`bsp.o` gcov 符数=0，窗口内无外来 make/configure 介入（inotify 全程监听仅录得本会话 configure 自身的临时文件清理）。

提交范围：`lib/physics2/unit_test/test_quadtree.cpp`、`lib/physics2/unit_test/Makefile.rules`（补列 cxxSource）、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、本文档。

### 7.21 覆盖率补强批次 21：fixed_sized_allocator.cpp 平台宏死测试直驱（2026-09-30）

**根因闭环（"有测试却 0%"第三例，机制不同于漏列/死门）**：`lib/cstdmf/fixed_sized_allocator.cpp`（289 行 0%）的 test_allocate.cpp 里既有两组 FixedSizedAllocator 用例（testFull1/testFull2），却整组包在 `ENABLE_FIXED_SIZED_POOL_ALLOCATOR` 守卫下——config.hpp:123 `defined(_WIN32) || (defined(MF_SERVER) && ENABLE_MEMORY_DEBUG)`，el7 服务端测试构建两臂恒 0，守卫体在单测二进制里编译为空。该宏是内存调试器配套口径（ENABLE_MEMORY_DEBUG 走整套 debug allocator），开宏等于换掉被测对象本身，不取；类本身无条件编入 libcstdmf，故新建 `test_fixed_sized_allocator.cpp` 12 用例直驱，cstdmf_test 334→**346**。

**12 个用例的可测面**：构造（定尺寸表/自动表）与六 accessor 无池臂、空表全查询臂；deallocate(NULL) 早退；超界直落 heap hooks（memorySize/allocationInfo 堆臂，allocFlags_=0）；池分配全链（首池建立 free-block 链、满池 16/16、二池头插、memorySize/allocationInfo 池臂 IF_POOL_ALLOC）；三池链表 relink（中间池 prev/next 双非空臂 → lastFree 消耗至 numFree=0 跳过重链 → 尾池 prevPool_ 非空 nextPool_ 空臂）；池移除三形态（中间/尾/独池 + span 归零 + 重初始）；单项池 firstFree_=null 臂（poolSize==allocSize）；reallocate 五分支（NULL、同档快返 `allocSizes_[newPool]==currentPool->allocSize_`、池→异档 copySize=min(allocSizes_[newPool],size)、池→超界落堆、堆→堆 realloc、堆→池 copySize=min(poolSize,size)，图案 memcmp 校验拷贝量）；autoFindPool 建档（32 建档、48 追加、超 maxPoolSize 堆直通）；池 span 数组倍增与归零塌缩（65 单项池倍增一轮回、全删归零、再分配重初始）；析构释放未还池（三档故意不 deallocate）。

**残余 3 行死臂与源码隐患登记（不硬凑）**：findPool 排序 shift `:755-772`——autoPools 建档路径本身维持单调递增（新档恒大于尾档），`:757` 比较恒 false，`:759-771` swap 体结构性不可达，仅手工构造乱序表可触（现构造只收只读指针，属假设性输入）；残余行 `:760-761,:766` 恰为该臂。另登记 `getNumPoolItemsForSize :641` 未命中即读 `allocSizes_[-1]` 的源码级越界隐患（调用方约定先行 poolExistsForSize，生产面无触发，不修只记）。MF_ASSERT 失败臂（`MF_ASSERT_NOALLOC(numPoolItems > 0)`，poolSize 小于档尺寸即崩 + ENTER_DEBUGGER）不测；STATISTICS/DUMP_ALLOCS/BOUNDS_CHECKING/MEMORY_FILLS 调试宏面本构建不参编。

**测试设计陷阱两处（写码期修正）**：① 析构 `:173` **无条件** `(*free_)(poolSpans_)`——即使从未建池也解引用函数指针，故每个用例必先 `installHeapHooks()`（挂 heapAllocate/heapDeallocate/heapReallocate/heapMemorySize 四钩子），否则空表析构即崩；② **free-list LIFO**——deallocate 头插回收，relink 用例中被回收的 scratch 块会被后续 allocate 再发出，teardown 若按原下标盲 free 即双重释放（两连错修正：回收者经持有变量显式 free，未回收的空闲 scratch 跳过）。

**gate1 断言失败实录**：`test_fixed_sized_allocator.cpp(389): "expected: '2' but was: '1'"`——getFirstPoolNumFreeForSize(32) 断言写成分配前值 64/32=2，漏算"本次分配消耗一格"；修复为 `size_t( 64 / 32 - 1 )`。单 TU 重编复验后 gate2 全绿。

**15:58 代际环境事件（python 污染第二次实证）**：无桩门禁发车前发现对象树 15:58 被并发会话整体清零（残留 53 个 .o，来源会话无进程存活，批次 20 已有同型实录）；续跑构建撞出 `python_install` 三连败——`SIZEOF_VOID_P` undeclared / `long must be 4 or 8` / `Require native threads`，与批次 20 并发 configure 互踩**同签名**。取证 pyconfig.h 全 undef（0 个 define 命中）→ 定向 `rm -rf build/el7/third_party/python` 单进程 `make python_install` 重配（EXIT=0，SIZEOF_LONG 8 / SIZEOF_VOID_P 8 恢复）→ 全树重建 558 TU 约 9 分钟 → 门禁绿。树上"没有 make"≠"没有 configure"教训再次应验。

**覆盖率对账（gcovr，补测前→后）**：文件级 `fixed_sized_allocator.cpp` 0%→**99%**（286/289，残余 :760-761,:766）。本批采用**精准插桩口径**：`rm` cstdmf 对象目录 → `user_shouldBuildCodeCoverage=1` 只重编 cstdmf TU 拿 -fprofile-arcs → 混合树重链 21 个二进制 → 21 模块跑完 93 个 gcda → gcovr 三容错聚合 → 跑完删桩重编恢复。普通 .cpp 单 TU 编译，文件级分母两代一致、与全量插桩**精确可比**；模块级不可比——模板头（watcher.hpp）实例化宇宙随插桩 TU 集缩放（6414→2149 行），分母变了，故模块表维持批次 18/20 全量插桩值不刷新。

**门禁记录**：gate1 败于 :389 断言（如上）；修复后 **gate2 GATE_EXIT=0**——21 模块 result 全 0、**1163 用例全绿**（cstdmf_test 346、network 207、pyscript 118、script 285）；精准插桩轮 INST_EXIT=0（93 gcda、21 模块 1163 绿）；删桩恢复轮 **gate3 GATE_EXIT=0**（gcda=0、`test_fixed_sized_allocator.o`/`fixed_sized_allocator.o` nm gcov 符数=0、result 全 0 无 failed_lines）。

提交范围：`lib/cstdmf/unit_test/test_fixed_sized_allocator.cpp`（新）、`lib/cstdmf/unit_test/Makefile.rules`（cxxSource 补列）、`TESTING.md`（TODO 移除 + 收口记录）、`docs/upgrade-plan/migration-status.md`、本文档。

### 7.22 覆盖率补强批次 22：watcher.hpp 逐类可测性判定 + 39 用例（2026-10-01）

**候选背景与判定口径**：TODO 表降序首位 `lib/cstdmf/watcher.hpp`（基线 5471/6414 行、14%，批次 18/20 全量插桩口径）——巨型模板头，覆盖率的分母是**模板实例宇宙**（gcovr 按实例逐实例计数），生产 TU 中从不执行的实例化把聚合值压低，与 quad_tree.ipp 的 68% 聚合 vs 98% 测试 TU 视角同型。故本批主交付为**逐类可测性判定**：可测类补单测直驱，不可测面逐条登记；覆盖率对账用**两阶段全量插桩**（stash 本批改动 → 干净全量插桩 21 模块 + gcovr BEFORE → pop → 增量重编重跑 + gcovr AFTER）保证前后分母同口径可比，替代批次 21 的精准插桩口径（对模板头不可比，见 §7.21）。

**39 个用例的可测面**（新建 `test_watcher_gaps.cpp`，cstdmf_test 346→385；既有 test_watcher.cpp 43 用例不重复合——其未触达的流协议/容器族/工厂重载/全局 root 面为本批补位）：

- **流协议函数族**（头内联，11 用例）：`watcherStreamToValue` 空流/截断 mode 字节拒绝、int64 upcast downcast 往返、uint64 整型化往返、`long`/泛型值往返（重载解析：`long` 精确匹配 `const int64` 非模板重载走 INT len8，`uint64` 无精确非模板重载落泛型 STRING typed）、坏 size 拒绝、bool size≠1 拒绝、STRING fallback 拒绝、`watcherStreamToValueType` 语义；`watcherValueToStream` 泛型（STRING typed）/`WatcherDataType`（TYPE typed）双形态。
- **基类默认实现**（2）：裸 `Watcher` 桩驱动 visitChildren/addChild 默认 false 臂与 `Watcher::visitChildren(base,path,visitor)` 便利包装的三参委托（`using Base::visit` 保持三参可见性——派生类 hiding 陷阱）。
- **SequenceWatcher**（5）：labels 短于向量截断臂、labelSubPath 委托、自定义 index converter、streamDoc 嵌套 miss、addChild 斜杠拒绝/二次拒绝。
- **MapWatcher**（3）：自定义 key converter（IntKeyConverter 双向）、streamDoc 嵌套 miss、addChild 同上。
- **DataWatcher**（3+1）：NULL base/坏路径拒绝、setFromStream 无 packet 拒绝、uint64 setFromStream、（收编）非空路径四臂拒绝 + `__doc__` 路径（`isDocPath` 臂 watcher.hpp:2643）。
- **MemberWatcher/FuncWatcher**（2+2+1）：MF_WRITE_ACCESSOR null getter 臂、坏串+双流臂、string/int get-set 双流、（收编）setFromString 格式变体（"3.14"→3 部分解析 stringstream 语义、坏串、空串，运行时验证通过）。
- **解引用族**（3）：DereferenceWatcher ok/null base、SmartPointerDeref（`SmartPointer<uint>` 不可编译——uint 无 incRef/decRef，`FakeSmartUintHolder` 布局替身 + 单成员偏移 0 无虚表论证）、ContainerBounce changeContainer。
- **AbsoluteWatcher/FreezeWatcher/工厂**（3）：Absolute NULL base 五方法守卫 + 透传（watcher.cpp 五臂 base==NULL 短路实证）、Freeze freeze/unfreeze 循环、makeWatcher/makeNonRefWatcher const-ref 与 bare 重载（裸 NULL 歧义需 static_cast——watcher.hpp 编译性修正之一）。
- **全局 root 面**（1+1）：addWatcher 全重载（值/函数/注释臂）、重复 add 返 NULL、getWatcher、rootWatcher 读写往返、removeChild 清理；（收编）SafeWatcher grab/give 透传。

**死臂与不可测面登记（逐条）**：

1. **rootWatcher()/fini() 结构性死臂**：头内联 `rootWatcher()` 创建臂（watcher.hpp:3012-3015）与 `fini()` removeChild 臂（:3028-3030）——`rootWatcherInternal()`（watcher.cpp:61-70）惰性保证 `g_pRootWatcher` 恒非 NULL ⇒ :3012 条件恒 false；`g_pRootWatcher` 恒等于内部根本身 ⇒ fini 的 `g_pRootWatcher != &watcher` 恒 false ⇒ :3028-3030 不可达。本构建不使用 `DECLARE_WATCHER_DATA` 宏（`g_pRootWatcherPath` 默认 NULL），path≠NULL 路径整体不可达。
2. **hasRootWatcher() false 臂** :2997-2999：仅根不存在窗口（进程早期/fini 后）可达；进程级全局态 + FiniJob 退出钩子竞争，避免全局态风险不硬凑。
3. **`_XBOX360` 分支** :261-263：平台宏死代码，el7 不参编。
4. **MF_ASSERT 中止臂**：断言未触发即测试通过的证明（批次 20 既有口径）。
5. **uint64 提取器源码缺陷**（登记不修）：:483-499 upcast 块（:492-494）后缺 else——size==4 时先读 4 字节赋值，随后 :497 无条件 `stream >> value` 再读 8 字节 → 流必然下溢返回 false。`WatcherGaps_stream_uint32ToUint64Fails` 以注释钉死此现状行为；生产语义上 uint32 源到 uint64 目标的扩展读取恒失败，源码修正超出测试批次范围。
6. **SafeWatcher MutexHolder grab 单行**：grab/give 已被用例覆盖，若聚合仍报未盖属 gcovr 实例归因疑点。
7. **方法体在 watcher.cpp 的类**（DirectoryWatcher/RWLockWatcher/Callable 族）：头文件仅声明无执行行，归属 .cpp 批次（watcher.cpp 不在本批 TODO 行内）。

**收编追加块审读（并行会话第四击，批次 20 先例续篇）**：03:01 opencode 会话向 test_watcher_gaps.cpp 追加 3 用例（4 空格缩进可辨 provenance）。逐用例审读：① `base_nonEmptyPathRejection`——API 全部核实存在（`WatcherPathRequestV2(const BW::string&)`、DataWatcher 直驱 ctor 默认 READ_ONLY、`isDocPath` 臂），原样收编；② `SafeWatcher_grabGiveCycle`——**必崩 bug**：直驱构造的 DataWatcher 其 `rValue_` 是引用成员（watcher.hpp:2686 `TYPE & rValue_`），`getAsString` 的 `&rValue_ + base` 算术下 base 必须为 NULL；用例传 `&value` ⇒ `&value + &value` 在 x86-64 上为非规范地址 ⇒ SIGSEGV 毒化整个门禁。修正 base→NULL 并改名 `grabGivePassthrough`（无 cycle 语义）+ 语义注记后收编；③ `Member_setFromString_variousFormats`——`makeNonRefWatcher` 三参形态与本文件既有用法一致，"3.14"→3 部分解析符合 stringstream 语义（运行时验证通过），原样收编。

**watcher.hpp 编译性修正两处（非语义变更）**：① addReferenceWatcher RO 版 :3048-3049 裸 NULL 在 makeWatcher 重载间歧义 → `static_cast<void (OBJECT_TYPE::*)(const RETURN_TYPE &)>(NULL)`；② FreezeWatcher ctor :3246-248 成员初始化顺序对齐声明序（-Wreorder）。

**共享树实录（本批三起）**：① 02:50-02:54 已死亡会话的孤儿三连（python_install/cstdmf make/全量 -j12）并发撞出 CPython 第二次 configure 互踩——`sed: can't read conftest.c: No such file` → pyconfig.h 全 undef 三连签名（§7.20/§7.21 同型），pyinstall.log 实锤，进程父会话已消亡；② 03:00 静默探测拦下测量链首发（PROBE_BUSY），03:01 同批 opencode 会话追加上述 3 用例并重试 python_install；③ 测量链 STEP0 升级为**持续静默等待**（30 分钟窗口内要求 2.5 分钟连续无 make 才发车）后成行。追加块收编与 385 用例验证的重编与 python_install（仅 third_party/python 子树）目录不相交，范围受限发车一次并注明。另：03:18 首发测量链被系统内存压力回收（51GB 总量 0 free，opencode CPython 构建高峰），环境回落后（35GB available、树静默）重启成行——共享树窗口不可控时，链式脚本的分步日志与退出码标记使中断恢复零损耗。

**覆盖率对账（gcovr，两阶段全量插桩同口径）**：`watcher.hpp` 943/6414（14%）→ **1545/6943（22%）**——覆盖 +602 实例行、缺口 5471→5398；总行 +529 为新测试 TU 带入的实例宇宙扩容（quad_tree.ipp 同型口径）。模块级 cstdmf 52.4%→**54.2%**（51%→54% 表值）；其余模块与批次 18/20 值一致（network +3 行属 watcher 实例溢出）。**基线澄清**：BEFORE 干净复测 6414/943/14% 与批次 20 快照逐位一致——批次 21 台账所记 cov_batch20 混沌窗口疑点解除，旧基线有效。

**门禁记录**：39 用例版本单模块实跑 `385 tests run / no test failures`（RUN_EXIT=0）；插桩 BEFORE 轮 21 模块 1163 用例全绿（GATE_EXIT=0，501 gcda）→ pop 后 AFTER 轮 21 模块 **1202 用例全绿**（GATE_EXIT=0，gcovr 两轮 EXIT=0）；删桩恢复终局门禁（gcda=0、对象 gcov 符数=0）结果见提交信息。

提交范围：`lib/cstdmf/unit_test/test_watcher_gaps.cpp`（新，39 用例）、`lib/cstdmf/unit_test/Makefile.rules`（cxxSource 补列）、`lib/cstdmf/watcher.hpp`（编译性修正两处）、`TESTING.md`（TODO 移除 + 收口记录 + 模块表）、`docs/upgrade-plan/migration-status.md`、本文档。

### 7.23 覆盖率补强批次 23：profiler.cpp 收口 + 假绿审计 + 孤儿复活（2026-10-12）

本批同时清了两类账：① 前会话遗留的**红门禁提交** `dc5cd043`（test_profiler.cpp 13 个新用例带着 13 处失败入库，违反门禁）；② 全仓**假绿审计**（"测试验了但没真验"）。cstdmf_test 385→398，全量 1202→1220。

**红门禁 13 失败逐根因修复**（全部是测试侧断言错误，引擎行为以读码+实测为准）：

1. **getAsString 不驱动 statistics watcher 数据填充**（5 败）：`ProfileLinesWatcher::visitChildren`（profiler.cpp:131-137）先 `g_profiler.getStatistics(&profileOutput_)` 再走 SequenceWatcher 枚举——这是 statistics watcher 数据填充的**唯一**驱动路径；`SequenceWatcher::getAsString` 只走 findChild、不填充，裸目录路径返回 `"<DIR>"` + true。旧断言直接 getAsString 后期待统计文本，读到的是空壳。修复：断言改走 `Watcher::visitChildren("profiler/statistics", visitor)`（公开 CSTDMF_DLL API）用 Batch23StatsVisitor 收集标签/值，再对单个条目 getAsString。
2. **EVENT_COUNTER 断言方向错**：`PROFILER_EVENT_COUNTER` 声明的事件计数器**没有** statistics 文本 display entry（仅 JSON trace 的 `"ph":"C"` 路径投递），旧断言在文本里找名字必然落空。修复：反向断言文本里**不含**该名字，并以注释钉住 JSON 才是其投递面。
3. **空帧字母序断言 + 时序型断言不可控**（2 败源）：name/numcalls 排序断言跑在无条目的帧上（npos<npos 恒真但先序断言假）；TIME 模式的时序断言依赖 busy-wait 差值，volatile 自旋虽在 -O2 存活（g++ -S 实证）但 OS 抢注/冷启动 1-in-N 翻车（实测 8 跑 1 败）。修复：删时序断言（留注释），排序行为改钉确定性比较器（`SortByNumCalls` 按计数 3:1）；**每个模式断言前重录帧**——"每个 tick 消费自上一 tick 起累积的条目"，多模式共用一帧是本测试族的结构性错误。
4. **层级导航可见性门控**（3 败）：线程 HierEntry 初始 HIDDEN ⇒ MainThread 不可见；`toggleChildren` 对直接子做 XOR VISIBLE；next/prev 走 HIGHLIGHTED(0x2) 色位判定；prev 到 root（All Threads）有界耗尽。旧断言未先 toggle 就期待可见、并把守卫条目兄弟混进断言。修复：toggle→next 有界循环（≤Profiler::MAX_STACK_DEPTH）按色位定位 + prev-drain 回 root 全链断言。
5. **counter 落在错帧**：`PROFILER_COUNTER` 事件加在了非 dump 帧——被读取的文件来自最终 dump 帧。修复：挪到 count=1 的最终帧。
6. **线程槽扫描两错**：slot 名与 worker 名非同一字符串（trampoline 自动注册为 `Batch23WorkerThread`）；join 后槽位已被 removeThread 延迟删除。修复：worker 存活期断言 `: Batch23WorkerThread`、按名扫描定位槽、join 后守卫式断言（槽未定位则显式失败）。

**假绿审计结果**（机械化扫描：未链接孤儿 / 宏守卫恒 0 / 同义反复 CHECK 三类）：

1. **`test_fixed_sized_allocator.cpp` 析构测试 `CHECK(true)`**（批次21 遗留假绿）：换计数钩子实断言——堆上构造 allocator、装计数钩子（转发 BW::Allocator::heap* 并计数）、分配 8/16/64、快照 allocsBeforeDtor、bw_safe_delete 后断言 frees≥allocs 且 allocs 总数守恒。
2. **孤儿测试复活**（"文件存在≠参编"，批次20 第三例续篇）：`scene/test_scene_object_flags.cpp`（+2 用例，scene 3→5）与 `network/test_piggybacks_longer_than_normal.cpp`（+3 用例，Large_piggyback 三用例）两文件从未进各自 Makefile.rules 的 cxxSource，复活后一次跑绿。
3. **`cstdmf/test_object_pool.cpp` 确认编译坏**：object_pool.hpp:343 调 `LookUpTable::reserve()`，本树 lookup_table.hpp 从未有此成员（reserve 仅是构造参数）——上游测试与头漂移，正是其孤儿原因；cstdmf Makefile.rules 内注释登记，不硬凑。
4. **fprintf 诊断残留清除**：红门禁期间的临时诊断打印全部删除。
5. **环境漂移**：network_test ConfigTest "Insufficient recv buffer"——内核更新把 `net.core.rmem_max` 重置回 4MB（2026-10-01 曾设 16MB）；sysctl 恢复（非持久，TESTING.md 已登记陷阱档）。

**覆盖率对账（gcovr，精准插桩口径——只重编 cstdmf TU，同批次 21）**：`profiler.cpp` 6% → **94%**（1155/1222 行）；`profiler.hpp` 100%、`profiler.ipp` 84%。残余 67 行不可测臂逐条登记：`SortByNamePtr` 比较器（BW_UNUSED 死代码）、HitchDetector 全族、MF_SERVER 排除族、`ENABLE_PER_CORE_PROFILER=0` 面、`processEntriesTask_` 不 delete（静态任务槽复用）、jsonDump else-if 停臂（force 下不可达）、tick stall WARNING、frameNumber 竞态窗口、END mismatch 健康态 ASSERT、rootWatcher fini 悬垂、`doBackgroundTask` !historyFile_ 竞态。模块级 cstdmf 表值维持批次 22 全量插桩值（54%），全量复测待下轮（与批次 21 处置同口径）。

**门禁记录**：修复后 cstdmf_test 398/0 连跑 8×；全量 21 模块门禁 1220 全绿两轮（Python 3.13.15 树与 3.13.16 升级树各一轮，GATE_EXIT=0）；删桩恢复后 gcda=0、对象 gcov 符数=0、cstdmf_test 复跑 398/0。

**本批确立的引擎事实（profiler 语义，测试者口径）**：

1. **statistics watcher 的数据填充只发生在 visitChildren**：任何"读一眼 getAsString 就该有数据"的预期都是错的；裸目录路径返回 `"<DIR>"` + true 是 SequenceWatcher 的目录哨兵语义。
2. **每个 tick 消费自上一 tick 起累积的条目**：跨模式共用一帧会让后位模式拿到空帧或被前位模式污染的帧。
3. **时序型排序断言在单测里不可持有**：busy-wait 差值受 OS 抢注污染；排序行为断言一律改确定性比较器。
4. **线程 HierEntry 初始 HIDDEN**：`internalAddThread` 设 visible_=true 的是统计表线程行，不是层级树条目；层级树要从 root 逐层 toggle。
5. **SimpleThread trampoline 自动以线程名注册**：slot 名 ≠ 传入 worker 名；退出自动 removeThread（延迟删除），join 后槽位即刻不可靠。

提交范围：`lib/cstdmf/unit_test/test_profiler.cpp`（13 用例修复）、`lib/cstdmf/unit_test/test_fixed_sized_allocator.cpp`（CHECK(true) 换计数钩子）、`lib/cstdmf/unit_test/Makefile.rules`（test_object_pool 注释登记）、`lib/scene/unit_test/Makefile.rules` + `lib/network/unit_test/Makefile.rules`（孤儿复活）、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、本文档；另附 Python 3.13.16 升级（见 §6 批次 8）。

### 7.24 覆盖率补强批次 24：smartpointer.hpp 直驱收口（2026-10-13）

TESTING.md TODO 表首位候选落位。`smartpointer.hpp` 此前**没有任何专属测试**：覆盖率全部来自 watcher（SmartPtrDerefWatcher）、circular_queue、memory_debug 等测试的顺带使用。本批新建 `test_smartpointer.cpp` 29 用例直驱，cstdmf_test 398→427。

**用例覆盖面**：

1. **ReferenceCount**：inc/dec/refCount 全计数序列（末次释放 `delete this`）、拷贝构造计数独立（各从 0 起）、**第二基类多继承**场景的 delete this（对应头内 `-Wfree-nonheap-object` pragma 注释的 Mercury::Channel 形状——ReferenceCount 作第二基类，经非零偏移基指针析构）。
2. **SafeReferenceCount**：原子 inc/dec 计数、`destroy()` 虚钩子在计数归零时**恰好调用一次**（栈上实例 + 空操作 override 证明钩子可覆写且不误删）、4 线程 × 500 次 inc/dec 锤击后计数精确归零（BW_ATOMIC32 族真原子性）、默认 destroy 路径 delete this。
3. **自由函数族**：`incrementReferenceCount`/`decrementReferenceCount`/`hasZeroReferenceCount`（decrementReferenceCount 归零即删——自由函数与成员 decRef 同义）、`null_pointer_check`（no-op 钩子直接实例化调用）。
4. **ConstSmartPointer**：空构造/裸指针构造（自增）/已加引用偷取构造（不自增）、**零计数偷取构造的 MF_ASSERT_DEV 臂**（`hasDevelopmentAssertions(false)` 下 writeAndAssert 降级 WARNING 提前返回，臂可非致命覆盖，log_msg.cpp criticalMessageHelper 提前 return 路径）、拷贝共享、赋值三臂（换对象：旧对象归零即删/换空/同对象早退含自赋值）、解引用、`==`/`!=`/`<`/`>` 各三形态（指针序断言用与裸指针比较的等价性，不假设分配顺序）、布尔转换两臂、`hasObject`/`exists`/`getObject`。
5. **SmartPointer**：空/裸/偷取构造、非 const 解引用与成员访问（实测改写对象）、**模板构造与模板赋值派生→基**（`SmartPointer<Base>(SmartPointer<Derived>&)`、`b = d`）、上转 ConstProxy 拷贝、`STEAL_REFERENCE`/`NEW_REFERENCE` 常量语义（偷取=接管既有引用、新建=自增）。

**钉死的口径事实**：`ConstSmartPointer` 在非 `_XBOX` 构建**没有模板拷贝构造**（`_XBOX` 才有）——`ConstSmartPointer<Base>` 不能直接从 `ConstSmartPointer<Derived>` 构造，基类智能指针只能经显式 `get()` 共享同一对象；头文件注释里 `b = d` 的示例仅对 `SmartPointer` 成立（它有模板构造/赋值）。`ReferenceCount`/`SafeReferenceCount` 的 `operator=` 私有为编译期约束（无可执行行）；`ENABLE_REFERENCE_COUNT_THREADING_DEBUG` check() 块仅 `_DEBUG` 编译（config.hpp:126，el7 hybrid 恒 0）。无 weak_ptr，循环引用须手动打破（不写故意泄漏的环形引用用例）。

**覆盖率对账（gcovr，精准插桩口径——只重编 cstdmf TU，同批次 21/23）**：`smartpointer.hpp` 84/91 → **117/118 实例行**（新测试 TU 带入实例宇宙 91→118 行，口径同批次 22 watcher 注记）。唯一残余 `:121` `~ReferenceCount()` 空体为**结构性死臂**：构造函数受保护，任何可实例化对象都是派生类且覆写析构，该基析构体永不为最外层析构。TESTING.md 表值 679/1519（2026-09-28 全量插桩口径）是多 TU 实例事件聚合分母，与精准口径不可直接换算，随本批撤出 TODO 表、以测试 TU 视角登记收口。

**门禁记录与进程教训**：cstdmf_test 427/0 连跑 4×（含 3 轮稳定性复跑）。**插桩态只能跑受测模块**：带着插桩 libcstdmf.a 直接跑全量门禁，其余 20 个宿主链接必败（`__gcov_exit`/`__gcov_merge_add` 未定义——全量门禁在批次 22 那种全量插桩下才成立）；正确次序为插桩态取数 → 删桩重建（gcda=0、对象 gcov 符数=0 复验）→ 全量门禁以删桩态为准。删桩恢复后全量 21 模块门禁 1249 全绿（GATE_EXIT=0）。

提交范围：`lib/cstdmf/unit_test/test_smartpointer.cpp`（新）、`lib/cstdmf/unit_test/Makefile.rules`、`cmake/samples/cstdmf/CMakeLists.txt`（双轨同步 48→49 TU）、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、本文档。

### 7.25 覆盖率补强批次 25：script_math.cpp 收口（2026-10-13）

TODO 表候选落位。`script_math.cpp` 在批次 5 已有 23 用例（`test_script_math.cpp`），但表值 655/1920（65%）按全量插桩口径显示仍有约千行暗区。本批先以精准插桩口径复判（只重编 pyscript TU，同批次 21/23/24）：**1000/1083（92.3%）**——分母 1083 是单 TU 实例宇宙，与表值 1920 不可直接换算，复判后真实暗区仅 83 行。新建 `test_script_math_gaps.cpp` 10 用例，全部直驱原型测试从未触达的行。

**用例覆盖面**（10 用例）：

1. **一元负/正与标量左乘**（`-v`/`+v`/`2.5 * v`，Vector2/Vector4）：只挂在通用 `PyVector<V>` number 协议上；Vector3 自带 `_py_multiply` 特化，故原型测试（全是 Vector3）永不触达共享体。
2. **原地运算符**：Vector2/Vector4 的 `*=`、Vector3 的 `*=`（叉积特化），及 `+=`/`-=`/`*=`/`/=` 对不可转换实参抛 TypeError 的 NotImplemented 臂。
3. **富比较**：GT/LE/GE 三臂（原型只测了 LT/GE/==/!= 中的部分组合）+ Vector2/Vector4 的 `tp_richcompare` 分发槽。
4. **实参校验回退**：`distSqrTo('x')`、`flatDistSqrTo()` 无参/四元组误喂、`flatDistTo()`/`flatDistTo('x')`（原型完全没碰这个方法）、`setPitchYaw` 单参与错型参。
5. **z setter 与 yaw/pitch getter**：原型只写过 x/y/w、从没读过 yaw/pitch——`v3.z = 7.0` 回读、`Vector3(1,0,1).yaw` ≈ 0.7854、`pitch` ≈ 0。
6. **MatrixProduct 单操作数**：只设第二操作数（`second.b = ta`、`a` 缺省）走 `b_->matrix( m )` 分支而非后乘分支；另补一个双边 `flip`。
7. **Vector4Product 单操作数 + getRegister 回读**：`second.b = (1,2,3,4)` 缺 `a`；`Math.getRegister( 0 ).value.x`。
8. **Vector4Combiner 缺操作数早退**：`Vector4Combiner().value` 与操作数置 None 后取值。
9. **look-at 上向量回退**：视线近 +Y 时 LOOKAT / LOOKAT_SCALEZ 姿态的上向量回退臂（up.source=(0,2,0,0)，断言行列式非零）。
10. **Vector4Shader 两拒绝臂**：用户寄存器上限（65 个 `addInputVector` 后 shader 仍健康）；非临时 provider 被要求充当输出寄存器的 `addOp( 6, 常量, 常量 )` 形态。

**平台边界发现（Windows-only）**：ProviderStore 的矩阵半边在本平台不可达——`MatrixProvider` 只在 autoTick 构造时注册进 store（ctor :40 / dtor :49），而唯一这么构造的子类 `MatrixAnimation` 整体包在 `#if defined( _WIN32 )` 内（script_math.cpp:1016 起），`nm libpyscript.a` 取证 MatrixAnimation 符数为 **0**。Linux 侧 store 的 `add`/`del` 矩阵重载与 `tick` 矩阵循环体是**构造性死区**，非缺测试；Vector4 半边（`Vector4Animation` autoTick=true）有测试触达。测试文件以代码注释钉死该结论，不硬凑。

**覆盖率对账（gcovr，精准插桩口径）**：1000/1083（92.3%）→ **1050/1083（97.0%）**，+50 实例行。残余 33 行全数分类登记：

- **Windows-only 构造性死区（13 行）**：40/49（autoTick ctor/dtor 臂）、5269/5271/5272（store add 矩阵重载）、5281/5283/5284/5286/5288/5290（store del 矩阵重载）、5322/5323（tick 矩阵循环体）；
- **死臂（3 行）**：1241-1243 `safeSetVector` 失败分支（`setVector` 恒返 true）；
- **死成员（8 行）**：2231/2233/2238/2240/2245/2248/2253/2256——`pyGet_x/y/z/w` 单字符 getter 体，被 `PyVector::pyGetAttribute` 的单字符截获取代后永不调用的历史接口（属性表仍注册但先被拦截）；
- **宏归属幻影行（9 行）**：796、3258、4329/4330、4512/4513/4514、4657/4658——`PY_RW_ATTRIBUTE_DECLARE`/`PY_SCRIPT_CONVERTERS` 在 .cpp 的展开点：访问器体实际生成于 pyobject_plus.hpp 头内行（pyGet :430-431 / pySet :467-468），执行归属头行，.cpp 展开点行恒 0，而对应读写已被测试触达（如 Vector4Swizzle 的 y/z/w 读写）。

**门禁记录**：pyscript_test 118→**128** 用例全绿连跑 3×（插桩态）；删桩复验（gcda=0、对象 gcov 符数=0）后全量 21 模块门禁 **1259** 全绿（GATE_EXIT=0）。插桩态只跑受测模块、删桩后才能全量的纪律同批次 24。

表值 655/1920 撤出 TESTING.md TODO 表，以精准口径 1050/1083（97.0%）登记收口。下批首位候选 `resmgr/datasection.cpp`（600/878、31%，表上剩余候选最大项）。

提交范围：`lib/pyscript/unit_test/test_script_math_gaps.cpp`（新）、`lib/pyscript/unit_test/Makefile.rules`、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、本文档。

### 7.26 覆盖率补强批次 26：datasection.cpp 直驱收口（2026-10-13）

TODO 表候选落位。`resmgr/datasection.cpp` 是 resmgr 的抽象基类层（878 行表值口径下 600 行未覆盖），此前无专属测试——覆盖全部来自 bwresource/xml_section/zip_section 等宿主测试顺带。本批先以精准插桩口径复判（只重编 resmgr TU，同批次 21/23/24/25）：**169/876（19.3%）**——表值 600/878 属 2026-09-28 全量插桩口径，不可直接换算；复判后真实暗区 707 行，几乎覆盖 read*/write*/as*/set* 全族、迭代器、copy/compare、sanitise、setWatcherValues、createAppropriateSection 回落臂。新建 `test_datasection.cpp` 21 用例。

**用例覆盖面**（21 用例）：

1. **迭代器全协议**：默认构造互等/对 begin 不等、前增协议、后增旧位拷贝（`old = it++` 后 old.tag()="a"、it.tag()="b"）、`==`/`!=`（含同索引异 section 不等）、`operator*`、`tag()` 经 childSectionName。
2. **索引与首子**：getIndex 命中/异客 -1、childSectionName、openFirstSection 双臂。
3. **openSection 路径学**：空路径→自身、深路径、尾斜杠停在命名子、缺叶拒绝/创建、单标创建。
4. **openSections**：平铺 2、嵌套 "a/b" 2、"zz/b" 0、"missing" 0。
5. **deleteSection/deleteSections**：深路径删叶、重复删除失败、顶层缺失失败、deleteSections 扫荡同名。
6. **16 型标量读写往返 + 缺失默认**（bool/int/uint/long/int64/uint64/float/double/string + 深路径）。
7. **wideString/blob/binary 往返**：binary 需 BinSection 宿主——XML 子持基类 setBinary 拒写，恰好反证基类 setBinary :1973-1976。
8. **Vector2/3/4/Matrix34 往返 + 缺失默认**（Matrix 经 applyToOrigin 校验）。
9-10. **复数读写 11+11**：标量族与向量/矩阵族平铺与嵌套路径；"no/parent" 读空臂。
11-12. **基类 as\* 15 式默认回显**（BinSection 只覆写 binary 族，其余全部落到 datasection.cpp 基类体）+ **基类 set\* 全拒**（setBinary 例外为 true）。
13. **createAppropriateSection 五臂**：合法 XML、allowModifyData=false 拷贝保护（缓冲首字节不变）、非法 XML 回落 BinSection（dynamic_cast 断言）、allowModifyData=true 拒绝（NULL）、creator 短路。
14. **splitSaveAsFileName 三臂（经 BinSection::save）**：protected 直测不可达。缺失父拒；无斜杠臂写 "."（fopen 目录失败）——首版写 "lots_of_files" 曾把文件真实落进 game/res/bigworld/（BWResource 根为 DirSection(MultiFileSystem)，写穿搜索路径），已删并改设计；zip 父臂 "exist.zip/sub" 命中成功臂且以 purgeAll 前后包夹，防缓存内变更泄漏给同进程后跑的 test_zip_section。
15. **setWatcherValues**：空路径早退、`DataWatcher<int>` 注册后驱动 77 写入、dot→space 重试臂、空值双失败静默。
16-17. **copy**：Bin→Bin modifyCurrent=true 短路拷贝/false 不拷（须用新 dst——首版复用已拷贝的 dst 导致断言自相矛盾）；XML→XML 全树 compare==0（首版根 tag 不同被 compare 的 sectionName 检查否决，改同根名）。
18. **copySections**：tagged 选择拷、all 合并覆盖（同 tag 后写胜）。
19-20. **compare/binaryCompare 全差异臂**：NULL→1、名异、值异、数异、子异 break 臂、相等 0（首版 binaryCompare 的"相等树"夹具带值差被 -1 否决，改同构夹具）。
21. **复数缺失父守卫族（19 条）+ sanitise 族**：DirSection 挂不存在基路径（`new DirSection("b26_missing_root_dir", nativeFileSystem)`）使 writer 侧 `newSection→makeDirectory` 失败、reader 侧 openSection 落空，一次性驱动全部 20 条 `if(!pSection)` 守卫且零文件副作用（mkdir 不递归建父）；sanitise/unsanitise 基类恒等、sanitiseSectionName false、isValidSectionName true（XMLSection 覆写版行为不同，宿主换 BinSection）。

**陷阱与取证**：

- **迭代器 operator\* 空子回退臂 = 到达即崩**（datasection.cpp:90-93，唯一残余 3 行）：`openChild(index_)` 落空时回退体调用 `dataSection_->childSectionName( index_ )`，而基类 childSectionName（:166-169）实现为 `this->openChild( index )->sectionName()`——在同场景（子已删/索引越界）恰好 NULL 解引用。测试实证：`begin()` 后 `delChildren()` 再 `*it` → SIGSEGV（gdb 栈顶 datasection.cpp:168）。该臂是防御性死代码且自身携带崩溃隐患，登记不修（批次 22/24「不修只记」先例）；实际触达需 begin() 与解引用之间子表收缩，正常协议下不可达。
- **BWResource 根写穿**：rootSection 是 `DirSection("", MultiFileSystem)`，save 经它 writeFile 会落进搜索路径（game/res/bigworld 优先）。凡断言"写失败"的用例，写目标必须天然不可创建（"." 目录名）。
- **BWResource 缓存跨测试文件污染**：同进程后跑的 test_zip_section 会拿到被污染的缓存 zip（test_datasection 按 link 序先跑），zip 父臂用例必须 purgeAll 包夹。
- **测试运行 CWD 伪迹**：resmgr_test 会在 CWD 落 `xmldataresource.xml`（test_data_resource 侧行为），全量跑测试统一在仓外沙箱目录执行。

**门禁记录**：resmgr_test 50→**71** 用例全绿（插桩态）；删桩复验（gcda=0、对象 gcov 符数=0）后全量 21 模块门禁 **1280** 全绿（GATE_EXIT=0）。精准口径复判 **169/876 → 873/876（99.7%）**，唯一残余即上述 :90-93。

表值 600/878 撤出 TESTING.md TODO 表，以精准口径 873/876（99.7%）登记收口。下批首位候选 `pyscript/pywatcher.cpp`（416/730、43%，表上剩余候选最大项）。

提交范围：`lib/resmgr/unit_test/test_datasection.cpp`（新）、`lib/resmgr/unit_test/Makefile.rules`、`lib/resmgr/unit_test/CMakeLists.txt`、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、本文档。

### 7.27 覆盖率补强批次 27：pywatcher.cpp 直驱收口（2026-10-13）

TODO 表首位候选落位。`pyscript/pywatcher.cpp` 是 BigWorld.addWatcher/addFunctionWatcher 的承载层与 watcher↔Python 双向转换层（730 行表值口径下 416 行未覆盖，表上剩余候选最大项）。原型 test_pywatcher.cpp 只走 Happy-Path 往返。本批先以精准插桩口径复判（只重编 pyscript TU，同批次 21/23/24/25/26）：**312/717（43.5%）**——表值 416/730 属 2026-09-28 全量插桩口径，不可直接换算；复判后真实暗区 405 行。新建 `test_pywatcher_gaps.cpp` 22 用例（pyscript_test 128→150）。

**用例覆盖面**（22 用例）：

1. **PyObjectWatcher 特化分发**：mapping/sequence 特化 watcher 的 getAsString/setFromString/getAsStream/setFromStream 非空路径四臂、visitChildren 计数与标签、恒假 addChild（字符串名经 MapWatcher 指针解析必 miss——watcher.hpp :115 通用指针解析器，测试断言干净失败面）。
2-9. **SimplePythonWatcher 全操作面**（公共类但生产树零实例化，只能测试 TU 直驱）：getAsString 标量族（NULL/bytes/str/孤代理/int/BadStrNoDict）、getAsString 目录与路径（dict/Widget/list `<DIR>`、递归取子、缺子空串）、setFromString/setFromStream 恒 true 桩、getAsStream 标量族与目录路径族（孤代理编外失败臂、缺子流空串）、visitChildren dict/实例/递归/stopper 语义（addWatcherPath 返回 false 截断但计数回调仍报全量）/NULL base。
10-11. **PyAccessorWatcher 流路径**：getAsStream 全臂（正常值、`__doc__` 注释臂、getter 抛错、str() 抛错、孤代理 str 三错误族、setFromStream NULL 流/坏载荷 UNKNOWN/setter 抛错/置后重取失败四拒绝臂）。
12-17. **PyFunctionWatcher**：INT/UINT/FLOAT/BOOL/UNKNOWN 实参提取与混合元组、四类坏载荷尺寸拒绝臂（int64 拒 3、uint64 拒 3〔避批次 22 登记的 size-4 缺 else 缺陷〕、double 拒 3〔收 8/4〕、bool 拒 2）、返回值编码（int/超 int64 大整数失败臂/float/bool/bytes/plain object）、stdio 钩子收集与调用失败、deferred 三态（callback/errback 带 value/errback 无 value）、BadDeferred 接线失败 printError 臂（:1445）。
18-22. **模块级错误臂**：py_setWatcher 的 str() 抛错与孤代理两臂、py_addWatcher setter 不可调用、py_addFunctionWatcher 的非 callable/坏实参表/坏实参元组/重路径四臂（全部带 comment——comment 形参"可选"实不可省，见下）。

**陷阱与取证**：

- **pending Python 异常毒化 = 假绿新形态（本批最重要教训）**：SimplePythonWatcher 缺子路径（pythonChildBase :1698 末段 `PyObject_GetAttrString` 在 mapping miss 后补一枪属性查找）会把 KeyError/AttributeError 遗留在错误指示器上；孤代理 encode 失败的 traceback 打印（:893/:1732 PyErr_PrintEx）写含 U+D800 的文本到被捕获的 stderr 时自身再抛 UnicodeEncodeError 也可能遗留。指示器非空期间所有 `PyImport_ImportModule`（fixture 模块导入）静默失败，而测试里 `if (pModule == NULL) return;` 让用例静默跳过、整体照样报绿。对策三件套：每个此类用例后 `PyErr_Clear()`、fixture 取回全部 CHECK 化（29 处）、fixtureModule() 失败即 PyErr_Print 并清除。**批次前段一度把 :1775-1791 判为 gcov 内联归属怪癖（批次 18 族），实为该毒化的静默跳过——结论撤回**；毒化修复后该块 17 行与 getAsStream 孤代理臂 :1834-1835 自然转绿，最终 670/717。
- **gcov 断点双证伪**：gdb 断点 :1831（AsUTF8String 调用行）无法绑定（行表合并到 :1829），:1834 断点绑定成功但全程不命中；PyErr_PrintEx 函数入口断点 47 次命中、栈内无 getAsStream 帧——孤代理 getAsStream 臂此前确实从未执行。结论靠断言逻辑闭包（CHECK(!getAsStream(surro)) 绿 + 该分支唯一 false 出口）+ 毒化根因闭环。
- **三处源码级隐患登记（不修只记，批次 22/24/26 先例）**：
  1. `SimplePythonWatcher::visitChildren` 映射臂对一切 `PyMapping_Check` 为真的对象先走 `PyMapping_Keys`——list/tuple/str/range 全部命中，keys 为 NULL 后 `PySequence_Fast(NULL)` 仍 NULL，`Py_DECREF(NULL)`（:1934-1938）崩溃（gdb SIGSEGV 栈顶 :1936 实证）；纯序列臂 :1943-1946 与索引标签臂 :1971-1975 因 `PySequence_Check` 内建全部同时过 `PyMapping_Check` 而结构性不可达。
  2. `getPyObjectWatcherData` 的 PyUnicode 分支对 `PyUnicode_AsUTF8String` 结果 MF_ASSERT（:757-763）——函数 watcher 返回孤代理 str 即进程中止；只有先做 NULL 检查的 PyAccessorWatcher 路径可携带孤代理载荷。
  3. `py_addFunctionWatcher` 的 comment 形参声明可选（"sOOi|s"）实不可省：PyFunctionWatcher 在 init() 校验前构造，`Watcher::Watcher(comment)` 于 watcher.cpp :546 `std::string(NULL)` 抛 logic_error——省略 comment 的注册必败（20 处 "Unhandled exception" 假失败经 gdb catch-throw 定位）。
- **Script::ask NULL 实参静默**：`pArgs == NULL` 时不调用直接返 NULL（script.cpp :1115 起）——无参函数 watcher 必须显式传 `PyTuple_New(0)`，否则 deferred 回调永不接线（'cb' 不落地）。

**门禁记录**：pyscript_test 128→**150** 用例全绿（插桩态与无桩态双验）；删桩复验（gcda=0、对象 gcov 符数=0）后全量 21 模块门禁 **1302** 全绿（GATE_EXIT=0；network_test 首跑 1 失败为 TESTING.md 已登记的主机 sysctl 回落陷阱，恢复 rmem_max 后复跑绿）。精准口径复判 **312/717 → 670/717（93.4%）**，残余 47 行全数分类：putPyObjectOnStream FLOAT/BOOL/STRING 失败臂+default+getPyObjectWatcherData NULL 臂（唯一调用方 finishSetFromStream :1252-1268 忽略返回值，结构性死代码）、STRING 提取器 break :616（自描述不可失败）、GetItem/PyTuple_New 边界臂 :1186/:1191/:1207/:1338/:1340（长度同包刚读出）、Py3 迁移死臂 :1766/:1870（PyObject_Str 恒返 str 或 NULL）、序列分支不可达 :1683-1696/:1943-1946/:1971-1975、需活 stdout 钩子臂 :1300/:1393/:1395-1396、fini 后窗口臂 :845/:847。

表值 416/730 撤出 TESTING.md TODO 表，以精准口径 670/717（93.4%）登记收口。下批首位候选 `resmgr/bwresource.cpp`（386/746、48%），次选 `entitydef/method_description.cpp`（323/478、32%）。

提交范围：`lib/pyscript/unit_test/test_pywatcher_gaps.cpp`（新）、`lib/pyscript/unit_test/Makefile.rules`、`lib/pyscript/unit_test/res/test_pywatcher.py`、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、本文档。（pyscript 的 CMakeLists.txt 为本仓 cmake 双轨未覆盖的滞留目标——缺 test_pywatcher.cpp 等 10+ 个已参编 TU，批次 23-25 均未触碰，本批同此，留待组件 CMake 推广事项统一处理。）

### 7.28 覆盖率补强批次 28：bwresource.cpp + method_description.cpp 双收口（2026-10-13）

TODO 表两个候选同时落位（bwresource 386/746 48%、method_description 323/478 32% 均为 2026-09-28 全量插桩表值口径）。两文件都走精准插桩口径复判（只重编 resmgr / entitydef TU，同批次 21/23-27）：**bwresource 322/746（43%）**、**method_description 153/466（32%）**——method_description 表值与精准值差一倍，因宿主测试分布在 entitydef 之外的模块，全量口径下被分摊。新建 `test_bwresource_paths.cpp` 17 用例（resmgr_test 71→88）+ `test_method_description_gaps.cpp` 33 用例（entitydef_test 285→318），合计 50 用例，门禁 1302→**1352**。

**bwresource.cpp 43% → 62%（322/746 → 464/746）用例覆盖面**（17 用例）：

1. **路径格式化族**：getFilename（正/反斜杠/空串/尾斜杠）、getExtension（含点前缀/无扩展/目录内）、removeExtension、changeExtension、pathIsRelative（相对/绝对/反斜杠/盘符）、formatPath（补尾斜杠/空串臂——空串不补成 "/"）。
2. **getFilePath 平台语义 #ifdef 双臂**：非 Windows 走 dirname（"a/b/c.txt"→"a/b"、"c.txt"→"."、反斜杠视为普通字符）；Windows 臂登记不测。
3. **getCurrentDirectory 非 Windows TODO 空串臂**（源码内 TODO stub）。
4. **路径列表**：getPathNum/getDefaultPath/getPath 越界空串臂（-1、size()、size()+100）/getPathAsCommandLine（引号包裹）/isValid/path 缓冲区出入参。
5. **addPath/delPath**：默认追加（-1 转 size_t 越过范围检查落到 size()，测试注释钉死）、index 0 头插、delPath 三连还原。
6. **purge/purgeAll/save**：DataSectionCensus::find 联动、非 census save 拒绝臂。
7. **watchAccessFromCallingThread + RAII holder 双向**、checkAccessFromCallingThread 两种状态下安全。
8. **modification monitor 族**：监听器增删、enableModificationMonitor、flushModificationMonitor、hasPendingModification、ignoreFileModification 一次性（ACTION_MODIFIED）与永久（ACTION_DELETED）两臂。
9. **文件类型查询**：isDir/isFile/fileExists（res 相对名）vs fileAbsolutelyExists（原生绝对路径）双轨、getFileType FT_FILE/FT_DIRECTORY。
10. **modifiedTime/isFileOlder**：utime 回拨 36000s 做确定性比较；只测 0 阈值（见隐患 4）。
11. **resolveToAbsolutePath**：FT_FILE/FT_DIRECTORY/FT_NOT_FOUND 与 res 相对解析（返回值带前缀）。

**method_description.cpp 32% → 70%（153/466 → 329/466）用例覆盖面**（33 用例）：

1. **parse 默认值/选项**：replayExposureLevel 默认 OTHER_CLIENTS、priority 默认 FLT_MAX 哨兵、exposed 组件位（CELL+OWN/ALL 合法、BASE+ALL 拒绝、Exposed on CLIENT 拒绝、BOGUS 拒绝）、replay 暴露三级+BOGUS 拒绝、detailDistance 平方换算（100→10000）。
2. **old-style 解析**：`<Arg>` 子节点族、空名臂、returnValues 与 NOT_A_TYPE 失败臂、arg 失败回卷。
3. **exposed 索引/msgID**：setExposedIndex 组件位、internalIndex 直读、NULL range → index 2 且 msgID uint8 回绕 255（Mercury::MessageID 为 uint8，-1 哨兵读作 255，注释钉死）、range(10,19) 两例算术（5 exposed & exposedID 3 → msgID 13 sub -1；12 exposed & exposedID 10 → msgID 19 sub 1）。
4. **流协议**：addSubMessageID 0 vs 1 字节、streamSize（定长 4、exposed CELL true 4+sizeof(EntityID)、STRING 与子索引 -1）、add_to_stream/server/client（exposed 前置 EntityID 42 往返；client 面需先 setExposedMsgID）、returnValues 流往返（uint8 0 + int32 7）。
5. **md5**：同 parse 同摘要、异 args、异 legacy index。
6. **clientSafety**：INT32 → 0、MAILBOX → CLIENT_UNUSABLE 位（UNSUPPORTED_TYPE）、shouldRecord。
7. **toString 唯一可达警告点**：MemberDescription protected / MethodDescription private（子类直调被编译器两次拒绝）→ 经 VariableLengthHeaderSize≠default + setExposedMsgID 触发 WARNING 分支驱动，oversizeWarnLevelAsString()=="log"。
8. **杂项**：components、is_signature_equal、p_watcher 单例非空、createErrorObject（环境无关 exists 断言）、areValidArgs（exposed CELL firstOrdinaryArg=1 / None source 合法 / 字符串 source 非法）、areValidReturnValues、extractSourceEntityID（plain 不变 / exposed 剥 123 / None → NULL_ENTITY_ID / 空 tuple → null）、convertKeywordArgs、createDictFromTuple。
9. **getArgsAsTuple 四臂**：valid、空 → null、多余数据 CHEAT null、exposed（**流只含 args、sourceID 作形参**——消息头在上游已剥，故测试只写 int32 实参、以 42 作 sourceID 实参 → (42,5)）。
10. **returnValuesOrFailure**：成功 uint8 0、失败 uint8 1 + 错误名 + 空串 → 1 + null、pickled 分支以 !pickled.empty() 守卫。
11. **callMethod/isMethodHandledBy**：exec 定义类 + eval 建实例（**Py_file_input 返 None，须 exec-then-eval**——批次 27 毒化教训的应用：每步后 clearPendingError + 取回全部 CHECK 化 + get() != Py_None 守卫）。

**登记不修只记（五件）**：

1. `addSubPaths` 的 `STRINGVECTOR newPaths( paths.size() )` 预分配 size 个空串污染路径列表。
2. `ensurePathExists`/`ensureAbsolutePathExists` 先把 '/'→'\\' 再 mkdir——非 Windows 下恒建不了目录（Windows-only 行为）。
3. `isFileOlder` 把 mustBeOlderThanSecs 乘 1e7（Windows 100ns 单位）而 unix_file_system.cpp:119 存 st_mtime（秒）→ 非零阈值在 Unix 恒 inert；getCurrentDirectory() 非 Windows 返 ""（TODO stub）。
4. `returnValuesStreamSize` 在 method_description.hpp:257 声明但**全仓无定义**（死声明，不调用只记）。
5. 全仓无 BWTwoWay 模块 → staticInitTwoWay() 失败、createErrorObject 返 null ScriptObject；Mercury::MessageID 为 uint8 故 -1 哨兵读作 255；DEFAULT_VARIABLE_LENGTH_HEADER_SIZE = 1（network/interface_element.hpp:75）；MAILBOX → UNSUPPORTED_TYPE（data_types.cpp:114）→ clientSafety CLIENT_UNUSABLE(0x2)。

**门禁记录**：精准口径 **bwresource 322/746 → 464/746（62%）**、**method_description 153/466 → 329/466（70%）**；残余 bwresource 282 行（impl 初始化 188-484、purge 递归 551-564、searchPathsForFile/searchForFile 1179-1378、EDITOR_ENABLED 死区 1389-1392/1493-1579、ensurePathExists 族 1886-1974、addPaths/addSubPaths 族 2054-2213、fixCommonRootPath 2285-2353、monitor 包装 2558-2640、overrideAppDirectory/dissolveFilename 2702-2883，以 el7 服务端构建结构性不可达面为主）与 method_description 137 行（PyDeferredResponse 58-135、copy/赋值 254-268、createErrorObject 带 func 288-298、parse 缺 name 347-348、object-with-id 636-646、handleException 族 747-798、callMethod 错误/returnValues 分支 850-923、网络发送路径 970-1071、2 参 createArgsFromStream 1163/1166、:1216）全数分类登记。删桩复验（gcda=0、4 个对象 gcov 符数=0）后全量 21 模块门禁 **1352** 全绿。下批首位候选 `network/machine_guard.cpp`（28%，bwmachined 面之外仍有可测面；受限大件 entitydef/entity_description.cpp 665 行、moo/image.ipp 589 行维持候选）。

提交范围：`lib/resmgr/unit_test/test_bwresource_paths.cpp`（新）、`lib/resmgr/unit_test/Makefile.rules`、`lib/entitydef/unit_test/test_method_description_gaps.cpp`（新）、`lib/entitydef/unit_test/Makefile.rules`、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、`README.md`、`todo.md`、本文档。

### 7.29 覆盖率补强批次 29：entitydef/entity_description.cpp 直驱收口（2026-10-15）

TODO 表候选 `entitydef/entity_description.cpp`（665 行）的「受限：需 EntityType/EntityDef XML 装载机全套」判定**被推翻**——生产侧唯一调用点 `entity_description_map.cpp:362` 经 `desc.parse( typeName, /*componentName*/ BW::string(), pDistributionDecider )` 传入，全程只依赖 BWResource 单例与 scripts/entity_defs 下的 fixture .def 树。新建 `test_entity_description.cpp` 30 用例（entitydef_test 318→348），精准插桩口径 **0% → 64%（424/663 实例行）**，门禁 1397→**1427**。

**entity_description.cpp 0% → 64%（424/663）用例覆盖面**（30 用例）：

1. **parse 最小形态与失败面**：最小 .def 解析、标量属性 Flags 各位与 Persistent、缺 .def 失败、parseComponents 下 property(0) 为实体自有属性（hp）、组件属性在其后。
2. **方法三列表**：BaseMethods/CellMethods/ClientMethods；`<Exposed/>` 的两种正确形态（`<baseExposed><Exposed/></baseExposed>`）与 ClientMethods 内 `<Exposed/>` 恒拒绝；无 Client 脚本时 ClientMethods 拒绝。
3. **继承与优先级**：Parent 递归继承与缺父失败、Volatile 优先级平方与 yaw≥pitch≥roll 校验、LoDLevels 标签匹配 detailLevel、parseDistribution 标签解析与 detail 三 arm。
4. **decider 面**：Distribution 三 decider 位、ClientName 弃用告警、parseDecider 无 scripts 失败。
5. **TempProperties / Components / Implements / parseService**：TempProperties 解析；Components 装载、缺 def 失败、重名失败；Implements 接口属性；parseService 的 Methods→base 列表映射与强制非持久化；无 base 失败。
6. **checkExposedMethods 四象限**：proxy/非 proxy × 合法/非法；SupersedeSmoke。
7. **getDataDomains 全映射**：BASE_ENTITY_BACKUP/OFFLOAD→BASE_DATA、CELL_CREATION/CELL_ENTITY_BACKUP/OFFLOAD→CELL_DATA、CELL_GHOST_CREATION→CELL_DATA|ONLY_OTHER_CLIENT_DATA、CLIENT_PLAYER_CREATION→FROM_BASE_TO_CLIENT_DATA、CLIENT_PLAYER_CELL_CREATION→FROM_CELL_TO_CLIENT_DATA、DATABASE_ENTITY→BASE_DATA|ONLY_PERSISTENT_DATA；CLIENT_ENTITY_CELL_CREATION/CLIENT_ENTITY_LOD_DATA 两个 CRITICAL_MSG 不可达臂登记。
8. **shouldSkipPass 矩阵**：EXACT_MATCH 置位时仅当 dataDomains 与 PASS_JUMPER[pass] 精确相等才跑（4×4 矩阵逐位断言），否则按位重叠判定；shouldConsiderData 各 pass 过滤（PASS_FILTER 双位）与 ONLY_PERSISTENT_DATA 臂。
9. **流往返**：addSectionToStream→readStreamToDict 往返与 addSectionToDictionary 直写 dict。

**实测坑位**：`EntityDescription::parse` 第二参 componentName 生产恒传空串——传实体名会把全部属性标记为 componentised、fullName 带 "Entity." 前缀，addSectionToStream 按 fullName 找值落默认值 0（gdb + PyObject_Print 实证 dict = `{'batch29Stream.hp': 0}`）；Script 属性须用 `ScriptInt::check(ScriptObject)` + `ScriptInt(p.get()).asLong()`（无 isInteger()）；XMLSection 需 `#include "resmgr/xml_section.hpp"`；StreamContentType 枚举在 BW 命名空间而非 EntityDescription 内。

**残余登记（239 行）**：readStreamToSection/readTaggedClientStreamToDict/addAttributesToStream/addDictionaryToStream 网络流读侧、MD5 族（addToMD5/addPersistentPropertiesToMD5）、setExposedMsgIDs、checkMethods、pIdentifier/isTempProperty/clientServerProperty 族访问器、getDataDomains 两个 CRITICAL_MSG 不可达臂与析构/模板实例暗区。

提交范围：`lib/entitydef/unit_test/test_entity_description.cpp`（新）、`lib/entitydef/unit_test/Makefile.rules`、`cmake/samples/entitydef/CMakeLists.txt`、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、`README.md`、`todo.md`、本文档（§7.29）。commit `9d9dc38b`。

### 7.30 覆盖率补强批次 30：network/machine_guard.cpp 直驱收口（2026-10-15）

TODO 表下批首位候选 `network/machine_guard.cpp`（批次17 登记的 28% 全量口径）。MGM 消息编解码面**不依赖 bwmachined**——全部 16+1 个消息类自持 wire 格式，写侧 readImpl/writeImpl 与读侧 `MachineGuardMessage::create` 配对即可直驱。新建 `test_machine_guard_messages.cpp` 27 用例（network_test 229→256），精准插桩口径 **28% → 87%（668/764 实例行）**，门禁 1427→**1454**。

**machine_guard.cpp 28% → 87%（668/764）用例覆盖面**（27 用例）：

1. **机器统计面**：HighPrecisionMachineMessage / WholeMachineMessage 全字段写-读往返（Low/High 两套 InterfaceStats 位宽差异钉死）+ c_str。
2. **进程面**：ProcessMessage 全字段往返（含额外块：major/minor/patch/interfaceVersion/username/defDigest）、额外块前向兼容跳过（声明多 4 字节 + 追写 4 字节 dummy，consume 干净、无 error）、欠声明报错（声明 1 → 字段吃完 → is.error()）；matches 六参逐位断言（category/uid/pid/port/id/name，未置位忽略）；categoryToString 三臂（SERVER_COMPONENT/WATCHER_NUB/UNKNOWN）与 c_str 嵌入。
3. **ProcessStats / Listener / Signal**：ProcessStats 往返 + c_str；Listener 往返 + c_str 的 RegBirthL/RegDeathL 两标签臂（按 PARAM_IS_MSGTYPE + ADD_BIRTH/DEATH_LISTENER 位）；Signal 往返 + setControlledShutdown=SIGUSR1 / setKill=SIGINT / setHardKill=SIGQUIT 三臂。
4. **创建面**：Create 往返 + c_str（CreateMessage / c_str_name / uid 三形态）；CreateWithArgs 往返 + c_str 的 args 列表与空列表（`args=[]`）臂。
5. **杂项消息**：Tags（count + exists_ + strings）、User（PARAM_CHECK_COREDUMPS 下 CoreDump 三元组 / PARAM_GET_VERSION 下 versionString / 无 flag 时双空）、init(struct passwd&) + getConfFilename（`%s/.bwmachined.conf`）、matches 两参位、Reset、Pid、MachinedAnnounce（birth count / death addr union 双 arm + MachinedBirth/MachinedDeath）、MachinePlatform（直读 platformInfo_）、UnknownMessage（create 回显 0x55 + MESSAGE_NOT_UNDERSTOOD 位 + body 保留 + c_str "4 bytes long"）。
6. **ReplyHandler 面**：RecordingReplyHandler 16 型分发矩阵（每个 on*Message 落位断言）+ 未知型默认臂（0x55 → onUnhandledMsg）；基类 ReplyHandler 16 个默认 on*Message 全落 onUnhandledMsg（返回 true 保持处理）。

**实测坑位**：CppUnitLite2 的 `CHECK_EQUAL(const char*, const char*)` **比较指针**（45 处 assert 全部要 strcmp 化，否则报 "expected: 'x' but was: 'x'" 同文不同址）；`MachineGuardMessage::write` 非 const（roundTrip 助手取非 const 引用）；精准插桩在 libnetwork.a 场景须**重打包静态库**（instrumented obj 复制进 build 树 obj 后须 `make libnetwork` 重归档，否则链上的是旧库）；g++ 链接须**显式 `--coverage`** 才链 libgcov（否则 __gcov_exit/__gcov_merge_add 未定义）；gcda 落在**编译期 -o 路径**（/tmp 暂存目录）而非 build 树 obj 目录，gcovr 以该目录为根。

**残余登记（96 行）**：sendto / sendAndRecv(Endpoint&) / sendAndRecv(srcip,destaddr) / sendAndRecvFromEndpointAddr 的 UDP 套接字面 83 行（需活 socket + 绑 PORT_MACHINED=20018 回环，引入环境依赖，登记不测）；typeStr 的 `** UNKNOWN **` 默认臂 12 行；seq ticker 回绕臂（s_seqTicker_ 达 0xffff 后 ++）1 行。

提交范围：`lib/network/unit_test/test_machine_guard_messages.cpp`（新）、`lib/network/unit_test/Makefile.rules`、`lib/network/unit_test/CMakeLists.txt`、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、`README.md`、`todo.md`、本文档（§7.29 补记 + §7.30）。

### 7.31 覆盖率补强批次 31：entitydef/entity_description.cpp MD5 族直驱（2026-10-15）

§7.29 残余登记中的 **MD5 族**（addToMD5 / addPersistentPropertiesToMD5，entity_description.cpp:1740-1822）本批直驱——这是 L36 热重载设计指定的 schema 版本原语（.def 变更检测以实体摘要为准）。既有 TU `test_entity_description.cpp` 追加 11 用例（entitydef_test 348→359），精准插桩口径 **64% → 70%（424/663 → 468/663 实例行）**，门禁 1454→**1465**。

**摘要规则（从源码钉死）**：`addToMD5` = 名称 + 逐属性（仅 `isClientServerData()` 者：`clientServerFullIndex()` 原生序 int + `DataDescription::addToMD5`）+ client 方法**全部**（`method->addToMD5(md5, count++)`）+ base/cell 方法**仅 `isExposed()` 者**（且 count 只对入选者递增，非暴露方法不挤位）；`addPersistentPropertiesToMD5` = 名称 + 逐属性（仅 `isPersistent()` 者）。CELL_PUBLIC（DATA_GHOSTED）与 BASE（DATA_BASE）均不含 OTHER_CLIENT/OWN_CLIENT/REPLAY 位，`isClientServerData()` 为假——三分区：hp(CELL_PUBLIC) 双摘要零贡献、owner(BASE+Persistent) 只进持久摘要、nick(ALL_CLIENTS) 只进全量摘要。

**entity_description.cpp MD5 族用例覆盖面**（11 用例）：

1. **确定性**：同字节重解析，双摘要相等（回归守卫：摘要只依赖 schema 内容，无指针/解析顺序噪声）。
2. **CELL_PUBLIC 零贡献**：hp 的类型 UINT16→FLOAT32 整体替换，双摘要均不动。
3. **client-server 属性只进全量摘要**：加 nick(ALL_CLIENTS) → 全量摘要动、持久摘要不动。
4. **BASE+Persistent 属性只进持久摘要**：hp 改 BASE+Persistent → 持久摘要动、全量摘要不动。
5. **非持久属性对持久摘要零贡献**：加 nick(ALL_CLIENTS) 后持久摘要与空 def 相同（全量摘要不同作对照）。
6. **未暴露 base 方法零贡献**：带非暴露 basePing 的摘要与无 BaseMethods 的摘要相等。
7. **暴露 base 方法即入摘要**：`<Exposed/>` 使摘要变化（flags_ 暴露位 + 摘要收录双重效应）。
8. **cell 方法同一暴露门控**：cellTick 非暴露不动、暴露即动。
9. **client 方法无暴露门控**：`<Exposed/>` 在 ClientMethods 恒拒绝（parse 失败），方法一旦存在即进摘要。
10. **暴露方法顺序敏感**：交换两个暴露 base 方法 → legacyExposedIndex 对调 → 摘要变化。
11. **client-server 属性顺序敏感**：交换两个 ALL_CLIENTS 属性 → clientServerFullIndex 对调（parseProperties 按 def 顺序编号，entity_description.cpp:664-671）→ 全量摘要变化而持久摘要不动（持久摘要无 index）。

**实测坑位**：① `BWResource::openSection` **先查 `DataSectionCensus` 再碰文件系统**（bwresource.cpp:614-625）——同一路径重写 .def 后重解析必须先 `BWResource::instance().purgeAll()` 驱逐，否则读到旧段（before/after 同名实体 fixture 的前置条件）；② CppUnitLite2 的 `CHECK` 宏展开为 `result_`/`m_name` 测试体成员，**自由助手函数里不可用**，用 `MF_VERIFY` 替代；③ `MD5::Digest` 默认构造不初始化字节（md5.hpp:29 `Digest() { }`），必须走 `Digest(MD5&)` 或 `operator=`；④ `BWResource::purgeAll()` 是成员函数非静态。

**精准插桩补遗**：`make -n` 对**已存在且最新**的 obj 静默无输出（"is up to date"），须加 `-B` 强制重算才吐编译行。

**残余登记（195 行）**：parseProperties 属性覆盖/override 臂、LoDLevels/Volatile/Implements/Components 解析面、addSectionToStream 流写侧、supersede 迁移面、watcher 面等。

提交范围：`lib/entitydef/unit_test/test_entity_description.cpp`（既有 TU 追加）、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、`README.md`、`todo.md`、本文档（§7.31）。

### 7.32 覆盖率补强批次 32：entitydef/entity_description.cpp 流/访问器/暴露消息号直驱（2026-10-15）

§7.31 残余登记中的**流写侧访客族 + 读侧 + 访问器 + 暴露消息号**四块本批直驱。既有 TU `test_entity_description.cpp` 追加 7 用例（entitydef_test 359→366），精准插桩口径 **70% → 89%（468/663 → 591/663 实例行）**，门禁 1465→**1472**。

**摘要/流协议规则（从源码钉死）**：`addToStream` 助手循环 `NUM_PASSES = 4` 个 pass，`shouldSkipPass` 按 `PASS_JUMPER[4]` = {BASE、BASE|CLIENT、CELL|CLIENT、CELL}（EXACT_MATCH 置位时精确相等、否则位重叠）决定跑不跑；`shouldConsiderData` 的 `PASS_FILTER[4][2]` 是 isBaseData × isClientServerData 二维真值表 → hp(CELL_PUBLIC：非 base 非 CS) 落 pass3、owner(BASE：base 非 CS) 落 pass0、nick(ALL_CLIENTS：非 base 且 CS) 落 pass2、pass1（base 且 CS）在无此类属性时贡献 0 字节。`pDataSizes` 每个跑过的 pass 记 `stream.size() - 该 pass 前尺寸`，故**必须按 4 开数组**（`numDataSizes=3` 时 `MF_ASSERT_DEV(numDataSizes == actualPass-1)` 放行但第 4 pass 不记录）。`clientServerProperties_` 经 `allocateClientServerFullIndexes` **按 `streamSize()` 稳定排序**（定长升序在前、变长按名义尺寸升序在后，tie 保持 def 顺序）——`clientServerProperty(n)` 的 n 是排序后序号而非 def 顺序，`clientServerFullIndex()` 也是排序后的号，tagged client 流的 index 同理。

**entity_description.cpp 流/访问器/暴露消息号用例覆盖面**（7 用例）：

1. **addDictionaryToStream 往返**：dict{hp:5, owner:7, nick:"bob"} 三段域全开 → 逐 pass 尺寸 {4, 0, 4, 2} 且和等于流总长 → readStreamToDict 回读值全对且 `is.remainingLength()==0`（字典映射面 + pass 分派 + 尺寸记账三合一）。
2. **缺键/错型/null mapping**：dict 只给 hp → owner 读回 0、nick 读回 ""（AddToStreamMapVisitor `isErrorOnNULL()==false` 走默认值）；hp 给 ScriptString（UINT16 期望 int）→ 换默认值后整体失败；null `ScriptMapping` → 前置拒绝。
3. **addAttributesToStream 往返与双臂**：Python 实例属性 → 流 → dict 回读全对；对象缺属性 → AddToStreamAttributeVisitor `isErrorOnNULL()==true` → 失败；NULL 对象 → 前置拒绝。
4. **readStreamToSection**：值 section → 流 → 新建 XML section，`readString("nick")=="bob"`、`readInt("secret")==9`。
5. **readTaggedClientStreamToDict**：手工按 tagged 格式组流（count u8 + 逐项 [clientServerFullIndex u8 + 该属性 addToStream 输出]），覆盖 other-client 与 own-client 数据；顺带断言 clientServerProperty 的排序结果 [score, secret, nick]。
6. **访问器**：pIdentifier（Identifier 属性须同时 `<Persistent>true</Persistent>`——parse 对 Indexed 默认为真，非持久 Indexed 直接 parse 失败）、isTempProperty、clientServerPropertyCount/clientServerProperty、无 Identifier 时 pIdentifier()==NULL。
7. **setExposedMsgIDs + checkMethods**：唯一暴露 base 方法取 range(10,19) 首槽 msgID=10、未暴露方法保留 -1 哨兵读作 255（Mercury::MessageID uint8 回绕）；checkMethods 的 warnOnMissing 双臂与「Python 类缺方法即 false」。

**实测坑位**：① `clientServerProperties_` 按流尺寸稳定排序（见上）——首个版本按 def 顺序取 index 0/1/2，把 ScriptString 喂给 UINT16 属性、ScriptInt 喂给 STRING 属性，`addToStream` 类型不匹配全红；② `NUM_PASSES=4` 而非常见猜测的 3，dataSizes 少开一个导致「逐 pass 尺寸和 8 ≠ 流总长 10」的假失败（未记录的第 4 pass 正好是 2 字节的 hp）；③ `MemoryOStream` 追加原始字节是 `addBlob`（无 `append`）；④ `XMLSection` 的 `asString`/`asInt` 是 **protected**，须经基类 `DataSectionPtr` 用 `readString`/`readInt`；⑤ `ScriptString` 取值用 `c_str()`（无 `asChar`）；⑥ **/tmp 配额**——精准插桩产出 187 MB 二进制，两轮插桩 + 多份 cp 副本很快把 tmpfs 写满（`cp: error writing … Disk quota exceeded`），插桩目录须随测随清。

**残余登记（72 行）**：parse 错误/警告臂（307-357）、LoD/Volatile 缺省臂（416,419）、parseComponents 缺臂（508,511）、parseProperties 覆盖与 clientSafety 臂（598-728）、parseMethods 错误臂（937-977）、checkExposedMethods 臂（1061-1075）、访客 MF_EXIT/边界臂（1163-1312）、读侧边界臂（1590-1702）、访问器 MF_EXIT 臂（1864）与 watcher 面（1882,1884）——以失败/断言臂为主，直驱需逐一构造失败输入，边际收益低。

提交范围：`lib/entitydef/unit_test/test_entity_description.cpp`（既有 TU 追加）、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、`README.md`、`todo.md`、本文档（§7.32）。

### 7.33 热重载回滚链 supersede 语义直驱（2026-10-15）

todo C 档「server 侧 reload 迁移语义测试承载」的下一片：`Entity::migrate` 的 `__class__` 换/回滚本体仍需 Entity 实例与运行时（harness 不可达，缺口原样），但其回滚路径**依赖的可达语义体**——`EntityType::old` → 旧描述 `supersede(CELL)`——在 lib/entitydef 层可直接驱动。本批把这套语义钉死并产出热重载设计稿的 **F7** 失败模式。既有 TU `test_entity_description.cpp` 追加 4 用例（entitydef_test 366→**370**），门禁 1472→**1476**，覆盖率口径不动（supersede 臂批次 29 smoke 已过线，本批补的是功能效应断言）。

**supersede 语义链（从源码钉死）**：`EntityMethodDescriptions::supersede`（entity_method_descriptions.cpp:218-231）清名→索引 map、全部方法改名 `old_` 前缀、按新名重建 map；`EntityDescription::supersede`（entity_description.cpp:892-904）按组件分派 BASE→base_、CELL→cell_、其余仅 WARNING。效果：`find(旧名)`→NULL、`find("old_"+名)`→同对象、`internalMethod`/`exposedMethod` 下标访问不受影响（走 exposedMethods_ 下标不查 map）。生产调用链：`Entity::migrate`（entity.cpp:4267，`pNewType->old(pOldType)` 先于 `__class__` 尝试 :4275）→ `EntityType::old`（entity_type.cpp:168-185，首设才生效，重复设不同值 dev 断言）→ 旧描述 `supersede(CELL)`；`__class__` 失败回滚 `pEntityType_ = pOldType`（entity.cpp:4287）**无 un-supersede**（全仓 grep 确认 supersede 仅 entity_description/entity_method_descriptions/entity_type 三处）。

**用例覆盖面（4 用例）**：
1. `EntityDescription_supersedeRenamesCellMethods`——supersede(CELL) 后 `find("cellTick")`/`find("cellHeal")`→NULL、`find("old_cellTick")` 可达且 `== internalMethod(0)`、`exposedMethod(0)` 仍解析且名字带 old_ 前缀（**msgID 派发面不受影响**的直接证据）；base/client 列不动。
2. `EntityDescription_supersedeRenamesBaseMethods`——supersede(BASE) 同语义，cell/client 列不动。
3. `EntityDescription_supersedeTwiceStacksPrefixes`——重复 supersede 前缀叠加（`old_old_cellTick`），印证 `EntityType::old` 首设守卫的必要性（重复调用是破坏性的）。
4. `EntityDescription_supersedeChangesMd5`——supersede 后 `addToMD5` 漂移（暴露方法改名直接进 digest），**schema digest 必须在 supersede 前采集**；无属性实体 persistent digest 不变（只含实体名，对照面）。

**F7（入 docs/upgrade-plan/hot-reload-productionization.md 失败模式清单）**：回滚"成功"的实体处于半残态——按名字的 mailbox 方法查找全断（`mailbox.cpp:122/240` `findMethod` → `description().cell()/base().find(attr)` → NULL → 脚本侧报错），按 msgID 的暴露派发仍活；设计稿「双版本兼容窗口」与「失败回滚」两节已按此修正（窗口只保得住下标/msgID 派发，按名补解析需另设计；逆操作必须同时回退 supersede 改名）。

提交范围：`lib/entitydef/unit_test/test_entity_description.cpp`（既有 TU 追加）、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、`docs/upgrade-plan/hot-reload-productionization.md`、`README.md`、`todo.md`、本文档（§7.33）。

### 7.34 覆盖率批次 34：entitydef/method_description.cpp 纯助手族直驱（2026-10-15）

已登记候选（残余 137 行）的**可驱面复核**结论：批次 28 分类中除纯助手族外全部维持受限（PyDeferredResponse 58-135 需两路调用上下文、handleException 族 747-798 与 callMethod 错误臂 850-923 需活 Mercury 通道或异常回复管线、网络发送 970-1071 需活通道、parse 空 name 臂 347-348 不可驱动——XML 段名不可为空、无合法构造路径；staticInitTwoWay 错误臂 254-298 需破坏 BWTwoWay 模块、环境操纵不做）。本批直驱四个**零覆盖**的纯助手（`test_method_description.cpp` 追加 5 用例，entitydef_test 370→**375**），精准插桩 329/466 70%→**337/466 72%**，门禁 1476→**1481**。

**语义面（从源码钉死）**：
- `extractSourceEntityID`（method_description.cpp:610-658）——Server→Server 调用约定核心：仅暴露 CELL 方法取参首槽作 sourceEntityID；int 直取（`convertTo` 成功路径）、None 记 NULL_ENTITY_ID、其余从 `.id` 属性回退（无属性报错返回空元组）；两种成功都**返回去头元组**喂 addToServerStream。非暴露/BASE 方法原样透传、sourceEntityID 不动。
- `convertKeywordArgs`（:716-719 → method_args.cpp:389-442）——位置前缀原样拷贝，命名尾槽**按名**从 kwargs 取值；kwargs 按**个数**计数（total = 位置数 + kwargs 个数 必须恰等于形参数），缺名/计数不符报错。注意不是「剩余 kwargs 任意分配」——尾参必须按声明名命中。
- `areValidReturnValues`（:941-951 → MethodArgs::checkValid）——按 `<ReturnValues>` 节校验元数与逐元素类型。
- `createDictFromTuple`（:1238-1241 → method_args.cpp:448-472）——合法元组 → 以声明返回名为键的 dict。

**实测坑位**：① `ScriptObject::operator==` 是**指针等价**（py_script_object.hpp:596，`obj1.get() == obj2.get()`）——值断言必须走 `ScriptString::check` + `c_str()` strcmp、`ScriptInt::check` + `asLong()`，任何 `obj == ScriptObject(ScriptString::create(...))` 形式都对临时对象比指针必败；② `ScriptDict` 无 `size()`（只有 next/getItem/setItem），dict 断言逐键 getItem；③ `<ReturnValues>` 节用命名子元素（`<name> STRING </name>`，MethodArgs::parse 默认旧样式、段名即参数名）；④ `ScriptTuple` 的 `size()` 返回 Py_ssize_t、`STANDARD_SCRIPT_OBJECT_IMP` 提供 `explicit ScriptTuple(const ScriptObject&)` 构造（check 后可安全转换）。

残余 129 行维持批次 28 受限分类（PyDeferredResponse/handleException/callMethod 错误臂/网络发送/staticInit 错误臂/parse 空 name 臂）。

提交范围：`lib/entitydef/unit_test/test_method_description.cpp`（既有 TU 追加）、`TESTING.md`、`docs/upgrade-plan/migration-status.md`、`README.md`、`todo.md`、本文档（§7.34）。
