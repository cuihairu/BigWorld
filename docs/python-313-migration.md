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

## 4. BWHooks 官方接口方案（替代 #define 重定向）

2.7 的机制是在 obmalloc.c/pymem.h 里 `#define malloc BW_Py_malloc` 等，把 CPython 内部所有裸 malloc 兜进钩子层。3.13 采用官方嵌入接口等价实现：

1. `PyMem_SetAllocator(PYMEM_DOMAIN_RAW, &allocator)`：raw 域是 PyMem/PyObject 全部分配的根基，四个入口全部转发 `BW_Py_malloc/realloc/calloc/free`（钩子表为空时直通 libc，与 2.7 行为逐字节一致，保留 size==0→1 语义）。
2. `PyObject_SetArenaAllocator`：pymalloc arena 分配走钩子层（对应 2.7 在 obmalloc.c 里的 #define）。
3. 两项都在 `Script::init()` 中、`Py_Initialize()` 之前安装——官方要求 raw 域只能在初始化前设置。
4. `_hashlib` 构造常量表的 ignore 区间照旧（引擎内存统计不重复计数）。

## 5. 剩余风险

| # | 风险 | 影响 | 计划 |
|---|---|---|---|
| R1 | vendored OpenSSL 1.0.0d：低于 3.13 最低要求（1.1.1），且在 gcc 15（默认 C23）下无法编译（`bool` 关键字冲突） | `_ssl/_hashlib` 缺席 lib-dynload；依赖 py ssl 的工具受限 | vcpkg 任务引入新版 OpenSSL 后：恢复 `--with-openssl`、共享模块列表、symbols 采集与 libpython 依赖（代码位置已留注释） |
| R2 | `third_party/python_modules` 全部为 Py2 生态包（Twisted 11、SQLAlchemy 0.6、Zope 2.11、oursql 等） | 集群监控/日志工具等运行时 Python 服务不可用 | 单独升级各包到 Py3 兼容版本后再启用 mak；不在编译验证关键路径 |
| R3 | Windows 构建（PCbuild/VS 工程）未迁移 | Windows 侧无法构建 | 后续批次 |
| R4 | 客户端/大文件资源（FantasyDemo 等）未在 Linux 验证范围 | — | 按批次推进 |

## 6. 批次与提交

| 批次 | 内容 | 验证 | 提交 |
|---|---|---|---|
| 1 | 换源 3.13.15 + 补丁重移植 + 构建系统 + 工具脚本 Py3 化 | `make libbwpython3.13` 全链通过：libbwpython3.13.a + python.exe + 54 个共享模块 + 库拷贝 | 待填 |
| 2 | lib/pyscript 移植 | libpyscript 编译通过 | 待填 |
| 3 | 引擎其余 C++ 按目录移植 | 各目录编译 | 待填 |
| 4 | 服务端/工具 Python 脚本 2to3 | py_compile 全量扫描 | 待填 |

---

*命中的 C-API 统计与补丁清单复核于 2026-09-24；CPython 源码 tag v3.13.15。*
