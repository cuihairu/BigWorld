# 源码导读

<div class="arch-hero">

本章帮助你快速理解 BigWorld 源码的整体结构和阅读路径。不是目录罗列，而是按职责和依赖关系组织的导航图。

</div>

## 源码入口

BigWorld 源码位于 `programming/bigworld/`，主要分为以下几层：

```
programming/bigworld/
├── server/           # 服务端进程
├── client/           # 客户端
├── lib/              # 核心库 (最重要)
├── tools/            # 编辑器和工具
├── third_party/      # 第三方依赖
├── build/            # 构建脚本
└── common/           # 共享定义
```

## 核心库结构

`lib/` 是整个引擎的核心，包含 57 个子库。按职责可分为：

### 基础设施层

| 库 | 职责 | 关键文件 |
|---|------|----------|
| `cstdmf` | 基础工具库 | `debug.hpp`, `memory.hpp`, `timer.hpp` |
| `network` | 网络栈 (Mercury) | `channel.hpp`, `bundle.hpp`, `endpoint.hpp` |
| `math` | 数学库 | `vector3.hpp`, `matrix.hpp` |
| `resmgr` | 资源管理 | `resource.hpp`, `file_system.hpp` |

### 服务器共享层

| 库 | 职责 | 关键文件 |
|---|------|----------|
| `entitydef` | 实体定义 | `entity_description.hpp`, `data_type.hpp` |
| `script` | 脚本绑定 | `script_object.hpp`, `script_value.hpp` |
| `pyscript` | Python 桥接 | `py_script_object.hpp`, `script_output_stream.hpp` |
| `db` | 数据库抽象 | `idatabase.hpp`, `db_status.hpp` |

### 渲染与场景层

| 库 | 职责 | 关键文件 |
|---|------|----------|
| `moo` | 渲染引擎 | `device.hpp`, `render_context.hpp` |
| `scene` | 场景管理 | `scene_provider.hpp` |
| `space` | 空间管理 | `space.hpp` |
| `chunk` | 室内场景 | `chunk.hpp`, `chunk_space.hpp` |

## 服务端进程

`server/` 目录包含 MMO 架构的核心进程：

| 进程 | 职责 | 入口文件 |
|------|------|----------|
| `baseapp` | 玩家基础数据 | `main.cpp` |
| `cellapp` | 游戏世界逻辑 | `main.cpp` |
| `dbapp` | 数据库代理 | `main.cpp` |
| `loginapp` | 登录服务 | `main.cpp` |
| `baseappmgr` | BaseApp 管理 | `main.cpp` |
| `cellappmgr` | CellApp 管理 | `main.cpp` |
| `dbappmgr` | DBApp 管理 | `main.cpp` |
| `reviver` | 进程恢复 | `main.cpp` |

## 阅读路径建议

### 路径一：网络栈

从 `lib/network/` 入手，理解 Mercury 可靠 UDP：

1. `endpoint.hpp` - Socket 抽象
2. `channel.hpp` - 逻辑通道
3. `bundle.hpp` - 消息打包
4. `packet.hpp` - 包结构
5. `udp_channel.hpp` - UDP 可靠通道

### 路径二：实体系统

从 `lib/entitydef/` 入手：

1. `entity_description.hpp` - 实体描述
2. `data_type.hpp` - 数据类型系统
3. `data_description.hpp` - 属性描述
4. `method_description.hpp` - 方法描述

### 路径三：脚本嵌入

从 `lib/pyscript/` 和 `lib/script/` 入手：

1. `script_object.hpp` - 脚本对象基类
2. `py_script_object.hpp` - Python 对象包装
3. `script_output_stream.hpp` - 序列化输出

### 路径四：服务端主循环

从任意 `server/*/main.cpp` 入手：

1. 进程初始化
2. 事件循环启动
3. 网络监听
4. Tick 处理

## 关键命名空间

| 命名空间 | 说明 |
|----------|------|
| `Mercury` | 网络栈 |
| `BW` | 核心工具 |
| `Script` | 脚本绑定 |
| `DB` | 数据库 |
| `EntityDef` | 实体定义 |

## 构建系统

构建入口：

- Linux: `programming/Makefile` → `build/make/`
- Windows: `programming/bigworld/CMakeLists.txt`
- Docker: `build/docker/Dockerfile`

详见 [构建与运行体系](/analysis/build-and-runtime)。

## 下一步

- 了解 [总体画像](/analysis/overview) - 项目整体情况
- 阅读 [架构研究方法](/architecture/research-method) - 如何分析源码
- 查看 [进程拓扑](/architecture/process-topology) - 分布式架构
