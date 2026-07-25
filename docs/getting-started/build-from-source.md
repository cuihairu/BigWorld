# 从源码构建

<div class="arch-hero>

本文档详细说明如何从源码构建 BigWorld 的各个组件。

</div>

## 构建系统概览

BigWorld 使用混合构建系统：

| 组件 | 构建系统 | 入口文件 |
|------|----------|----------|
| 服务器 | Make + CMake | `programming/Makefile` |
| 客户端 | CMake | `programming/bigworld/CMakeLists.txt` |
| 工具 | CMake | `programming/bigworld/CMakeLists.txt` |
| Docker | Dockerfile | `build/docker/Dockerfile` |

## Linux 服务器构建

### 环境准备

```bash
# CentOS 7
sudo yum groupinstall -y "Development Tools"
sudo yum install -y \
    cmake3 \
    python-devel \
    openssl-devel \
    mariadb-devel \
    sqlite-devel \
    readline-devel \
    gdbm-devel \
    bzip2-devel

# Ubuntu 22.04
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    python3-dev \
    libssl-dev \
    libmysqlclient-dev \
    libsqlite3-dev \
    libreadline-dev
```

### 构建步骤

```bash
# 进入项目根目录
cd programming

# 查看可用目标
make help

# 构建所有服务器组件
make -C build/make server

# 构建特定组件
make -C build/make baseapp
make -C build/make cellapp
make -C build/make dbapp
make -C build/make loginapp
```

### 构建产物

构建产物位于：

```
game/bin/server/
├── linux64/
│   ├── baseapp
│   ├── cellapp
│   ├── dbapp
│   ├── loginapp
│   ├── baseappmgr
│   ├── cellappmgr
│   ├── dbappmgr
│   └── bwmachined
└── linux64_debug/
    └── ... (调试版本)
```

### CMake 构建 (推荐)

```bash
# 进入项目目录
cd programming/bigworld

# 配置
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/bigworld

# 编译
cmake --build build -j$(nproc)

# 安装
cmake --install build
```

## Windows 客户端构建

### 环境准备

1. 安装 Visual Studio 2022
2. 安装 CMake 3.20+
3. 安装 Python 3.10+
4. 安装 OpenSSL (vcpkg 或手动)

### 构建步骤

```powershell
# 进入项目目录
cd programming\bigworld

# 生成 Visual Studio 项目
cmake -G "Visual Studio 17 2022" -A x64 -B build

# 编译
cmake --build build --config Release

# 或者直接打开 Visual Studio
start build\BigWorld.sln
```

### 构建产物

```
game\bin\client\
├── bigworld.exe
├── bigworld.pdb
└── *.dll
```

## Docker 构建

### Dockerfile

```dockerfile
FROM ubuntu:22.04

# 安装依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    python3-dev \
    libssl-dev \
    libmysqlclient-dev \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /src

# 复制源码
COPY . .

# 构建
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)
```

### 使用 Docker

```bash
# 构建镜像
docker build -t bigworld-build -f build/docker/Dockerfile .

# 运行构建容器
docker run --rm -v $(pwd):/src bigworld-build

# 或进入容器手动构建
docker run --rm -it -v $(pwd):/src bigworld-build /bin/bash
```

## 构建选项

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | Release | 构建类型 |
| `CMAKE_INSTALL_PREFIX` | /usr/local | 安装路径 |
| `BUILD_SERVER` | ON | 构建服务器 |
| `BUILD_CLIENT` | ON | 构建客户端 |
| `BUILD_TOOLS` | ON | 构建工具 |
| `ENABLE_TESTS` | OFF | 启用测试 |

### 使用示例

```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTS=ON \
    -DBUILD_CLIENT=OFF
```

## 常见问题

### 找不到 Python

```bash
CMake Error: Could not find Python
```

**解决**:
```bash
# 指定 Python 路径
cmake -B build -DPython_ROOT_DIR=/usr/bin/python3
```

### OpenSSL 版本不兼容

```bash
error: 'SSL_CTX_new' was not declared
```

**解决**:
```bash
# 检查 OpenSSL 版本
openssl version

# 如果是 3.x，可能需要添加兼容标志
cmake -B build -DOPENSSL_NO_DEPRECATED=ON
```

### 内存不足

```bash
c++: fatal error: Killed signal terminated program cc1plus
```

**解决**:
```bash
# 减少并行编译数
cmake --build build -j2

# 或增加 swap
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

## 参考资料

- [项目结构](/getting-started/project-structure)
- [核心 API](/getting-started/api-reference)
- [构建系统现代化](/upgrade-plan/build-system-modernization)
