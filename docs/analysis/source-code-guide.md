# 源码导读

<div class="arch-hero">

本章给出 BigWorld 源码的阅读地图。重点不是罗列目录，而是把进程入口、消息注册、EntityDef、数据库、客户端通信和 Cell 迁移串成可验证的源码链。

</div>

## 阅读边界

源码主目录是 `programming/bigworld/`：

```
programming/bigworld/
├── server/           # BaseApp、CellApp、DBApp、LoginApp 等服务端进程
├── lib/              # Mercury、EntityDef、DB、Script、Watcher 等共享库
├── client/           # 客户端运行时
├── tools/            # 编辑器、运维与离线工具
├── common/           # 共享定义
├── build/            # 构建脚本
└── third_party/      # 第三方依赖
```

不要从完整扫描 `lib/` 或 `third_party/` 开始。BigWorld 的关键关系不是目录树关系，而是“接口注册 -> 网络消息 -> handler -> 实体/数据库状态变更”的运行链。

建议先读这些稳定锚点：

| 主题 | 先读文件 | 关键函数或类型 |
|---|---|---|
| 进程入口 | `lib/server/bwservice.hpp` | `BIGWORLD_MAIN`, `bwMainT`, `doBWMainT` |
| 主循环 | `lib/server/server_app.cpp`, `lib/network/event_dispatcher.cpp` | `ServerApp::runApp`, `ServerApp::run`, `ServerApp::advanceTime`, `EventDispatcher::processOnce` |
| 服务初始化 | `server/baseapp/baseapp.cpp`, `server/cellapp/cellapp.cpp`, `server/dbapp/dbapp.cpp`, `server/loginapp/loginapp.cpp` | `BaseApp::init`, `CellApp::init`, `DBApp::init`, `LoginApp::init` |
| 网络接口 | `lib/network/interface_macros.hpp`, `lib/network/network_interface.hpp` | `BEGIN_MERCURY_INTERFACE`, `registerWithInterface`, `NetworkInterface` |
| 实体定义 | `lib/entitydef/entity_description_map.cpp` | `EntityDescriptionMap::parse`, `parseInternal`, `parseServices` |
| 持久化 | `server/baseapp/base.cpp`, `server/dbapp/dbapp.cpp`, `lib/db_storage_mysql/` | `Base::writeToDB`, `DBApp::writeEntity`, `IDatabase::putEntity`, `PropertyMapping::create` |

## 进程启动链

服务端目录下存在 `main.cpp`，但多数逻辑不是写在 `main()` 里，而是通过 `BIGWORLD_MAIN` 宏接入统一服务框架。

真实启动链可以按下面顺序读：

1. `server/<app>/main.cpp` 使用 `BIGWORLD_MAIN` 调用 `bwMainT<App>()`。
2. `lib/server/bwservice.hpp` 中 `BIGWORLD_MAIN` 展开真实 `main()`，初始化 `BWResource`、`BWConfig` 和命令行参数。
3. `bwMainT` 创建 `Mercury::EventDispatcher` 和 `Mercury::NetworkInterface`。
4. `doBWMainT` 初始化 `ServerAppConfig`，构造具体 `SERVER_APP`，然后调用 `serverApp.runApp()`。
5. `ServerApp::runApp()` 先调用派生类 `init()`，成功后进入 `ServerApp::run()`。
6. `ServerApp::run()` 调用 `mainDispatcher_.processUntilBreak()`，事件循环再进入 `EventDispatcher::processOnce()`。

这条链解释了为什么不能只看各进程目录下的 `main.cpp`：`main.cpp` 只是进程类型选择点，真正的初始化、定时器、网络事件分发在共享服务框架中完成。

## 服务注册链

Mercury 接口不是运行时反射式自动发现，而是由接口定义宏生成 message 描述，再在进程初始化时注册到 `NetworkInterface`。

| 进程 | 注册位置 | 接口 |
|---|---|---|
| BaseApp | `server/baseapp/baseapp.cpp` | `BaseAppIntInterface`, `BaseAppExtInterface` |
| CellApp | `server/cellapp/cellapp.cpp` | `CellAppInterface` |
| DBApp | `server/dbapp/dbapp.cpp` | `DBAppInterface` |
| LoginApp | `server/loginapp/loginapp.cpp` | `LoginInterface`, `LoginIntInterface` |
| BaseAppMgr | `server/baseappmgr/baseappmgr.cpp` | `BaseAppMgrInterface` |
| CellAppMgr | `server/cellappmgr/cellappmgr.cpp` | `CellAppMgrInterface` |
| DBAppMgr | `server/dbappmgr/dbappmgr.cpp` | `DBAppMgrInterface` |
| Reviver | `server/reviver/reviver.cpp` | `ReviverInterface` |

验证方式：先在接口头文件中查 `BEGIN_MERCURY_INTERFACE`，再查对应进程的 `registerWithInterface()`，最后顺着 `bundle.startMessage()` 或 `bundle.startRequest()` 找调用方。

## 主循环与 Tick

主循环由 `EventDispatcher` 驱动，游戏时间推进由派生进程在 timer handler 中触发。

核心链路：

```
ServerApp::run()
  -> EventDispatcher::processUntilBreak()
  -> EventDispatcher::processOnce()
  -> processFrequentTasks()
  -> processTimers()
  -> processNetwork()
```

BaseApp 的游戏 tick 入口是 `BaseApp::tickGameTime()`；CellApp 的游戏 tick 入口是 `CellApp::handleGameTickTimeSlice()`。它们都会在各自逻辑中调用或依赖 `ServerApp::advanceTime()`，再触发 `onEndOfTick()`、`onStartOfTick()`、`callUpdatables()` 等统一阶段。

阅读时要分清两层：

| 层次 | 代码 | 作用 |
|---|---|---|
| 事件分发 | `EventDispatcher::processOnce()` | 网络、timer、统计、频繁任务 |
| 游戏时间 | `ServerApp::advanceTime()` | 推进 `GameTime` 并运行 tick 生命周期 |
| 进程逻辑 | `BaseApp::tickGameTime()`, `CellApp::handleGameTickTimeSlice()` | Base/Cell 各自的备份、负载、AOI、迁移等逻辑 |

## 网络消息链

Mercury 的阅读顺序建议从“消息如何发出”和“消息如何落到 handler”两端夹击：

```
发送侧:
InterfaceElement
  -> Bundle::startMessage/startRequest
  -> Channel / UDPChannel
  -> PacketSender

接收侧:
PacketReceiver
  -> NetworkInterface
  -> UDPBundleProcessor
  -> InterfaceElement handler
  -> 具体 App / Entity 方法
```

关键文件：

| 文件 | 关注点 |
|---|---|
| `lib/network/interface_macros.hpp` | 接口宏如何生成 `Args::start()`、`startRequest()`、`registerWithInterface()` |
| `lib/network/bundle.hpp` | 消息如何写入 bundle |
| `lib/network/udp_bundle.cpp` | UDP bundle 如何开始 message/request |
| `lib/network/network_interface.hpp` | 网络接口持有 channel、sender、receiver、请求管理器 |
| `lib/network/packet_receiver.hpp` | socket 输入如何进入 Mercury |
| `lib/network/channel.hpp`, `lib/network/udp_channel.hpp` | 可靠、有序、重传和 indexed channel 的核心状态 |

追一条 RPC 不要先猜协议格式，应先 `rg "startRequest( SomeInterface::someMessage"` 找发送方，再到接口定义中看 message 类型，最后到 handler 所属类看状态变更。

## EntityDef 链

`.def` 文件是 BigWorld 的实体契约入口，不只是脚本声明。它决定属性域、方法暴露、序列化顺序、协议 digest 和数据库映射基础。

阅读链：

```
scripts/entities.xml
  -> scripts/entity_defs/<Entity>.def
  -> EntityDescriptionMap::parse()
  -> EntityDescriptionMap::parseInternal()
  -> EntityDescription
  -> DataDescription / MethodDescription
  -> DataType::addToStream/createFromStream
```

重点字段含义：

| `.def` 标记 | 源码承载 | 影响 |
|---|---|---|
| `Persistent` | `DataDescription::isPersistent()` | 是否进入持久化数据域 |
| `Identifier` | `EntityDescription::pIdentifier()` | 是否可用业务标识查 DBID |
| `Indexed` | `DataDescription::isIndexed()` | MySQL mapping 是否创建索引型列 |
| `DatabaseLength` | `DataDescription::databaseLength()` | 字符串、blob、用户类型等数据库长度限制 |
| Client/Base/Cell 域 | `EntityDescription` data domains | 决定属性在 base、cell、client 间如何序列化 |

验证 `.def` 影响时，不要只看 XML。需要同时查：

| 验证点 | 源码 |
|---|---|
| 属性是否持久化 | `DataDescription::parse()` 读取字段，`DataDescription::isPersistent()` 返回判断 |
| 只写持久化属性 | `EntityDescription::ONLY_PERSISTENT_DATA`, `Base::addToStream()` |
| 协议一致性 | `EntityDescriptionMap::addPersistentPropertiesToMD5()` |
| 数据库字段映射 | `PropertyMappingsPerType`, `PropertyMapping::create()` |
| 方法参数序列化 | `MethodDescription::addToStream()`, `MethodDescription::createFromStream()` |

详见 [序列化与 EntityDef](/architecture/serialization-entitydef) 和 [EntityDef 契约与协议生成](/architecture/entitydef-contract-generation)。

## 属性变更与“脏数据”

BigWorld 里“属性变更”有两条不同用途的链，不能混成一种 ORM 式脏字段追踪。

第一条是运行时同步链：

```
属性被脚本或容器修改
  -> PropertyOwner / PropertyChange
  -> Entity::onOwnedPropertyChanged()
  -> HistoryEvent / ghostedDataUpdate / Witness::sendToClient()
```

这条链用于把变化推给客户端、ghost 或 replay。它关心的是网络可见性、AOI、嵌套属性变更和消息大小，不等价于“这个字段稍后要写数据库”。

第二条是显式持久化链：

```
Base.writeToDB()
  -> Base::writeToDB()
  -> Base::addToStream()
  -> EntityDescription::ONLY_PERSISTENT_DATA
  -> DBAppInterface::writeEntity
  -> DBApp::writeEntity()
  -> WriteEntityHandler
  -> IDatabase::putEntity()
```

这条链由 `writeToDB` 触发，按照 `.def` 的 `Persistent` 域筛选 base/cell 数据并序列化。源码重点在 `Base::writeToDB()`、`Base::requestCellDBData()`、`Base::addToStream()` 和 `DBApp::writeEntity()`。

因此，读源码时应把“脏数据”理解为两类事实：

| 场景 | 代表源码 | 语义 |
|---|---|---|
| 属性变了，需要同步 | `PropertyChange`, `Entity::onOwnedPropertyChanged()` | 增量网络事件 |
| 需要落库 | `Base::writeToDB()`, `WRITE_BASE_CELL_DATA`, `ONLY_PERSISTENT_DATA` | 显式持久化快照 |

## 数据库映射链

BigWorld 的数据库层是“EntityDef 驱动的持久化映射”，不是典型 ORM。

落库链路：

```
Base::writeToDB(flags)
  -> 如果有 Cell 数据，先向 CellAppInterface::writeToDBRequest 要 cell 快照
  -> Base::addToStream(flags, stream, pCellData)
  -> DBAppInterface::writeEntity
  -> DBApp::writeEntity()
  -> WriteEntityHandler::writeEntity()
  -> DBApp::putEntity()
  -> IDatabase::putEntity()
  -> MySqlDatabase::putEntity() / XMLDatabase::putEntity()
```

映射链路：

```
EntityDescriptionMap
  -> EntityDescription
  -> DataDescription
  -> PropertyMappingsPerType
  -> PropertyMapping::create()
  -> PrimitiveMapping / StringLikeMapping / SequenceMapping / FixedDictMapping / UserTypeMapping ...
```

与 ORM 的区别：

| 维度 | BigWorld 源码 | 典型 ORM |
|---|---|---|
| 模型来源 | `.def` + `EntityDescription` | 语言对象 class / annotation / schema |
| 保存触发 | 显式 `writeToDB` 和登录/登出/备份流程 | Unit of Work、事务提交或对象状态 flush |
| 字段选择 | `Persistent` 数据域 + `ONLY_PERSISTENT_DATA` | mapped columns 或 tracked fields |
| 关系表达 | mailbox、DBID、Identifier、Base/Cell 数据流 | object relation、foreign key、lazy loading |
| 运行目标 | 分布式 MMO 实体状态落库 | 应用对象到关系表映射 |

读数据库相关源码时，先确认 `.def` 中属性是否 `Persistent`，再看 `PropertyMapping::create()` 如何根据 `DataType` 选择具体 mapping。不要从 SQL 表结构反推实体模型，否则会漏掉 Base/Cell 分区、Identifier 缓存、secondary DB 和 auto-load 标记。

详见 [持久化与 DB 线程模型](/architecture/persistence-db-model)。

## 客户端通信链

BigWorld 里的“前端通信”主要指客户端运行时与 LoginApp/BaseApp/CellApp 的协议链。入口不在 HTTP API，而在 Mercury interface 和 `ServerConnection`。

登录和接管链：

```
客户端 LoginInterface::login
  -> LoginApp::login()
  -> DBAppInterface::logOn
  -> BaseAppMgr / BaseApp 创建或恢复 Base
  -> BaseAppExtInterface::baseAppLogin
  -> Proxy 接管客户端 channel
```

运行时上下行链：

| 方向 | 接口 | 代表源码 |
|---|---|---|
| 客户端到 LoginApp | `LoginInterface` | `lib/connection/login_interface.hpp`, `server/loginapp/loginapp.cpp` |
| 客户端到 BaseApp | `BaseAppExtInterface` | `lib/connection/baseapp_ext_interface.hpp`, `server/baseapp/baseapp.cpp`, `server/baseapp/proxy.cpp` |
| 服务端到客户端 | `ClientInterface` | `lib/connection/client_interface.hpp`, `server/baseapp/proxy.cpp`, `server/cellapp/entity.cpp` |
| 客户端本地协议处理 | `ServerConnection` | `lib/connection/server_connection.cpp` |

`Proxy` 是客户端连接在服务端的代表：它负责认证后的 channel、切换 BaseApp、向客户端发送 `ClientInterface` 消息，并把客户端输入转给 Base/Cell 实体方法。`ServerConnection` 则是客户端侧对这些协议的封装。

详见 [登录、会话与 Proxy 接管](/architecture/login-session-proxy-flow) 和 [通信抽象与 RPC](/architecture/communication-rpc)。

## Actor 相似点与边界

BigWorld 的实体通信有 actor-like 特征，但不是通用 Actor runtime。

相似点：

| 相似点 | 源码表现 |
|---|---|
| 地址化通信 | mailbox、EntityID、Mercury address |
| 消息驱动 | `bundle.startMessage()` / `startRequest()` |
| 单实体封装状态 | Base/Cell 实体方法围绕实体实例执行 |
| 分布式位置透明的一部分 | Base mailbox、Cell mailbox、Proxy 转发 |

边界：

| 差异 | BigWorld 源码表现 |
|---|---|
| 没有通用 mailbox runtime | mailbox 是 MMO 实体和 Mercury 协议封装，不是统一 actor 调度器 |
| 调度核心不是 actor scheduler | 主循环是 `EventDispatcher` + timer +网络 handler |
| 状态被 EntityDef 分域 | Base/Cell/Client/Persistent 域决定数据流向 |
| 位置迁移是 Cell/App 机制 | Offload、Ghost、Witness 由空间分区和 AOI 驱动 |

所以可以把 BigWorld 理解为“面向 MMO 实体的消息化分布式对象系统”，但源码分析时不能直接套 Actor 模型解释所有行为。

## AOI、Ghost 与 Offload

Cell 侧源码要按空间管理读，不要只从实体类本身读。

核心链：

```
CellApp::handleGameTickTimeSlice()
  -> CellApp::updateBoundary()
  -> Cell / OffloadChecker
  -> EntityGhostMaintainer::check()
  -> Entity::createGhost()
  -> Entity::offload()
  -> RealEntity::writeOffloadData()
  -> Space::createGhost()
  -> RealEntity::readOffloadData()
```

AOI 与客户端可见性链：

```
Witness::update()
  -> EntityCache / HistoryEvent
  -> ClientInterface::enterAoI / leaveAoI / property update / method call
  -> ServerConnection handler
```

Offload 的关键不是“移动对象内存”，而是把 real entity 的必要状态序列化到目标 Cell，同时在原 Cell 和相邻 Cell 上维护 ghost、haunt、next real address 等顺序约束。相关源码集中在 `server/cellapp/entity.cpp`、`real_entity.cpp`、`entity_ghost_maintainer.cpp`、`offload_checker.cpp`、`witness.cpp`。

详见 [AOI、Witness 与 Ghost](/architecture/aoi-witness-ghost)、[Cell 分区与负载均衡](/architecture/cell-partition-load-balance) 和 [实体迁移与 Offload](/architecture/entity-migration-offload)。

## 观测与测试入口

源码验证不要只靠静态阅读。BigWorld 自带 Watcher、Profiler、单元测试和网络测试入口。

| 目标 | 源码入口 |
|---|---|
| 运行时观测 | `lib/network/watcher_nub.hpp`, `lib/server/watcher_forwarding_collector.cpp` |
| 服务指标 | 各 App 的 `addWatchers()` |
| Tick 性能 | `ServerApp::advanceTime()`, `AUTO_SCOPED_PROFILE` |
| 网络行为测试 | `lib/network/unit_test/` |
| EntityDef 序列化测试 | `lib/entitydef/unit_test/` |
| Cell ghost/offload 测试 | `server/cellapp/unit_test/` |

找测试时优先 `rg "TEST"` 和 `rg "BW_ADD_TEST"`。找运行时观测时优先 `rg "addWatchers"`，再看 Watcher path 如何挂到根节点。

## 交叉验证方法

读 BigWorld 源码时，建议固定使用这些检索入口：

| 要验证的问题 | 检索词 |
|---|---|
| 某接口是否真的注册 | `registerWithInterface(` |
| 某消息是谁发的 | `startMessage( Interface::message` |
| 某 RPC 是否等回复 | `startRequest( Interface::message` |
| 某属性怎么序列化 | `addToStream(`, `createFromStream(` |
| 某字段是否只持久化 | `ONLY_PERSISTENT_DATA`, `isPersistent()` |
| Cell 数据是否参与落库 | `WRITE_BASE_CELL_DATA`, `writeToDBRequest` |
| 客户端协议在哪里处理 | `ClientInterface::`, `BaseAppExtInterface::`, `ServerConnection::` |
| Offload 顺序如何保证 | `ghostSetNextReal`, `writeOffloadData`, `readOffloadData` |
| handler 是否允许回调 | `callbacksPermitted` |

源码分析结论至少要能同时回答三个问题：消息或数据从哪里来、被哪个 handler 接住、最终改变了哪个实体或数据库状态。

## 下一步

- 阅读 [架构研究方法](/architecture/research-method)，统一证据分级和源码引用口径。
- 阅读 [进程拓扑与职责切分](/architecture/process-topology)，把 App 角色和进程关系先固定下来。
- 阅读 [主循环、Tick 与事件分发](/architecture/event-loop)，再回到本章按调用链查源码。
- 阅读 [持久化与 DB 线程模型](/architecture/persistence-db-model)，补齐 `.def`、DBApp、MySQL mapping 的落库路径。
