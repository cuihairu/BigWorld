namespace BigWorld.ClientSdk.Connection
{
    /// <summary>
    /// Login / entity-creation state machine states
    /// (task CL-1: login → create → onLoginCheckStatus → entity creation).
    /// </summary>
    public enum ConnectionState
    {
        /// <summary>No active connection.</summary>
        Disconnected,

        /// <summary>TCP connect in flight.</summary>
        Connecting,

        /// <summary>Client header frame ("TCP\r\n\r\n") sent, awaiting server.</summary>
        Handshaking,

        /// <summary>authenticate/login sent, awaiting onLoginCheckStatus.</summary>
        Authenticating,

        /// <summary>Login accepted; awaiting player entity creation.</summary>
        AwaitingEntities,

        /// <summary>Player entity created; fully connected.</summary>
        Connected,

        /// <summary>Server sent loggedOff (or local logout).</summary>
        LoggedOff
    }
}