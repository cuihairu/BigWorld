# 目录与模块

## 核心目录职责

### `programming/bigworld/build`

构建入口与平台脚本集中区，覆盖：

- `build/make`
  Linux/服务端 Make 构建体系。
- `build/cmake`
  Windows 侧和部分工程生成逻辑。
- `build/docker`
  CentOS 7 构建容器定义。
- `build/xcode`
  旧式 Apple 平台构建辅助脚本。

这是典型的多年代叠加构建系统。

### `programming/bigworld/lib`

引擎基础库集合，是整个仓库的核心复用层。可以按职责粗分为：

- 基础设施：`cstdmf`、`network`、`math`、`resmgr`、`build`
- 服务器共享：`server`、`db`、`entitydef`、`script`、`pyscript`
- 渲染与场景：`moo`、`romp`、`scene`、`space`、`terrain`、`chunk`
- 工具与编辑器共享：`asset_pipeline`、`ual`、`gizmo`、`visual_manipulator`

其中最关键的脚本运行时耦合库是：

- `lib/pyscript`
- `lib/script`
- `lib/entitydef_script`

它们直接决定嵌入式 Python 运行时的维护成本。

### `programming/bigworld/server`

服务端进程层，符合 MMO 典型多进程架构：

- `baseapp`
- `baseappmgr`
- `cellapp`
- `cellappmgr`
- `dbapp`
- `dbappmgr`
- `loginapp`
- `reviver`

此外还有：

- `server/tools`
  配套运维与数据处理工具，如 `bwmachined`、`message_logger`、`sync_db`、`transfer_db`。
- `baseapp_extensions`、`cellapp_extensions`、`dbapp_extensions`
  面向引擎扩展或数据库后端的专用扩展区。

### `programming/bigworld/client`

客户端主程序与客户端脚本桥接层。这里的 Python 绑定也非常重，意味着脚本运行时变更不会只影响服务端。

### `programming/bigworld/tools`

离线工具和编辑器集合，包括：

- `worldeditor`
- `modeleditor`
- `particle_editor`
- `assetprocessor`
- `navgen`
- 各类导出器与编译工具

这些工具中大量存在嵌入式 Python 交互和 UI 脚本桥接。

### `programming/bigworld/third_party`

重依赖目录，包含：

- `python`
- `openssl`
- `curl`
- `mongodb`
- `sqlite`
- `jsoncpp`
- `re2`
- `recastnavigation`

这里反映出仓库长期采用“源码内置第三方”的供应策略。优点是可控，缺点是升级成本高。

## Python 相关模块分布

Python 能力不是集中在单一目录，而是散落在多个层面：

- 运行时库：`third_party/python`
- Python 桥接库：`lib/pyscript`、`lib/script`
- 各子系统适配：`common/py_*`、`client/script_bigworld.cpp`、`server/baseapp/script_bigworld.cpp`
- 工具集成：`tools/*` 中大量 `PyString_*`、`PyInt_*`、`PyRun_SimpleString`
- 共享模块与标准库安装：`build/make/third_party_python.mak`
- 附加 Python 包：`build/make/third_party_python_modules.mak`

这说明 Python 并不是外挂脚本，而是项目运行时模型的一部分。

## 示例与演示目录

### `programming/fantasydemo`

主要包含演示内容和 PHP Web 集成示例，不是引擎主线代码，但对理解生态整合方式有参考价值。

### `programming/bigworld/examples`

包含客户端集成示例，其中 `client_integration/python/simple` 是理解脚本侧实体定义与运行方式的较好切入点。
