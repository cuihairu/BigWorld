# 从源码构建

<div class="arch-hero">

本文按当前仓库中的 Makefile、CMakeLists 和 Dockerfile 说明 BigWorld 的构建入口。重点是明确真实源码边界，而不是把旧工程改写成现代通用构建流程。

</div>

## 构建系统概览

BigWorld 保留了多条构建链：

| 构建链 | 入口文件 | 主要目标 |
|---|---|---|
| Linux Make | `programming/Makefile` -> `programming/bigworld/build/make/Makefile` | 服务端组件、服务端工具、单元测试和部分第三方库 |
| Docker | `programming/bigworld/build/docker/Dockerfile` | 固定 CentOS 7 构建环境 |
| CMake | `programming/bigworld/CMakeLists.txt` | Windows 工程和 remote server 生成路径 |
| Windows 脚本 | `programming/bigworld/build/*.bat` | Windows 工程生成辅助 |

Linux 服务端源码分析应优先看 Make 链；客户端、工具和 Windows 工程再看 CMake 链。

## Linux 服务端构建

### 依赖基线

Dockerfile 使用 `centos:7`，并安装以下关键依赖：

```text
make
gcc
gcc-c++
rpm-build
mariadb-devel
python-devel
sqlite-devel
readline-devel
gdbm-devel
bzip2-devel
ncurses-devel
binutils-devel
```

这说明当前服务端构建依赖的是旧式系统包和内置第三方库组合。不要直接假设现代发行版包名、编译器或 Python 版本可以无差异替换。

### 构建命令

从仓库根目录执行：

```sh
make -C programming
```

Dockerfile 注释里的示例命令：

```sh
make -s -j64 -rR -C programming
```

`-j64` 只是示例，应按实际机器 CPU、内存和第三方库构建稳定性调整。

### 常用目标

顶层 Makefile 注释中定义了几个主目标：

| 目标 | 说明 |
|---|---|
| `all` | 构建源码组件 |
| `everything` | 构建更多非源码产物，例如 RPM |
| `clean` | 清理中间文件 |
| `bw-run-all-unit-tests` | 运行全部单元测试 |
| `bw-run-unit-tests` | 运行单元测试，失败时尽早停止 |

如果要定位某个服务端组件，可继续查对应目录下的 `Makefile.rules`。

## 输出目录

Makefile 中的输出路径由 `BW_BUILD_PLATFORM`、`BW_CONFIG` 和相关变量拼出。

| 类型 | 变量 | 路径形式 |
|---|---|---|
| 服务端二进制 | `BW_SERVER_BIN_DIR` | `bin/server/<platform_config>/server` |
| 服务端工具 | `BW_SERVER_TOOLS_BIN_DIR` | `bin/server/<platform_config>/tools` |
| 单元测试 | `BW_UNIT_TEST_DIR` | `bin/server/<platform_config>/unit_tests` |
| 第三方产物 | `BW_THIRD_PARTY_INSTALL_DIR` | `bin/server/<host_platform_config>/third_party` |

在默认安装目录下，产物位于 `game/` 树中。Dockerfile 注释中也指出 Make 输出通常进入 `game/bin/server/el7/`。

## Docker 构建

Dockerfile 位于：

```text
programming/bigworld/build/docker/Dockerfile
```

构建镜像示例：

```sh
docker buildx build . \
  -f programming/bigworld/build/docker/Dockerfile \
  -t bigworld-build
```

进入容器执行构建的示例：

```sh
docker run --rm -it \
  -u "$(id -u):$(id -g)" \
  --mount type=bind,source="${PWD}",target=/home/bigworld/host \
  --mount type=tmpfs,target=/home/bigworld/rpmbuild \
  --env HOME=/home/bigworld \
  -w /home/bigworld/host \
  bigworld-build
```

容器内再执行：

```sh
make -s -j"$(nproc)" -rR -C programming
```

Dockerfile 注释明确提示：初次 third_party 构建可能不适合并行构建，某些 Python 相关 patch/output 问题可能导致需要重跑。这是构建链现实，不应在文档中隐藏。

## CMake 工程边界

`programming/bigworld/CMakeLists.txt` 的源码事实：

- `CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12)`。
- 必须指定 `BW_CMAKE_TARGET`，否则直接报错。
- 普通路径只支持 Windows；非 Windows 会报 “Only Windows builds are currently supported.”
- remote server 是单独路径，由 `BW_IS_REMOTE_ONLY` 和 `BW_REMOTE_PLATFORM` 控制。
- 支持的 MSVC token 是 `vc9` 到 `vc14`。

因此，不应写成：

```sh
cmake -B build
cmake --build build
```

这种通用现代 CMake 流程不符合当前根 CMakeLists 的行为。使用 CMake 时必须先明确目标和平台，例如 Windows 工程、client、tools 或 remote server。

## 单元测试

测试目标分布在各模块 `unit_test/CMakeLists.txt` 或 Make 构建规则中。

源码入口示例：

| 测试区域 | 路径 |
|---|---|
| 网络 | `programming/bigworld/lib/network/unit_test/` |
| EntityDef | `programming/bigworld/lib/entitydef/unit_test/` |
| BgTaskManager | `programming/bigworld/lib/cstdmf/unit_test/test_bgtasks.cpp` |
| BaseApp | `programming/bigworld/server/baseapp/unit_test/` |
| CellApp | `programming/bigworld/server/cellapp/unit_test/` |
| DBApp | `programming/bigworld/server/dbapp/unit_test/` |

运行测试前先确认对应目标已被当前平台构建链纳入。找测试目标可用：

```sh
rg "BW_ADD_TEST" programming/bigworld
rg "TEST\\(" programming/bigworld/lib programming/bigworld/server
```

## 构建排查顺序

排查构建问题时建议按顺序确认：

1. 当前目标是 Linux server、Windows client/tool，还是文档站。
2. 使用的是 Make 链还是 CMake 链。
3. `BW_BUILD_PLATFORM`、`BW_CONFIG`、`BW_INSTALL_DIR` 是否符合预期。
4. 第三方库是否已构建并复制到 `bin/server/<platform>/third_party`。
5. 目标目录的 `Makefile.rules` 是否被顶层 Makefile 收集。
6. 如果是 CMake，`BW_CMAKE_TARGET` 和平台判断是否通过。
7. 如果是运行失败，再查资源路径、`BWResource::init`、`BWConfig::init` 和 App `init()`。

## 相关文档

- [构建与运行体系](/analysis/build-and-runtime)
- [构建、平台与依赖治理](/architecture/build-platform-dependencies)
- [源码导读](/analysis/source-code-guide)
