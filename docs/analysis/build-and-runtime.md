# 构建与运行体系

## 构建入口

BigWorld 同时保留 Make、CMake、批处理脚本和容器脚本。源码分析时不要把这些入口理解成互相等价，它们服务的平台和目标不同。

| 入口 | 代表路径 | 主要用途 |
|---|---|---|
| Make | `programming/Makefile`, `programming/bigworld/build/make/Makefile` | Linux 服务端和部分第三方库构建 |
| CMake | `programming/bigworld/CMakeLists.txt`, `programming/bigworld/build/cmake/` | Windows/IDE 工程生成和跨平台工程描述 |
| 批处理脚本 | `programming/bigworld/build/*.bat` | Windows 构建辅助 |
| Docker | `programming/bigworld/build/docker/Dockerfile` | 固定 Linux 构建环境 |

构建体系本身不是架构核心，但它决定哪些服务进程、库和工具会被编译到同一个运行闭环中。

## Linux 服务端构建链

Linux 构建主入口是：

```sh
make -C programming
```

入口会进入 `programming/bigworld/build/make/Makefile`，再按平台配置、目标类型和模块 Makefile 组织构建。

重点源码和配置：

| 文件 | 作用 |
|---|---|
| `programming/Makefile` | 顶层转发入口 |
| `programming/bigworld/build/make/Makefile` | BigWorld Make 构建主控 |
| `programming/bigworld/build/make/platform_*.mak` | 平台差异和编译参数 |
| `programming/bigworld/server/*/Makefile.rules` | 服务端进程输出目录和源文件规则 |
| `programming/bigworld/lib/*/Makefile.rules` | 共享库构建规则 |

服务端二进制通常输出到 `game/bin/server/<platform>`。源码分析运行问题时，需要同时看构建产物位置、资源路径和配置文件加载路径。

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
