# 构建与运行体系

## 构建入口

BigWorld 同时保留 Make、vcpkg、CMake、批处理脚本和容器脚本。源码分析时不要把这些入口理解成互相等价，它们服务的平台和目标不同。

| 入口 | 代表路径 | 主要用途 |
|---|---|---|
| Make | `programming/Makefile`, `programming/bigworld/build/make/Makefile` | Linux 服务端主构建链（已现代化为 C++23 口径），含嵌入式 CPython 3.13 与 21 模块单测 |
| vcpkg | manifest `programming/bigworld/vcpkg.json`，构建时设 `VCPKG_ROOT` 指向官方 vcpkg | 第三方依赖（openssl/curl/jsoncpp/zlib 等）在 make 过程中自动物化 |
| CMake | `programming/bigworld/CMakeLists.txt`, `programming/bigworld/build/cmake/` | Windows/IDE 工程生成和跨平台工程描述 |
| 批处理脚本 | `programming/bigworld/build/*.bat` | Windows 构建辅助 |
| Docker | `programming/bigworld/build/docker/Dockerfile` | 遗产入口——centos:7 已于 2024-06 EOL，其 gcc 4.8 无法承载 C++23；干净构建环境改由每日构建工作流承担 |
| GitHub Actions | `.github/workflows/daily-build.yml` + `scripts/package-daily.sh` | ubuntu-24.04 + gcc-14：全量构建 → 21 模块单测门禁 → tar/deb/rpm 三格式产物（详见[打包与每日构建](/upgrade-plan/packaging-and-daily-build)） |

构建体系本身不是架构核心，但它决定哪些服务进程、库和工具会被编译到同一个运行闭环中。

## Linux 服务端构建链

Linux 构建主入口是：

```sh
make -C programming
```

入口会进入 `programming/bigworld/build/make/Makefile`，再按平台配置、目标类型和模块 Makefile 组织构建。现代化后的口径是 C++23 + gcc-14 级编译器；首次全量构建需设 `VCPKG_ROOT` 指向官方 vcpkg，manifest 模式会在 make 过程中自动物化第三方依赖。

重点源码和配置：

| 文件 | 作用 |
|---|---|
| `programming/Makefile` | 顶层转发入口 |
| `programming/bigworld/build/make/Makefile` | BigWorld Make 构建主控 |
| `programming/bigworld/build/make/platform_*.mak` | 平台差异和编译参数 |
| `programming/bigworld/build/make/third_party_python.mak` | 嵌入式 CPython 3.13 构建、标准库安装与共享模块哨兵 |
| `programming/bigworld/server/*/Makefile.rules` | 服务端进程输出目录和源文件规则 |
| `programming/bigworld/lib/*/Makefile.rules` | 共享库构建规则 |

服务端二进制输出到 `game/bin/server/el7/`。平台探测（`platform_info.py`）把无 `/etc/redhat-release` 的 Debian 系主机也映射为 `el7` 工具链配置——`el7` 是历史命名，不代表运行环境要求。

嵌入式 Python 构建有两个容易踩中的暗门：

- `python_install`（把 CPython 标准库装入 `game/`）受 `user_shouldInstallPython=1` 门控，全构建系统无默认值——不设则标准库不安装，嵌 Python 程序启动即死于 `No module named 'encodings'`；
- `bwsentinel` 对约 60 个 Python 共享扩展逐一做存在性检查：缺系统 dev 头文件（如 `liblzma-dev`）时 CPython 会静默少编模块而不报错，由哨兵兜底断言。

源码分析运行问题时，需要同时看构建产物位置、资源路径和配置文件加载路径。

## CMake 工程链

`programming/bigworld/CMakeLists.txt` 是 CMake 入口，配合 `build/cmake/` 下的配置脚本生成工程。它对理解 Windows 工具、客户端和部分库的组织方式有价值。

重点不是 CMake 版本，而是工程如何把模块连接起来：

| 关注点 | 源码或配置 |
|---|---|
| 库和目标组织 | `programming/bigworld/CMakeLists.txt` |
| 平台配置 | `programming/bigworld/build/cmake/` |
| 单元测试目标 | 各模块 `unit_test/CMakeLists.txt` 中的 `BW_ADD_TEST` |
| 服务端配置引用 | `BWConfiguration_server.cmake` |

如果 CMake 引用了仓库中不存在的脚本或路径，应记录为源码树与工程配置漂移，而不是直接推断运行时行为。

## 服务进程启动模型

服务端运行入口不是手写散落的 `main()` 逻辑，而是统一宏入口：

```
server/<app>/main.cpp
  -> BIGWORLD_MAIN
  -> bwMainT<App>
  -> doBWMainT
  -> ServerApp::runApp
  -> App::init
  -> ServerApp::run
  -> EventDispatcher::processUntilBreak
```

关键文件：

| 文件 | 作用 |
|---|---|
| `lib/server/bwservice.hpp` | 定义 `BIGWORLD_MAIN`、`bwMainT`、`doBWMainT` |
| `lib/server/server_app.cpp` | `runApp`、`run`、`advanceTime` |
| `lib/network/event_dispatcher.cpp` | timer、网络事件和 frequent task 分发 |
| `server/baseapp/main.cpp` | BaseApp/ServiceApp 类型选择 |
| `server/dbapp/main.cpp` 等 | 具体 App 类型绑定 |

因此排查“服务为什么没启动起来”时，应按 `main.cpp -> bwservice.hpp -> App::init()` 查，而不是只看进程目录下的入口文件。

## 配置与资源加载

`BIGWORLD_MAIN` 展开后会初始化资源系统和配置系统。服务进程后续再在 `App::init()` 中读取自己的配置、绑定网络接口、注册 Watcher 和启动后台任务。

常见链路：

```
BWResource::init
  -> BWConfig::init
  -> bwParseCommandLine
  -> ServerAppConfig::init
  -> App::init
```

源码分析配置问题时，要区分三类输入：

| 输入 | 代表位置 | 影响 |
|---|---|---|
| 命令行参数 | `bwParseCommandLine` | 进程启动参数和资源路径 |
| 全局配置 | `BWConfig` | 网络、tick、服务参数 |
| 资源树 | `BWResource` | 脚本、EntityDef、标准资源和工具资源 |

EntityDef、脚本和部分第三方运行时内容都通过资源路径参与加载，所以“能编译”不等于“能初始化服务进程”。

## 运行时进程类别

运行时可以分成四类：

| 类别 | 代表进程或工具 | 源码关注点 |
|---|---|---|
| MMO 服务进程 | `baseapp`, `cellapp`, `dbapp`, `loginapp` | Mercury interface、事件循环、实体状态 |
| Manager 进程 | `baseappmgr`, `cellappmgr`, `dbappmgr` | 进程注册、负载、hash、空间分区 |
| 控制与观测工具 | `bwmachined`, `message_logger`, Watcher 工具 | 进程发现、日志、运行时控制 |
| 离线工具与编辑器 | `worldeditor`, `assetprocessor`, `navgen` | 资源生产、场景数据、客户端内容 |

不要只验证单个二进制能启动。BigWorld 的运行正确性取决于 LoginApp、BaseAppMgr、CellAppMgr、DBAppMgr、BaseApp、CellApp、DBApp 之间的注册和消息链是否闭合。

## 测试入口

源码内存在多类测试入口：

| 区域 | 代表路径 | 用途 |
|---|---|---|
| 网络测试 | `lib/network/unit_test/` | Channel、可靠性、分片、窗口、TCP/UDP 行为 |
| EntityDef 测试 | `lib/entitydef/unit_test/` | DataType、stream size、属性变化、序列化 |
| 服务端测试 | `server/baseapp/unit_test/`, `server/cellapp/unit_test/` | BaseApp/CellApp 局部行为 |
| DB 测试 | `server/dbapp/unit_test/`, `lib/db/unit_test/` | DB interface 和部分 DB 行为 |
| 通用测试框架 | `lib/unit_test_lib/` | 单测宏、多进程测试辅助 |

全量门禁入口是 make 目标（需 `VCPKG_ROOT`）：

```sh
env VCPKG_ROOT=<vcpkg> make -C programming bw-run-all-unit-tests
```

当前基线为 **21 个模块、1113 个用例**，二进制在 `game/bin/server/el7/unit_tests/` 下、从各模块源码 `unit_test/` 目录用全路径执行；每日构建工作流在干净环境以同一口径执行。

验证某个架构判断时，优先找对应单测。若没有测试，应在文档中标记为“源码推断”而不是“已验证行为”。

## 构建与运行排查顺序

排查运行时问题建议按这个顺序：

1. 确认目标是服务进程、工具、客户端还是编辑器。
2. 找对应 `main.cpp` 和 `BIGWORLD_MAIN` 绑定的 App 类型。
3. 进入 `App::init()`，检查网络接口、配置、EntityDef、DB、Watcher 注册。
4. 查 interface 是否 `registerWithInterface()`。
5. 用 `startMessage()` / `startRequest()` 追调用方。
6. 如果涉及实体状态，再查 EntityDef 数据域和序列化流。
7. 如果涉及落库，再查 `Base::writeToDB()` 到 `IDatabase::putEntity()`。

这套顺序能避免把构建问题、配置问题、网络注册问题和业务 handler 问题混在一起。
