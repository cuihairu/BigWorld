using System;

namespace BigWorld.ClientSdk.Protocol
{
    /// <summary>
    /// Thrown when the peer sends data that violates the Mercury wire protocol
    /// (bad flags, unknown message id, truncated payload). Mirrors the
    /// REASON_CORRUPTED_PACKET / REASON_NONEXISTENT_ENTRY paths in
    /// TCPBundleProcessor::dispatchMessages.
    /// </summary>
    public class ProtocolException : Exception
    {
        public ProtocolException(string message)
            : base(message)
        {
        }

        public ProtocolException(string message, Exception inner)
            : base(message, inner)
        {
        }
    }
}