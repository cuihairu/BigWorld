# 源码风险边界

## 风险不是单点问题

BigWorld 的风险主要来自跨进程状态、协议契约和脚本/C++ 边界叠加。源码分析时不要把风险简化成“某个库旧”或“某个目录复杂”，而要追踪具体闭环是否能被验证。

## 高风险一：接口宏隐藏真实调用边界

Mercury interface 通过宏定义消息，再由 `registerWithInterface()` 注册到 `NetworkInterface`。这让调用边界不如普通函数调用直观。

风险表现：

| 表现 | 需要验证的源码 |
|---|---|
| 只看到 handler，看不到发送方 | `bundle.startMessage()` / `bundle.startRequest()` |
| 只看到接口定义，看不到注册进程 | `registerWithInterface()` |
| 不清楚是否等待回复 | `startRequest()` 和 reply handler |
| 不清楚消息可靠性 | interface 宏、Bundle 和 Channel 参数 |

处理方式：每个跨进程结论都必须同时引用接口定义、注册点和至少一个发送点。

## 高风险二：EntityDef 同时影响多条链

`.def` 不是单纯实体配置。它会影响：

| 影响面 | 代表源码 |
|---|---|
| 属性域 | `DataDescription`, `EntityDescription` |
| 方法参数 | `MethodDescription`, `MethodArgs` |
| 网络序列化 | `addToStream()`, `createFromStream()` |
| 客户端同步 | `ClientInterface::entityProperty`, `nestedEntityProperty`, `sliceEntityProperty` |
| DB 持久化 | `ONLY_PERSISTENT_DATA`, `PropertyMapping::create()` |
| digest 一致性 | `EntityDescriptionMap::addPersistentPropertiesToMD5()` |

风险表现是字段改动看似局部，实际可能破坏客户端协议、DB schema、实体迁移流或热更新路径。

## 高风险三：属性同步和落库不是同一机制

属性变化链和数据库写入链不同：

| 链路 | 代表源码 | 风险 |
|---|---|---|
| 运行时同步 | `PropertyChange`, `Entity::onOwnedPropertyChanged()` | 误以为发送给客户端就等于已持久化 |
| 显式落库 | `Base::writeToDB()`, `DBApp::writeEntity()`, `IDatabase::putEntity()` | 误以为属性变化会自动 flush 到 DB |

如果把 BigWorld 当成 ORM，会误判保存语义。数据库写入依赖 `writeToDB`、`Persistent` 数据域、Base/Cell 数据快照和 DBApp 后端任务，而不是对象字段自动脏检查。

## 高风险四：Cell 迁移依赖消息顺序和状态转换

Offload/Ghost/Witness 不是普通对象迁移。

关键源码：

| 源码 | 风险点 |
|---|---|
| `EntityGhostMaintainer::check()` | 判断是否创建 ghost 或 offload |
| `Entity::offload()` | real entity 转移入口 |
| `RealEntity::writeOffloadData()` | 迁移流写出顺序 |
| `Space::createGhost()` | 目标 Cell 创建 ghost/real 的入口之一 |
| `Entity::ghostSetNextReal()` | real 切换期间的消息顺序约束 |
| `Witness::writeOffloadData()` | 客户端视野状态迁移 |

风险表现是局部改动可能破坏 real/ghost 生命周期、buffered ghost message、AOI 连续性或客户端位置同步。

## 高风险五：主循环是同步 handler 驱动

`EventDispatcher::processOnce()` 在同一事件循环中处理 timer、网络和 frequent task。具体 App 的 handler 直接在这条链上执行。

风险表现：

| 风险 | 代表源码 |
|---|---|
| handler 耗时影响 tick | `EventDispatcher::processOnce()`, `ServerApp::advanceTime()` |
| Base/Cell tick 逻辑与网络回调交织 | `BaseApp::tickGameTime()`, `CellApp::handleGameTickTimeSlice()` |
| 脚本回调可能改变对象生命周期 | `callbacksPermitted(false/true)`, entity migration code |
| 后台任务完成仍需回主循环收尾 | DBApp、MySQL task、background task manager |

阅读源码时要确认代码是在主线程事件循环、timer 回调、网络 handler 还是后台任务中执行。

## 高风险六：Watcher 既是观测也是控制面

Watcher 不只是指标导出。部分 Watcher 支持写操作，Manager 还会通过 forwarding watcher 汇聚多个进程的状态。

关键源码：

| 源码 | 关注点 |
|---|---|
| `lib/network/watcher_nub.hpp` | Watcher 网络入口 |
| `lib/server/watcher_forwarding_collector.cpp` | Watcher 跨进程汇聚 |
| 各 App 的 `addWatchers()` | 暴露哪些运行时状态和控制项 |

风险表现是把 Watcher 当成只读监控后，忽视它对运行时状态和管理面的影响。涉及 Watcher 的文档结论必须说明读写边界。

## 高风险七：构建配置和源码树可能漂移

构建脚本、工程文件、PDF 文档和当前源码树不一定完全一致。

验证点：

| 问题 | 检查方式 |
|---|---|
| 构建脚本引用路径是否存在 | `rg` 配置中的脚本名和目标名 |
| CMake/Make 是否覆盖同一目标 | 对比 `CMakeLists.txt` 与 `Makefile.rules` |
| 文档描述是否仍对应源码 | 用函数名、接口名、文件路径回查 |
| 测试目标是否可构建 | 查 `BW_ADD_TEST` 和对应源码 |

如果只能从旧文档看到结论，但当前源码找不到对应文件，应标记为文档漂移，不应直接写成源码事实。

## 风险验证清单

每个风险结论至少回答这些问题：

| 问题 | 检索入口 |
|---|---|
| 消息是谁定义的 | `BEGIN_MERCURY_INTERFACE` |
| 接口在哪里注册 | `registerWithInterface(` |
| 谁发送消息 | `startMessage(`, `startRequest(` |
| 数据如何编码 | `addToStream(`, `createFromStream(` |
| 状态在哪里改变 | 具体 App、Entity、Manager、DB handler |
| 是否有测试覆盖 | `BW_ADD_TEST`, `TEST(` |
| 是否有运行时观测 | `addWatchers()` |

这份清单比泛泛描述“技术债”更有用，因为它能把风险落到可复查的源码证据链上。
