# ADR-0002：第三方依赖从 vendored 源码树切换到 vcpkg manifest


- **状态**：已接受（2026-10-12 落地，`2bdfc5aa` 切换、`1180ece6` 钉 baseline）
- **范围**：`programming/bigworld/third_party/`、`third_party_*.mak`、
  `build/cmake/FindBW*.cmake`、CI/每日构建工作流

## 背景

`third_party/` 下 20 余个库全部是 2008-2014 年代的 vendored 源码树
（OpenSSL 1.0.0d、curl 7.x、jsoncpp 0.7.0、zlib 1.2.8、libpng 1.6.2、
sqlite 3.6.23 …），自带一套 configure/visualc 构建脚本：

- OpenSSL 1.0.0d 低于 Python 3.13 的最低要求（1.1.1），且在 gcc15 默认
  C23 下编不过（`bool` 关键字冲突）——直接阻塞解释器 `_ssl`/`_hashlib`；
- 每个库一套私有构建脚本，版本无 lockfile、升级无参考点、审计面大。

## 决策

可用 vcpkg 端口等价替代的库统一改为 **vcpkg manifest 引入**
（`programming/bigworld/vcpkg.json` 钉 `builtin-baseline`，可复现解析；
自定义 triplet `x64-linux-bw`），make 与 CMake 两条链都吃 vcpkg toolchain
产物：**OpenSSL 3.6 / curl 8.x / jsoncpp 1.9 / zlib / libpng / sqlite3 /
bzip2**。链接名保持旧名（`zip`、`bwcurl`），消费面改动最小化
（png 弃私有头改公开 API、sqlite 只留包装层）。

**明确保留 vendored** 的三类（切换范围表里逐条给了理由）：
CPython（BWHooks 补丁树，见 [ADR-0001](adr-0001-python-313-embedded-interpreter)）、
MongoDB legacy C++ 驱动（vcpkg 是 3.x API，与 legacy 调用面不兼容）、
png/zip 源码树（Windows CMake 工程仍引用，R3 落地后删除）。

## 取舍

| 备选 | 否掉的原因 |
|---|---|
| 逐库原地升级 vendored 树 | 每库仍要自带构建脚本与补丁维护；7 个库 × 长期 = 持续债，无 lockfile |
| 直接吃系统包（apt） | CI/用户机版本不可控，违反离线可复现；macOS/Windows 更碎 |
| 全部 vcpkg 化（含 CPython/mongo） | CPython 端口无 BWHooks；mongo 端口 API 代际不匹配，硬切等于重写消费面 |
| CMake FetchContent | make 主链吃不到；两套依赖解析（FetchContent + make）比单一 manifest 更糟 |

## 后果

- **正面**：OpenSSL 3.6 解锁 `_ssl`/`_hashlib`（R1 关闭）；依赖升级 =
  改 manifest 一行 + baseline 前移，binary cache 让 CI/本地秒级；
  `builtin-baseline` 使任何时点的构建可复现。
- **代价/风险**：zlib 分配不再走 `MY_ZCALLOC` 钩子 → **zlib 分配不进 BW
  内存统计**（已登记的观测口径变化）；jsoncpp 消费面走 deprecated API
  （有告警面，暂容忍）。
- **后续**：R3 落地后删除 `third_party/png`、`third_party/zip` 残树；
  vcpkg 升级走 baseline 前移流程。

证据：[第三方依赖切换到 vcpkg](/zh/vcpkg-migration)。
