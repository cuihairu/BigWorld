# 项目结构

<div class="arch-hero>

BigWorld 是一个大型 C/C++ MMO 引擎，本文档说明项目的目录布局和职责划分。

</div>

## 顶层目录

```
BigWorld/
├── programming/          # 源代码 (核心)
├── docs/                 # 文档 (VitePress)
├── .github/              # GitHub 配置
├── .spec-workflow/       # 规范工作流
├── logo.png              # 项目 Logo
├── package.json          # Node.js 配置 (文档)
├── README.md             # 项目说明
├── CONTRIBUTING.md       # 贡献指南
├── MAINTAINTERS.md       # 维护者
├── LICENSE               # 许可证
└── THIRD-PARTY-NOTICES   # 第三方声明
```

## 源代码目录

### `programming/`

```
programming/
├── bigworld/             # BigWorld 主体
│   ├── server/           # 服务端进程
│   ├── client/           # 客户端
│   ├── lib/              # 核心库
│   ├── tools/            # 工具
│   ├── third_party/      # 第三方依赖
│   ├── build/            # 构建脚本
│   ├── common/           # 共享定义
│   └── examples/         # 示例代码
├── fantasydemo/          # FantasyDemo 示例
├── Makefile              # 顶层 Makefile
└── Makefile.rules        # Make 规则
```

### `programming/bigworld/server/`

服务端进程目录：

| 目录 | 进程 | 职责 |
|------|------|------|
| `baseapp/` | BaseApp | 玩家基础数据管理 |
| `cellapp/` | CellApp | 游戏世界逻辑 |
| `dbapp/` | DBApp | 数据库代理 |
| `loginapp/` | LoginApp | 登录服务 |
| `baseappmgr/` | BaseAppMgr | BaseApp 管理器 |
| `cellappmgr/` | CellAppMgr | CellApp 管理器 |
| `dbappmgr/` | DBAppMgr | DBApp 管理器 |
| `reviver/` | Reviver | 进程恢复 |
| `tools/` | - | 服务端工具 |
| `*_extensions/` | - | 扩展模块 |

### `programming/bigworld/lib/`

核心库目录 (57 个子库)：

#### 基础设施

| 库 | 职责 |
|---|------|
| `cstdmf` | 基础工具库 (调试、内存、计时器) |
| `network` | 网络栈 (Mercury) |
| `math` | 数学库 |
| `resmgr` | 资源管理 |

#### 服务器共享

| 库 | 职责 |
|---|------|
| `entitydef` | 实体定义系统 |
| `script` | 脚本绑定抽象 |
| `pyscript` | Python 桥接层 |
| `db` | 数据库抽象 |
| `server` | 服务器共享代码 |

#### 渲染与场景

| 库 | 职责 |
|---|------|
| `moo` | 渲染引擎 |
| `scene` | 场景管理 |
| `space` | 空间管理 |
| `chunk` | 室内场景 |
| `terrain` | 地形系统 |

#### 客户端专用

| 库 | 职责 |
|---|------|
| `connection` | 客户端连接 |
| `connection_model` | 连接模型 |
| `camera` | 相机系统 |
| `controls` | 输入控制 |

### `programming/bigworld/tools/`

编辑器和工具：

| 工具 | 职责 |
|------|------|
| `worldeditor/` | 世界编辑器 |
| `modeleditor/` | 模型编辑器 |
| `particle_editor/` | 粒子编辑器 |
| `assetprocessor/` | 资源处理器 |
| `navgen/` | 导航网格生成 |

### `programming/bigworld/third_party/`

第三方依赖 (24 个)：

| 依赖 | 用途 |
|------|------|
| `python` | Python 解释器 |
| `openssl` | 加密库 |
| `curl` | HTTP 客户端 |
| `mongodb` | MongoDB 驱动 |
| `sqlite` | SQLite 数据库 |
| `jsoncpp` | JSON 解析 |
| `re2` | 正则表达式 |
| `recastnavigation` | 导航网格 |

### `programming/bigworld/build/`

构建脚本：

```
build/
├── make/                 # Linux Make 构建
│   ├── Makefile          # 主 Makefile
│   ├── platform_el6.mak  # CentOS 6 配置
│   ├── platform_el7.mak  # CentOS 7 配置
│   └── third_party_python.mak
├── cmake/                # CMake 配置
├── docker/               # Docker 配置
│   └── Dockerfile
└── *.bat                 # Windows 批处理
```

## 文档目录

### `docs/`

```
docs/
├── index.md              # 首页
├── .vitepress/           # VitePress 配置
├── public/               # 静态资源
├── getting-started/      # 快速开始
├── architecture/         # 架构研究 (28 章)
├── analysis/             # 项目分析
├── migration/            # 迁移专题
└── references/           # 参考资料
```

## 依赖关系

```
┌─────────────┐
│   server/   │ ← 服务端进程
└──────┬──────┘
       │
┌──────▼──────┐
│    lib/     │ ← 核心库
└──────┬──────┘
       │
┌──────▼──────┐
│ third_party │ ← 第三方依赖
└─────────────┘
```

## 下一步

- [快速开始](/getting-started/) - 构建运行
- [源码导读](/analysis/source-code-guide) - 阅读路径
- [架构研究方法](/architecture/research-method) - 深入学习
