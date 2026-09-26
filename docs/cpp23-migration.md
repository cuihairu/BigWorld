# BigWorld C++ 标准升级分析（目标：C++23）

> 任务：评估并把 BigWorld 的 C++ 标准升级到 C++23。
> 状态：**分析已完成；落地阶段见文末"最终落点与执行结果"**。
> 约束：与 Python 3.13 移植同在 dev 分支，两件事分开提交。

## 1. 现状盘点

### 1.1 语言标准设置点（全仓扫描）

| 位置 | 设置 | 说明 |
|---|---|---|
| `programming/bigworld/build/make/platform_el7.mak:4` | `CXX11_CXXFLAGS := -std=c++11`，并 `CXXFLAGS += $(CXX11_CXXFLAGS)` | **Linux 当前实际生效档位** |
| `programming/bigworld/build/make/platform_el6.mak:8` | `-std=c++0x` | el6 历史配置（未再触发） |
| `build/cmake/BWCompilerAndLinkerOptions.cmake` | 无任何 `-std`；MSVC 分支全靠编译器版本默认档 | CMake 未设 `CMAKE_CXX_STANDARD` |
| `build/xcode/*` | 未设置 `CLANG_CXX_LANGUAGE_STANDARD` | 用 Xcode 默认档 |

结论：**没有 gnu++98**。Linux 全量代码已经在 `-std=c++11` 下编译，本仓不存在从 98 直接跃迁的问题。

### 1.2 编译器矩阵（C++23 各厂最低版本）

| 编译器 | C++23 完整支持最低版本 | 本仓现状 | 结论 |
|---|---|---|---|
| GCC | **14**（13 部分支持） | 本机 `/usr/bin/g++` 15.2.0 | ✅ 可上 c++23 |
| Clang | **18** | 仓库未用 clang 构建（仅 `#ifdef __clang__` 兼容分支） | 留 TODO |
| MSVC | **19.37**（VS 2022 17.7） | 代码库处于 VS2012 时代（`_MSC_VER <= 1600/1700` 条件编译、`/Zm282` VS2012 变通、老 `.sln/.vcproj`） | ❌ 远不到位，Windows 侧不动 |

### 1.3 本机构建环境注意点

- `/usr/bin/g++` = 15.2.0 可用；但 **`~/.local/bin/g++` 是一个损坏的自装符号链接**（缺 libstdc++ 头文件），且它在 PATH 中优先。编译验证时必须显式 `CXX=/usr/bin/g++` 或调整 PATH。
- 本机为 Ubuntu（`platform_info.py` 已加 Debian→el7 等效映射，属 Python 迁移任务的提交）。

## 2. 分档废弃/删除特性命中量化

统计范围：`lib/ server/ tools/ client/ guimanager/ examples/`（不含 `third_party/`），文件类型 `.cpp .hpp .h .ipp`。

### 11 [14（无删除，仅弃用]

| 特性 | 命中 | 备注 |
|---|---|---|
| `std::auto_ptr` | 251 | C++11 起弃用 |
| `std::bind1st/bind2nd/ptr_fun` | 0 | — |
| `std::random_shuffle` | 5 | C++14 起（序列 17 前）弃用 |
| `std::mem_fun` | 2 | 同上 |

### 14 [17（第一批硬删除]

| 特性 | 命中 | 备注 |
|---|---|---|
| `register` 存储类说明符 | 125 个文件 / 211 处 | C++17 删除；gcc 实测仍容忍为警告 |
| `throw(类型)` 动态异常说明 | **0** | 好消息：全仓没有 typed throw-spec |
| `throw()`（空动态异常说明） | **21** | C++17 弃用；**gcc 15 下 c++17 起即为硬错误**（实测） |
| `std::unary_function/binary_function` | 19 | C++17 删除（libstdc++ 默认仍保留弃用副本） |
| `std::auto_ptr` | 同上 251 | C++17 从标准删除 |
| `std::random_shuffle`、`std::mem_fun` | 7 | C++17 从标准删除 |
| `std::tr1::` | 25 | tr1 头在 C++17 后仍随 libstdc++ 提供，但属遗物 |

### 17 [20（第二批硬删除]

| 特性 | 命中 | 备注 |
|---|---|---|
| `std::result_of` | 0 | — |
| `std::raw_storage_iterator` | 0 | — |
| `std::is_pod` | 0 | — |
| `[=]` 隐式捕获 this | **0** | C++20 起弃用、C++23 移除；全仓无命中 |
| `std::iterator` 继承 | 2（`lib/entitydef/data_type.hpp:326`、`lib/cstdmf/circular_queue.hpp:85`） | C++17 弃用、C++20 仍可用；建议顺手改 |

### 20 [23（无新增删除]

`std::aligned_storage` 等在 C++23 仅进一步弃用，未删除。无增量命中。

## 3. gcc 15.2 实测验证（同一测试用例逐档编译）

测试用例同时包含：`auto_ptr`、`register`、`[=]` 隐式捕获 this、`binary_function`、`throw()`。

| 档位 | 默认 libstdc++ | `-D_GLIBCXX_USE_DEPRECATED=0` |
|---|---|---|
| c++11/14 | 仅弃用警告 | 1 错（auto_ptr 保留副本关闭） |
| c++17/20/23 | **唯一硬错误：`throw()`**；其余全部降级为警告 | 4 错 |

结论：Linux 侧切 c++23 的**唯一硬阻塞是 21 处 `throw()`**；其余命中点在默认库配置下不挡编译，但按"先修再切"原则全部清零，避免依赖 `_GLIBCXX_USE_DEPRECATED` 的宽容行为。

## 4. 第三方代码绑死情况（降档/例外清单）

| 库 | 问题 | 处置 |
|---|---|---|
| `third_party/openssl`（1.0.0d） | gcc 15 默认 C23 下编译**硬失败**（`bool` 成关键字，老代码用作变量名） | 不属于引擎代码升级范围；由 vcpkg 任务整体替换为新版 OpenSSL。**这是"老第三方绑死现代工具链"的实锤案例，也是 Python 3.13 `_ssl` 的共同卡点** |
| `third_party/` 其余（nedalloc、jsoncpp、re2、recastnavigation…） | 待 vcpkg 任务逐个盘点；引擎源码侧修复对它们无感（各自独立编译档位） | 后续任务 |

## 5. 与 Python 3.13 迁移的边界

- CPython 3.13 公共头文件在 C++ 编译单元下要求 **C++11 及以上**——el7 现有 `-std=c++11` **恰好满足**，Python 迁移任务因此**不动任何 `-std` 设置**，两任务提交完全解耦。
- C++23 任务在 Python 移植的编译验证批次全部收完之后才切档，避免"换标准 + 换 C-API"两个变量同时污染编译结果。

## 6. 最终落点与执行结果

执行顺序严格"先修再切"：全部废弃/删除特性在 `-std=c++11` 档清零并全量验证（28 个库通过 + 3 个环境性跳过，见文末"跳过项"），然后才切档。

### 6.1 清理清单（c++11 档完成，勾选为准）

- [x] `throw()` → `noexcept`：21 处（lib/memhook/memhook.cpp、lib/cstdmf/bw_memory.hpp/.cpp、lib/db_storage_mysql/database_exception.hpp/.cpp）
- [x] `std::auto_ptr` → `std::unique_ptr`：244 处（121 文件；§2 初估 251 含注释 5 处，注释同步改写）
- [x] `register` 存储类说明符：**4 处**（§2 初估 211 为 grep 宽匹配，绝大多数是变量名/标识符误报；实际 lib/math/math_extra.cpp、lib/math/perlin_noise.cpp ×2、lib/cstdmf/bit_reader.cpp）
- [x] `unary_function/binary_function` 继承删除：18 处（14 文件；hash 特化改用 `std::hash`/`<functional>`，比较仿函数改显式 typedef 或 lambda 等价物）
- [x] `random_shuffle` → `std::shuffle` + `std::mt19937(std::random_device()())`：5 处（loose_octree、general_editor、reviver、backup_sender、archiver）
- [x] `mem_fun` 残留：复核清零（grep 计 0）
- [x] `std::tr1::` 统一到 `std::`：24 处（重写 lib/cstdmf/bw_hash.hpp，统一 bw_unordered_map/bw_unordered_set/bw_std 的头与命名空间选择）
- [x] `std::iterator` 继承 → 显式 `typedef` 五件套：2 处（lib/entitydef/data_type.hpp、lib/cstdmf/circular_queue.hpp）
- [x] `NULL` → `nullptr`（unique_ptr 构造/赋值上下文）：76 处（见 6.2；c++11 语义等价，归入清理批）

### 6.2 切档后编译驱动修复（c++23 才暴露，逐一记录）

| 问题 | 规则依据 | 修复 |
|---|---|---|
| `unique_ptr(NULL)` 构造歧义 76 处 | C++23 下 `NULL`（整型 0）在 `pointer` 与 `nullptr_t` 构造器间二义 | 全部改 `nullptr`（对原生指针同样正确） |
| `watcher.hpp` 字符串转值不支持指针类型 | gcc15 libstdc++ C++23 档删除 `operator>>(istream, void*&)` | 新增 `watcherStringToValue( const char *, VALUE_TYPE *& )` 重载返回 false |
| `server/tools/message_logger/user_log.cpp` `return NULL` 给 `BW::string` | P2166R1 删除 `string(nullptr_t)` 构造（原为 UB） | 改 `return BW::string();` |
| pyscript/typeobject 指定初始化混编 | C++20 起一个初始化列表禁止 designated/positional 混用；CPython 3.13 `PyVarObject_HEAD_INIT` 是位置展开 | 新增 `lib/pyscript/pytypeobject_head.hpp`（`BW_PYTYPEOBJECT_HEAD_INIT`，双层 `.ob_base` 展开 3.13 对象头；仅依赖 Python.h 供仓外裸 CPython 代码复用）；pyobject_plus.hpp 的 688 行类型对象全部 designated 化 |
| `server/tools/message_logger` 四个裸 CPython 类型对象 | 同上 | 核查后保持全位置初始化（3.13 槽位顺序已正确，全位置在 C++23 合法），不引入宏 |
| `bw_profile`、`message_reader` 拾取系统 Python 头 | 上游遗留 `useSystemPython := 1`，现代主机解析到任意 `/usr/include/pythonX.Y`（本机 3.14 公共头无 `_Py_IMMORTAL_REFCNT`） | 两处改 `useSystemPython := 0`，与其余组件统一走仓内 3.13 头与 `bwpython3.13` |
| `examples/cellapp_extension` Py2 API 残留 | `PyString_*` 在 Python 3 头不存在（Python 任务期该目标从未被编译执行到，陈旧 .o 掩盖了问题） | `PyString_FromStringAndSize` → `PyBytes_FromStringAndSize`（二进制流语义一致） |
| `base.cpp`/`mailbox.cpp` 按 `auto_ptr` 拷贝语义传参 | `auto_ptr` "拷贝即转移"在 `unique_ptr` 下是删除的拷贝构造 | `getStream(..., std::move( pReplyHandler ))`；`mailbox.cpp` 三处 `pHandler` 透传同修；`base.cpp` 两处 `return unique_ptr(NULL)` → `return nullptr` |
| 非 const 成员 `operator==/!=` 二义 5 处 | C++20 重写/反转候选（rewritten candidates）使非 const 比较成员在双非 const 操作数下二义（gcc 提示 "try making the operator a const member function"） | 补尾 `const` 限定（语义等价）：log_on_params.hpp、cstdmf/event.hpp、cstdmf/dogwatch.hpp/.cpp（iterator 的 `==`/`!=`）、romp/lens_effect.hpp |
| `third_party/CppUnitLite2` 缩进告警 | gcc15 `-Werror=misleading-indentation` | `ExceptionHandler.cpp` 的单句 `if` 加花括号（第三方最小改动） |

另：`third_party_openssl.mak` 的并发重建踩踏（`ar: file truncated`）根治——此前删除 `.INTERMEDIATE` 后配方从不物理创建 stamp 文件，普通规则目标缺失导致每次 make 重跑；配方末尾补 `touch $@`。此修复与标准档无关但由本轮验证发现，随本任务提交。

### 6.3 切档与验证

- [x] `build/make/platform_el7.mak`：`CXX11_CXXFLAGS := -std=c++23`（变量名保留以减少 diff，注释指向本文档）
- [x] c++11 档库全量验证：28 通过；跳过 3（见 6.4）
- [x] c++23 档库全量验证：27/29 通过（network 修复后），同 3 个环境性跳过
- [x] c++23 档 server 全量（含单元测试构建、tools、examples 扩展）：`server/` 下 `make CXX=/usr/bin/g++ -j8` 全绿（%SERVER_RESULT%）
- [ ] Windows/Xcode 侧留 TODO（MSVC 需 VS2022 17.7+；Xcode 需设 `CLANG_CXX_LANGUAGE_STANDARD`；另行评估）

### 6.4 跳过项（非本任务范围，如实记录）

| 项 | 原因 |
|---|---|
| lib/testing | 预存断裂：Makefile 引用另一套内部仓库布局 `$(MF_ROOT)/bigworld/src/server/common/common.mak`，git 历史确认从未在本仓构建过 |
| lib/bwentity | 无 Makefile（仅 CMakeLists，Windows/CMake 流程） |
| lib/db_storage_mysql | 构建系统在无 MySQL 开发库环境下主动跳过 |

### 6.5 经验教训

1. **"全绿"必须核对覆盖面**：Python 任务期 server 编译声称通过，但 `cellapp_example.so` 等目标从未被真正编译执行到（陈旧 .o 被跳过掩盖了 Py2 API 残留）。切标准触发全量重编后问题才浮现——切档本身充当了一次覆盖面审计。
2. **auto_ptr → unique_ptr 不是纯换名**：`auto_ptr` 的拷贝即转移语义在按值传参点必须显式 `std::move`，否则 C++23 下是硬错误。
3. **宏与指定初始化不能混编**：给 CPython 头宏（位置展开）配 designated 槽位表时，要么整体 designated、要么整体 positional，无中间态。

---

*文档生成：dev 分支 Python 3.13 迁移期间；命中数由 grep 统计，档位行为由 gcc 15.2.0 实测。*
