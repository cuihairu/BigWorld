# 目录与模块

## 阅读原则

BigWorld 的目录结构不能只按“库名含义”理解。更可靠的方式是按运行职责分组，再为每组找到源码入口、消息接口和状态出口。

## 共享库层

`programming/bigworld/lib` 是引擎共享机制层，其中最影响服务端架构的模块如下：

| 模块 | 职责 | 源码入口 |
|---|---|---|
| `server` | 服务进程框架、配置、时间、Watcher 转发 | `bwservice.hpp`, `server_app.cpp`, `time_keeper.cpp` |
| `network` | Mercury 网络栈、Bundle、Channel、Packet、Interface 宏 | `network_interface.hpp`, `bundle.hpp`, `udp_channel.hpp`, `interface_macros.hpp` |
| `connection` | 客户端协议封装、Login/BaseApp/Client interface | `server_connection.cpp`, `client_interface.hpp`, `baseapp_ext_interface.hpp`, `login_interface.hpp` |
| `entitydef` | EntityDef 解析、DataType、属性/方法描述和序列化 | `entity_description_map.cpp`, `entity_description.cpp`, `data_description.hpp`, `method_description.hpp` |
| `db` | DBApp interface 工具和数据库通用定义 | `dbapp_interface.hpp`, `dbapp_interface_utils.cpp` |
| `db_storage` | DB 后端抽象接口 | `idatabase.hpp` |
| `db_storage_mysql` | MySQL 后端、表映射、后台任务 | `mysql_database.cpp`, `mappings/property_mapping.cpp`, `tasks/put_entity_task.hpp` |
| `db_storage_xml` | XML DB 后端实现 | `xml_database.cpp` |
| `script`, `pyscript`, `entitydef_script` | 脚本对象、嵌入式解释器桥接、EntityDef 脚本包装 | `script_object.hpp`, `py_script_object.hpp`, `py_entitydef.cpp` |

渲染、场景和工具共享库也在 `lib` 下，例如 `moo`、`chunk`、`scene`、`terrain`、`asset_pipeline`、`ual`。它们对客户端和编辑器很重要，但理解服务端 MMO 状态模型时应先读上表中的模块。

## 服务端进程层

`programming/bigworld/server` 是多进程 MMO 架构的核心。每个进程都通过 `BIGWORLD_MAIN` 接入统一启动框架，再注册自己的 Mercury interface。

| 目录 | 主要职责 | 关键源码 |
|---|---|---|
| `baseapp` | Base 实体、Proxy、客户端接管、Base 备份、DB 写入入口 | `baseapp.cpp`, `base.cpp`, `proxy.cpp`, `mailbox.cpp` |
| `cellapp` | Cell 实体、空间、AOI、Witness、Ghost、Offload | `cellapp.cpp`, `entity.cpp`, `real_entity.cpp`, `witness.cpp`, `offload_checker.cpp` |
| `dbapp` | DB 请求处理、登录数据、实体读写、DB 后端调用 | `dbapp.cpp`, `write_entity_handler.cpp`, `login_handler.cpp` |
| `loginapp` | 登录入口、挑战协议、账号认证、BaseApp 接入 | `loginapp.cpp`, `message_handlers.cpp` |
| `baseappmgr` | BaseApp 管理、负载、全局 base、恢复协调 | `baseappmgr.cpp`, `baseapp.cpp` |
| `cellappmgr` | CellApp 管理、空间分区、负载均衡、Cell 创建/退休 | `cellappmgr.cpp`, `space.cpp`, `cell_data.cpp` |
| `dbappmgr` | DBApp 注册、DBApp hash、启动协调 | `dbappmgr.cpp`, `dbapp.hpp` |
| `reviver` | 进程级监控和恢复请求 | `reviver.cpp`, `component_reviver.cpp` |

服务端源码的关键不是每个目录内部有什么类，而是这些目录之间通过哪些 interface 通信。例如 BaseApp 写 DB 会跨 `baseapp -> cellapp -> dbapp -> db_storage_mysql`，Cell 迁移会跨 `cellappmgr -> cellapp -> entity/real_entity/witness`。

## 接口定义层

接口头文件是跨进程源码分析的索引。

| 接口文件 | 注册进程 | 代表用途 |
|---|---|---|
| `server/baseapp/baseapp_int_interface.hpp` | BaseApp internal interface | BaseApp 间调用、Base/Cell 转发、备份 |
| `lib/connection/baseapp_ext_interface.hpp` | BaseApp external interface | 客户端连接 BaseApp 后的上行消息 |
| `server/cellapp/cellapp_interface.hpp` | CellApp | Cell 创建、实体方法、Ghost、Offload、Witness 请求 |
| `server/baseappmgr/baseappmgr_interface.hpp` | BaseAppMgr | BaseApp 注册、全局 base、负载与恢复 |
| `server/cellappmgr/cellappmgr_interface.hpp` | CellAppMgr | CellApp 注册、Cell/Space 管理、offload 控制 |
| `lib/db/dbapp_interface.hpp` | DBApp | 登录、实体读写、space 持久化 |
| `server/dbappmgr/dbappmgr_interface.hpp` | DBAppMgr | DBApp 注册与 hash 更新 |
| `lib/connection/login_interface.hpp` | LoginApp external interface | 登录、probe、挑战响应 |
| `server/loginapp/login_int_interface.hpp` | LoginApp internal interface | 内部关闭和状态协调 |
| `server/reviver/reviver_interface.hpp` | Reviver | 组件监控和恢复 |

验证一个跨进程流程时，先从 interface 文件确认消息定义，再用 `registerWithInterface()` 找接收进程，用 `startMessage()` 或 `startRequest()` 找发送方。

## 客户端与连接层

客户端通信分布在三处：

| 区域 | 作用 |
|---|---|
| `lib/connection` | 客户端侧连接协议、`ServerConnection`、Login/BaseApp/Client interface |
| `server/baseapp/proxy.cpp` | 服务端侧客户端连接代表，负责认证后 channel、切换 BaseApp、转发客户端方法 |
| `server/cellapp/entity.cpp` 和 `witness.cpp` | AOI、属性更新、实体方法和位置更新最终写入 `ClientInterface` |

不要把前端通信理解成普通 Web API。BigWorld 的客户端协议是 Mercury message，登录后由 `Proxy` 接管连接，运行期通过 `ClientInterface` 和 `BaseAppExtInterface` 双向通信。

## EntityDef 与数据库层

EntityDef 和数据库映射跨多个模块：

| 源码 | 作用 |
|---|---|
| `lib/entitydef/entity_description_map.cpp` | 解析 `entities.xml` 和 `.def` |
| `lib/entitydef/data_description.cpp` | 解析属性标记，如 `Persistent`、`Identifier`、`Indexed`、`DatabaseLength` |
| `server/baseapp/base.cpp` | `writeToDB` 入口，组装 base/cell 持久化流 |
| `server/dbapp/dbapp.cpp` | 接收 `DBAppInterface::writeEntity` |
| `server/dbapp/write_entity_handler.cpp` | 解析写入请求并调用 DB 后端 |
| `lib/db_storage/idatabase.hpp` | DB 后端抽象接口 |
| `lib/db_storage_mysql/mappings/` | 属性到 MySQL 表/列的映射 |

这条链说明：数据库 schema 不是独立建模层，而是由 EntityDef 的持久化属性和类型系统驱动。它与典型 ORM 的对象关系映射不同，保存触发点也不是自动 flush，而是显式 `writeToDB` 和登录/登出/备份流程。

## 工具与运维层

`programming/bigworld/server/tools` 和 `programming/bigworld/tools` 不只是辅助脚本。它们覆盖集群控制、日志、DB 同步、资源处理和编辑器流程。

| 目录 | 代表用途 |
|---|---|
| `server/tools/bwmachined` | 机器守护、进程发现和控制面基础 |
| `server/tools/message_logger` | 分布式日志收集和读取 |
| `server/tools/sync_db`, `transfer_db`, `consolidate_dbs` | 数据库运维和转换 |
| `tools/worldeditor`, `modeleditor`, `particle_editor` | 资源与编辑器工具 |
| `tools/assetprocessor`, `navgen` | 资源处理和导航数据生成 |

这些目录对源码分析的价值在于验证运行时假设：进程如何发现、日志如何聚合、DB 如何维护、资源如何进入客户端和服务端。

## 示例目录

`programming/fantasydemo` 和 `programming/bigworld/examples` 可用于理解引擎使用方式，但不应作为架构结论的唯一依据。架构结论应优先来自 `programming/bigworld/lib` 和 `programming/bigworld/server` 的核心源码。
