# 项目结构

](/> [English)) | **中文**

<div class="arch-hero">

BigWorld 是一个大型 C/C++ MMO 引擎，本文档说明项目的目录布局和职责划分。

</div>

## 顶层目录

```
BigWorld/
├── programming/          # 源代码 (核心)
├── docs/                 # 文档 (VitePress)
├── game/                 # 构建输出根 (Makefile 默认安装目录)
├── scripts/              # 辅助脚本 (CMake 样板构建/每日打包)
├── .github/              # GitHub 配置
├── logo.svg              # 项目 Logo
├── package.json          # Node.js 配置 (文档)
├── README.md             # 项目说明
├── CONTRIBUTING.md       # 贡献指南
├── MAINTAINTERS.md       # 维护者
├── TESTING.md            # 测试口径与陷阱登记
├── todo.md               # 巡检待办账本
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

第三方依赖：22 个 vendored 目录（另有 7 个依赖走 vcpkg，见下）。

| 依赖 | 用途 |
|------|------|
| `python` | 内嵌 Python 3.13 解释器（BigWorld 补丁版，不走 vcpkg） |
| `mongodb` | legacy MongoDB C++ 驱动（vcpkg mongo-cxx-driver 3.x API 不兼容，保留） |
| `CppUnitLite2` | 单元测试框架 |
| `re2` | 正则表达式 |
| `recastnavigation` | 导航网格 |
| `nedalloc` / `nvtt` / `nvmeshmender` | 内存分配 / 纹理压缩 / 网格修正 |
| `png` / `zip` / `sqlite` / `curl` | 历史 vendored 副本（平台相关） |
| `openssl-vs2010` … `openssl-vs2015` / `openssl_android` | 历史平台构建副本 |
| `putty` / `WTL` / `openautomate` / `cmake-win32-x86` / `python_modules` | 工具库与构建支持 |

el7 构建路径的 vcpkg 端口（`programming/bigworld/vcpkg.json`）：
`openssl`、`curl`（openssl feature）、`jsoncpp`、`zlib`、`libpng`、`sqlite3`、`bzip2`（后者仅供 CPython `_bz2` 扩展）。

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
├── public/               # 静态资源 (logo.svg、pdf/)
├── getting-started/      # 快速开始
├── architecture/         # 架构研究 (28 章)
├── analysis/             # 项目分析
├── upgrade-plan/         # 迁移与升级专题 (6 篇)
├── references/           # 参考资料
├── pdf/                  # 原始 PDF（服务端白皮书/构建指南/安装指南，3 份）
├── python-313-migration.md  # Python 3.13 迁移全量台账
├── vcpkg-migration.md    # vcpkg 迁移专题
└── cpp23-migration.md    # C++23 迁移专题
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

- [快速开始](/zh/getting-started/) - 构建运行
- [源码导读](/zh/analysis/source-code-guide) - 阅读路径
- [架构研究方法](/zh/architecture/research-method) - 深入学习
