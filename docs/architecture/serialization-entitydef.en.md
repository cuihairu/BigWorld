# Serialisation and EntityDef

> **English** | [中文](serialization-entitydef.md)

<div class="arch-hero">

BigWorld's serialisation is not an isolated `serialize()` utility function. It is
the glue between the EntityDef type system, Python script objects, network
Bundles, database persistence and hot-reload migration. Only by understanding
it can you judge where a given property or method argument actually ends up:
client, Base, Cell, Ghost, DB or migration stream.

</div>

## Conclusion First

BigWorld's serialisation chain has three layers:

1. `BinaryOStream` / `BinaryIStream` define the lowest-level binary read/write
   interface.
2. `DataType` / `DataDescription` / `MethodArgs` write properties and method
   arguments into a stream according to the EntityDef type system.
3. `EntityDescription` organises entity property streams by data domains such as
   Base, Cell, Client and Persistent.

Source entries:

- `BinaryOStream` / `BinaryIStream` at
  [binary_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/binary_stream.hpp:24)
  and
  [binary_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/binary_stream.hpp:64).
- `MemoryOStream` / `MemoryIStream` at
  [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:27)
  and
  [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:73).
- `DataType::addToStream()` at
  [data_type.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_type.cpp:93).
- `DataDescription::addToStream()` at
  [data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:374).
- `EntityDescription::addToStream()` at
  [entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:1472).
- `MethodArgs::addToStream()` and `createFromStream()` at
  [method_args.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:213)
  and
  [method_args.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:295).

## Serialisation Is Not Only Networking

BigWorld's EntityDef type system serves all of the following at once:

- Internal C++ engine objects.
- Python script properties and method arguments.
- Client/server network synchronisation.
- Base/Cell entity migration.
- Database persistence.
- Default values, type checking and script hot-reload migration.

So it is not simply a "network message schema". It is closer to a unified type
contract for the game runtime.

<MermaidDiagram title="Where EntityDef serialisation sits">
flowchart TD
  A[EntityDef XML] --> B[EntityDescription]
  B --> C[DataDescription]
  C --> D[DataType]
  D --> E[BinaryOStream/BinaryIStream]
  E --> F[Mercury Bundle]
  E --> G[DB persistence]
  E --> H[Base/Cell migration]
  E --> I[Client property sync]
  J[Python ScriptObject] --> C
  C --> K[DataSource/DataSink]
</MermaidDiagram>

## The BinaryStream Layer

**Overview:** `BinaryOStream` / `BinaryIStream` are the lowest-level binary
read/write interface. They do not care about what the data means; they only read
and write the byte stream efficiently. Everything else in serialisation builds
on this.

**Source entry:** [binary_stream.hpp:24](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/binary_stream.hpp:24)

```cpp
// binary_stream.hpp:24 - BinaryOStream interface
class BinaryOStream
{
public:
    virtual ~BinaryOStream() {}

    // Core method: reserve n bytes of space and return a writable pointer
    virtual void * reserve( int nBytes ) = 0;

    // Add a block of data (declaration only, binary_stream.hpp:41)
    virtual void addBlob( const void * pBlob, int size );

    // Move data from an input stream (:40)
    void transfer( BinaryIStream & from, int length );

    // Write a packed integer (:47, implemented at :254)
    inline void writePackedInt( int len );

    // Write a string length (:48, internally writePackedInt)
    void writeStringLength( int len )	{ this->writePackedInt( len ); }

    // Add primitive types
    BinaryOStream & operator<<( bool value );
    BinaryOStream & operator<<( int8 value );
    BinaryOStream & operator<<( uint8 value );
    BinaryOStream & operator<<( int16 value );
    BinaryOStream & operator<<( uint16 value );
    BinaryOStream & operator<<( int32 value );
    BinaryOStream & operator<<( uint32 value );
    BinaryOStream & operator<<( int64 value );
    BinaryOStream & operator<<( uint64 value );
    BinaryOStream & operator<<( float value );
    BinaryOStream & operator<<( double value );
};
```

```cpp
// binary_stream.hpp:64 - BinaryIStream interface
class BinaryIStream
{
public:
    virtual ~BinaryIStream() {}

    // Core method: take out n bytes of data
    virtual const void * retrieve( int nBytes ) = 0;

    // Remaining length
    virtual int remainingLength() const = 0;

    // Read a packed integer (:89)
    inline int readPackedInt();

    // Error state (:92, not virtual)
    bool error() const { return error_; }

    // Read primitive types (operator>> is a free inline function and macro,
    // :309/:329/:407)
    BinaryIStream & operator>>( bool & value );
    BinaryIStream & operator>>( int8 & value );
    BinaryIStream & operator>>( uint8 & value );
    BinaryIStream & operator>>( int16 & value );
    BinaryIStream & operator>>( uint16 & value );
    BinaryIStream & operator>>( int32 & value );
    BinaryIStream & operator>>( uint32 & value );
    BinaryIStream & operator>>( int64 & value );
    BinaryIStream & operator>>( uint64 & value );
    BinaryIStream & operator>>( float & value );
    BinaryIStream & operator>>( double & value );
};
```

**Flowchart:**

<MermaidDiagram title="BinaryStream read/write flow">
sequenceDiagram
    participant Writer as Writer
    participant Stream as BinaryOStream
    participant Buffer as Memory buffer
    participant Reader as Reader

    Writer->>Stream: reserve(nBytes)
    Stream->>Buffer: allocate space
    Buffer->>Writer: return a writable pointer
    Writer->>Buffer: fill in the data
    
    Writer->>Stream: operator<<(value)
    Stream->>Stream: reserve(sizeof(value))
    Stream->>Buffer: write data
    
    Reader->>Stream: retrieve(nBytes)
    Stream->>Buffer: read data
    Buffer->>Reader: return a data pointer
    
    Reader->>Stream: operator>>(value)
    Stream->>Stream: retrieve(sizeof(value))
    Stream->>Reader: return the value
</MermaidDiagram>

**Detailed walkthrough:**

1. **The `reserve()` pattern**: this is the most efficient way to write. The
   caller gets a writable pointer and fills in the data directly, avoiding an
   extra copy.

2. **`addBlob()`**: writes a block of binary data in one go; internally it calls
   `reserve()` + `memcpy()`.

3. **`transfer()`**: moves data from an input stream, avoiding an intermediate
   buffer. Commonly used for message forwarding.

4. **`writePackedInt()`**: packed integer encoding, where small integers take
   fewer bytes. Used for `EntityID`, `MethodIndex` and the like.

5. **Error handling**: `BinaryIStream::error()` records the stream's error
   state, preventing out-of-bounds reads.

**Why not std::stream:**

- `BinaryOStream` / `BinaryIStream` are lighter weight, with no formatting or
  locale overhead
- They operate on memory directly, avoiding buffer copies
- They can be embedded straight into a Mercury Bundle and sent with zero copy
- A game protocol does not need to be self-describing, it only needs efficient
  read and write

### The MemoryStream Layer

**Overview:** `MemoryOStream` inherits from `BinaryOStream`, `BinaryIStream`
and `SafeAllocatable` at the same time, so it can be written into in memory and
then read back out as an input stream. This is the basis of in-process message
passing.

**Source entry:** [memory_stream.hpp:27](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:27)

```cpp
// memory_stream.hpp:27 - MemoryOStream implementation
class MemoryOStream : public BinaryOStream, public BinaryIStream,
    public SafeAllocatable
{
public:
    MemoryOStream( int size = 64 );
    virtual ~MemoryOStream();

    // Write implementation
    virtual void * reserve( int nBytes );

    // Read implementation
    virtual const void * retrieve( int nBytes );

    // Remaining length
    virtual int remainingLength() const;

    // Reset
    void reset();

    // Get the data pointer
    void * data();
    int size() const;

protected:
    char * pBegin_;
    char * pCurr_;
    char * pEnd_;
    char * pRead_;
    bool shouldDelete_;
    bool canRewind_;
};
```

**Typical uses:**

1. **Message assembly**: write the message header, then the message body, then
   send the whole thing at once
2. **Temporary buffering**: assemble a complex data structure in memory, then
   write it to persistent storage
3. **Test support**: simulate a network stream for unit tests

**Key details:**

- `reserve()` grows the buffer dynamically, avoiding an over-large
  pre-allocation
- `retrieve()` advances the read cursor, supporting sequential reads
- `reset()` resets the cursor, so the buffer can be reused
- `data()` and `size()` give you the final data, for writing to a file or to the
  network

- Write variable-length data into a temporary buffer first, then compute the
  length.
- Build a sub-stream, then write it back into the parent stream.
- Do a round-trip in unit tests.

`MemoryIStream` wraps an existing block of memory instead, and is used to read
from a packet, a database blob or test data.

## DataType: How a Type Is Written to a Stream

`DataType::addToStream()` takes a `DataSource` and a `BinaryOStream`, and writes
the type's `StreamElement`s one by one. Source:
[data_type.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_type.cpp:93).

One key detail is the sub-stream:

- On `isSubstreamStart()`, a new `MemoryOStream` is created.
- The current write target switches to the innermost sub-stream.
- On `isSubstreamEnd()`, the sub-stream's length and contents are written back
  into the parent stream.

This means BigWorld's type system has to handle nested variable-length
structures, such as arrays, dictionaries, class objects and optional values. It
is not a memcpy of a C struct's memory layout.

## DataDescription: Property Semantics

`DataDescription` is not merely a data type. It also carries the semantics of
which data domains a property belongs to, whether it is client-server data,
whether it is persistent, and the variable-length header.

`DataDescription::addToStream()` has one important branch; see
[data_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/data_description.cpp:374):

- If the property is client-server data and the stream size is variable-length.
- Write into a temporary `MemoryOStream lengthStream` first.
- Check whether it overflows.
- Then `transfer` the temporary stream into the real output stream.

This is a defensive boundary for the game network: client-facing variable-length
properties must not be allowed to grow without limit, so the length is checked at
encode time.

## EntityDescription: Organising Entity Streams by Data Domain

`EntityDescription::addToStream()` walks the entity's properties and selects
which ones to write according to `dataDomains` and the pass rules. Source:
[entity_description.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:1472).

Its job is not to "serialise the whole object"; it emits a different view per
scenario:

- Creating a Base entity needs Base/Persistent data.
- Creating a Cell entity needs Cell data.
- Client sync needs only client-visible data.
- A database save may need only persistent data.
- Entity migration needs the data organised per the Base/Cell migration protocol.

This is the core value of BigWorld's EntityDef: one entity definition can give
rise to many runtime data streams.

## MethodArgs: Method Arguments

`MethodArgs::addToStream()` writes the method arguments into the stream in their
defined order; see
[method_args.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:213).

It will:

- Read the sequence starting from the `DataSource`.
- If there are not enough arguments, try to write the default value and record
  an error.
- For each argument, call the corresponding `DataType::addToStream()`.
- Return whether it all succeeded.

`MethodArgs::createFromStream()` reads the arguments back out of a
`BinaryIStream` and writes them into a `DataSink`; see
[method_args.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:295).

This chain connects:

<div class="flow-strip">
  <span class="flow-node">Python method call</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">MethodArgs</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataType</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BinaryStream</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Mercury Bundle</span>
</div>

## DataSource and DataSink

In the source, `DataType`, `DataDescription` and `MethodArgs` do not depend
directly on a single Python object interface; they read and write through the
`DataSource` / `DataSink` abstractions.

The point of this design:

- The same type logic can read from a Python object, an XML/DataSection, a
  database result or some other source.
- A deserialisation target can equally be a Python object, a dictionary, a test
  sink or a database structure.
- The type system is decoupled from the concrete data source.

This is a DIP-compliant design: the serialisation logic depends on an abstract
data source rather than branching on Python/XML/DB concrete types everywhere.

The cost is a longer call chain, so a newcomer reading a single function cannot
easily see where the final object is actually read from or written to.

## Wire Format Characteristics

BigWorld's serialisation format leans towards a "compact binary stream with a
shared schema" rather than "self-describing messages".

Characteristics:

- Field order is decided by EntityDef and MethodArgs.
- Types are decided by the definition shared by both sides.
- Variable-length fields use a length header or a packed int.
- Fixed-length fields can be encoded directly by type.
- Stream errors propagate through `BinaryIStream::error_` and return values.
- Protocol compatibility depends on entity definitions, interface definitions
  and MD5/version checks.

Advantages:

- Compact encoding.
- Low overhead on high-frequency paths.
- The type system is deeply bound to the engine runtime.

Disadvantages:

- No field tags, so inserting or deleting a field needs a strict compatibility
  strategy.
- A packet capture is hard to parse standalone without the schema.
- A weak cross-language toolchain.
- Error handling depends on callers checking return values; a corrupted stream
  may only surface at the far end.

## Relationship to Hot Reload

The hot reload chapter analyses `CellApp::reloadScript()` and `Entity::migrate()`
in detail; here we only explain why serialisation affects hot reload.

An entity hot reload is not just replacing a Python class:

- `EntityType` may change.
- UDO types may change.
- Mailbox types may change.
- Live entities need to be migrated onto the new class.
- The stream format of properties and method arguments must still be understood
  by both the old and the new side.

If EntityDef or DataType changes incompatibly, hot reload escalates from a "class
migration problem" into a "protocol and state migration problem". This is one of
the fundamental reasons the source stays cautious about production hot reload.

## Trade-offs in the Source

BigWorld's serialisation takes on wider duties than ordinary network message
encoding:

- It has to serve Python script objects and the C++ Entity runtime.
- It has to select different property sets per Base/Cell/Client/Persistent data
  domain.
- It has to support default values, type checking, method arguments and property
  sync.
- It has to work together with Mercury Bundles, reliable UDP and entity
  migration.
- It also has to let the DB mapping layer reconstruct database columns from the
  same persistent stream.

The source cost:

- BinaryStream is not self-describing on its own, so both sides must share
  EntityDef, DataType and field order.
- Python object conversion, default values, error stream state and complex
  nested types all affect the same chain.
- An EntityDef change affects networking, the DB, hot reload and migration at
  once, so it is not a localised serialisation problem.

## The Complete Serialisation Call Chains

### Serialising Properties to the Network

The full call chain for serialising Entity properties to the client:

Source entry: [entity.cpp:2475](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2475)

<div class="flow-strip">
  <span class="flow-node">Entity::writeClientUpdateDataToBundle()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">walk the DataDescription</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataDescription::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataType::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataSource gets the value</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BinaryOStream writes</span>
</div>

### Deserialising Properties from the Network

The full call chain for deserialising properties on the client:

Source entry: [entity_description.cpp:1567](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/entity_description.cpp:1567)
(see also `readTaggedClientStreamToDict` at :1622)

<div class="flow-strip">
  <span class="flow-node">EntityDescription::readStreamToDict()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">walk the DataDescription</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataDescription::createFromStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataType::createFromStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BinaryIStream reads</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataSink writes the target</span>
</div>

### Serialising Method Arguments

The full call chain for serialising RPC method arguments:

Source entry: [method_args.cpp:213](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/method_args.cpp:213)

<div class="flow-strip">
  <span class="flow-node">MethodArgs::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">walk the argument list</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataType::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DataSource gets the value</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BinaryOStream writes</span>
</div>

### Persistent Serialisation

The full call chain for persisting an Entity to the database:

Source entry: base.cpp

<div class="flow-strip">
  <span class="flow-node">Base::writeToDB()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Base::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">EntityDescription::addToStream()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">ONLY_PERSISTENT_DATA</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">write the persistent properties</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DBApp::writeEntity()</span>
</div>

## Source-Code Verification Focus

Serialisation tests must cover round-trip and compatibility:

- add/create round-trip for every primitive `DataType`.
- arrays, dictionaries, class, fixed dict, nested sub-streams.
- the overflow check for client-server variable-length properties.
- the default-value write path when arguments are insufficient.
- caller handling after `BinaryIStream::error()` is set.
- data consistency of what EntityDescription emits for different `dataDomains`.
- the compatibility matrix between old and new EntityDefs.
- field order and type consistency when the DB mapping layer consumes a
  persistent stream.

The source already has `lib/entitydef/unit_test/test_stream.cpp`, which contains
test entries for `createFromStream()` and `addToStream()`. A later testing
chapter needs to assess which types it covers and which migration and failure
scenarios are missing.

## Scope of This Chapter

This chapter explains the basic chain of serialisation and EntityDef. Later
chapters will continue with the entity model, the three-tier Base/Cell/Client
semantics, hot reload, entity migration, testing and controllable time.
