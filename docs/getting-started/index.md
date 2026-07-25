---
layout: doc
---

# 快速开始

<div class="arch-hero">

本章帮助你快速了解 BigWorld 项目，从获取源码到构建运行。

</div>

## 环境要求

### 操作系统

| 平台 | 版本 | 用途 |
|------|------|------|
| CentOS | 7.x | 服务器构建 |
| Ubuntu | 22.04 | 推荐开发环境 |
| Windows | 10+ | 客户端/工具 |

### 开发工具

| 工具 | 版本 | 说明 |
|------|------|------|
| Git | 2.30+ | 版本控制 |
| CMake | 3.20+ | 构建系统 |
| GCC | 11+ | C++ 编译器 |
| Python | 3.10+ | 构建脚本 |

### 依赖库

```bash
# Ubuntu/Debian
sudo apt-get install -y \
    build-essential \
    cmake \
    python3-dev \
    libssl-dev \
    libmysqlclient-dev \
    libsqlite3-dev \
    libreadline-dev

# CentOS/RHEL
sudo yum install -y \
    gcc-c++ \
    cmake3 \
    python3-devel \
    openssl-devel \
    mariadb-devel \
    sqlite-devel \
    readline-devel
```

## 获取源码

```bash
# 克隆仓库
git clone https://github.com/your-org/BigWorld.git
cd BigWorld

# 初始化子模块 (如果有)
git submodule update --init --recursive
```

## 构建项目

### Linux 构建

```bash
# 进入构建目录
cd programming

# 配置
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build -j$(nproc)

# 安装 (可选)
cmake --install build --prefix /opt/bigworld
```

### Docker 构建

```bash
# 构建镜像
docker build -t bigworld-build -f build/docker/Dockerfile .

# 运行构建
docker run --rm -v $(pwd):/src bigworld-build \
    cmake -B build -DCMAKE_BUILD_TYPE=Release
docker run --rm -v $(pwd):/src bigworld-build \
    cmake --build build -j$(nproc)
```

### Windows 构建

```powershell
# 使用 Visual Studio
cmake -G "Visual Studio 17 2022" -B build
cmake --build build --config Release
```

## 运行示例

### 启动服务器

```bash
# 进入安装目录
cd game/bin/server/linux64

# 启动 machined (进程管理器)
./bwmachined

# 启动 DBApp
./dbapp &

# 启动 BaseApp
./baseapp &

# 启动 CellApp
./cellapp &

# 启动 LoginApp
./loginapp &
```

### 运行 FantasyDemo

```bash
# 进入示例目录
cd programming/fantasydemo

# 启动服务器
./start_server.sh

# 连接客户端 (需要 Windows)
# 运行 game/bin/client/bigworld.exe
```

## 下一步

- [项目结构](/getting-started/project-structure) - 了解目录布局
- [从源码构建](/getting-started/build-from-source) - 详细构建指南
- [核心 API](/getting-started/api-reference) - API 参考
- [架构研究方法](/architecture/research-method) - 深入学习

## 常见问题

### 构建失败

**问题**: CMake 版本过低
```bash
CMake Error: CMake 3.20 or higher is required. You are running version 2.8.12
```

**解决**: 升级 CMake
```bash
# Ubuntu
sudo apt-get install cmake

# 或使用 snap
sudo snap install cmake --classic
```

### 依赖缺失

**问题**: 找不到 OpenSSL
```bash
Could NOT find OpenSSL
```

**解决**: 安装开发包
```bash
sudo apt-get install libssl-dev
```

### 权限问题

**问题**: 无法绑定端口
```bash
bind: Permission denied
```

**解决**: 使用高端口或 sudo
```bash
# 使用 1024 以上的端口
./loginapp --port 20013

# 或使用 sudo (不推荐)
sudo ./loginapp
```
