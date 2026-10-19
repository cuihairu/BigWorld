# 核心 API 参考

> [English](api-reference.en.md) | **中文**

<div class="arch-hero">

BigWorld 核心库的 API 参考，覆盖网络、实体定义、脚本绑定、实体、数据库五大子系统。
以下所有签名均按当前源码逐条核对，并标注 `file:line` 证据；与历史版本有出入处以源码为准。

</div>

## Mercury 网络 API

Mercury 是 BigWorld 的网络栈，提供可靠的 UDP 通信。头文件位于 `lib/network/`。

### 核心类

#### `Mercury::NetworkInterface`

网络接口，管理所有通道（`lib/network/network_interface.hpp`）。

```cpp
namespace Mercury {

class NetworkInterface {
public:
    // 创建接口：分发器指针 + 接口类型 + 可选监听端口/网卡
    NetworkInterface( EventDispatcher * pMainDispatcher,
        NetworkInterfaceType interfaceType,
        uint16 listeningPort = 0,
        const char * listeningInterface = NULL );   // :65-67

    // 按地址查找通道（返回 UDPChannel*）
    UDPChannel * findChannel( const Address & addr, ... );  // :98

    // 经指定地址发送（bundle 类型为 UDPBundle）
    void send( const Address & address,
               UDPBundle & bundle, ... );           // :216
};

} // namespace Mercury
```

> 注意：历史文档曾写 `NetworkInterface(dispatcher, addr)` 与
> `addHandler(MessageID, MessageHandler*)`——源码中构造第二参数是
> `NetworkInterfaceType`（不是地址），且全树不存在 `addHandler`。
> 处理器的真实注册方式见下文「消息处理器」。

#### `Mercury::Channel`

逻辑通道，封装 UDP 连接（`lib/network/channel.hpp`）。

```cpp
namespace Mercury {

class Channel {
public:
    // 发送 Bundle（地址由通道自身持有）
    void send( Bundle * pBundle = NULL );           // :143

    // 获取对端地址
    const Address & addr() const;                    // :125

    // 检查连接状态
    bool isConnected() const;                        // :75

    // 设置加密（BlockCipher 智能指针，非 StreamFilter）
    void setEncryption( BlockCipherPtr pBlockCipher ) = 0;  // :113
};

} // namespace Mercury
```

#### `Mercury::Bundle`

消息打包器，继承 `BinaryOStream`（`lib/network/bundle.hpp`）。

```cpp
namespace Mercury {

class Bundle : public BinaryOStream {
public:
    // 开始消息（InterfaceElement 携带 MessageID 与长度协商信息）
    virtual void startMessage( const InterfaceElement & ie,
                               ... );               // :80

    // 模板化发送：参数列表即线上格式
    template <typename ArgsType>
    void sendMessage( const ArgsType & args, ... );  // :127 起
};

} // namespace Bundle
```

> 注意：历史文档的 `writeInt32()/writeString()/writeBlob()/finish()`
> 在源码中不存在——写入走 `BinaryOStream` 的流式接口，消息边界由
> `startMessage/startRequest/startReply` 表达。

### 消息处理器

处理器通过 `InterfaceElement` 构造时绑定（`lib/network/interface_element.hpp`）：

```cpp
// 处理器基类
class InputMessageHandler;                    // :25

// InterfaceElement 构造即绑定处理器
InterfaceElement( const char * name = "",
                 MessageID id = 0,
                 int8 lengthStyle = INVALID_MESSAGE,
                 int lengthParam = 0,
                 InputMessageHandler * pHandler = NULL );  // :90-92

InputMessageHandler * pHandler() const;        // :157

// 内置空处理器示例
class EmptyMessageHandler
    : public Mercury::InputMessageHandler;     // common_message_handlers.hpp:28
```

### 使用示例

```cpp
#include "network/network_interface.hpp"
#include "network/bundle.hpp"

// 创建网络接口（接口类型 + 监听端口）
Mercury::NetworkInterface interface( &dispatcher,
                                     NETWORK_INTERFACE_INTERNAL,
                                     0, NULL );

// 消息处理器挂在 InterfaceElement 上
class MyHandler : public Mercury::InputMessageHandler {
    virtual void handleMessage( const Mercury::Address & addr,
                                Mercury::UnpackedMessageHeader & header,
                                BinaryIStream & data ) {
        // 处理消息
    }
};

MyHandler handler;
Mercury::InterfaceElement myElement( "MyMessage", MSG_MY_MESSAGE,
                                    Mercury::VARIABLE_LENGTH, 0,
                                    &handler );

// 发送消息
Mercury::Bundle bundle;
bundle.startMessage( myElement, false );
bundle << 12345 << "hello";          // BinaryOStream 流式写入
interface.send( targetAddr, bundle );
```

## EntityDef API

EntityDef 是实体定义系统，管理实体属性和方法。头文件位于 `lib/entitydef/`。

### 核心类

#### `EntityDescription`

实体描述，定义实体类型（`lib/entitydef/entity_description.hpp`）。

```cpp
class EntityDescription : public BaseUserDataObjectDescription {  // :127
public:
    // 解析实体定义（按名）
    bool parse( const BW::string & name,
                const BW::string & componentName, ... );           // :134

    // 客户端类型名 / 组件可见性
    const BW::string & clientName() const;                          // :194
    bool canBeOnCell() const;                                      // :202
    bool canBeOnBase() const;                                      // :203
    bool canBeOnClient() const;                                    // :204
    bool isClientType() const;                                      // :211

    // 属性遍历（客户端-服务端共有属性）
    unsigned int clientServerPropertyCount() const;                 // :225
    const DataDescription * clientServerProperty(
        unsigned int n ) const;                                     // :227
    // 按名查找属性（继承自 BaseUserDataObjectDescription）
    DataDescription * findProperty(
        const BW::string & name ) const;   // base_user_data_object_description.hpp:55

    // 事件/变更属性计数与临时属性判定
    unsigned int numEventStampedProperties() const;                 // :236
    unsigned int numLatestChangeOnlyMembers() const;                // :238
    bool isTempProperty( const BW::string & name ) const;           // :240

    // 服务/持久化标志
    bool isService() const;                                         // :251
    bool isPersistent() const;                                      // :252
    bool forceExplicitDBID() const;                                 // :253

    // 访问者遍历（参数为属性描述）
    virtual bool visit( const DataDescription & propDesc ) = 0;     // :57
};
```

#### `DataDescription`

属性描述，继承 `MemberDescription`（`lib/entitydef/data_description.hpp`）。

```cpp
class DataDescription : public MemberDescription {   // :77
public:
    // 属性名（继承自 MemberDescription，返回 BW::string）
    const BW::string & name() const;                 // member_description.hpp:50

    // 数据类型
    DataType * dataType();                           // :172
    const DataType * dataType() const;               // :173

    // 是否持久化到数据库
    bool isPersistent() const;                       // :128
};
```

#### `DataType`

数据类型系统（`lib/entitydef/data_type.hpp`）。

```cpp
class DataType : public ReferenceCount {             // :39
public:
    // 序列化：数据源 -> 流
    bool addToStream( DataSource & source, ... );    // :100

    // 反序列化：流 -> 对象
    bool createFromStream( BinaryIStream & stream, ... );  // :103

    // 默认值输出（经 DataSink，非 PyObject*）
    virtual bool getDefaultValue( DataSink & output ) const = 0;  // :87
};
```

> 注意：历史文档的 `DataType::name()` 与 `defaultValue()` 签名与源码
> 不符——`name()` 由属性侧（`MemberDescription`）持有，默认值经
> `getDefaultValue(DataSink&)` 输出。

### 使用示例

```cpp
#include "entitydef/entity_description.hpp"
#include "entitydef/data_type.hpp"

// 按组件名解析实体定义（EntityType 持有 EntityDescription）
EntityDescription & desc = entityType->description();

// 遍历客户端-服务端共有属性
for ( unsigned int i = 0; i < desc.clientServerPropertyCount(); i++ )
{
    const DataDescription * prop = desc.clientServerProperty( i );
    printf( "Property: %s\n", prop->name().c_str() );
}

// 按名查找
const DataDescription * pHP =
    desc.findProperty( "hp" );
if ( pHP && pHP->dataType() )
{
    // 经 DataType 做流式读写
}
```

## 脚本绑定 API

脚本绑定位于 `lib/script/`，经 `pyscript/script.hpp` 引入。

### 核心类

#### `ScriptObject`

脚本对象基类，封装 `PyObject*`（`lib/script/py_script_object.hpp`）。

```cpp
class ScriptObject {
public:
    // 取底层 PyObject*（借用语义，不做 NULL 检查）
    PyObject * get() const;                          // :271 等

    // 是否为 None
    bool isNone() const;                             // :448

    // 读属性（模板，需传入错误处理器）
    template <class ERROR_HANDLER>
    ScriptObject getAttribute( const char * key,
        const ERROR_HANDLER & errorHandler ) const;  // :286

    // 写属性（模板，需传入错误处理器）
    template <class ERROR_HANDLER>
    bool setAttribute( const char * key,
        const ScriptObject & valueObj,
        const ERROR_HANDLER & errorHandler ) const;  // :398-401

    // 调用方法（无参 / 带 ScriptArgs 两个模板）
    template <class ERROR_HANDLER>
    ScriptObject callMethod( const char * methodName,
        const ERROR_HANDLER & errorHandler,
        bool allowNullMethod = false ) const;       // :405-408

    template <class ERROR_HANDLER>
    ScriptObject callMethod( const char * methodName,
        const ScriptArgs & args,
        const ERROR_HANDLER & errorHandler,
        bool allowNullMethod = false ) const;       // :410-413
};

class ScriptArgs : public ScriptObject { ... };      // :756
```

> 注意：历史文档的 `getAttr/setAttr` 在源码中名为
> `getAttribute/setAttribute`（且为需错误处理器的模板）；
> `ScriptObject::create()` 静态工厂亦不存在。

#### `ScriptDict`

字典对象（`lib/script/py_script_dict.ipp`，类声明 `lib/script/oc_script_object.hpp:109`）。

```cpp
class ScriptDict : public ScriptObject {
public:
    static ScriptDict create( int capacity = 0 );     // :7

    bool setItem( const char * key,
                  const ScriptObject & item );        // :23
    bool setItem( const ScriptObject & key,
                  const ScriptObject & item );        // :41

    ScriptObject getItem( const char * key,
        ... ) const;                                  // :57
    ScriptObject getItem( const ScriptObject & key,
        ... ) const;                                  // :88
};
```

#### `ScriptList`

列表对象（`lib/script/py_script_list.ipp`）。

```cpp
class ScriptList : public ScriptObject {
public:
    bool append( const ScriptObject & object ) const;        // :19
    ScriptObject getItem( size_type pos ) const;             // :32
    bool setItem( size_type pos, ScriptObject item ) const;  // :46
    size_type size() const;                                  // :60
};
```

### 使用示例

```cpp
#include "pyscript/script.hpp"

ScriptDict dict = ScriptDict::create();
dict.setItem( "name", ScriptObject( PyUnicode_FromString( "Alice" ) ) );
dict.setItem( "health", ScriptObject( PyLong_FromLong( 100 ) ) );

// 带错误处理器的属性读写
ScriptObject name = dict.getAttribute( "name",
    ScriptObject::ThrowOnError() );

// 调用方法（args 为 ScriptArgs）
ScriptArgs args;
args << 10;
ScriptObject result = obj.callMethod( "takeDamage", args,
    ScriptObject::ThrowOnError() );
if ( result.isNone() ) { /* 方法返回 None */ }
```

## 实体 API

### `Entity`

客户端实体基类，继承 `BWEntity`（`client/entity.hpp:97`）：

```cpp
class Entity : public BWEntity {              // client/entity.hpp:97
    // 实体类型
    EntityType & type();                      // :149
    const EntityType & type() const;          // :150
    // 物理移动等
    void physicsMovement( double time,
        const Position3D & position,
        EntityID vehicleID,
        const Direction3D & direction );      // :170-171
};
```

脚本侧实体封装为 `PyEntity`（`client/entity.hpp:53`），智能指针
`PyEntityPtr = ScriptObjectPtr< PyEntity >`（`client/entity.hpp:54`）。

### `Proxy`

玩家代理实体，继承 `Base`（`server/baseapp/proxy.hpp`）：

```cpp
class Proxy : public Base {                   // server/baseapp/proxy.hpp:74
public:
    Proxy( EntityID id, DatabaseID dbID,
           EntityType * pType );               // :79-80

    const Mercury::Address & clientAddr() const;  // :120
    void sendToClient();                          // :184
    bool attachToClient(
        const Mercury::Address & clientAddr, ... );  // :136
};
```

> 注意：历史文档称 `Entity : public ScriptObject`、
> `Proxy : public Entity` 且含 `getPlayerData()`——源码中 Entity 继承
> `BWEntity`、Proxy 继承 `Base`，且不存在 `getPlayerData()`。

## 数据库 API

### `IDatabase`

数据库接口，**异步回调式**（`lib/db_storage/idatabase.hpp`）：

```cpp
class IDatabase {
public:
    // 取实体（EntityDBKey + 回调）
    virtual void getEntity( const EntityDBKey & entityKey,
        ... ) = 0;                                // :109

    // 写实体（EntityKey + 回调）
    virtual void putEntity( const EntityKey & entityKey,
        ... ) = 0;                                // :221

    // 删实体
    virtual void delEntity( ... ) = 0;            // :231
};
```

> 注意：三个方法均为 `void` 返回 + 完成回调
> （`IEntityGetHandler` / `IEntityPutHandler` / `IEntityDelHandler`），
> 历史文档所写的同步返回 `Entity*` 签名与源码不符。

## 参考资料

- [源码导读](/analysis/source-code-guide)
- [Mercury 可靠 UDP](/architecture/mercury-reliable-udp)
- [通信抽象与 RPC](/architecture/communication-rpc)
- [序列化与 EntityDef](/architecture/serialization-entitydef)
