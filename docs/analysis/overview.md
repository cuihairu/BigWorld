# 总体画像

## 一句话判断

BigWorld 开源版是一个以 C++ 服务端、Mercury 网络协议、EntityDef 契约和嵌入式脚本层共同组成的大型 MMO 引擎源码仓库。它的核心不是某一种语言或工具链，而是多进程状态分片、实体生命周期、客户端同步和数据库持久化之间的闭环。

## 源码主线

从源码关系看，BigWorld 至少有六条主线：

| 主线 | 代表源码 | 要回答的问题 |
|---|---|---|
| 进程框架 | `programming/bigworld/lib/server/` | 每个服务进程如何启动、注册接口并进入事件循环 |
| Mercury 网络 | `programming/bigworld/lib/network/` | 消息如何被打包、发送、接收、分发和重试 |
| 实体契约 | `programming/bigworld/lib/entitydef/` | `.def` 如何决定属性域、方法、序列化顺序和 digest |
| 服务端状态 | `programming/bigworld/server/` | BaseApp、CellApp、DBApp、LoginApp 等进程如何协作 |
| 客户端连接 | `programming/bigworld/lib/connection/`, `programming/bigworld/client/` | 客户端如何登录、接管 Proxy、接收 AOI 和属性更新 |
| 持久化映射 | `programming/bigworld/lib/db_storage*`, `programming/bigworld/server/dbapp/` | EntityDef 属性如何映射到数据库并被写入 |

读源码时应优先把这六条线串起来，而不是按目录顺序逐个文件阅读。

## 运行闭环

最小服务端闭环可以这样理解：

```
BIGWORLD_MAIN
  -> bwMainT / doBWMainT
  -> ServerApp::runApp
  -> 具体 App::init
  -> Interface::registerWithInterface
  -> EventDispatcher::processOnce
  -> Mercury handler
  -> Entity / Manager / DB 状态变更
```

实体相关闭环更复杂：

```
entities.xml / .def
  -> EntityDescriptionMap
  -> Base / Cell / Client / Persistent 数据域
  -> Bundle / MethodDescription / DataDescription 序列化
  -> BaseApp / CellApp / Proxy / DBApp
  -> AOI、Ghost、Offload、writeToDB
```

这解释了 BigWorld 源码的主要难点：一个 `.def` 属性可能同时影响客户端可见性、Cell ghost 同步、Base 备份、DB 持久化、热更新和迁移流。

## 仓库层级

从根目录观察，主线源码集中在以下区域：

| 目录 | 作用 |
|---|---|
| `programming/bigworld` | BigWorld 主体源码，包含客户端、服务端、工具、库、第三方依赖和构建系统 |
| `programming/fantasydemo` | 示例内容与演示资源，可辅助理解业务层如何使用引擎 |
| `docs/pdf` | 原始文档资料，可作为背景材料，但源码结论应以当前仓库为准 |

`programming/bigworld/lib` 是共享机制层，`programming/bigworld/server` 是 MMO 服务进程层。前者定义“消息、类型、资源、脚本、DB 抽象”，后者把这些机制组装成具体运行时。

## 项目规模特征

基于目录扫描可得到以下事实：

| 区域 | 一级目录数量 | 含义 |
|---|---:|---|
| `programming/bigworld/lib` | 57 | 共享库数量多，跨模块依赖明显 |
| `programming/bigworld/server` | 12 | 服务端进程与扩展目录并存 |
| `programming/bigworld/server/tools` | 10 | 运维、日志、数据处理工具是服务端生态的一部分 |
| `programming/bigworld/third_party` | 24 | 仓库采用内置第三方依赖策略 |

这些数字只能说明规模，不能直接说明架构。架构判断必须回到源码调用链：接口在哪里注册、消息在哪里发出、handler 修改了什么状态、状态最终是否进入客户端或数据库。

## 架构分层

按职责可把源码分成五层：

| 层 | 内容 | 关键边界 |
|---|---|---|
| 基础设施层 | `cstdmf`, `network`, `resmgr`, `math` | 时间、内存、资源、网络基础能力 |
| 契约与脚本层 | `entitydef`, `script`, `pyscript`, `entitydef_script` | `.def`、DataType、脚本对象和 C++ 边界 |
| 服务端进程层 | `baseapp`, `cellapp`, `dbapp`, `loginapp`, managers | 进程职责、RPC、状态分片和故障恢复 |
| 客户端与工具层 | `client`, `tools`, editor libraries | 客户端协议、编辑器、资源生产链 |
| 持久化与运维层 | `db_storage*`, `server/tools`, Watcher | DB 映射、后台任务、观测与控制 |

这五层不是严格单向依赖。比如 EntityDef 同时被网络、脚本、DB 和客户端使用；Watcher 同时用于观测和部分控制；Base/Cell 实体状态跨进程流动。

## 当前文档目标

本站的源码分析目标是：

- 把 BigWorld 的多进程职责和消息链讲清楚。
- 把 EntityDef 如何影响网络、DB、客户端和迁移讲清楚。
- 把 Base、Cell、Client、Proxy、Witness、Ghost 的状态边界讲清楚。
- 把构建、运行、测试、Watcher 和日志作为源码验证入口，而不是只作为使用说明。

后续阅读建议从 [源码导读](/analysis/source-code-guide) 开始，再进入 [架构研究方法](/architecture/research-method) 和各专题章节。
