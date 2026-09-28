![BigWorld for everyone](logo.svg)

# BigWorld (TM) Open-Source Edition

This repository contains BigWorld Open-Source Edition (OSE) that is comprised of server, 
client and tools components which are used together to build massively multiplayer games 
and virtual worlds.

BigWorld OSE key features:

 - Load-Balancing
 - Scalability
 - Fault Tolerance

For more details refer to [BigWorld Technology Server Whitepaper](<docs/pdf/BigWorld Technology Server Whitepaper.pdf>)

## 项目状态

 - **技术栈**：C++23 服务端（Linux）/ 嵌入式 **Python 3.13**（已从 2.7 完成迁移）/ [vcpkg](https://vcpkg.io) 第三方依赖管理
 - **质量门禁**：21 个模块单测、1113 个用例全绿；gcovr 插桩覆盖率实跑基线（补强批次 1–18 完成）
 - **状态**：dev 分支日常构建与全量测试保持全绿

Python 2.7 → 3.13 迁移的完整过程、逐批验收与陷阱记录见[迁移全量台账](docs/python-313-migration.md)；迁移现状与各模块覆盖率基线见[迁移与覆盖率现状](docs/upgrade-plan/migration-status.md)。

## 文档站

架构研究、项目分析与迁移工程的中文文档基于 VitePress 构建（源文件在 `docs/`）：

 - 在线阅读：<https://cuihairu.github.io/BigWorld/>
 - 本地构建：`npm install && npm run docs:build`（产物在 `docs/.vitepress/dist/`）

## Dependencies

服务端（Linux，日常验证路径）：

 - Linux x86-64（el7 平台目标）+ GCC，C++23
 - [vcpkg](https://vcpkg.io)：环境变量 `VCPKG_ROOT` 指向本地 vcpkg 目录
 - Node.js（仅文档站构建需要）

快速构建与自测：

```bash
cd programming
env VCPKG_ROOT=/path/to/vcpkg make -s -j16 -rR                  # 全量构建
env VCPKG_ROOT=/path/to/vcpkg make -s -rR bw-run-all-unit-tests # 21 模块单测
```

历史开发环境（客户端/工具链，未随本次技术栈现代化重新验证）：

 - Windows 10 with WSL2 and Hyper-V manager enabled.
 - CentOS 7
 - Microsoft Visual Studio 2019-2022
 - Vagrant
 - Docker

## Installation & Configuration

Server installation instructions are located in a separate [Server Installation Guide](<docs/pdf/Server Installation Guide.pdf>) document.
Server build instructions can be found in [Server Build Guide](<docs/pdf/Server Build Guide.pdf>).

## Getting involved

Instructions on _how_ to contribute can be found in [CONTRIBUTING](CONTRIBUTING.md) document.

<a href="https://sourceforge.net/p/bigworld/code/HEAD/tree/" target="_blank">Forked from </a>

<a href="https://drive.google.com/file/d/1hLm_Ox0v-xIen8c4MvwRHQhkutf9bIpK/view?usp=share_link" target="_blank">Full resources link</a>

<a href="https://blog.csdn.net/antsmall/article/details/139781143" target="_blank">Fantasydemo Server&Client tutorial</a>
