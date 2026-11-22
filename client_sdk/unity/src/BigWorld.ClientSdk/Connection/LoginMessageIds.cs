using BigWorld.ClientSdk.Protocol;

namespace BigWorld.ClientSdk.Connection
{
    /// <summary>
    /// Message ids used by the login / entity-creation state machine.
    ///
    /// The client→server ids mirror BigWorld's ClientInterface declaration order
    /// (programming/bigworld/lib/connection/client_interface.hpp) — Mercury
    /// assigns ids sequentially by declaration (interface_minder.cpp:38-49), so
    /// authenticate (the first declared message) is id 0.
    ///
    /// The server→client ids (onLoginCheckStatus / createEntity) follow the
    /// kbengine login flow the SDK targets (see client_sdk/README.md and the
    /// PROTOCOL.md login rows). Because the exact server-side interface depends
    /// on the deployed server, these are kept in one place so a host can
    /// substitute its own table.
    /// </summary>
    public sealed class LoginMessageIds
    {
        public static readonly LoginMessageIds Default = new LoginMessageIds();

        // client → baseapp (ClientInterface, client_interface.hpp:33-46)
        // authenticate carries the uint32 session key.
        public MessageSpec Authenticate =
            new MessageSpec(0x00, "authenticate", MessageLengthStyle.Fixed, 4);

        // server → client (kbengine login flow; see README)
        // onLoginCheckStatus carries the login status code (and any login data).
        public MessageSpec OnLoginCheckStatus =
            new MessageSpec(0x21, "onLoginCheckStatus", MessageLengthStyle.Variable, 2);

        // server → client (kbengine onCreatedProxies / onEntityEnterWorld shape)
        // createEntity carries entityId + entityTypeId + initial data.
        public MessageSpec CreateEntity =
            new MessageSpec(0x22, "createEntity", MessageLengthStyle.Variable, 2);

        // client_interface.hpp:166-168 loggedOff carries a uint8 reason.
        public MessageSpec LoggedOff =
            new MessageSpec(0x23, "loggedOff", MessageLengthStyle.Fixed, 1);

        /// <summary>Build the server→client table used for parsing inbound frames.</summary>
        public MessageSpecTable ServerToClientTable() =>
            MessageSpecTable.FromSpecs(new[]
            {
                MessageSpec.Reply,
                OnLoginCheckStatus,
                CreateEntity,
                LoggedOff
            });
    }
}