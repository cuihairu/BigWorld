# 构建、平台与依赖治理

<div class="arch-hero">

BigWorld 的现代化不能只改 Python 版本或网络后端。它的真实约束在构建系统、平台宏、第三方依赖、Python 嵌入方式、CMake/Makefile 双轨、服务端/客户端/工具链共存，以及老商业引擎遗留的可选中间件开关。

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

所以 Python 3.12 迁移必须同时处理：

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
- 文档和现代化必须明确“本仓库实际可构建范围”，不能假设所有功能都能打开。

## CMake 与 Makefile 双轨

仓库中大量存在 `Makefile.rules`：

- `programming/bigworld/Makefile.rules`
- `programming/bigworld/lib/Makefile.rules`
- `programming/bigworld/server/Makefile.rules`
- `programming/bigworld/lib/network/Makefile.rules`
- `programming/bigworld/server/baseapp/Makefile.rules`
- 以及第三方和工具目录下的规则文件。

这说明构建历史不是单一现代 CMake，而是经历过 Makefile、CMake、平台配置和 IDE 工程生成的多轨共存。

现代化风险：

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

这就是为什么 Python 3.12 不能被放在顶栏当成独立菜单。它是全局现代化的一条线，应该嵌在构建、脚本、热更新、线程和 EntityDef 章节里理解。

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

现代化含义：

- OpenSSL 版本升级可能影响 API、ABI 和算法可用性。
- 加密代码不能只按编译通过判断安全。
- 如果迁移到现代 Linux 发行版，需要处理系统 OpenSSL 3.x 与旧代码兼容。
- 如果保留老 OpenSSL，又会带来安全维护问题。

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

现代化时不能只按“纯 Linux CMake 项目”理解。需要决定：

- 是否继续支持 Windows 客户端/工具链。
- 是否只先支持 Linux server。
- 是否保留 IDE solution 生成。
- 是否把 server 构建从大工程拆出来。

## 当时为什么这样选

高置信工程判断：

- 商业游戏引擎需要一套源码覆盖客户端、服务器、工具和第三方中间件。
- CMake 2.8 年代，现代 target-based CMake 尚未普及。
- 自带 Python 源码能提高可复现性，避免依赖目标机器系统 Python。
- Makefile.rules 保留可能是历史 Linux build 和内部构建系统兼容。
- 功能开关允许不同授权和平台裁剪。

代价：

- 依赖和宏条件复杂。
- 构建路径多，文档容易过期。
- 老 CMake 风格不利于现代 IDE、包管理和 CI。
- 第三方库安全升级困难。
- Python 3.12、OpenSSL 3、现代 GCC/Clang 会同时暴露大量兼容问题。

## 现代方案对比

<div class="decision-table">

| 维度 | BigWorld 当前 | 现代推荐 | 判断 |
| --- | --- | --- | --- |
| 构建系统 | CMake 2.8 风格 + Makefile.rules | target-based CMake / presets | 分阶段迁移，不要一次重写 |
| 依赖管理 | vendored third_party + find modules | vcpkg/Conan/system packages | 服务端依赖可先清单化 |
| Python | 源码嵌入 `libpython` | 明确 CPython build profile | Python 3.12 需专项工程 |
| OpenSSL | `BWOpenSSL REQUIRED` | OpenSSL 3 或兼容封装 | 需安全审计和 API 适配 |
| 测试 | `BW_ADD_TEST` | CTest + CI + artifacts | 先接旧测试，再增强 |
| 平台 | 多平台历史兼容 | 明确 server-first baseline | 不要无目标地兼容所有平台 |
| 构建产物 | 大工程统一生成 | server/client/tools 分层 | 可逐步拆分 |

</div>

## 现代化路线

建议按最小风险顺序推进：

1. 固化当前服务端最小构建基线，记录 OS、编译器、CMake、Python、OpenSSL。
2. 接入 CI，只构建 server 关键库和现有单元测试。
3. 生成依赖清单，标注 vendored、系统依赖、已废弃依赖。
4. 把 CMake 升级分成“兼容新版本运行”和“重写 target model”两步。
5. Python 3.12 单独做 spike，不直接污染主线。
6. OpenSSL 先建立封装和测试，再升级版本。
7. 逐步拆分 server-only 构建，避免客户端/工具历史依赖阻塞服务端现代化。

## 不建议的做法

- 不跑旧测试就升级 CMake。
- 一次性删除 Makefile.rules。
- 直接把系统 Python 3.12 链进来。
- 不审计加密代码就升级 OpenSSL。
- 为了“现代”引入大型包管理器但不固定 lockfile。
- 同时升级编译器、CMake、Python、OpenSSL、网络模型和线程模型。

这些做法都会把问题叠加，导致无法定位回归来源。

## 本章边界

本章解释构建和依赖治理。下一章做现代 MMO 架构对比：把 BigWorld 放在 actor、ECS、云原生、QUIC、io_uring、Kubernetes、event sourcing 等现代方案旁边比较，明确哪些值得学，哪些不应照搬。
