<p align="center">
  <img src="logo.svg" alt="BigWorld logo" width="120" />
</p>

<p align="center">
  English | <a href="README.zh.md">简体中文</a>
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

## Project status

 - **Stack**: C++23 server (Linux) / embedded **Python 3.13.16** (migrated from 2.7) / [vcpkg](https://vcpkg.io) third-party dependency management
 - **Quality gate**: 21 module test suites, 1505 cases all green (measured 2026-10-15, after batch 36: fantasydemo placeholder assets + hot-reload action sequence + entity message routing decision table); gcovr instrumented coverage baseline (reinforcement batches 1–32 complete)
 - **State**: the dev branch daily build and full test run stay green

The full Python 2.7 → 3.13 migration, per-batch acceptance and the pitfall log: [migration ledger](docs/python-313-migration.md). Current migration state and per-module coverage baselines: [migration & coverage status](docs/upgrade-plan/migration-status.md). Plan and backlog ledger: [todo.md](todo.md) (maintained by patrol sweeps, with a three-tier inventory of implemented / design-only / not started).

## Documentation site

The architecture-research, project-analysis and migration-engineering docs (Chinese) are built with VitePress (sources in `docs/`):

 - Read online: <https://cuihairu.github.io/BigWorld/>
 - Build locally: `npm install && npm run docs:build` (output in `docs/.vitepress/dist/`)

## Dependencies

Server (Linux, the daily verification path):

 - Linux x86-64 (el7 platform target) + GCC, C++23
 - [vcpkg](https://vcpkg.io): the `VCPKG_ROOT` environment variable points at a local vcpkg checkout
 - Node.js (documentation-site builds only)

Quick build and self-test:

```bash
cd programming
env VCPKG_ROOT=/path/to/vcpkg make -s -j16 -rR                  # full build
env VCPKG_ROOT=/path/to/vcpkg make -s -rR bw-run-all-unit-tests # 21 module test suites
```

Legacy development environment (client/tooling; not re-validated as part of this stack modernization):

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

## Local testing conventions

CI and local development split into three lanes by change surface — a one-line
doc change should not trigger a full build.

1. **Doc changes** (`*.md`, `docs/**`, images): no build. The main CI
   workflow's `paths-ignore` already skips those paths.
2. **Code changes**: run only the affected subset, never the full suite.
   - `ctest --test-dir build -R '<affected tests>'`
3. **Full suite** (`ctest --test-dir programming/bigworld/build/cmake-cstdmf`):
   only for CMakeLists.txt / cmake/**, `.github/**`, Docker/Compose config
   changes, or pre-release.
