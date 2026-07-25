# 实体模型：Base / Cell / Client

<div class="arch-hero">

BigWorld 的实体不是单对象模型，而是拆成 Base、Cell、Client 三个权威域。Base 承载会话、数据库和跨 Cell 协调；Cell 承载空间、位置、AOI、物理和 real/ghost；Client 只接收经过 Witness 和带宽调度过滤后的视图。

</div>

## 先给结论

BigWorld 的核心抽象是“同一个游戏实体在不同进程中有不同部分”：

- `Base`：长期在线身份、数据库 ID、Proxy/客户端连接、Cell mailbox、创建/销毁 Cell 实体、写 DB、备份归档。
- `Cell Entity`：空间内权威对象，维护位置、方向、Controller、AOI、real/ghost、事件历史和客户端同步。
- `Client Entity`：客户端可见投影，通过 Witness、EntityCache、Bundle 和 EntityDef 同步生成。

关键源码：

- `Base` 注释说明 Base entity 驻留在 BaseApp，可创建关联 Cell entity，并通过 `CellEntityMailBox` 与 Cell entity 通信，见 [base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:38)。
- `Base` 提供 `createCellEntity()`、`restoreTo()`、`cellBundle()`、`sendToCell()`、`writeToDB()`、`backupTo()`、`offload()` 等接口，见 [base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:97)。
- `Entity` 注释说明 Cell entity 可为 real 或 ghost，每个实体只有一个 authoritative real，可有 0 到多个 ghost，见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:89)。
- `Entity` 能访问 base 和 client mailbox，远程调用由 `.def` 文件指定，见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:127)。

## 三层职责

<div class="decision-grid">
  <div class="decision-card">
    <h3>Base</h3>
    <p>身份、会话、数据库、Proxy、全局逻辑、Cell 引用和跨 Cell 协调。</p>
  </div>
  <div class="decision-card">
    <h3>Cell</h3>
    <p>空间权威、位置、物理、AOI、real/ghost、Controller 和移动。</p>
  </div>
  <div class="decision-card">
    <h3>Client</h3>
    <p>只接收可见实体和属性投影，不拥有服务端权威状态。</p>
  </div>
</div>

这种分层不是 MVC，也不是普通 ECS。它是 MMO 分布式状态归属模型。

## Base 的职责

`Base` 继承 `PyObjectPlus`，是 Python 脚本可见对象。它保存：

- `EntityID`
- `DatabaseID`
- `EntityType`
- Cell mailbox
- 当前 `SpaceID`
- UDP channel
- timers
- backup/archive 策略
- destroy/create cell pending 状态

源码见 [base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:58)。

### Base 核心数据结构

```cpp
// base.hpp:58 - Base 类定义
class Base: public PyObjectPlus
{
public:
    Base( EntityID id, DatabaseID dbID, EntityTypePtr pType );

    EntityID id() const                    { return id_; }
    DatabaseID databaseID() const          { return databaseID_; }
    CellEntityMailBox * pCellEntityMailBox() const { return pCellEntityMailBox_; }
    SpaceID spaceID() const                { return spaceID_; }
    Mercury::UDPChannel & channel()        { return *pChannel_; }
    bool isProxy() const                   { return isProxy_; }
    bool isDestroyed() const               { return isDestroyed_; }

    // 关键状态标志
    bool isCreateCellPending() const       { return isCreateCellPending_; }
    bool isGetCellPending() const          { return isGetCellPending_; }
    bool isDestroyCellPending() const      { return isDestroyCellPending_; }
    bool hasBeenBackedUp() const           { return hasBeenBackedUp_; }
};
```

**设计要点：**
- `id_` 是运行时 EntityID，由 BaseApp 分配，用于进程内快速查找
- `databaseID_` 是持久化 ID，初始为 0，首次 `writeToDB()` 后由 DBApp 分配
- `pCellEntityMailBox_` 指向 Cell Entity，Base 通过它发送消息到 CellApp
- `pChannel_` 是到 CellApp 的 UDP 通道，承载所有 Base-Cell 通信
- `isProxy_` 标记是否为玩家代理（有客户端连接），普通 Base 没有客户端

### Base 重要方法

- `writeToDB()`：写数据库。
- `requestCellDBData()`：向 Cell 请求持久化数据。
- `backupTo()` / `writeBackupData()`：备份到其他 BaseApp。
- `offload()`：BaseApp 退休或负载迁移时迁出。
- `createCellEntity()`：创建对应 Cell 实体。
- `setCurrentCell()`：更新 Cell 实体所在 CellApp 地址。
- `callBaseMethod()` / `callCellMethod()`：跨域方法调用。

这说明 Base 是实体长期身份和跨域协调的中心。

## Cell Entity 的职责

`Entity` 也继承 `PyObjectPlus`，但它驻留在 CellApp，负责空间内权威逻辑。

源码注释列出关键语义：

- real / ghost。
- 位置和方向。
- Controller 驱动的位置变化。
- AOI。
- Client 可见性。
- Base mailbox 和 Client mailbox。

见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:89)。

### Entity 核心数据结构

```cpp
// entity.hpp:138 - Entity 类定义
class Entity : public PyObjectPlus
{
public:
    // 位置验证：防止 NaN 坐标
    static bool isValidPosition( const Position3D &c )
    {
        const float MAX_ENTITY_POS = 1000000000.f;
        return (-MAX_ENTITY_POS < c.x && c.x < MAX_ENTITY_POS &&
            -MAX_ENTITY_POS < c.y && c.y < MAX_ENTITY_POS &&
            -MAX_ENTITY_POS < c.z && c.z < MAX_ENTITY_POS);
    }

    // 实体初始化：real 和 ghost 走不同路径
    bool initReal( BinaryIStream & data, const ScriptDict & properties,
        bool isRestore,
        Mercury::ChannelVersion channelVersion,
        EntityPtr pNearbyEntity );

    void initGhost( BinaryIStream & data );

    // offload 迁移
    void offload( CellAppChannel * pChannel, bool isTeleport );
    void onload( const Mercury::Address & srcAddr,
        const Mercury::UnpackedMessageHeader & header,
        BinaryIStream & data );

    // ghost 创建
    void createGhost( Mercury::Bundle & bundle );
};
```

**设计要点：**
- `initReal()` 和 `initGhost()` 是两条完全不同的初始化路径
- real entity 从二进制流恢复完整状态（包括属性、控制器、AOI）
- ghost entity 只恢复位置和必要状态，不拥有权威逻辑
- `isValidPosition()` 防止 NaN 坐标污染空间系统
- offload/onload 是实体迁移的核心，real 变成 ghost，ghost 变成 real

### Cell Entity 保存和暴露

- `position()` / `direction()`。
- `volatileInfo()`。
- `isReal()` / `pReal()`。
- `realAddr()` / `nextRealAddr()`。
- `Space` / `Cell`。
- `EventHistory`。
- `writeClientUpdateDataToBundle()`。
- `addHistoryEventLocally()`。
- `offload()` / `onload()` / `createGhost()`。

见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:163)。

## Real 与 Ghost

每个 Cell Entity 有且只有一个 authoritative real，可能有多个 ghost。

ghost 的作用：

- 让邻近 CellApp 提前拥有实体副本。
- 支撑 AOI 跨 Cell 边界。
- 支撑 entity offload 前后消息不中断。
- 支撑 CellApp death 和恢复场景下的状态判断。

real 的职责：

- 拥有权威脚本逻辑。
- 写 real-only 属性。
- 驱动位置、Controller 和事件历史。
- 维护 haunts，也就是 ghost 所在 CellApp。

这就是 BigWorld 与普通分片服务器的差异：它不是简单把坐标落在哪个分片就只在哪个分片存在，而是在边界附近主动维护 ghost。

## Mailbox 是跨域引用

Base 可以通过 `CellEntityMailBox` 与 Cell entity 通信；Cell entity 可以通过 Base mailbox 和 Client mailbox 调用远端方法。

这和 [通信抽象与 RPC](/architecture/communication-rpc) 直接相关：

- Mailbox 不是对象指针。
- 它封装 EntityID、地址、组件归属和接口方法。
- 方法参数由 EntityDef 的 `MethodArgs` 序列化。
- 传输层由 Mercury Bundle / Channel 承载。

所以 BigWorld 的“对象方法调用”本质是分布式消息。

## 生命周期回调

Base 侧回调包括：

- `onDestroy`
- `onOnload`
- `onRestore`
- `onGetCell`
- `onLoseCell`
- `onCreateCellFailure`
- `onWriteToDB`
- `onPreArchive`
- `onTimer`

源码见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:447)。

Cell 侧回调包括：

- `onRestore`
- `onGhostCreated`
- `onGhostDestroyed`
- `onWriteToDB`
- `onNoise`
- `onSpaceGone`
- `onGetWitness`
- `onLoseWitness`
- `onWitnessed`
- `onEnteringCell`
- `onEnteredCell`
- `onLeavingCell`
- `onLeftCell`

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:733)。

这些回调是脚本层扩展点，但也是风险点：回调中可能销毁、迁移、offload 实体，所以 C++ 代码经常要防止对象生命周期变化。

## 跨域方法调用链

Base 与 Cell 之间的方法调用通过 Mailbox 和 Mercury 消息完成。这是 BigWorld 分布式对象模型的核心：一个实体的方法调用可能跨越进程边界。

### Cell -> Base 调用链

**概述：** Cell Entity 需要调用 Base 方法时（如通知 Base 写数据库、请求 Base 创建新 Cell），通过 `callBaseMethod()` 将调用序列化为 Mercury 消息，发送到 BaseApp。

**源码入口：** [entity.cpp:5255](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5255)

```cpp
// entity.cpp:5255 - Cell Entity 调用 Base 方法
bool Entity::callBaseMethod( int methodIndex, BinaryIStream & data,
    bool isForGhost )
{
    // 1. 查找方法定义
    const MethodDescription * pMethod = 
        pType_->description().base().internalMethod( methodIndex );
    
    // 2. 创建 Base Entity Mailbox
    BaseEntityMailBox mailbox( pType_, id_, baseAddr_ );
    
    // 3. 获取输出流并写入方法调用
    Mercury::Bundle & bundle = mailbox.startMessage( 
        BaseAppIntInterface::callBaseMethod );
    bundle << methodIndex;
    bundle.transfer( data, data.remainingLength() );
    
    return true;
}
```

**流程图：**

<MermaidDiagram title="Cell -> Base 方法调用流程">
sequenceDiagram
    participant Cell as Cell Entity
    participant Mailbox as BaseEntityMailBox
    participant Bundle as Mercury Bundle
    participant Channel as UDP Channel
    participant BaseApp as BaseApp
    participant Base as Base Entity

    Cell->>Mailbox: callBaseMethod(index, data)
    Mailbox->>Bundle: startMessage(callBaseMethod)
    Bundle->>Bundle: 写入 methodIndex
    Bundle->>Bundle: 写入参数数据
    Bundle->>Channel: 发送消息
    Channel->>BaseApp: UDP 包
    BaseApp->>Base: dispatch callBaseMethod
    Base->>Base: 执行 Python 方法
</MermaidDiagram>

**详细讲解：**

1. **方法索引查找**：`methodIndex` 是 EntityDef 中方法的内部索引，由 `.def` 文件编译时生成。Cell 和 Base 共享同一个 EntityDef，所以索引一致。

2. **Mailbox 封装**：`BaseEntityMailBox` 不是简单指针，而是封装了 `EntityID`、目标地址（BaseApp 地址）和实体类型。它知道如何路由消息。

3. **Bundle 序列化**：参数通过 `BinaryIStream` 直接转移到 Bundle，避免额外拷贝。Bundle 内部管理可靠/不可靠语义。

4. **异步语义**：这是单向消息，Cell 不等待 Base 回复。如果需要回复，Base 会通过 `callCellMethod()` 反向调用。

### Base -> Cell 调用链

**概述：** Base 需要调用 Cell 方法时（如通知 Cell 移动实体、创建子实体），通过 `callCellMethod()` 将调用序列化，发送到 CellApp。

**源码入口：** [base.cpp:1391](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1391)

```cpp
// base.cpp:1391 - Base 调用 Cell 方法
bool Base::callCellMethod( int methodIndex, BinaryIStream & data )
{
    // 1. 检查 Cell Mailbox 是否存在
    if (!pCellEntityMailBox_)
    {
        ERROR_MSG( "Base::callCellMethod: No cell mailbox\n" );
        return false;
    }
    
    // 2. 通过 Cell Mailbox 发送
    Mercury::Bundle & bundle = pCellEntityMailBox_->startMessage(
        CellAppIntInterface::callCellMethod );
    bundle << id_;
    bundle << methodIndex;
    bundle.transfer( data, data.remainingLength() );
    
    return true;
}
```

**流程图：**

<MermaidDiagram title="Base -> Cell 方法调用流程">
sequenceDiagram
    participant Base as Base Entity
    participant Mailbox as CellEntityMailBox
    participant Bundle as Mercury Bundle
    participant Channel as UDP Channel
    participant CellApp as CellApp
    participant Entity as Cell Entity

    Base->>Mailbox: callCellMethod(index, data)
    Mailbox->>Bundle: startMessage(callCellMethod)
    Bundle->>Bundle: 写入 EntityID
    Bundle->>Bundle: 写入 methodIndex
    Bundle->>Bundle: 写入参数数据
    Bundle->>Channel: 发送消息
    Channel->>CellApp: UDP 包
    CellApp->>Entity: dispatch callCellMethod
    Entity->>Entity: 执行 Python 方法
</MermaidDiagram>

**详细讲解：**

1. **Cell Mailbox 检查**：Base 必须有对应的 Cell Entity 才能调用 Cell 方法。如果 Cell 已销毁或未创建，`pCellEntityMailBox_` 为空。

2. **EntityID 传递**：CellApp 有多个实体，消息中需要携带 `EntityID` 让 CellApp 路由到正确的 Entity。

3. **地址路由**：`pCellEntityMailBox_` 知道目标 CellApp 的地址，这个地址可能因实体迁移而变化。Base 通过 `setCurrentCell()` 更新地址。

4. **失败处理**：如果 CellApp 已死或实体已迁移，消息会丢失。BigWorld 通过 CellApp 死亡检测和 Base 恢复机制处理这种情况。

### 属性同步到客户端

**概述：** Cell Entity 的属性变更需要同步到客户端。这不是实时全量同步，而是通过 Witness 和 AOI 过滤后，只发送可见且变更的属性。

**源码入口：** [entity.cpp:2475](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2475)

```cpp
// entity.cpp:2475 - Cell Entity 写客户端更新数据
void Entity::writeClientUpdateDataToBundle( Mercury::Bundle & bundle,
    bool isFullUpdate )
{
    // 1. 写入车辆变更（如果有）
    this->writeVehicleChangeToBundle( bundle );
    
    // 2. 遍历事件历史，写入属性变更
    EventHistory::iterator iter = eventHistory_.begin();
    while (iter != eventHistory_.end())
    {
        if (iter->isForClient())
        {
            iter->writeToBundle( bundle );
        }
        ++iter;
    }
    
    // 3. 写入 volatile 数据（位置、方向等高频数据）
    if (volatileInfo_.hasVolatile())
    {
        this->writeVolatileDetailedDataToBundle( bundle );
    }
}
```

**流程图：**

<MermaidDiagram title="Cell -> Client 属性同步流程">
sequenceDiagram
    participant Cell as Cell Entity
    participant History as EventHistory
    participant Bundle as Mercury Bundle
    participant Witness as Witness
    participant Cache as EntityCache
    participant Client as Client

    Cell->>History: 记录属性变更
    Cell->>Cell: writeClientUpdateDataToBundle()
    Cell->>Bundle: 写入车辆变更
    Cell->>Bundle: 写入事件历史
    Cell->>Bundle: 写入 volatile 数据
    Bundle->>Witness: 发送给 Witness
    Witness->>Cache: 更新 EntityCache
    Cache->>Client: 同步到客户端
</MermaidDiagram>

**详细讲解：**

1. **EventHistory 机制**：属性变更不是立即发送，而是记录到 `EventHistory`。每个事件标记是否需要同步到客户端、是否可靠。

2. **Volatile 数据**：位置、方向等高频数据使用 `volatileInfo_` 控制同步频率。不同实体类型可以有不同的同步策略。

3. **Witness 过滤**：只有在客户端 AOI 内的实体才会同步。Witness 管理每个客户端的可见实体集合。

4. **EntityCache 缓存**：客户端有 `EntityCache` 缓存已知实体，避免重复同步。只有属性变更才发送。

5. **带宽控制**：Bundle 内部有带宽限制，单个 tick 内发送的数据量有上限，防止网络拥塞。

## 实体恢复调用链

当 CellApp crash 后，Base 通过 `restoreTo()` 恢复 Cell Entity。这是 BigWorld 高可用性的核心机制：即使 CellApp 崩溃，玩家的 Base Entity 仍然存活，可以恢复到新的 CellApp。

**概述：** CellApp 崩溃时，CellAppMgr 检测到死亡，通知 BaseAppMgr，BaseAppMgr 再通知所有 BaseApp。每个 BaseApp 检查自己是否有 Entity 的 Cell 在死亡的 CellApp 上，如果有，就从备份数据恢复到新的 CellApp。

**源码入口：** [base.cpp:3810](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3810)

```cpp
// base.cpp:3810 - Base 恢复 Cell Entity
void Base::restoreTo( const Mercury::Address & addr,
    const Mercury::UnpackedMessageHeader & header,
    BinaryIStream & data )
{
    // 1. 读取备份数据
    this->readBackupData( data );
    
    // 2. 检查是否有 Cell Entity
    if (!this->hasCellEntity())
    {
        return;
    }
    
    // 3. 创建新的 Cell Entity
    this->createCellEntityOn( addr );
    
    // 4. 更新 Cell 地址
    this->setCurrentCell( spaceID_, addr );
}
```

**流程图：**

<MermaidDiagram title="CellApp 崩溃后实体恢复流程">
sequenceDiagram
    participant CellApp as CellApp (死亡)
    participant CellAppMgr as CellAppMgr
    participant BaseAppMgr as BaseAppMgr
    participant BaseApp as BaseApp
    participant Base as Base Entity
    participant NewCellApp as 新 CellApp

    CellApp->>CellAppMgr: 死亡检测 (心跳超时)
    CellAppMgr->>BaseAppMgr: 通知 CellApp 死亡
    BaseAppMgr->>BaseApp: 广播 CellApp 死亡
    BaseApp->>Base: 检查 Cell 是否在死亡 CellApp
    Base->>Base: 读取 cellBackupData_
    Base->>NewCellApp: createCellEntityOn()
    NewCellApp->>NewCellApp: 恢复 Cell Entity
    Base->>Base: setCurrentCell(新地址)
</MermaidDiagram>

**详细讲解：**

1. **死亡检测**：CellAppMgr 通过心跳机制检测 CellApp 死亡。如果 CellApp 超过一定时间没有心跳，CellAppMgr 标记它为死亡。

2. **备份数据**：Base 定期通过 `backupTo()` 将 Cell Entity 的状态备份到其他 BaseApp。备份数据存储在 `cellBackupData_` 中。

3. **恢复时机**：Base 收到 CellApp 死亡通知后，立即检查自己的 Cell 是否在死亡的 CellApp 上。如果是，就从备份数据恢复。

4. **新 CellApp 选择**：BaseApp 会选择一个存活的 CellApp 来恢复 Cell Entity。选择算法考虑负载、空间位置等因素。

5. **状态恢复**：备份数据包含 Entity 的属性、位置、AOI 状态等。恢复后，Entity 在新 CellApp 上继续运行，客户端可能感受到短暂卡顿。

**为什么这样设计：**

- **玩家不掉线**：Base Entity 始终在 BaseApp 上，即使 CellApp 崩溃，玩家连接不会断开。
- **快速恢复**：从备份数据恢复比从数据库加载快得多，减少玩家等待时间。
- **透明迁移**：对客户端来说，只是感觉到短暂卡顿，不需要重新登录。

## 为什么这样拆

这种拆分解决了 MMO 的几个问题：

- 玩家连接和空间模拟不一定在同一进程。
- 空间负载可以按 Cell 切分迁移。
- Base 长期存在，Cell 可随位置创建、销毁、迁移。
- 数据库写入由 Base 统一协调，必要时请求 Cell 数据。
- Client 只看 Witness 筛选后的投影，不需要知道完整服务端状态。

这是 BigWorld 的核心设计价值之一。

## 代价

代价同样明显：

- 一个实体的状态分散在 Base、Cell、Client、DB、Ghost 中。
- 生命周期复杂，创建/销毁/迁移都要跨进程协调。
- Mailbox 可能过期，Channel version 需要处理旧包。
- EntityDef 变更会影响多个域。
- 脚本回调可能改变迁移过程中的对象状态。
- 调试一个实体问题需要跨 BaseApp、CellApp、DBApp、客户端日志。

## 源码取舍

Base/Cell/Client 三层模型的源码取舍是把不同权威域拆开：

- Base 承担长期身份、数据库协调、Proxy 和跨 Cell 生命周期管理。
- Cell 承担空间、位置、AOI、物理和 real/ghost 状态。
- Client 只接收 Witness 筛选后的实体投影和自身可调用的 exposed 方法。
- DB 只持久化 EntityDef persistent 视图，不保存完整运行时对象图。

源码代价是一个实体问题通常跨多个域传播：Mailbox、Channel、EntityDef、DBID、Cell real/ghost、Client entity cache 都可能参与同一条故障链。

## 源码验证重点

实体模型测试应覆盖域边界：

- Base-only、Cell-only、Client-visible、Persistent 属性应按 EntityDef 数据域进入对应流。
- 创建 Cell entity 时，Base mailbox、Cell mailbox 和客户端 player 关系应一致。
- Cell 丢失、offload、restore 后，Base 持有的 Cell mailbox 不能指向旧地址。
- Client 方法只能通过 exposed method range 调用，不能绕过 Proxy 直连 CellApp。
- DB 写入应由 Base 协调，必要时请求 Cell persistent 数据。
- Ghost 只应作为跨 Cell 副本和迁移辅助，不能被误当成客户端实体连接。

## 本章边界

本章解释实体三层模型。下一章继续分析 AOI、Witness 与 Ghost，这是 Client 可见性和跨 Cell 边界一致性的核心。
