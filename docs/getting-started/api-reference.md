# 核心 API 参考

<div class="arch-hero">

BigWorld 核心库的 API 参考，覆盖网络、实体、脚本三大子系统。

</div>

## Mercury 网络 API

Mercury 是 BigWorld 的网络栈，提供可靠的 UDP 通信。

### 核心类

#### `Mercury::NetworkInterface`

网络接口，管理所有连接。

```cpp
namespace Mercury {

class NetworkInterface {
public:
    // 创建接口
    NetworkInterface(EventDispatcher& dispatcher, 
                     const Address& addr);
    
    // 发送数据
    void send(const Address& addr, Bundle& bundle);
    
    // 注册处理器
    void addHandler(MessageID id, MessageHandler* handler);
    
    // 获取通道
    Channel* findChannel(const Address& addr);
};

} // namespace Mercury
```

#### `Mercury::Channel`

逻辑通道，封装 UDP 连接。

```cpp
namespace Mercury {

class Channel {
public:
    // 发送 Bundle
    void send(Bundle* bundle);
    
    // 获取地址
    const Address& addr() const;
    
    // 检查连接状态
    bool isConnected() const;
    
    // 设置加密
    void setEncryption(StreamFilterPtr pFilter);
};

} // namespace Mercury
```

#### `Mercury::Bundle`

消息打包器。

```cpp
namespace Mercury {

class Bundle {
public:
    // 开始消息
    void startMessage(MessageID id);
    
    // 写入数据
    void writeInt32(int32 value);
    void writeString(const char* str);
    void writeBlob(const void* data, int size);
    
    // 完成消息
    void finish();
};

} // namespace Mercury
```

### 使用示例

```cpp
#include "network/network_interface.hpp"
#include "network/bundle.hpp"

// 创建网络接口
Mercury::NetworkInterface interface(dispatcher, Mercury::Address(0, 0));

// 注册消息处理器
class MyHandler : public Mercury::MessageHandler {
    virtual void handleMessage(const Mercury::Address& addr,
                               Mercury::UnpackedMessageHeader& header,
                               BinaryIStream& data) {
        // 处理消息
    }
};

MyHandler handler;
interface.addHandler(MSG_MY_MESSAGE, &handler);

// 发送消息
Mercury::Bundle bundle;
bundle.startMessage(MSG_MY_MESSAGE);
bundle.writeInt32(12345);
bundle.writeString("hello");
bundle.finish();
interface.send(targetAddr, bundle);
```

## EntityDef API

EntityDef 是实体定义系统，管理实体属性和方法。

### 核心类

#### `EntityDescription`

实体描述，定义实体类型。

```cpp
class EntityDescription {
public:
    // 获取属性数量
    int numProperties() const;
    
    // 获取属性描述
    const DataDescription* property(int index) const;
    
    // 查找属性
    const DataDescription* findProperty(const char* name) const;
    
    // 获取方法数量
    int numMethods() const;
    
    // 获取方法描述
    const MethodDescription* method(int index) const;
};
```

#### `DataDescription`

属性描述。

```cpp
class DataDescription {
public:
    // 获取属性名
    const char* name() const;
    
    // 获取数据类型
    DataType* dataType() const;
    
    // 获取详情类型
    DataDetail detail() const;
    
    // 是否为客户端属性
    bool isClientType() const;
    
    // 是否为数据库属性
    bool isPersistent() const;
};
```

#### `DataType`

数据类型系统。

```cpp
class DataType {
public:
    // 获取类型名
    const char* name() const;
    
    // 序列化
    void addToStream(PyObject* obj, BinaryOStream& stream);
    
    // 反序列化
    PyObject* createFromStream(BinaryIStream& stream);
    
    // 默认值
    PyObject* defaultValue() const;
};
```

### 使用示例

```cpp
#include "entitydef/entity_description.hpp"
#include "entitydef/data_type.hpp"

// 获取实体描述
EntityDescription& desc = EntityDescription::getDescription("Avatar");

// 遍历属性
for (int i = 0; i < desc.numProperties(); i++) {
    const DataDescription* prop = desc.property(i);
    printf("Property: %s, Type: %s\n", 
           prop->name(), 
           prop->dataType()->name());
}

// 创建实体
PyObject* entity = desc.createEntity();
```

## 脚本绑定 API

### 核心类

#### `ScriptObject`

脚本对象基类。

```cpp
class ScriptObject {
public:
    // 获取 Python 对象
    PyObject* get() const;
    
    // 检查是否为空
    bool isNone() const;
    
    // 获取属性
    ScriptObject getAttr(const char* name) const;
    
    // 设置属性
    bool setAttr(const char* name, PyObject* value);
    
    // 调用方法
    ScriptObject callMethod(const char* name, 
                           PyObject* args = NULL);
};
```

#### `ScriptDict`

字典对象。

```cpp
class ScriptDict : public ScriptObject {
public:
    // 创建字典
    static ScriptDict create();
    
    // 设置值
    bool setItem(const char* key, PyObject* value);
    
    // 获取值
    PyObject* getItem(const char* key) const;
    
    // 检查键
    bool hasKey(const char* key) const;
};
```

#### `ScriptList`

列表对象。

```cpp
class ScriptList : public ScriptObject {
public:
    // 创建列表
    static ScriptList create(int size = 0);
    
    // 设置值
    bool setItem(int index, PyObject* value);
    
    // 获取值
    PyObject* getItem(int index) const;
    
    // 追加值
    bool append(PyObject* value);
    
    // 获取大小
    int size() const;
};
```

### 使用示例

```cpp
#include "pyscript/script.hpp"

// 创建脚本对象
ScriptObject obj = ScriptObject::create();

// 设置属性
obj.setAttr("name", PyUnicode_FromString("Alice"));
obj.setAttr("health", PyLong_FromLong(100));

// 调用方法
ScriptObject result = obj.callMethod("takeDamage", 
    PyLong_FromLong(10));

// 检查结果
if (result.isNone()) {
    printf("Method returned None\n");
}
```

## 实体 API

### `Entity`

游戏实体基类。

```cpp
class Entity : public ScriptObject {
public:
    // 获取实体 ID
    EntityID id() const;
    
    // 获取实体类型
    const char* type() const;
    
    // 获取位置
    const Position3D& position() const;
    
    // 设置位置
    void setPosition(const Position3D& pos);
    
    // 获取属性
    PyObject* getAttr(const char* name) const;
    
    // 设置属性
    bool setAttr(const char* name, PyObject* value);
};
```

### `Proxy`

玩家代理实体。

```cpp
class Proxy : public Entity {
public:
    // 获取客户端地址
    const Mercury::Address& clientAddr() const;
    
    // 发送到客户端
    void sendToClient(Bundle& bundle);
    
    // 获取玩家数据
    PyObject* getPlayerData() const;
};
```

## 数据库 API

### `IDatabase`

数据库接口。

```cpp
class IDatabase {
public:
    // 获取实体
    virtual void getEntity(const char* entityType,
                          DatabaseID dbID,
                          IEntityGetHandler* handler) = 0;
    
    // 写入实体
    virtual void putEntity(const char* entityType,
                          DatabaseID dbID,
                          PyObject* entity,
                          IEntityPutHandler* handler) = 0;
    
    // 删除实体
    virtual void delEntity(const char* entityType,
                          DatabaseID dbID,
                          IEntityDelHandler* handler) = 0;
};
```

## 参考资料

- [源码导读](/analysis/source-code-guide)
- [Mercury 可靠 UDP](/architecture/mercury-reliable-udp)
- [通信抽象与 RPC](/architecture/communication-rpc)
- [序列化与 EntityDef](/architecture/serialization-entitydef)
