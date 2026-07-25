# 构建、平台与依赖治理

<div class="arch-hero">

BigWorld 的构建约束不在单个依赖版本，而在构建系统、平台宏、第三方依赖、Python 嵌入方式、CMake/Makefile 双轨、服务端/客户端/工具链共存，以及老商业引擎遗留的可选中间件开关。

</div>

## 先给结论

BigWorld 的构建系统体现了一个大型跨平台游戏引擎的历史状态：

- 顶层 CMake 仍是 `CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )` 风格。
- 同时存在大量 `Makefile.rules`，说明构建体系不是单一现代 CMake。
- `BW_PYTHON_AS_SOURCE` 默认开启，说明 Python 可以作为源码构建进工程。
- 有大量功能开关：PCH、FMOD、Scaleform、SpeedTree、Umbra、Awesomium。
- 网络库依赖 OpenSSL，CMake 中有 `FIND_PACKAGE( BWOpenSSL REQUIRED )`。
- 单元测试通过 `BW_ADD_TEST(...)` 接入。
- 服务端、客户端、工具和第三方库在同一个大工程内编排。

关键源码：

- 顶层特性开关见 [CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/CMakeLists.txt:144)。
- 库和可执行遍历添加见 [CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/CMakeLists.txt:201)。
- remote only 的 `BUILD_SERVER` 目标见 [CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/CMakeLists.txt:238)。
- 网络库加密相关文件列表见 [lib/network/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/CMakeLists.txt:43)。
- 网络库 OpenSSL 依赖见 [lib/network/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/CMakeLists.txt:235)。
- `pyscript` 依赖 `libpython` 的条件见 [lib/pyscript/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/CMakeLists.txt:96)。
- `network_unit_test` 注册测试见 [lib/network/unit_test/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/CMakeLists.txt:69)。

## 构建分层

<MermaidDiagram title="BigWorld 构建分层">
flowchart TD
  A[programming/bigworld/CMakeLists.txt] --> B[BWConfiguration target]
  B --> C[Feature flags]
  C --> D[Libraries]
  C --> E[Executables]
  D --> F[cstdmf network entitydef pyscript server]
  E --> G[BaseApp CellApp DBApp LoginApp Tools Client]
  D --> H[Third party libs]
  H --> I[OpenSSL zip sqlite libpython]
  A --> J[Unit Tests]
  A --> K[Makefile.rules legacy path]
</MermaidDiagram>

这个结构的关键是：BigWorld 不是一个 Python 项目，而是 C++ 大工程里嵌入 Python。

因此替换或升级嵌入式 Python 运行时时必须同时处理：

- C++ 编译器和 ABI。
- Python 源码/库构建方式。
- `pyscript` 和 `script` 库。
- 第三方 Python 包。
- 服务器二进制和工具二进制。
- 资源路径和运行时部署。

## 功能开关

顶层 CMake 设置默认特性：

- `BW_PYTHON_AS_SOURCE ON`
- `BW_PCH_SUPPORT ON`
- `USE_MEMHOOK OFF`
- `BW_FMOD_SUPPORT OFF`
- `BW_SCALEFORM_SUPPORT OFF`
- `BW_SPEEDTREE_SUPPORT OFF`
- `BW_UMBRA_SUPPORT OFF`
- `BW_AWESOMIUM_SUPPORT OFF`

源码见 [CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/CMakeLists.txt:144)。

这反映了商业游戏引擎的典型形态：

- 同一源码树要适配不同产品和授权组件。
- 某些中间件不一定开源或可用。
- 构建结果取决于目标配置。
- 文档必须明确“本仓库实际可构建范围”，不能假设所有功能都能打开。

## CMake 与 Makefile 双轨

仓库中大量存在 `Makefile.rules`：

- `programming/bigworld/Makefile.rules`
- `programming/bigworld/lib/Makefile.rules`
- `programming/bigworld/server/Makefile.rules`
- `programming/bigworld/lib/network/Makefile.rules`
- `programming/bigworld/server/baseapp/Makefile.rules`
- 以及第三方和工具目录下的规则文件。

这说明构建历史不是单一现代 CMake，而是经历过 Makefile、CMake、平台配置和 IDE 工程生成的多轨共存。

构建风险：

- 只改 CMake 可能遗漏 Makefile 路径。
- 只在 Linux server 构建通过，不代表工具链和客户端可用。
- 构建宏可能影响源码条件编译，文档分析必须标注目标平台。
- CI 需要先选定最小目标，而不是一次覆盖所有历史平台。

## Python 嵌入方式

`BW_PYTHON_AS_SOURCE` 默认开启。`pyscript` 在该开关下链接 `libpython`：

```cmake
IF(BW_PYTHON_AS_SOURCE)
    BW_TARGET_LINK_LIBRARIES( pyscript INTERFACE libpython )
ENDIF()
```

源码见 [lib/pyscript/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/CMakeLists.txt:96)。

设计含义：

- Python 不是外部脚本解释器进程。
- Python 运行时是 C++ 进程的一部分。
- 升级 Python 会影响编译、链接、初始化、线程状态、模块加载、对象生命周期和 ABI。

这说明嵌入式 Python 运行时不是孤立依赖。它会穿透构建、脚本、热更新、线程和 EntityDef 章节，不能按普通脚本解释器替换处理。

## OpenSSL 与网络依赖

网络库文件列表包含：

- `elliptic_curve_checksum_scheme.cpp`
- `encryption_filter.cpp`
- `encryption_stream_filter.cpp`

源码见 [lib/network/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/CMakeLists.txt:43)。

并且网络库要求：

```cmake
FIND_PACKAGE( BWOpenSSL REQUIRED )
ADD_DEFINITIONS( -DUSE_OPENSSL )
BW_TARGET_LINK_LIBRARIES( network INTERFACE ${BWOPENSSL_LIBRARIES} )
```

源码见 [lib/network/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/CMakeLists.txt:235)。

源码边界：

- OpenSSL 版本升级可能影响 API、ABI 和算法可用性。
- 加密代码不能只按编译通过判断安全。
- 系统 OpenSSL 版本与旧代码兼容性必须单独验证。
- 保留旧 OpenSSL 会带来安全维护问题。

## 单元测试接入

`network_unit_test` 的 CMake 展示了测试接入方式：

- `BW_ADD_EXECUTABLE( network_unit_test ... )`
- 链接 `cstdmf`、`network`、`unit_test_lib`
- `BW_ADD_TEST( network_unit_test )`

源码见 [lib/network/unit_test/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/CMakeLists.txt:60)。

BaseApp 单元测试也通过 `BW_ADD_TEST( baseapp_unit_test )` 注册，见 [server/baseapp/unit_test/CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/unit_test/CMakeLists.txt:21)。

这说明：

- 测试基础设施存在。
- 现代 CI 可以先从现有 `BW_ADD_TEST` 目标接入。
- 不应一开始重写测试框架。
- 应先让旧测试在现代构建环境跑起来，再补新测试。

## Remote Only 与服务端目标

顶层 CMake 在 `BW_IS_REMOTE_ONLY` 时创建 `BUILD_SERVER` 目标，并使用 `_NON_EXISTANT_FILE.cpp` 作为生成源占位。源码见 [CMakeLists.txt](/home/cui/workspaces/BigWorld/programming/bigworld/CMakeLists.txt:238)。

这类写法说明旧构建系统要服务于：

- Visual Studio solution 生成。
- 远程构建 server。
- IDE 工程组织。
- 服务器命令目标。

源码分析时不能只按“纯 Linux CMake 项目”理解。需要先确认：

- 是否继续支持 Windows 客户端/工具链。
- 是否只先支持 Linux server。
- 是否保留 IDE solution 生成。
- 是否把 server 构建从大工程拆出来。

## 源码取舍

构建系统的源码取舍来自一套工程同时覆盖 server、client、tools 和 third_party：

- 商业游戏引擎需要一套源码覆盖客户端、服务器、工具和第三方中间件。
- 自带 Python 源码能提高可复现性，避免依赖目标机器系统 Python。
- Makefile.rules 保留可能是历史 Linux build 和内部构建系统兼容。
- 功能开关允许不同授权和平台裁剪。
- CMake、Makefile、third_party 和平台宏共同决定真实构建图。

源码代价：

- 依赖和宏条件复杂。
- 构建路径多，文档容易过期。
- 第三方库安全升级困难。
- 编译器、CMake、Python、OpenSSL 等基础依赖变化会同时暴露兼容问题。

## 源码验证重点

构建和依赖验证应围绕可复现性：

- 服务端最小构建基线应记录 OS、编译器、CMake、Python、OpenSSL 和 third_party 版本。
- server 关键库和现有单元测试应能在同一套配置下重复构建。
- 依赖清单应标注 vendored、系统依赖、已废弃依赖和安全敏感依赖。
- CMake 路径和 Makefile.rules 路径如果同时保留，必须明确产物差异。
- Python 嵌入、OpenSSL、MySQL、MongoDB、Scaleform 等可选依赖应有开关和失败信息。
- server-only 构建不应被客户端/工具历史依赖无意阻断。

## 明确风险

- 不跑旧测试就升级 CMake。
- 一次性删除 Makefile.rules。
- 直接把系统 Python 链进来而不校验嵌入式运行时 ABI。
- 不审计加密代码就升级 OpenSSL。
- 引入大型包管理器但不固定 lockfile。
- 同时升级编译器、CMake、Python、OpenSSL、网络模型和线程模型。

这些做法都会把问题叠加，导致无法定位回归来源。

## 本章边界

本章解释构建和依赖治理。后续源码分析引用构建问题时，应回到具体依赖、宏开关、平台路径和测试基线，而不是把它泛化成”升级工具链”。

## 相关文档

- Python 迁移相关，详见 [Python 升级路线图](/upgrade-plan/python-upgrade-plan)。
- 第三方依赖升级，详见 [第三方依赖升级清单](/upgrade-plan/third-party-deps)。
- 构建系统现代化，详见 [构建系统现代化](/upgrade-plan/build-system-modernization)。
- 技术债与风险分析，详见 [技术债与风险](/analysis/risks)。
