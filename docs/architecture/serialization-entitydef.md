# 序列化与 EntityDef

<div class="arch-hero">

BigWorld 的序列化不是一个孤立的 `serialize()` 工具函数，而是 EntityDef 类型系统、Python 脚本对象、网络 Bundle、数据库持久化和热更新迁移之间的粘合层。理解它，才能判断一个属性或方法参数究竟会进入客户端、Base、Cell、Ghost、DB 还是迁移流。

</div>

## 先给结论

BigWorld 的序列化链路分三层：

1. `BinaryOStream` / `BinaryIStream` 定义最底层二进制读写接口。
2. `DataType` / `DataDescription` / `MethodArgs` 负责按 EntityDef 类型系统把属性和方法参数写入流。
3. `EntityDescription` 按 Base、Cell、Client、Persistent 等数据域组织实体属性流。

源码入口：

- `BinaryOStream` / `BinaryIStream` 在 [binary_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/binary_stream.hpp:24) 和 [binary_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/binary_stream.hpp:64)。
- `MemoryOStream` / `MemoryIStream` 在 [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:27) 和 [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:73)。
- `DataType::addToStream()` 在 [data_type.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_type.cpp:93)。
- `DataDescription::addToStream()` 在 [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:374)。
- `EntityDescription::addToStream()` 在 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:1472)。
- `MethodArgs::addToStream()` 与 `createFromStream()` 在 [method_args.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:213) 和 [method_args.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:295)。

## 序列化不只是网络

BigWorld 的 EntityDef 类型系统同时服务：

- C++ 引擎内部对象。
- Python 脚本属性和方法参数。
- 客户端/服务端网络同步。
- Base/Cell 实体迁移。
- 数据库持久化。
- 默认值、类型检查和脚本热更新迁移。

因此它不是单纯“网络消息 schema”。它更像游戏运行时的统一类型契约。

<MermaidDiagram title="EntityDef 序列化位置">
flowchart TD
  A[EntityDef XML] --> B[EntityDescription]
  B --> C[DataDescription]
  C --> D[DataType]
  D --> E[BinaryOStream/BinaryIStream]
  E --> F[Mercury Bundle]
  E --> G[DB 持久化]
  E --> H[Base/Cell 迁移]
  E --> I[Client 属性同步]
  J[Python ScriptObject] --> C
  C --> K[DataSource/DataSink]
</MermaidDiagram>

## BinaryStream 层

`BinaryOStream` 是输出接口，最关键的方法是 `reserve(int nBytes)`，调用者拿到可写内存后直接填入数据。它也提供：

- `addBlob()`：拷贝一段二进制数据。
- `transfer()`：从输入流搬运指定长度。
- `writePackedInt()`：写压缩整数。
- `writeStringLength()`：写字符串长度。

源码见 [binary_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/binary_stream.hpp:24)。

`BinaryIStream` 是输入接口，核心方法是：

- `retrieve(int nBytes)`：取出指定长度数据。
- `remainingLength()`：剩余长度。
- `readPackedInt()`：读取压缩整数。
- `error()`：记录流错误状态。

源码见 [binary_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/binary_stream.hpp:64)。

这个接口非常底层，优点是快、轻、容易嵌入 packet/bundle；代价是缺少现代 schema 系统的自描述、反射和版本演进能力。

## MemoryStream 层

`MemoryOStream` 同时继承 `BinaryOStream` 和 `BinaryIStream`，可以先写入内存，再作为输入流读出。源码见 [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:27)。

典型用途：

- 先把变长数据写入临时 buffer，再计算长度。
- 构造子流，最后再写回父流。
- 单元测试中做 round-trip。

`MemoryIStream` 则包装已有内存块，用于从 packet、数据库 blob 或测试数据中读取。

## DataType：类型如何写入流

`DataType::addToStream()` 接收 `DataSource` 和 `BinaryOStream`，按类型的 `StreamElement` 逐项写入。源码见 [data_type.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_type.cpp:93)。

一个关键细节是 substream：

- 遇到 `isSubstreamStart()` 时创建新的 `MemoryOStream`。
- 当前写入目标切换到最内层 substream。
- 遇到 `isSubstreamEnd()` 时，把子流长度和内容回填到上一层流。

这说明 BigWorld 的类型系统需要处理嵌套变长结构，例如数组、字典、类对象、可选值等。它不是简单按 C struct 内存布局 memcpy。

## DataDescription：属性语义

`DataDescription` 不是单纯的数据类型。它还包含属性属于哪些域、是否 client-server data、是否 persistent、变长 header 等语义。

`DataDescription::addToStream()` 中有一个很重要的分支，见 [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:374)：

- 如果属性是 client-server data 且 stream size 为变长。
- 先写入临时 `MemoryOStream lengthStream`。
- 检查是否超长。
- 再把临时流 transfer 到真实输出流。

这个逻辑体现了游戏网络的防御边界：客户端相关变长属性不能无限写入，必须在编码阶段就检查长度。

## EntityDescription：按数据域组织实体流

`EntityDescription::addToStream()` 遍历实体属性，并按 `dataDomains` 和 pass 规则选择要写入的属性。源码见 [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:1472)。

它的职责不是“把整个对象全部序列化”，而是按场景输出不同视图：

- 创建 Base 实体时需要 Base/Persistent 数据。
- 创建 Cell 实体时需要 Cell 数据。
- 客户端同步只需要 client 可见数据。
- 数据库保存可能只需要 persistent 数据。
- 实体迁移需要按 Base/Cell 迁移协议组织数据。

这就是 BigWorld EntityDef 的核心价值：同一个实体定义可以派生多种运行时数据流。

## MethodArgs：方法参数

`MethodArgs::addToStream()` 负责把方法参数按定义顺序写入流，见 [method_args.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:213)。

它会：

- 从 `DataSource` 开始读取 sequence。
- 参数不足时尝试写入默认值，并记录错误。
- 对每个参数调用对应 `DataType::addToStream()`。
- 返回整体是否成功。

`MethodArgs::createFromStream()` 则从 `BinaryIStream` 读出参数并写入 `DataSink`，见 [method_args.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:295)。

这条链路连接了：

<div class="flow-strip">
  <span class="flow-node">Python 方法调用</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">MethodArgs</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataType</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BinaryStream</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Mercury Bundle</span>
</div>

## DataSource 与 DataSink

源码中 `DataType`、`DataDescription`、`MethodArgs` 都不直接依赖单一 Python 对象接口，而是通过 `DataSource` / `DataSink` 抽象读写。

这个设计的意义：

- 同一类型逻辑可以从 Python 对象、XML/DataSection、数据库结果或其他来源读取。
- 反序列化目标也可以是 Python 对象、字典、测试 sink 或数据库结构。
- 类型系统和具体数据来源解耦。

这是一个符合 DIP 的设计：序列化逻辑依赖抽象数据源，而不是到处判断 Python/XML/DB 的具体类型。

代价是调用链变长，新人阅读时难以从一个函数直接看到最终对象读写位置。

## Wire format 特征

BigWorld 的序列化格式更偏“共享 schema 的紧凑二进制流”，而不是“自描述消息”。

特征：

- 字段顺序由 EntityDef 和 MethodArgs 决定。
- 类型由双方共享定义决定。
- 变长字段使用长度头或 packed int。
- 固定长度字段可以直接按类型编码。
- 流错误通过 `BinaryIStream::error_` 和返回值传播。
- 协议兼容依赖实体定义、接口定义和 MD5/版本检查。

优点：

- 编码紧凑。
- 高频路径开销低。
- 类型系统深度绑定引擎运行时。

缺点：

- 缺少字段 tag，字段插入/删除需要严格兼容策略。
- 抓包不带 schema 很难独立解析。
- 跨语言工具链弱。
- 错误处理依赖调用者检查返回值，流一旦写坏可能远端才暴露。

## 与热更新的关系

热更新章节会详细分析 `CellApp::reloadScript()` 和 `Entity::migrate()`，这里只说明序列化为什么会影响热更新。

实体热更新不是只替换 Python class：

- EntityType 可能变化。
- UDO 类型可能变化。
- Mailbox 类型可能变化。
- 运行中实体需要迁移到新 class。
- 属性和方法参数的流格式必须仍能被新旧两侧理解。

如果 EntityDef 或 DataType 发生不兼容变化，热更新就会从“类迁移问题”升级为“协议和状态迁移问题”。这也是源码中对生产热更新保持谨慎态度的根本原因之一。

## 源码取舍

BigWorld 的序列化承担的职责比普通网络消息编码更宽：

- 它要服务 Python 脚本对象和 C++ Entity 运行时。
- 它要按 Base/Cell/Client/Persistent 数据域选择不同属性集合。
- 它要支持默认值、类型检查、方法参数和属性同步。
- 它要和 Mercury 的 Bundle、可靠 UDP、实体迁移协同。
- 它还要让 DB 映射层从同一段 persistent stream 还原数据库字段。

源码代价：

- BinaryStream 本身不自描述，读写双方必须共享 EntityDef、DataType 和字段顺序。
- Python 对象转换、默认值、错误流状态和复杂嵌套类型都会影响同一条链路。
- EntityDef 改动会同时影响网络、DB、热更新和迁移，不是局部序列化问题。

## 序列化完整调用链

### 属性序列化到网络

Entity 属性序列化到客户端的完整调用链：

源码入口：[entity.cpp:2475](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2475)

<div class="flow-strip">
  <span class="flow-node">Entity::writeClientUpdateDataToBundle()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">遍历 DataDescription</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataDescription::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataType::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataSource 获取值</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BinaryOStream 写入</span>
</div>

### 属性反序列化从网络

客户端属性反序列化的完整调用链：

源码入口：[entity_description.cpp:1472](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:1472)

<div class="flow-strip">
  <span class="flow-node">EntityDescription::createFromStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">遍历 DataDescription</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataDescription::createFromStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataType::createFromStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BinaryIStream 读取</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataSink 写入目标</span>
</div>

### 方法参数序列化

RPC 方法参数序列化的完整调用链：

源码入口：[method_args.cpp:213](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:213)

<div class="flow-strip">
  <span class="flow-node">MethodArgs::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">遍历参数列表</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataType::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataSource 获取值</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BinaryOStream 写入</span>
</div>

### 持久化序列化

Entity 持久化到数据库的完整调用链：

源码入口：[base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp)

<div class="flow-strip">
  <span class="flow-node">Base::writeToDB()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Base::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">EntityDescription::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">ONLY_PERSISTENT_DATA</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">写入 persistent 属性</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DBApp::writeEntity()</span>
</div>

## 源码验证重点

序列化测试必须覆盖 round-trip 和兼容性：

- 每个基础 `DataType` 的 add/create round-trip。
- 数组、字典、class、fixed dict、嵌套 substream。
- client-server 变长属性超长检查。
- 参数不足时默认值写入路径。
- `BinaryIStream::error()` 被设置后的调用者处理。
- EntityDescription 按不同 dataDomains 输出的数据一致性。
- 新旧 EntityDef 之间的兼容矩阵。
- persistent stream 被 DB 映射层消费时的字段顺序和类型一致性。

源码中已有 `lib/entitydef/unit_test/test_stream.cpp`，其中有 `createFromStream()` 和 `addToStream()` 的测试入口。后续测试章节需要评估它覆盖了哪些类型，缺少哪些迁移和异常场景。

## 本章边界

本章解释序列化和 EntityDef 的基本链路。后续章节会继续展开实体模型、Base/Cell/Client 三层语义、热更新、实体迁移、测试与可控时间。
