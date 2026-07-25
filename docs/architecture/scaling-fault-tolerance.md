# 动态扩展与容灾

<div class="arch-hero">

BigWorld 的扩展不是“拉起无状态副本”，而是 MMO 状态分片下的运行时接纳、负载均衡、Cell/Entity 迁移和进程级恢复。理解它必须区分资源调度、状态迁移和故障恢复三个层面。

</div>

## 先给结论

BigWorld 支持运行时加入新的 `BaseApp` / `CellApp`，也有 `Reviver + machined` 的进程级恢复机制。但它不是自动云原生弹性系统。

关键源码：

- `CellAppMgr` 初始化时注册 load balance、meta load balance、overload check timer，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:190)。
- `CellAppMgr::addApp()` 是接纳新 CellApp 的入口，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1245)。
- 新 CellApp 需要 BaseApp 和 Alpha DBApp 已知，否则暂不接纳，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1252)。
- `CellAppMgr::startTimer()` 启动 GameTick 并创建 `TimeKeeper`，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1774)。
- `CellApps::sendGameTime()` 向所有 CellApp 发送当前游戏时间，见 [cellapps.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellapps.cpp:540)。
- `ComponentReviver` 注册 birth/death listener 并定期 ping 组件，见 [component_reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/component_reviver.cpp:62)。
- `Reviver::revive()` 通过 `CreateMessage` 请求 machined 拉起进程，见 [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:440)。

## 三层扩展模型

BigWorld 的动态扩展可以分成三层：

<div class="decision-grid">
  <div class="decision-card">
    <h3>资源层</h3>
    <p>外部启动进程，进程向 machined 和 Manager 注册。</p>
  </div>
  <div class="decision-card">
    <h3>控制层</h3>
    <p>BaseAppMgr/CellAppMgr 接纳进程，分配 ID、同步共享数据、广播时间。</p>
  </div>
  <div class="decision-card">
    <h3>状态层</h3>
    <p>Cell 负载均衡、实体 offload、Ghost 维护、Base 迁移和恢复。</p>
  </div>
</div>

现代云平台主要解决资源层；BigWorld 的难点主要在状态层。

## CellApp 接纳流程

**概述：** 新 CellApp 启动后，需要向 CellAppMgr 注册。CellAppMgr 检查前置条件（BaseApp、DBApp 已知），如果满足，分配 ID、同步数据、接纳新 CellApp。

**源码入口：** [cellappmgr.cpp:1245](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1245)

```cpp
// cellappmgr.cpp:1245 - CellAppMgr 接纳新 CellApp
bool CellAppMgr::addApp( const Mercury::Address & addr,
    const Mercury::UnpackedMessageHeader & header,
    BinaryIStream & data )
{
    // 1. 检查前置条件
    if (baseAppAddr_.isNone())
    {
        ERROR_MSG( "CellAppMgr::addApp: No BaseApp known yet\n" );
        return false;
    }
    
    if (dbAlphaAddr_.isNone())
    {
        ERROR_MSG( "CellAppMgr::addApp: No DBApp Alpha known yet\n" );
        return false;
    }
    
    if (!allowNewCellApps_)
    {
        ERROR_MSG( "CellAppMgr::addApp: New cell apps not allowed\n" );
        return false;
    }
    
    if (isRecovery_)
    {
        ERROR_MSG( "CellAppMgr::addApp: Currently in recovery\n" );
        return false;
    }
    
    // 2. 分配 ID 和初始化数据
    CellAppInitData initData;
    initData.id = ++lastCellAppID_;
    initData.time = this->time();
    initData.baseAppAddr = baseAppAddr_;
    initData.dbAlphaAddr = dbAlphaAddr_;
    initData.isReady = true;
    initData.timeoutPeriod = Config::timeoutPeriod();
    
    // 3. 添加到 CellApp 集合
    CellApp * pApp = new CellApp( addr, initData.id );
    cellApps_.add( pApp );
    
    // 4. 放入 pending 列表
    pendingApps_.push_back( pApp );
    
    // 5. 回复初始化数据
    header.reply( initData );
    
    // 6. 下发共享数据
    this->sendSharedData( addr );
    this->sendGlobalData( addr );
    
    return true;
}
```

**流程图：**

<MermaidDiagram title="CellApp 接纳流程">
sequenceDiagram
    participant CellApp as 新 CellApp
    participant CellAppMgr as CellAppMgr
    participant BaseApp as BaseApp
    participant DBApp as DBApp Alpha

    CellApp->>CellAppMgr: addApp request
    
    alt 前置条件不满足
        CellAppMgr->>CellApp: 拒绝
    else 前置条件满足
        CellAppMgr->>CellAppMgr: 分配 ID
        CellAppMgr->>CellAppMgr: 初始化数据
        CellAppMgr->>CellAppMgr: cellApps_.add()
        CellAppMgr->>CellAppMgr: pendingApps_.push_back()
        CellAppMgr->>CellApp: reply CellAppInitData
        CellAppMgr->>CellApp: sendSharedData()
        CellAppMgr->>CellApp: sendGlobalData()
    end
</MermaidDiagram>

**详细讲解：**

1. **前置条件检查**：
   - `baseAppAddr_.isNone()`：必须知道 BaseApp 地址，否则无法创建 Base Entity
   - `dbAlphaAddr_.isNone()`：必须知道 DBApp Alpha，否则无法持久化
   - `allowNewCellApps_`：运行时开关，可以禁止新 CellApp 加入
   - `isRecovery_`：正在恢复时不允许新 CellApp，避免状态冲突

2. **ID 分配**：`lastCellAppID_` 单调递增，每个 CellApp 有唯一 ID，用于消息路由和状态管理。

3. **初始化数据**：`CellAppInitData` 包含 CellApp 需要的所有初始信息：
   - 当前游戏时间
   - BaseApp 地址（用于 Base-Cell 通信）
   - DBApp Alpha 地址（用于持久化）
   - 超时周期（用于心跳检测）

4. **pending 列表**：新 CellApp 先加入 `pendingApps_`，等待 ready 后才真正参与负载均衡。

5. **共享数据下发**：`sendSharedData()` 和 `sendGlobalData()` 同步全局配置，确保所有 CellApp 有一致的配置。

### CellAppMgr 初始化流程

**源码入口：** [cellappmgr.cpp:190](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:190)

```cpp
// cellappmgr.cpp:190 - CellAppMgr 初始化
bool CellAppMgr::init( bool isReload )
{
    // ... 其他初始化 ...
    
    // 注册负载均衡 timer
    mainDispatcher_.addTimer( Config::loadBalancePeriod() * 1000000,
        this, (void*)LOAD_BALANCE_TIMER, "LoadBalance" );
    
    // 注册 meta 负载均衡 timer
    mainDispatcher_.addTimer( Config::metaLoadBalancePeriod() * 1000000,
        this, (void*)META_LOAD_BALANCE_TIMER, "MetaLoadBalance" );
    
    // 注册过载检查 timer
    mainDispatcher_.addTimer( Config::overloadCheckPeriod() * 1000000,
        this, (void*)OVERLOAD_CHECK_TIMER, "OverloadCheck" );
    
    // 创建 TimeKeeper
    pTimeKeeper_ = new TimeKeeper( this, this->time() );
    
    return true;
}
```

**关键细节：**

- 负载均衡 timer 定期触发，检查 CellApp 负载并决定是否迁移 Cell
- meta 负载均衡 timer 处理更复杂的全局负载均衡决策
- 过载检查 timer 监控 CellApp 是否过载，触发保护机制
- TimeKeeper 用于跨进程时间同步

这说明 CellApp 不是随便启动就能承载实体，必须被控制面接纳并同步初始状态。

## 负载均衡与 GameTick

`CellAppMgr::init()` 注册三个定时器：

- `LoadBalance`
- `MetaLoadBalance`
- `OverloadCheck`

源码见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:190)。

`CellAppMgr::startTimer()` 使用 `Config::updateHertz()` 启动 `GameTick`，并创建 `TimeKeeper`。源码见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1774)。

CellAppMgr 会向所有 CellApp 广播 game time，见 [cellapps.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellapps.cpp:540)。

这说明扩展不是只把进程放进列表，还要把它纳入统一 Tick 和负载控制。

## Entity Offload

Cell 负载均衡最终会落到实体迁移和 Ghost 维护。

源码入口：

- `OffloadChecker::run()` 执行 offload 和 ghost 检查，见 [offload_checker.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/offload_checker.cpp:41)。
- `Cell::offloadEntity()` 调用 `Entity::offload()`，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:183)。
- `Entity::offload()` 是真实实体迁移入口之一，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:1968)。
- `RealEntity` 在 offload 前通知 ghosts，见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:243)。

这说明动态扩展真正难点不是“多启动一个进程”，而是：

- 何时切 Cell 边界。
- 哪些实体要迁移。
- Ghost 是否已准备好。
- 迁移期间消息如何缓冲。
- Channel version 如何识别过期包。
- Base/Cell/Client 三方状态如何一致。

这些内容后续实体迁移和 AOI 章节会深入展开。

## Reviver 容灾模型

`Reviver` 负责监控关键组件并请求 machined 重启。

`ComponentReviver::init()` 做三件事：

1. 读取 ping period、timeout。
2. 通过 machined 查找目标 interface 地址。
3. 注册 birth/death listener。

源码见 [component_reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/component_reviver.cpp:62)。

`ComponentReviver::handleTimeout()` 每次 timer 到期：

- 如果还允许 miss，则发送 ping request。
- 收到 `REVIVER_PING_YES` 后重置 miss 计数并标记 attached。
- miss 太多则调用 `revive()`。

源码见 [component_reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/component_reviver.cpp:252)。

`handleMessage()` 收到 death 消息时，如果地址匹配当前组件，也会触发 revive，见 [component_reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/component_reviver.cpp:180)。

<MermaidDiagram title="Reviver 恢复流程">
flowchart TD
  A[ComponentReviver] --> B[register birth/death listener]
  A --> C[start ping timer]
  C --> D{ping response?}
  D -- yes --> E[reset missed count attached]
  D -- no --> F[decrement pingsToMiss]
  F --> G{miss too many?}
  G -- no --> C
  G -- yes --> H[Reviver revive]
  B --> I{death message}
  I -- matching addr --> H
  H --> J[CreateMessage to machined]
  J --> K[start process with recover flag]
</MermaidDiagram>

## machined 的边界

`Reviver::revive()` 通过 `CreateMessage` 向本地 machined 请求创建进程：

- `cm.uid_ = getUserId()`
- `cm.recover_ = 1`
- `cm.name_ = createComponent`
- `cm.config_ = BW_COMPILE_TIME_CONFIG`
- 发送到 `127.0.0.1`

源码见 [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:450)。

源码里还有 TODO：最好知道 machined 是否真正成功启动进程，失败原因之一可能是 `machined.conf` 未配置正确。见 [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:459)。

这说明 Reviver 是进程级恢复，不是完整编排系统。它能请求重启，但不等价于现代 orchestrator 的调度、健康检查、资源约束、滚动升级和事件审计。

## CellApp crash 后的实体恢复调用链

当 CellApp 异常死亡时，BigWorld 通过以下调用链恢复实体：

### 第一阶段：CellAppMgr 检测死亡

CellAppMgr 通过 Channel 断开检测到 CellApp 死亡，调用 handleCellAppDeath()：

源码入口：[cellappmgr.cpp:1806](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1806)

调用链：

<div class="flow-strip">
  <span class="flow-node">CellApp Channel 断开</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">CellAppMgr::handleCellAppDeath()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">发送 SIGQUIT 给死亡 CellApp</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">创建 CellAppDeathHandler</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">pDeadApp->handleUnexpectedDeath()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">等待所有 CellApp 回复</span>
</div>

### 第二阶段：CellApp 收集实体状态

存活的 CellApp 收到 death 通知后，收集自己持有的 ghost 实体状态：

源码入口：[cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp)

调用链：

<div class="flow-strip">
  <span class="flow-node">CellApp 收到 handleCellAppDeath</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">遍历所有 ghost 实体</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">检查 ghost 是否属于死亡 CellApp</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">收集需要恢复的实体列表</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">回复 CellAppMgr</span>
</div>

### 第三阶段：CellAppMgr 通知 BaseAppMgr

所有 CellApp 回复后，CellAppMgr 通知 BaseAppMgr 开始恢复：

调用链：

<div class="flow-strip">
  <span class="flow-node">CellAppDeathHandler 收到所有回复</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">CellAppMgr 通知 BaseAppMgr</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BaseAppMgr 广播给所有 BaseApp</span>
</div>

### 第四阶段：BaseApp 恢复 Cell Entity

BaseApp 收到通知后，遍历所有 Base Entity，调用 restoreTo() 恢复 Cell Entity：

源码入口：[baseapp.cpp:1942](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:1942)

调用链：

<div class="flow-strip">
  <span class="flow-node">BaseApp::handleCellAppDeath()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">pDeadCellApps_->addApp()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">pDeadCellApps_->tick()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">遍历 bases_ 中的实体</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Base::restoreTo()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">从 cellBackupData_ 恢复</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">发送到新 CellApp</span>
</div>

### 第五阶段：CellApp 恢复实体

新 CellApp 收到恢复数据后，重建 Cell Entity：

调用链：

<div class="flow-strip">
  <span class="flow-node">新 CellApp 收到 restoreTo 请求</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">创建 Entity 对象</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">从备份数据恢复属性</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">重建 real/ghost 关系</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">通知 BaseApp 恢复完成</span>
</div>

### 恢复失败场景

恢复可能失败的场景：

- Base Entity 没有 cellBackupData_（未备份）
- 目标 CellApp 不可用
- 数据库不可用（无法读取持久化数据）
- 实体处于 offload 中间态
- 协议版本不兼容

源码见 [base.cpp:3810](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3810)。

## 故障恢复边界

BigWorld 有多层恢复：

- Reviver/machined：进程死亡后重启。
- Manager birth/death：组件发现和状态更新。
- BaseApp/CellApp death handler：处理实体和 Channel 恢复。
- DB 备份和实体持久化：恢复状态来源。
- Channel version 和 recently-dead channels：识别过期包和死亡组件残留消息。

但恢复不是魔法：

- 如果内存状态没有备份，恢复可能丢失。
- 如果 Entity 处于 offload 中间态，恢复逻辑更复杂。
- 如果数据库不可用，进程重启也无法完成业务恢复。
- 如果控制面本身脑裂，旧 machined 模型没有现代共识系统保证。
- 如果协议版本不兼容，重启后的进程也可能无法加入。

## 源码取舍

BigWorld 的扩展与容灾围绕 MMO 状态迁移，而不是普通请求分摊：

- 进程角色固定，管理器集中协调。
- machined 提供机器级进程发现和启动能力。
- Reviver 做轻量进程监控和重启。
- 负载均衡围绕 Cell、Entity 和 Ghost，而不是围绕 HTTP 请求。
- 多进程隔离崩溃域，避免单进程多线程全服崩溃。

源码代价：

- 进程是否存活与游戏状态是否可迁移是两件事。
- CellApp 接纳、Cell 分裂、Entity offload 和 Ghost 准备必须按顺序完成。
- Reviver 能重启进程，但无法凭空恢复没有备份或没有写库的内存状态。
- 控制面本身缺少强共识语义时，birth/death 和旧消息处理必须依赖 Manager 状态和 channel version 防护。

## 源码验证重点

扩展与容灾测试应覆盖状态迁移，而不是只测进程重启：

- 新 CellApp 在 BaseApp 和 Alpha DBApp 未知时应被拒绝接纳。
- CellApp 加入后，CellAppMgr 应更新负载、空间、Cell 边界和 watcher 状态。
- Cell 分裂、Cell 删除和 Entity offload 应维持 real/ghost/Witness 一致。
- BaseApp death、CellApp death 和 DBApp 不可用应分别触发对应恢复或失败路径。
- recently-dead channel 和 channel version 应阻止旧包污染新组件。
- 恢复路径应覆盖内存备份缺失、DB 不可用、offload 中间态和协议不兼容。

## 本章边界

本章只建立动态扩展与容灾的框架。后续需要继续深入实体模型、AOI/Ghost、Cell 分区、实体迁移和 DB 持久化，才能完整解释 BigWorld 的状态治理。
