# 打包与每日构建分析

<div class="arch-hero">

回答三个问题：打包是不是只有 RPM？Windows 能不能支持？每日构建怎么做？结论先行——**打包不欠构建只欠格式**（make 全量构建已产出完整安装树），**Windows 今天可用 WSL2、原生支持是分层工程**，**每日构建已落地为 GitHub Actions 工作流**（构建 + 21 模块单测门禁 + tar/deb/rpm 三格式产物）。

</div>

## 打包现状：RPM 是遗产，不是上限

仓库根的三个 RPM（`bigworld-devel` / `bigworld-bwmachined` / `bigworld-web-integration`，均为 `14.4.1.el7.x86_64`）是上游发行遗产；历史打包链是 `build/bw_internal/scripts/makeRPM.py` → `make rpm`，配套 `build/docker/Dockerfile`（centos:7）。这条链有两个硬伤：

- CentOS 7 已于 2024-06 EOL，其 gcc 4.8 无法承载现代化后的 C++23 构建口径；
- `makeRPM.py` 指向的是历史 `src/server` 布局，与本仓 `game/` 安装树已不一致，未随 vcpkg / Python 3.13 迁移验证。

关键事实是：**make 全量构建本身就完成了打包的大部分工作**——安装规则把产物铺进 `game/bin/server/el7/`（约 1.4 GB：examples / server / tools / unit_tests / third_party）和 `game/res/`（约 90 MB）。打包层只差“装进格式”：

<div class="decision-table">

| 格式 | 产出方式 | 定位 |
|------|----------|------|
| tar.gz | `tar` 直接打包安装树 | 保底格式，任何环境可解；`tar -C / -xzf` 即落位 `/opt/bigworld` |
| .deb | `dpkg-deb`（ubuntu/debian 自带） | Debian 系装机口径，声明 libc/libstdc++/mysql client 依赖，第三方库为 vcpkg 静态链接 |
| .rpm | `rpmbuild`（`apt install rpm` 即可，无需 CentOS） | 保留 RPM 交付口径，与历史 el7 产物对齐 |

</div>

三种格式共用同一 `/opt/bigworld/{bin,res}` 布局，版本口径统一为 `14.4.1+daily<UTC日期>.<git短sha>`（源头是 `vcpkg.json` 的 `version` 字段）。

## Windows 支持分层分析

“平时开发在 Windows”对应的近期与远期路径不同，分层看：

<div class="decision-table">

| 层级 | 可行性 | 依据 | 建议 |
|------|--------|------|------|
| WSL2（立即） | ✅ 今天可用 | `platform_info.py` 在 3.13 迁移期已把无 `/etc/redhat-release` 的 Debian 系主机映射为 el7 工具链配置；WSL2 即 Ubuntu 用户态，vcpkg 依赖清单（openssl/curl/jsoncpp/zlib/libpng/sqlite3/bzip2）全部可用 | Windows 开发机上直接走完整构建 + 21 模块单测，与 CI 同口径 |
| MSVC 冒烟（中期） | ⚠️ 需工程投入 | CMake 链与 `build/*.bat` 是历史 Windows 工程生成面（client / tools）；现存 `.vcproj` 是 VS2003 时代格式；vcpkg 本身跨平台已就位，缺的是工程文件现代化 | 先让平台无关库（cstdmf / math）过 MSVC 编译门禁，再逐步外扩 |
| 服务端原生 Windows（远期） | ⚠️ 大工程 | platform mak 无 win32 配置；`event_poller` 已抽象 select/poll/epoll（Windows select 有现成落点），但 I/O、路径、信号等平台层面广 | 不纳入每日构建范围 |

</div>

## 每日构建设计（已落地）

`.github/workflows/daily-build.yml` + `scripts/package-daily.sh`：

<div class="decision-table">

| 环节 | 设计 | 关键点 |
|------|------|--------|
| 触发 | `schedule: 23 21 * * *`（UTC，= 北京时间每日 05:23）+ `workflow_dispatch` | GitHub 只在**默认分支（main）**上跑 schedule——合并到 main 后自动生效；dev 分支用手动触发 |
| 环境 | ubuntu-24.04 固定版本 + gcc-14（update-alternatives 指到 g++ 默认位） | 平台探测的 Debian 回退使其自动落在 el7 工具链配置，无需覆盖 |
| 系统依赖 | readline/ncurses/bz2/sqlite3/gdbm/mysqlclient/ffi 等 dev 包 | `libmysqlclient-dev` 必装——`third_party_mysql.mak` 在编译链内；vendored CPython 3.13 构建需要这组头文件 |
| UDP 缓冲 | `sysctl net.core.rmem_max=16777216` | network_test 的 ConfigTest 门禁阈值，低于它整模块红 |
| 第三方 | 克隆官方 vcpkg + bootstrap，manifest（vcpkg.json）在 make 过程中自动物化 | `VCPKG_ROOT` 环境变量 |
| 门禁 | 全量构建 → `bw-run-all-unit-tests`（21 模块） | 打包只在上游两步全绿后执行 |
| 产物 | tar.gz / deb / rpm 三格式，`upload-artifact` 保留 14 天 | **不打 tag、不发 GitHub Release**（仓库规则）；每日产物随保留期自动过期 |

</div>

### 验证口径

- `scripts/package-daily.sh` 已在本机实跑验证（tar.gz 与 deb 实际产出并检查了内容；本机无 `rpmbuild`，rpm 分支经检查 + CI 首跑验证）。
- workflow 首跑依赖合并到 main 后的首次调度，或任意分支手动 `workflow_dispatch`——在此之前其正确性是**检查级**而非**运行级**，首个运行日志应重点核对 gcc-14 与 vcpkg manifest 两步。

### 与覆盖率工程的关系

每日构建不改变本地门禁口径（仍以 `bw-run-all-unit-tests` 全绿为准），它把“干净环境全绿”从本机事实升级为**每日外部事实**；覆盖率现状见[迁移与覆盖率现状](/upgrade-plan/migration-status)，逐批过程见[全量台账](/python-313-migration)。
