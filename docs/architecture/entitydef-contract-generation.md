# EntityDef 契约与协议生成

<div class="arch-hero">

EntityDef 不是普通配置文件。它把 `entities.xml`、实体 `.def`、组件 `.def`、脚本分布、属性标志、方法签名和网络消息范围合并成运行时协议契约。序列化章节解释“怎么写入流”，本章解释“哪些字段、方法和编号为什么存在”。

</div>

## 先给结论

BigWorld 的实体协议契约由运行时解析生成，而不是由离线 IDL 编译器统一产出：

1. `scripts/entities.xml` 决定实体类型集合和顺序。
2. 每个实体的 `scripts/entity_defs/<Entity>.def` 决定属性、方法、父定义、分布和 AOI 元信息。
3. `EntityDescriptionMap` 解析所有实体并分配 `EntityTypeID`、`clientIndex`、方法 exposed id 和 client-server property index。
4. `EntityDescription` 将同一份定义切成 Base、Cell、Client、Persistent、Ghosted 等不同视图。
5. `process_defs` 复用同一套解析逻辑，把结果导出成 Python 对象，供外部脚本生成工具或文档使用。

源码入口：

- `EntityDescriptionMap::parse()` 在 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:140)。
- `EntityDescriptionMap::parseInternal()` 在 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:347)。
- `EntityDescription::parse()` 在 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:190)。
- `EntityDescription::parseInterface()` 在 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:381)。
- `EntityDescription::parseProperties()` 在 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:588)。
- `EntityMethodDescriptions::init()` 在 [entity_method_descriptions.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_method_descriptions.cpp:31)。
- `process_defs` 主入口在 [main.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/tools/process_defs/main.cpp:1)。

## 契约输入

EntityDef 的输入不是单个文件：

- `scripts/entities.xml`：实体类型清单，支持 `ClientServerEntities` 和 `ServerOnlyEntities` 分组。
- `scripts/services.xml`：服务定义，服务只走服务端语义。
- `scripts/entity_defs/*.def`：实体定义。
- `scripts/entity_defs/components/*.def`：组件定义，可被实体复用。
- `scripts/entity_defs/interfaces/*.def`：接口定义，可被实体或服务实现。
- `scripts/client`、`scripts/base`、`scripts/cell`、`scripts/service` 下的 Python 文件：在旧式解析和分布判断中影响实体是否有对应脚本。

这解释了为什么 EntityDef 迁移不能只扫描 `.def` 文件。实体是否可以存在于 Client、Base 或 Cell，既可能由 `.def` 的 `Distribution` 明确声明，也可能由脚本文件是否存在推断。相关判断在 `HasScriptOrTagDistributionDecider`，见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:218)。

## 解析调用链

典型解析链路如下：

<MermaidDiagram title="EntityDef 解析链路">
flowchart TD
  A[scripts/entities.xml] --> B[EntityDescriptionMap::parse]
  B --> C[parseInternal]
  C --> D[EntityDescription::parse]
  D --> E[Parent 递归解析]
  D --> F[Distribution / ClientName]
  D --> G[parseInterface]
  G --> H[parseMethods]
  G --> I[parseComponents]
  I --> J[parseComponent]
  J --> K[组件 Properties / Methods]
  D --> L[parseProperties]
  B --> M[setExposedMessageIDs]
  B --> N[addToMD5]
</MermaidDiagram>

几个关键点：

- `EntityDescriptionMap::parse()` 优先读取 `ClientServerEntities` 和 `ServerOnlyEntities`，如果缺失则回退到旧式解析，见 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:163)。
- `EntityDescription::parse()` 会先处理 `Parent`，父定义会先被解析，见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:194)。
- `parseInterface()` 读取持久化、LoD、Volatile、AOI、方法、临时属性和组件，见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:391)。
- `parseComponents()` 会打开组件 `.def` 并把组件属性和方法合并到实体定义中，见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:452)。
- 服务通过 `parseServices()` 单独加入 EntityDescriptionMap，见 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:393)。

这不是简单的 XML 到 struct 映射，而是多来源合成、继承、组件合并和分布推断的过程。

## 类型 ID 与 Client Index

`EntityDescriptionMap::parse()` 在所有实体解析完后按 `vector_` 顺序分配 `EntityTypeID`：

- `desc.index(i)` 是服务端实体类型 ID。
- `map_[desc.name()] = desc.index()` 建立名字到 ID 的映射。
- 对客户端可见实体，`clientIndex` 默认等于当前顺序 ID。

源码见 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:214)。

这意味着 `entities.xml` 顺序具有协议含义。随意重排实体类型，不只是整理配置，可能改变实体类型 ID 和客户端/服务端共享契约。

`ClientName` 是一个历史兼容功能：服务端实体可声明不同客户端类型名。解析后会校验 alias 的客户端方法数和 client-server property 数是否一致，否则报错。源码见 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:442)。源码注释明确该特性已废弃，因此后续不应继续扩大使用范围。

## 属性契约

属性解析由 `DataDescription::parse()` 和 `EntityDescription::parseProperties()` 共同完成。

`DataDescription` 负责单个属性语义：

- `Type`：构造 `DataType`。
- `Flags`：映射为 `CELL_PRIVATE`、`CELL_PUBLIC`、`OTHER_CLIENTS`、`OWN_CLIENT`、`BASE`、`BASE_AND_CLIENT`、`CELL_PUBLIC_AND_OWN`、`ALL_CLIENTS`、`EDITOR_ONLY`。
- `Persistent`：是否进入持久化语义。
- `Identifier` / `Indexed`：数据库识别和索引语义。
- `Default`：默认值来源。

相关解析在 [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:119)。

`EntityDescription::parseProperties()` 负责实体级编号和约束：

- 普通属性获得 `index`。
- client-server 属性获得 `clientServerFullIndex`。
- 重写已有属性时复用原 index。
- client-server 属性包含 `PYTHON` 成员会警告潜在安全风险。
- client-server 属性包含 `MAILBOX` 成员会报错，因为不能发给客户端。
- `OTHER_CLIENTS` 属性会分配 event stamp 和 detail level。

源码见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:607)。

属性的关键不是“字段名是什么”，而是它进入哪些数据域。一个属性可能只存在于 Cell，也可能进入 Base、Own Client、Other Clients、Ghost、Persistent 或数据库索引路径。后续所有创建、迁移、同步、保存和热更新都依赖这些标志。

## `.def` 与数据库契约

你提到的几个数据库问题，本质上都从 `.def` 开始。

BigWorld 不是先有数据库模型再映射到实体，而是先解析 `.def`，再由 DB 层根据 persistent 子集建映射。也就是说，`.def` 在这里更像“协议 + 持久化契约”。

### 实体级标签

实体级至少有两个和数据库直接相关的开关：

- `Persistent`：实体类型是否允许持久化，见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:396)。
- `ExplicitDatabaseID`：是否允许显式指定 DBID，见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:391)。

`DBApp` 侧还有一层硬约束：

- `EntityDefs::isValidEntityType()` 只接受 persistent entity type，见 [db_entitydefs.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage/db_entitydefs.hpp:35)。

这意味着：

- 没有实体级 `Persistent`，整个类型就不进入 DB 契约。
- 后面即使某个属性写了 `Persistent`，没有实体级持久化能力也没有意义。

### 属性级标签

`DataDescription::parse()` 里和数据库直接相关的标签有：

- `Persistent`
- `Identifier`
- `Indexed`
- `Indexed/Unique`
- `DatabaseLength`

解析入口在 [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:220)。

源码语义可以直接总结为：

<div class="decision-table">

| 标签 | 含义 | 关键约束 |
| --- | --- | --- |
| `Persistent` | 属性进入持久化子集 | 不写则不会进入 `ONLY_PERSISTENT_DATA` |
| `Identifier` | 把该属性作为实体业务标识 | 自动推导为索引，默认唯一 |
| `Indexed` | 为持久化属性建索引 | 非 persistent 属性不能 indexed |
| `Indexed/Unique` | 控制索引是否唯一 | 仅在 indexed 时生效 |
| `DatabaseLength` | 影响字符串/复合类型的 DB 长度策略 | 不是所有类型都等价使用 |

</div>

其中有几个容易忽略的硬规则：

- `Identifier` 会把 `DATA_ID` 打开，并默认要求索引，见 [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:228)。
- `Indexed` 只允许出现在 persistent 属性上，否则直接报错，见 [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:238)。
- `Indexed/Unique` 会决定 `DATABASE_INDEXING_UNIQUE` 还是 `DATABASE_INDEXING_NON_UNIQUE`，见 [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:250)。

### Identifier 不是任意类型都能当主键

DB 侧对 Identifier 有额外限制，不是任何 `Persistent` 属性都能升格为实体标识：

- `EntityDefs::findIdentifier()` 明确只支持一个 Identifier。
- 它要求该属性类型是 `STRING`、`UNICODE_STRING` 或 `BLOB`。

源码见 [db_entitydefs.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage/db_entitydefs.cpp:33) 和 [db_entitydefs.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage/db_entitydefs.cpp:113)。

这和很多 ORM 默认“任意标量都能当唯一键”不同。BigWorld 这里是引擎做了收敛，方便统一 lookup / cache / DBID 绑定逻辑。

### `ONLY_PERSISTENT_DATA` 是真正的 DB 视图切片

前面说“EntityDescription 会切出不同视图”，数据库最关键的就是 `ONLY_PERSISTENT_DATA`。

证据有三层：

- `EntityDescription::addToStream()` 在遍历属性时会检查 `ONLY_PERSISTENT_DATA`，只让 persistent 属性进入输出，见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:1472)。
- `Base::addToStream()` 写 Base 数据时显式带上 `ONLY_PERSISTENT_DATA`，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3931)。
- `EntityTypeMapping::visit()` 建表和写库时也只访问 `BASE_DATA | CELL_DATA | ONLY_PERSISTENT_DATA`，见 [entity_type_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/entity_type_mapping.cpp:646)。

这说明数据库 schema、数据库写入和数据库 digest 三者用的是同一份 persistent 视图，而不是三套独立规则。

### `DatabaseLength`、复杂类型和表结构

`.def` 写完后不一定一一落成普通列，复杂类型会进入映射器判断：

- `PropertyMapping::create()` 会按 `DataType` 选择映射实现，见 [property_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/property_mapping.cpp:132)。
- `ARRAY` / `TUPLE` 在非索引场景下可能走 `SequenceMapping`，也可能在 `dbLen() > 0` 时走 `BlobbedSequenceMapping`，见 [property_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/property_mapping.cpp:149)。
- `CLASS` / `FIXED_DICT` 走 class-type mapping，见 [property_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/property_mapping.cpp:174)。
- 字符串类属性会检查 `DatabaseLength` 是否超限，见 [string_like_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/string_like_mapping.cpp:122)。

所以 `DatabaseLength` 不是一个“UI 提示参数”，它会真实影响底层映射策略和列定义。

### `.def` 改动为什么经常意味着 DB migration

从源码看，BigWorld 对 DB 契约变更是有显式签名和同步逻辑的：

- `EntityDefs::init()` 会计算完整 defs digest 和 persistent properties digest，见 [db_entitydefs.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage/db_entitydefs.cpp:67)。
- MySQL 初始化时会用 persistent properties digest 做兼容检查，见 [mysql_database.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mysql_database.cpp:918)。
- `TableSynchroniser` 会通过 `sync_db` 路径做表同步，见 [table_synchroniser.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/table_synchroniser.cpp:90)。

因此下列 `.def` 变更都不只是“脚本字段调整”：

- 新增或删除 `Persistent` 属性。
- 变更 `Identifier`。
- 变更 `Indexed` / `Indexed/Unique`。
- 调整复杂类型结构。
- 调整 `DatabaseLength`。

它们都可能导致 persistent digest 变化、DB schema 变化或已有数据解释方式变化。

## 方法契约

方法按目标端分为三组：

- `ClientMethods`：服务端调用客户端。
- `BaseMethods`：外部或 Cell 调用 Base。
- `CellMethods`：外部或 Base 调用 Cell。

`EntityDescription::parseMethods()` 按这三组解析，服务则使用 `Methods` 作为服务端方法集合。源码见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:553)。

`EntityMethodDescriptions::init()` 会为方法分配 `internalIndex`，并把 exposed 方法加入 `exposedMethods_`。客户端方法会被视为全部 exposed，见 [entity_method_descriptions.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_method_descriptions.cpp:56)。

方法契约至少包含：

- 方法名。
- 参数类型序列。
- 返回值类型序列。
- 是否 exposed。
- `internalIndex`。
- `exposedIndex`。
- 对应 Mercury message id 和 sub-message id。
- stream size。

这些字段不只是调试信息。远程方法调用时，双方通过方法编号和参数流解码找到同一个方法定义。

## Exposed Message ID

BigWorld 没有为每个实体方法静态分配一个全局唯一消息号，而是使用“消息范围 + exposed index + 必要时 sub-message id”的组合。

链路如下：

1. `EntityDescriptionMap::parse()` 接收 `ClientInterface` 和 `BaseAppExtInterface` 的 message range。
2. `setExposedMessageIDs()` 先统计所有实体的最大 exposed 方法数和 client-server 属性数。
3. 每个 `EntityDescription` 调用 `setExposedMsgIDs()`。
4. 每个 `MethodDescription` 通过 `ExposedMethodMessageRange::msgIDFromExposedID()` 得到 message id 和 sub-message id。

源码见 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:299) 和 [method_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_description.cpp:1265)。

这是一种节省消息 ID 空间的协议压缩设计。代价是协议解释依赖同一套 EntityDef、同一套 exposed 方法排序和同一套 message range，独立抓包或跨语言复现会更困难。

## Digest 语义

`EntityDescriptionMap::parse()` 末尾计算 digest，见 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:287)。

客户端相关 digest 的输入不是全部实体定义，而是 client type：

- `EntityDescriptionMap::addToMD5()` 跳过 server-only 类型，见 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:608)。
- `EntityDescription::addToMD5()` 包含实体名、client-server 属性、客户端方法、exposed Base 方法、exposed Cell 方法，见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:1740)。
- `DataDescription::addToMD5()` 包含属性名、标志、默认值和数据类型，见 [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:431)。
- `MethodDescription::addToMD5()` 包含方法成员描述、flags、参数和 legacy exposed index，见 [method_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_description.cpp:1294)。

DB 侧还有持久化属性 digest，用于数据库兼容检查。`EntityDescriptionMap::addPersistentPropertiesToMD5()` 只处理 persistent entity 和 persistent property，见 [entity_description_map.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description_map.cpp:629)。

因此 digest 不是通用文件 hash。它是按运行时协议语义裁剪后的兼容性签名。

## process_defs 的角色

`tools/process_defs` 是 EntityDef 契约的导出器。源码开头说明它会解析 `.def` 文件，生成描述所有实体类型的 Python 对象，然后调用一个 Python 回调模块，默认是 `ProcessDefs.process`。见 [main.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/tools/process_defs/main.cpp:1)。

它导出的结构包括：

- `entityTypes`：实体类型 tuple。
- `constants.digest`：EntityDef digest。
- `constants.maxExposedClientMethodCount`。
- `constants.maxExposedBaseMethodCount`。
- `constants.maxExposedCellMethodCount`。
- `constants.maxClientServerPropertyCount`。

源码见 [main.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/tools/process_defs/main.cpp:484)。

每个实体描述包含：

- `name`、`index`、`clientIndex`。
- `canBeOnClient`、`canBeOnBase`、`canBeOnCell`。
- `isService`、`isPersistent`。
- `clientMethods`、`baseMethods`、`cellMethods`。
- `allProperties`。
- `clientProperties`。
- `baseToClientProperties`。
- `cellToClientProperties`。

源码见 [main.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/tools/process_defs/main.cpp:244)。

这说明 BigWorld 已有“契约导出边界”，只是它输出的是 Python 对象回调；下游工具如果需要其他格式，应从这条导出链路读取同一份 `EntityDescriptionMap`，不能另写一套 `.def` 解析器。

## 运行时消费路径

多个运行时组件会独立解析同一套 EntityDef：

- BaseApp 的 `EntityType::init()` 解析 `entities.xml`，设置 `s_digest_`，并加载 Base 脚本。见 [entity_type.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/entity_type.cpp:240)。
- CellApp 的 `EntityType::init()` 解析同一套定义并加载 Cell 脚本。见 [entity_type.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_type.cpp:256)。
- DBApp 的 `DBApp::initEntityDefs()` 初始化 `EntityDefs`，并打印期望 digest。见 [dbapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.cpp:349)。
- Client、Bots、Replay 也会持有 EntityDef constants，用于连接、回放和方法/属性解码。

这带来一个工程约束：EntityDef 变更必须在所有参与方同步部署，否则连接、登录、回放、持久化和实体迁移都可能失败。

## 与序列化章节的边界

本章只解释契约生成，不重复底层编码：

- 本章关注 `.def` 如何变成属性/方法集合、编号、digest 和导出结构。
- [序列化与 EntityDef](/architecture/serialization-entitydef) 关注 `DataType`、`DataDescription`、`MethodArgs` 如何把 Python 对象或实体属性写入 `BinaryStream`。
- [通信抽象与 RPC](/architecture/communication-rpc) 关注消息如何通过 Mercury Interface、Bundle 和 handler 分发。
- [热更新与脚本迁移](/architecture/hot-reload-script-migration) 关注定义变化后运行中实体如何迁移。

如果把这些层混在一起，容易误判为“改一个字段只是改 XML”。实际影响范围至少包括协议编号、digest、客户端同步、DB schema、脚本类型检查和迁移路径。

## 源码取舍

BigWorld 的 EntityDef 契约设计服务的是同一份实体定义跨多个运行时视图复用：

- 一份定义同时驱动客户端、Base、Cell、DB 和脚本。
- 二进制协议紧凑，适合高频实体同步。
- 运行时直接可解析，便于工具链、编辑器和服务器共享。
- 通过 digest 快速拒绝不兼容客户端。
- 组件、接口和父定义提供一定复用能力。

源码代价也集中在契约稳定性上：

- 版本演进依赖顺序和编号，字段重排风险高。
- 协议 schema 隐含在 EntityDef、Mercury message range 和运行时解析代码里，外部工具必须复用 `process_defs` 或 `EntityDescriptionMap`。
- `ClientName`、脚本存在性推断等历史兼容规则增加理解成本。
- `.def` 同时承担网络契约和数据库契约，单点改动影响范围大。
- 多进程各自解析同一套定义，部署一致性要求高。

## 源码验证重点

EntityDef 契约测试不应只测 XML 是否能解析，还应覆盖：

- 实体顺序改变是否导致 type id 改变并被测试捕获。
- 父定义和组件属性覆盖是否保留预期 index。
- exposed 方法重排是否导致 digest 变化。
- client-server 属性增删改是否导致 digest 变化。
- server-only 属性变更是否不影响客户端 digest。
- persistent 属性变更是否影响 DB persistent properties digest。
- `process_defs` 导出结果是否与运行时 `EntityDescriptionMap` 一致。
- 不兼容客户端登录是否被 digest 检查拒绝。
- `.def` 属性的 `Persistent`、`Identifier`、`Indexed`、`DatabaseLength` 改动是否触发预期 persistent digest 或 DB schema 变化。
- `ClientName` alias 的客户端方法数和 client-server property 数不一致时必须报错。

这些测试能把“隐式协议规则”变成可审查边界，避免把 `.def` 改动误判成普通脚本变更。

## 本章边界

本章确认：BigWorld 有完整的 EntityDef 契约生成链路。后续分析序列化、通信、持久化和热更新时，应以 `EntityDescriptionMap`、`EntityDescription`、`DataDescription` 和 `MethodDescription` 这条源码链路作为共同依据。
