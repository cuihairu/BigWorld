# 构建与运行体系

## 构建体系不是单一路径

仓库同时维护多条构建链：

- `programming/Makefile` -> `programming/bigworld/build/make/Makefile`
- `programming/bigworld/CMakeLists.txt`
- `programming/bigworld/build/*.bat`
- `programming/bigworld/build/docker/Dockerfile`

这表明项目面向不同平台和历史团队流程长期演化，没有统一到单一构建标准。

## Linux / 服务端构建

Linux 构建主入口是：

```make
make -C programming
```

其核心特征：

- 偏向 `CentOS/RHEL` 系列平台
- 通过 `platform_el6.mak`、`platform_el7.mak` 做平台差异配置
- `el7` 已显式启用 `-std=c++11`
- 构建产物输出到 `game/bin/server/<platform>`

Dockerfile 也明确假设了 `centos:7`，并依赖：

- `python-devel`
- `mariadb-devel`
- `sqlite-devel`
- `readline-devel`
- `gdbm-devel`
- `bzip2-devel`

这套依赖链对新版嵌入式 Python 运行时并不直接兼容。

## Windows / 工具链构建

根 CMake 文件体现出几个重要事实：

- 最低版本仍是 `CMake 2.8.12`
- 工程主要面向 `Windows`
- 支持的编译器判断以 `MSVC9` 到 `MSVC14` 为主
- solution 生成仍是传统 Visual Studio 思路

也就是说：

- CMake 配置本身就偏旧
- Python 运行时变更不能脱离整体构建升级讨论

## 内嵌 Python 的构建方式

项目不是简单链接系统 Python，而是：

1. 在 `third_party/python` 中内置 Python 源码
2. 通过 `third_party_python.mak` 构建 `libbwpython2.7.a`
3. 构建并复制 `cPickle.so`、`zlib.so`、`_ssl.so` 等共享模块
4. 把 `Lib` 标准库和 `site-packages` 安装到 BigWorld 资源树中

这意味着：

- Python 解释器是项目产品的一部分
- 标准库目录结构和共享模块目录结构都被项目自定义了
- Python 升级会影响打包、部署、资源路径和模块加载逻辑

## Python 附加包构建方式

`third_party_python_modules.mak` 显示，项目还会打包和复制一批旧版 Python 依赖，例如：

- `Pympler 0.3.0`
- `pika 0.9.13`
- `oursql 0.9.2`
- `SQLAlchemy 0.6.6`
- `AsyncSQLAlchemy.py`

这些版本对新版 Python 运行时基本都不能直接使用。

## 运行时模型

运行时大体可分为三类：

1. 引擎进程
   例如 `baseapp`、`cellapp`、`dbapp`。
2. 运维与数据工具
   例如 `bwmachined`、`message_logger`、`sync_db`。
3. 编辑器与离线工具
   例如 `worldeditor`、`modeleditor`、`assetprocessor`。

三类都通过不同形式依赖 Python 嵌入或 Python 扩展，因此升级面是全仓库级的。

## 现状中的异常与信号

扫描中发现一个有价值的异常点：

- CMake 的 `BWConfiguration_server.cmake` 引用了 `tools/server/control_cluster.py`
- 仓库中实际没有找到 `control_cluster.py`

这通常说明至少存在以下一种情况：

- 仓库不完整
- 文档/脚本路径已经漂移
- 历史打包物与源码树已不完全一致

在开始运行时迁移前，必须先做一次“可构建性基线校验”。
