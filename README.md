<p align="center">
  <img src="logo.svg" alt="BigWorld logo" width="120" />
</p>

<h1 align="center">BigWorld (TM) Open-Source Edition</h1>

This repository contains BigWorld Open-Source Edition (OSE) that is comprised of server, 
client and tools components which are used together to build massively multiplayer games 
and virtual worlds.

BigWorld OSE key features:

 - Load-Balancing
 - Scalability
 - Fault Tolerance

For more details refer to [BigWorld Technology Server Whitepaper](<docs/pdf/BigWorld Technology Server Whitepaper.pdf>)

## 项目状态

 - **技术栈**：C++23 服务端（Linux）/ 嵌入式 **Python 3.13.16**（已从 2.7 完成迁移）/ [vcpkg](https://vcpkg.io) 第三方依赖管理
 - **质量门禁**：21 个模块单测、1485 个用例全绿（2026-10-15 实测，cellapp 实体消息 EntityID/SpaceID 关联接线批次 35 后）；gcovr 插桩覆盖率实跑基线（补强批次 1–32 完成）
 - **状态**：dev 分支日常构建与全量测试保持全绿

Python 2.7 → 3.13 迁移的完整过程、逐批验收与陷阱记录见[迁移全量台账](docs/python-313-migration.md)；迁移现状与各模块覆盖率基线见[迁移与覆盖率现状](docs/upgrade-plan/migration-status.md)。规划与待办账本见 [todo.md](todo.md)（patrol 巡检维护，含已实现/只有设计/完全没有三档盘点）。

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

## 本地测试口径

CI 与本地开发按改动面三路分流——改一行文档不该全量编译。

1. **文档改动**（`*.md`、`docs/**`、图片）：不编译。主 CI workflow 的
   `paths-ignore` 已跳过这些路径。
2. **代码改动**：只跑改动子集，不全量。
   - `ctest --test-dir build -R '<改动测试>'`
3. **全量**（`ctest --test-dir programming/bigworld/build/cmake-cstdmf`）：仅以下情形触发——CMakeLists.txt / cmake/**、
   `.github/**`、Docker/Compose 配置、发布前。

