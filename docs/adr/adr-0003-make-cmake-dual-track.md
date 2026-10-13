# ADR-0003：make 主链 + 按组件 CMake 样板双轨并存

- **状态**：已接受（2026-10-12 cstdmf 样板落地，2026-10-14 resmgr + network + CI matrix）
- **范围**：`programming/bigworld/build/make/`、`cmake/samples/`、
  `scripts/build-*-cmake.sh`、`.github/workflows/ci.yml`、
  `lib/*/CMakeLists.txt`（Windows 超级工程）

## 背景

仓库原生构建是 `build/make/` 一套递归 make（Linux 服务端唯一验证口径：
21 模块单测门禁在此跑），另有一套 2008 年代的 Windows CMake 超级工程
（`BWConfiguration_*.cmake` + `lib/*/CMakeLists.txt`，MSVC/编辑器面，未迁移）。
现代构建诉求（vcpkg toolchain、可复现、增量、IDE 支持）与两万人年 make 规则
的迁移风险之间存在矛盾。

## 决策

**不整体迁移**。Linux 侧维持 make 为唯一权威链（编译选项、TU 清单、门禁
全以它为准），另立 `cmake/samples/<组件>/CMakeLists.txt` 按组件样板
**自下而上**双轨并存：

- 样板编译选项/宏逐字取自 `make -n` 实测命令行；源清单逐条对照
  `Makefile.rules`（Windows/macOS TU 与清单外文件显式排除并注释）；
  依赖走共享 vcpkg manifest（见 [ADR-0002](adr-0002-vcpkg-third-party-deps)）；
- 每组件一条命令（`scripts/build-<组件>-cmake.sh`：configure → build →
  ctest），用例数与 make 链一致才收（cstdmf 427、resmgr 88、network 210）；
- CI（`ci.yml`）以 matrix 跑全部样板，防 manifest 与样板漂移；
- Windows 超级工程保持原样，归 R3 统一处理。

## 取舍

| 备选 | 否掉的原因 |
|---|---|
| 一次性把 make 链翻译成 CMake 超级工程 | 30+ 组件 × 平台宏矩阵，任何一处语义漂移都直接威胁 21 模块门禁；不可增量验收 |
| Bazel/Meson 等新系统重写 | 更大的人员/工具成本；且与既有 Windows CMake 工程生态无法复用 |
| 只留 make，不做 CMake | 无 IDE/增量现代化路径；vcpkg toolchain 集成与 CI 矩阵验证面单薄 |
| 直接修 Windows 超级工程上 Linux | 该工程深绑 MSVC/MFC/编辑器面，混入 Linux server profile 会把 R3 风险拉进主线 |

## 后果

- **正面**：双轨互相校验（样板用例数对 make 链是持续断言）；组件可按需
  现代化；新组件接入模板可复制（cstdmf → resmgr → network 已验证）。
- **代价/风险**：`Makefile.rules` 改源清单必须同步样板清单（样板头部已
  注明"keep in sync"）；两套构建语义长期并存，需 CI matrix 兜漂移；
  每样板 ~300 行显式清单是刻意为之的"翻译透明性"，不是债。
- **后续**：connection/entitydef 样板按同一模式叠加
  （connection 无 unit_test 需先建测试面、entitydef 依赖 8 库，单独评估）；
  R3 时 Windows 超级工程与 Linux 样板合流或分工再立 ADR。

证据：[vcpkg-migration §3](/vcpkg-migration)、
[构建系统现代化](/upgrade-plan/build-system-modernization)。
