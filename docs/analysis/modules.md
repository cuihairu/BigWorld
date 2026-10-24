# Directories and Modules


## Reading principle

BigWorld's directory structure cannot be understood from "library name
semantics" alone. A more reliable approach is to group by runtime
responsibility, then find the source entry, message interface, and state
outlet for each group.

## Shared-library layer

`programming/bigworld/lib` is the engine's shared-mechanism layer. The
modules that most influence the server architecture are:

| Module | Responsibility | Source entry |
|---|---|---|
| `server` | Server-process framework, configuration, time, Watcher forwarding | `bwservice.hpp`, `server_app.cpp`, `time_keeper.cpp` |
| `network` | Mercury network stack: Bundle, Channel, Packet, Interface macros | `network_interface.hpp`, `bundle.hpp`, `udp_channel.hpp`, `interface_macros.hpp` |
| `connection` | Client protocol encapsulation; Login/BaseApp/Client interfaces | `server_connection.cpp`, `client_interface.hpp`, `baseapp_ext_interface.hpp`, `login_interface.hpp` |
| `entitydef` | EntityDef parsing, DataType, property/method descriptions, serialization | `entity_description_map.cpp`, `entity_description.cpp`, `data_description.hpp`, `method_description.hpp` |
| `db` | DBApp interface helpers and common database definitions | `dbapp_interface.hpp`, `dbapp_interface_utils.cpp` |
| `db_storage` | DB backend abstraction interface | `idatabase.hpp` |
| `db_storage_mysql` | MySQL backend, table mappings, background tasks | `mysql_database.cpp`, `mappings/property_mapping.cpp`, `tasks/put_entity_task.hpp` |
| `db_storage_xml` | XML DB backend implementation | `xml_database.cpp` |
| `script`, `pyscript`, `entitydef_script` | Script objects, embedded interpreter bridge, EntityDef script wrappers | `script_object.hpp`, `py_script_object.hpp`, `py_component.cpp` |

Rendering, scene, and tool shared libraries also live under `lib` — for
example `moo`, `chunk`, `scene`, `terrain`, `asset_pipeline`, `ual`. They
matter for the client and editors, but when understanding the server MMO
state model, read the modules in the table above first.

## Server-process layer

`programming/bigworld/server` is the core of the multi-process MMO
architecture. Every process plugs into the unified startup framework through
`BIGWORLD_MAIN`, then registers its own Mercury interface.

| Directory | Main responsibility | Key source |
|---|---|---|
| `baseapp` | Base entities, Proxy, client takeover, Base backup, DB write entry | `baseapp.cpp`, `base.cpp`, `proxy.cpp`, `mailbox.cpp` |
| `cellapp` | Cell entities, spaces, AOI, Witness, Ghost, Offload | `cellapp.cpp`, `entity.cpp`, `real_entity.cpp`, `witness.cpp`, `offload_checker.cpp` |
| `dbapp` | DB request handling, login data, entity read/write, DB backend calls | `dbapp.cpp`, `write_entity_handler.cpp`, `login_handler.cpp` |
| `loginapp` | Login entry, challenge protocol, account authentication, BaseApp onboarding | `loginapp.cpp`, `message_handlers.cpp` |
| `baseappmgr` | BaseApp management, load, global base, recovery coordination | `baseappmgr.cpp`, `baseapp.cpp` |
| `cellappmgr` | CellApp management, space partitioning, load balancing, Cell creation/retirement | `cellappmgr.cpp`, `space.cpp`, `cell_data.cpp` |
| `dbappmgr` | DBApp registration, DBApp hash, startup coordination | `dbappmgr.cpp`, `dbapp.hpp` |
| `reviver` | Process-level monitoring and recovery requests | `reviver.cpp`, `component_reviver.cpp` |

The key to the server source is not which classes sit inside each directory,
but which interfaces these directories use to communicate. For example a
BaseApp DB write crosses `baseapp -> cellapp -> dbapp -> db_storage_mysql`,
and Cell migration crosses `cellappmgr -> cellapp -> entity/real_entity/witness`.

## Interface-definition layer

Interface headers are the index for cross-process source analysis.

| Interface file | Registered by | Representative use |
|---|---|---|
| `server/baseapp/baseapp_int_interface.hpp` | BaseApp internal interface | BaseApp-to-BaseApp calls, Base/Cell forwarding, backup |
| `lib/connection/baseapp_ext_interface.hpp` | BaseApp external interface | Upstream messages after a client connects to a BaseApp |
| `server/cellapp/cellapp_interface.hpp` | CellApp | Cell creation, entity methods, Ghost, Offload, Witness requests |
| `server/baseappmgr/baseappmgr_interface.hpp` | BaseAppMgr | BaseApp registration, global base, load and recovery |
| `server/cellappmgr/cellappmgr_interface.hpp` | CellAppMgr | CellApp registration, Cell/Space management, offload control |
| `lib/db/dbapp_interface.hpp` | DBApp | Login, entity read/write, space persistence |
| `lib/db/dbappmgr_interface.hpp` | DBAppMgr | DBApp registration and hash updates |
| `lib/connection/login_interface.hpp` | LoginApp external interface | Login, probe, challenge response |
| `server/loginapp/login_int_interface.hpp` | LoginApp internal interface | Internal shutdown and state coordination |
| `server/reviver/reviver_interface.hpp` | Reviver | Component monitoring and recovery |

When verifying a cross-process flow, first confirm the message definition in
the interface file, then use `registerWithInterface()` to find the receiving
process, and `startMessage()` or `startRequest()` to find the sender.

## Client and connection layer

Client communication is spread across three places:

| Area | Role |
|---|---|
| `lib/connection` | Client-side connection protocol, `ServerConnection`, Login/BaseApp/Client interfaces |
| `server/baseapp/proxy.cpp` | The server-side representative of a client connection; owns the post-authentication channel, switches BaseApps, and forwards client methods |
| `server/cellapp/entity.cpp` and `witness.cpp` | Where AOI, property updates, entity methods, and position updates are finally written to the `ClientInterface` |

Do not understand client-facing communication as an ordinary Web API. The
BigWorld client protocol is Mercury messages: after login a `Proxy` takes
over the connection, and at runtime communication is bidirectional through
`ClientInterface` and `BaseAppExtInterface`.

## EntityDef and database layer

EntityDef and database mapping span several modules:

| Source | Role |
|---|---|
| `lib/entitydef/entity_description_map.cpp` | Parses `entities.xml` and `.def` |
| `lib/entitydef/data_description.cpp` | Parses property tags such as `Persistent`, `Identifier`, `Indexed`, `DatabaseLength` |
| `server/baseapp/base.cpp` | `writeToDB` entry; assembles the base/cell persistence stream |
| `server/dbapp/dbapp.cpp` | Receives `DBAppInterface::writeEntity` |
| `server/dbapp/write_entity_handler.cpp` | Parses the write request and calls the DB backend |
| `lib/db_storage/idatabase.hpp` | DB backend abstraction interface |
| `lib/db_storage_mysql/mappings/` | Property-to-MySQL table/column mappings |

This chain shows the database schema is not an independently modeled layer;
it is driven by EntityDef's persistent properties and the type system. It
differs from typical ORM object-relational mapping, and the save trigger is
not an automatic flush but explicit `writeToDB` and login/logout/backup
flows.

## Tools and operations layer

`programming/bigworld/server/tools` and `programming/bigworld/tools` are not
just helper scripts. They cover cluster control, logging, DB
synchronization, asset processing, and editor flows.

| Directory | Representative use |
|---|---|
| `server/tools/bwmachined` | Machine daemon, process discovery, and control-plane foundation |
| `server/tools/message_logger` | Distributed log collection and reading |
| `server/tools/sync_db`, `transfer_db`, `consolidate_dbs` | Database operations and conversion |
| `tools/worldeditor`, `modeleditor`, `particle_editor` | Asset and editor tools |
| `tools/assetprocessor`, `navgen` | Asset processing and navigation-data generation |

These directories' value for source analysis is verifying runtime
assumptions: how processes are discovered, logs aggregated, DBs maintained,
and assets delivered to client and server.

## Example directories

`programming/fantasydemo` and `programming/bigworld/examples` help explain
engine usage, but they should not be the sole basis for architectural
conclusions. Conclusions should come primarily from the core source under
`programming/bigworld/lib` and `programming/bigworld/server`.
