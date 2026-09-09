---
layout: doc
---

# 快速开始

<div class="arch-hero">

本章用于快速定位源码阅读和构建入口。BigWorld 开源版的构建链偏旧，以下内容按当前仓库源码描述，不把它改写成现代通用 CMake 项目。

</div>

## 先读什么

如果目标是源码分析，建议先按这个顺序进入：

1. [源码导读](/analysis/source-code-guide)：建立进程入口、网络、EntityDef、DB、前端通信和 Offload 的阅读地图。
2. [总体画像](/analysis/overview)：理解源码主线和运行闭环。
3. [架构研究方法](/architecture/research-method)：区分源码事实、推断和开放问题。
4. [进程拓扑与职责切分](/architecture/process-topology)：固定 BaseApp、CellApp、DBApp、LoginApp 和 Manager 职责。

不要先从 `third_party`、旧 PDF 或升级计划开始，否则容易把外围依赖问题误当成架构主线。

## 环境基线

当前源码里的 Linux 构建路径明显以 CentOS/RHEL 系列为主要基线，Dockerfile 也使用 `centos:7`。

| 类型 | 源码依据 | 说明 |
|---|---|---|
| Linux 服务端 | `programming/Makefile`, `programming/bigworld/build/make/Makefile` | Make 是服务端主入口 |
| Docker 构建环境 | `programming/bigworld/build/docker/Dockerfile` | 基于 `centos:7`，安装 `gcc-c++`、`mariadb-devel`、`python-devel` 等 |
| Windows 工程 | `programming/bigworld/CMakeLists.txt` | CMake 入口要求 `BW_CMAKE_TARGET`，普通路径只支持 Windows 或 remote server |
| CMake 版本 | `CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12)` | 不是 CMake 3.x 风格项目 |

开发机可以用更新的系统阅读和生成文档，但如果要验证旧服务端构建，应优先用仓库提供的 Docker/CentOS 路径。

## 获取源码

```sh
git clone <repo-url> BigWorld
cd BigWorld
```

如果仓库包含子模块，再执行：

```sh
git submodule update --init --recursive
```

## 构建文档站

本站是 VitePress 文档，和 BigWorld 引擎二进制构建分开。

```sh
npm install
npm run docs:build
```

构建通过只能证明文档站可生成，不代表 BigWorld 服务端或客户端二进制可构建。

## Linux 服务端构建入口

源码入口：

```sh
make -C programming
```

Dockerfile 注释中的推荐形式：

```sh
make -s -j64 -rR -C programming
```

单元测试入口：

```sh
make -s -j64 -rR -C programming bw-run-all-unit-tests
```

实际构建时要结合机器资源调整 `-j` 数量。Dockerfile 也提示初次第三方库构建和 Python 构建输出可能导致构建过程需要重跑，这属于当前源码树的构建现实。

## 构建产物位置

Makefile 中服务端输出路径使用平台和配置拼出：

```text
game/bin/server/<platform_config>/server
game/bin/server/<platform_config>/tools
game/bin/server/<platform_config>/unit_tests
```

Dockerfile 注释中明确提到服务端输出通常进入：

```text
game/bin/server/el7/
```

中间产物位于：

```text
programming/bigworld/build/<platform>
```

分析运行问题时，需要同时检查二进制位置、资源路径和配置加载路径。

## 服务端运行顺序

完整集群启动依赖 machined、Manager、DBApp、BaseApp、CellApp、LoginApp 等多个组件注册和发现。不能只启动单个进程就判断架构行为。

源码阅读顺序：

1. `server/<app>/main.cpp`。
2. `lib/server/bwservice.hpp` 的 `BIGWORLD_MAIN`、`bwMainT`、`doBWMainT`。
3. 具体 `App::init()`。
4. `Interface::registerWithInterface()`。
5. `EventDispatcher::processOnce()`。

运行链路细节见 [构建与运行体系](/analysis/build-and-runtime) 和 [machined 控制面与进程发现](/architecture/machined-control-plane)。

## 下一步

- [项目结构](/getting-started/project-structure)：看目录和模块职责。
- [从源码构建](/getting-started/build-from-source)：看 Make、CMake、Docker 的真实边界。
- [源码导读](/analysis/source-code-guide)：按调用链阅读源码。
- [架构研究方法](/architecture/research-method)：统一证据口径。
