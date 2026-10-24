# 构建系统现代化


<div class="arch-hero">

BigWorld 的构建系统混合了 Make、CMake 和平台脚本，版本老旧。本文档规划构建系统的现代化路径。

</div>

## 当前状态

### 构建入口

| 入口 | 平台 | 状态 |
|------|------|------|
| `programming/Makefile` | Linux | 主要 |
| `programming/bigworld/CMakeLists.txt` | Windows | 主要 |
| `programming/bigworld/build/docker/Dockerfile` | Docker | 辅助 |
| `programming/bigworld/build/*.bat` | Windows | 辅助 |

### 版本基线

| 工具 | 当前版本 | 推荐版本 |
|------|----------|----------|
| CMake | 2.8.12 | 3.20+ |
| GCC | 14（CI 样板）/ 15（el7 镜像），C++23 已生效 | 保持 |
| Clang | - | 14+ (可选) |
| Make | GNU Make | 保持 |
| Docker | CentOS 7 | Ubuntu 22.04 |

> C++23 已在 make 主链生效（`programming/bigworld/build/make/platform_el7.mak:6`），
> 详见 [C++23 迁移](/zh/cpp23-migration)。

## 升级路径

### 第一阶段：CMake 现代化

**目标**: 从 CMake 2.8 升级到 3.20+

**主要变更**:

1. **最低版本要求**
```cmake
# 旧
cmake_minimum_required(VERSION 2.8.12)

# 新
cmake_minimum_required(VERSION 3.20)
```

2. **目标属性**
```cmake
# 旧
include_directories(${CMAKE_SOURCE_DIR}/lib)
add_executable(bigworld main.cpp)
target_link_libraries(bigworld network)

# 新
add_executable(bigworld main.cpp)
target_include_directories(bigworld PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(bigworld PRIVATE network)
```

3. **生成器表达式**
```cmake
# 旧
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

# 新
target_compile_definitions(bigworld PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)
```

### 第二阶段：平台基线更新

**目标**: 从 CentOS 7 升级到现代 Linux

**新 Dockerfile**:

```dockerfile
# 旧
FROM centos:7

# 新
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    python3-dev \
    libssl-dev \
    libmysqlclient-dev \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*
```

**依赖变更**:

| CentOS 7 包 | Ubuntu 22.04 包 |
|-------------|-----------------|
| python-devel | python3-dev |
| mariadb-devel | libmysqlclient-dev |
| sqlite-devel | libsqlite3-dev |
| openssl-devel | libssl-dev |

### 第三阶段：编译器升级

**目标**: 支持 C++17/20（该阶段目标已达成并超越：make 主链已切 C++23，
见 `platform_el7.mak:6` 与 [C++23 迁移](/zh/cpp23-migration)；以下配置为升级路径存档）

**CMake 配置**:

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

**编译器标志**:

```cmake
# 警告级别
add_compile_options(-Wall -Wextra -Wpedantic)

# 安全检查
add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
```

## CI/CD 集成

### GitHub Actions（已落地，2026-10-12）

实际生效的是两条构建 workflow（另有 `deploy-docs.yml` 文档发布）：

- `.github/workflows/ci.yml` —— **push/PR 门禁**：vcpkg manifest 钉版
  baseline 克隆 + binary cache（键=baseline+manifest 哈希）→ 配置/构建
  CMake 样板 matrix（`[cstdmf, resmgr, network, entitydef]`，`fail-fast: false`；
  [vcpkg-migration §3.1–§3.5](/zh/vcpkg-migration)）→ ctest。
  步骤具名，失败时上传一个含 configure/build/ctest 三日志的 artifact，
  失败可按步骤定位。
- `.github/workflows/daily-build.yml` —— **每日构建**：48 小时新鲜度判定 →
  全量 make + 21 模块单测门禁 → tar.gz/deb/rpm 三格式打包，artifact 交付。
  详见[打包与每日构建](/zh/upgrade-plan/packaging-and-daily-build)。

本地等价命令：CMake 样板各一条命令（`scripts/build-cstdmf-cmake.sh` /
`scripts/build-resmgr-cmake.sh` / `scripts/build-network-cmake.sh` /
`scripts/build-entitydef-cmake.sh`；vendored CPython 一腿另由
`scripts/build-python-cmake.sh` 单独构建，被 entitydef 样板自动带入）；
打包一条命令 `bash scripts/package-daily.sh <out-dir>`。

### Docker 构建

```bash
# 构建镜像（路径相对仓库根；Dockerfile 头注释同此形式）
docker buildx build -t bigworld-build \
  -f programming/bigworld/build/docker/Dockerfile .

# 运行构建
docker run --rm -v $(pwd):/src bigworld-build \
    cmake -B build -DCMAKE_BUILD_TYPE=Release
docker run --rm -v $(pwd):/src bigworld-build \
    cmake --build build -j$(nproc)
```

## 迁移步骤

### 步骤一：CMake 升级

1. 更新 `cmake_minimum_required`
2. 替换废弃命令
3. 添加现代目标属性
4. 测试所有平台

### 步骤二：平台升级

1. 创建新 Dockerfile
2. 更新依赖包名
3. 测试构建
4. 更新文档

### 步骤三：编译器升级

1. 设置 C++ 标准
2. 修复编译警告
3. 启用安全检查
4. 性能测试

## 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| CMake 命令废弃 | 构建失败 | 逐步替换，保持兼容 |
| 依赖包名变更 | 安装失败 | 文档更新，脚本适配 |
| 编译器警告增多 | 构建噪音 | 逐步修复，分阶段启用 |
| 性能回退 | 运行变慢 | 基准测试，回退机制 |

## 验证清单

- [ ] Linux 构建通过
- [ ] Windows 构建通过
- [ ] Docker 构建通过
- [ ] 单元测试通过
- [ ] 集成测试通过
- [ ] 性能无回退
- [ ] 文档更新

## 参考资料

- [CMake 文档](https://cmake.org/cmake/help/latest/)
- [GCC 文档](https://gcc.gnu.org/onlinedocs/)
- [构建与运行体系](/zh/getting-started/build-from-source)
