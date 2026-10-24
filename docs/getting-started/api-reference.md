# Core API Reference


<div class="arch-hero">

API reference for the BigWorld core libraries, covering five subsystems:
networking, entity definitions, script bindings, entities, and the database.
Every signature below has been checked entry by entry against the current
source and is annotated with `file:line` evidence; where it differs from
historical documentation, the source wins.

</div>

## Mercury Network API

Mercury is BigWorld's network stack and provides reliable UDP communication.
Its headers live under `lib/network/`.

### Core classes

#### `Mercury::NetworkInterface`

The network interface; manages all channels (`lib/network/network_interface.hpp`).

```cpp
namespace Mercury {

class NetworkInterface {
public:
    // Create an interface: dispatcher pointer + interface type +
    // optional listening port/NIC
    NetworkInterface( EventDispatcher * pMainDispatcher,
        NetworkInterfaceType interfaceType,
        uint16 listeningPort = 0,
        const char * listeningInterface = NULL );   // :65-67

    // Find a channel by address (returns UDPChannel*)
    UDPChannel * findChannel( const Address & addr, ... );  // :98

    // Send via the given address (the bundle type is UDPBundle)
    void send( const Address & address,
               UDPBundle & bundle, ... );           // :216
};

} // namespace Mercury
```

> Note: historical documents wrote `NetworkInterface(dispatcher, addr)` and
> `addHandler(MessageID, MessageHandler*)` — in the source the second
> constructor argument is a `NetworkInterfaceType` (not an address), and no
> `addHandler` exists anywhere in the tree. The real way handlers are
> registered is in "Message handlers" below.

#### `Mercury::Channel`

A logical channel wrapping a UDP connection (`lib/network/channel.hpp`).

```cpp
namespace Mercury {

class Channel {
public:
    // Send a bundle (the address is held by the channel itself)
    void send( Bundle * pBundle = NULL );           // :143

    // Peer address
    const Address & addr() const;                    // :125

    // Connection status
    bool isConnected() const;                        // :75

    // Set encryption (a BlockCipher smart pointer, not a StreamFilter)
    void setEncryption( BlockCipherPtr pBlockCipher ) = 0;  // :113
};

} // namespace Mercury
```

#### `Mercury::Bundle`

Message packer, inheriting `BinaryOStream` (`lib/network/bundle.hpp`).

```cpp
namespace Mercury {

class Bundle : public BinaryOStream {
public:
    // Start a message (the InterfaceElement carries the MessageID and
    // length-negotiation information)
    virtual void startMessage( const InterfaceElement & ie,
                               ... );               // :80

    // Templated send: the argument list is the on-the-wire format
    template <typename ArgsType>
    void sendMessage( const ArgsType & args, ... );  // from :127
};

} // namespace Bundle
```

> Note: the historical documents' `writeInt32()/writeString()/writeBlob()/finish()`
> do not exist in the source — writes go through the `BinaryOStream`
> streaming interface, and message boundaries are expressed by
> `startMessage/startRequest/startReply`.

### Message handlers

A handler is bound when its `InterfaceElement` is constructed
(`lib/network/interface_element.hpp`):

```cpp
// Handler base class
class InputMessageHandler;                    // :25

// The InterfaceElement constructor binds the handler
InterfaceElement( const char * name = "",
                 MessageID id = 0,
                 int8 lengthStyle = INVALID_MESSAGE,
                 int lengthParam = 0,
                 InputMessageHandler * pHandler = NULL );  // :90-92

InputMessageHandler * pHandler() const;        // :157

// Built-in no-op handler example
class EmptyMessageHandler
    : public Mercury::InputMessageHandler;     // common_message_handlers.hpp:28
```

### Usage example

```cpp
#include "network/network_interface.hpp"
#include "network/bundle.hpp"

// Create the network interface (interface type + listening port)
Mercury::NetworkInterface interface( &dispatcher,
                                     NETWORK_INTERFACE_INTERNAL,
                                     0, NULL );

// The message handler hangs on the InterfaceElement
class MyHandler : public Mercury::InputMessageHandler {
    virtual void handleMessage( const Mercury::Address & addr,
                                Mercury::UnpackedMessageHeader & header,
                                BinaryIStream & data ) {
        // handle the message
    }
};

MyHandler handler;
Mercury::InterfaceElement myElement( "MyMessage", MSG_MY_MESSAGE,
                                    Mercury::VARIABLE_LENGTH, 0,
                                    &handler );

// Send a message
Mercury::Bundle bundle;
bundle.startMessage( myElement, false );
bundle << 12345 << "hello";          // BinaryOStream streaming writes
interface.send( targetAddr, bundle );
```

## EntityDef API

EntityDef is the entity-definition system, managing entity properties and
methods. Its headers live under `lib/entitydef/`.

### Core classes

#### `EntityDescription`

An entity description, defining an entity type
(`lib/entitydef/entity_description.hpp`).

```cpp
class EntityDescription : public BaseUserDataObjectDescription {  // :127
public:
    // Parse an entity definition (by name)
    bool parse( const BW::string & name,
                const BW::string & componentName, ... );           // :134

    // Client type name / component visibility
    const BW::string & clientName() const;                          // :194
    bool canBeOnCell() const;                                      // :202
    bool canBeOnBase() const;                                      // :203
    bool canBeOnClient() const;                                    // :204
    bool isClientType() const;                                      // :211

    // Property iteration (client-server shared properties)
    unsigned int clientServerPropertyCount() const;                 // :225
    const DataDescription * clientServerProperty(
        unsigned int n ) const;                                     // :227
    // Find a property by name (inherited from BaseUserDataObjectDescription)
    DataDescription * findProperty(
        const BW::string & name ) const;   // base_user_data_object_description.hpp:55

    // Event/latest-change property counts and temp-property test
    unsigned int numEventStampedProperties() const;                 // :236
    unsigned int numLatestChangeOnlyMembers() const;                // :238
    bool isTempProperty( const BW::string & name ) const;           // :240

    // Service/persistence flags
    bool isService() const;                                         // :251
    bool isPersistent() const;                                      // :252
    bool forceExplicitDBID() const;                                 // :253

    // Visitor iteration (argument is a property description)
    virtual bool visit( const DataDescription & propDesc ) = 0;     // :57
};
```

#### `DataDescription`

A property description, inheriting `MemberDescription`
(`lib/entitydef/data_description.hpp`).

```cpp
class DataDescription : public MemberDescription {   // :77
public:
    // Property name (inherited from MemberDescription; returns BW::string)
    const BW::string & name() const;                 // member_description.hpp:50

    // Data type
    DataType * dataType();                           // :172
    const DataType * dataType() const;               // :173

    // Whether it is persisted to the database
    bool isPersistent() const;                       // :128
};
```

#### `DataType`

The data-type system (`lib/entitydef/data_type.hpp`).

```cpp
class DataType : public ReferenceCount {             // :39
public:
    // Serialization: data source -> stream
    bool addToStream( DataSource & source, ... );    // :100

    // Deserialization: stream -> object
    bool createFromStream( BinaryIStream & stream, ... );  // :103

    // Default-value output (via DataSink, not PyObject*)
    virtual bool getDefaultValue( DataSink & output ) const = 0;  // :87
};
```

> Note: the historical documents' `DataType::name()` and `defaultValue()`
> signatures do not match the source — `name()` is held on the property
> side (`MemberDescription`), and the default value is emitted through
> `getDefaultValue(DataSink&)`.

### Usage example

```cpp
#include "entitydef/entity_description.hpp"
#include "entitydef/data_type.hpp"

// Parse an entity definition by component name
// (EntityType holds the EntityDescription)
EntityDescription & desc = entityType->description();

// Iterate the client-server shared properties
for ( unsigned int i = 0; i < desc.clientServerPropertyCount(); i++ )
{
    const DataDescription * prop = desc.clientServerProperty( i );
    printf( "Property: %s\n", prop->name().c_str() );
}

// Find by name
const DataDescription * pHP =
    desc.findProperty( "hp" );
if ( pHP && pHP->dataType() )
{
    // stream read/write through DataType
}
```

## Script binding API

The script bindings live under `lib/script/` and are included through
`pyscript/script.hpp`.

### Core classes

#### `ScriptObject`

The script-object base class, wrapping `PyObject*`
(`lib/script/py_script_object.hpp`).

```cpp
class ScriptObject {
public:
    // Get the underlying PyObject* (borrowed semantics; no NULL check)
    PyObject * get() const;                          // :271 etc.

    // Whether it is None
    bool isNone() const;                             // :448

    // Read an attribute (template; an error handler must be supplied)
    template <class ERROR_HANDLER>
    ScriptObject getAttribute( const char * key,
        const ERROR_HANDLER & errorHandler ) const;  // :286

    // Write an attribute (template)
    template <class ERROR_HANDLER>
    bool setAttribute( const char * key,
        const ScriptObject & valueObj,
        const ERROR_HANDLER & errorHandler ) const;  // :398-401

    // Call a method (two templates: no args / with ScriptArgs)
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

> Note: the historical documents' `getAttr/setAttr` are named
> `getAttribute/setAttribute` in the source (and are templates requiring an
> error handler); the static factory `ScriptObject::create()` also does
> not exist.

#### `ScriptDict`

A dictionary object (`lib/script/py_script_dict.ipp`; class declaration in
`lib/script/oc_script_object.hpp:109`).

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

A list object (`lib/script/py_script_list.ipp`).

```cpp
class ScriptList : public ScriptObject {
public:
    bool append( const ScriptObject & object ) const;        // :19
    ScriptObject getItem( size_type pos ) const;             // :32
    bool setItem( size_type pos, ScriptObject item ) const;  // :46
    size_type size() const;                                  // :60
};
```

### Usage example

```cpp
#include "pyscript/script.hpp"

ScriptDict dict = ScriptDict::create();
dict.setItem( "name", ScriptObject( PyUnicode_FromString( "Alice" ) ) );
dict.setItem( "health", ScriptObject( PyLong_FromLong( 100 ) ) );

// Attribute read/write with an error handler
ScriptObject name = dict.getAttribute( "name",
    ScriptObject::ThrowOnError() );

// Call a method (args is a ScriptArgs)
ScriptArgs args;
args << 10;
ScriptObject result = obj.callMethod( "takeDamage", args,
    ScriptObject::ThrowOnError() );
if ( result.isNone() ) { /* the method returned None */ }
```

## Entity API

### `Entity`

Client entity base class, inheriting `BWEntity` (`client/entity.hpp:97`):

```cpp
class Entity : public BWEntity {              // client/entity.hpp:97
    // Entity type
    EntityType & type();                      // :149
    const EntityType & type() const;          // :150
    // Physics movement etc.
    void physicsMovement( double time,
        const Position3D & position,
        EntityID vehicleID,
        const Direction3D & direction );      // :170-171
};
```

On the script side an entity is wrapped as `PyEntity`
(`client/entity.hpp:53`), with the smart pointer
`PyEntityPtr = ScriptObjectPtr< PyEntity >` (`client/entity.hpp:54`).

### `Proxy`

The player proxy entity, inheriting `Base` (`server/baseapp/proxy.hpp`):

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

> Note: historical documents say `Entity : public ScriptObject`,
> `Proxy : public Entity`, and include `getPlayerData()` — in the source
> Entity inherits `BWEntity`, Proxy inherits `Base`, and `getPlayerData()`
> does not exist.

## Database API

### `IDatabase`

The database interface, **asynchronous callback style**
(`lib/db_storage/idatabase.hpp`):

```cpp
class IDatabase {
public:
    // Get an entity (EntityDBKey + callback)
    virtual void getEntity( const EntityDBKey & entityKey,
        ... ) = 0;                                // :109

    // Write an entity (EntityKey + callback)
    virtual void putEntity( const EntityKey & entityKey,
        ... ) = 0;                                // :221

    // Delete an entity
    virtual void delEntity( ... ) = 0;            // :231
};
```

> Note: all three methods return `void` and take completion callbacks
> (`IEntityGetHandler` / `IEntityPutHandler` / `IEntityDelHandler`); the
> synchronously-returning `Entity*` signatures in historical documents do
> not match the source.

## References

- [Source code guide](/analysis/source-code-guide)
- [Mercury reliable UDP](/architecture/mercury-reliable-udp)
- [Communication abstraction and RPC](/architecture/communication-rpc)
- [Serialization and EntityDef](/architecture/serialization-entitydef)
