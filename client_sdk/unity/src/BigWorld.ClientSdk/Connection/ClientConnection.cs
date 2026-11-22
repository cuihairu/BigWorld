using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using BigWorld.ClientSdk.Protocol;
using BigWorld.ClientSdk.Streams;
using BigWorld.ClientSdk.Transport;

namespace BigWorld.ClientSdk.Connection
{
    /// <summary>Args for a login status notification.</summary>
    public sealed class LoginStatusEventArgs : EventArgs
    {
        public LoginStatusEventArgs(bool ok, int statusCode, byte[] payload)
        {
            Ok = ok;
            StatusCode = statusCode;
            Payload = payload;
        }

        public bool Ok { get; }

        public int StatusCode { get; }

        public byte[] Payload { get; }
    }

    /// <summary>Args for a player-entity creation notification.</summary>
    public sealed class EntityCreatedEventArgs : EventArgs
    {
        public EntityCreatedEventArgs(int entityId, ushort entityTypeId, BwReader payload)
        {
            EntityId = entityId;
            EntityTypeId = entityTypeId;
            Payload = payload;
        }

        public int EntityId { get; }

        public ushort EntityTypeId { get; }

        public BwReader Payload { get; }
    }

    /// <summary>Args for an unhandled inbound message.</summary>
    public sealed class MessageEventArgs : EventArgs
    {
        public MessageEventArgs(MessageRecord record)
        {
            Record = record;
        }

        public MessageRecord Record { get; }
    }

    /// <summary>
    /// Login / entity-creation state machine over a Mercury TCP channel.
    ///
    /// Sequence (task CL-1: login → create → onLoginCheckStatus → entity creation):
    ///   1. TCP connect, then PacketSender emits the client header frame
    ///      ("TCP\r\n\r\n") — TCPChannel::doSend.
    ///   2. Client sends `authenticate` (uint32 session key) — client_interface.hpp.
    ///   3. Server replies `onLoginCheckStatus`; a non-zero status means failure.
    ///   4. Server pushes `createEntity` (entityId + entityTypeId + data);
    ///      the connection becomes Connected.
    ///   5. `loggedOff` (uint8 reason) terminates the session.
    /// </summary>
    public sealed class ClientConnection : IDisposable
    {
        private readonly ITransport _transport;
        private readonly PacketSender _sender;
        private readonly PacketReceiver _receiver;
        private readonly MessageSpecTable _serverToClient;
        private readonly LoginMessageIds _ids;
        private ConnectionState _state = ConnectionState.Disconnected;

        public ClientConnection(ITransport transport, LoginMessageIds? messageIds = null)
        {
            _transport = transport ?? throw new ArgumentNullException(nameof(transport));
            _ids = messageIds ?? LoginMessageIds.Default;
            _serverToClient = _ids.ServerToClientTable();
            _sender = new PacketSender(_transport);
            _receiver = new PacketReceiver();

            _transport.Connected += OnTransportConnected;
            _transport.DataReceived += OnDataReceived;
            _transport.Closed += OnTransportClosed;
        }

        public ConnectionState State
        {
            get => _state;
            private set
            {
                if (_state == value)
                {
                    return;
                }

                _state = value;
                StateChanged?.Invoke(this, value);
            }
        }

        public event EventHandler<ConnectionState>? StateChanged;

        public event EventHandler<LoginStatusEventArgs>? LoginCheckStatus;

        public event EventHandler<EntityCreatedEventArgs>? EntityCreated;

        public event EventHandler<MessageEventArgs>? UnhandledMessage;

        public event EventHandler<string>? Closed;

        /// <summary>Connect and run the login handshake.</summary>
        public async Task ConnectAsync(string host, int port, uint sessionKey)
        {
            State = ConnectionState.Connecting;
            await _transport.ConnectAsync(host, port).ConfigureAwait(false);
            State = ConnectionState.Handshaking;

            // authenticate { uint32 key } — fixed-length message, id from
            // ClientInterface declaration order (client_interface.hpp:38-43).
            byte[] bundle = BundleWriter.BuildSingle(
                _ids.Authenticate,
                writer => writer.WriteUInt32(sessionKey));
            _sender.SendBundle(bundle);
            State = ConnectionState.Authenticating;
        }

        /// <summary>Send an arbitrary message to the server (post-login RPCs).</summary>
        public void SendMessage(MessageSpec spec, Action<BwWriter> writePayload)
        {
            byte[] bundle = BundleWriter.BuildSingle(spec, writePayload);
            _sender.SendBundle(bundle);
        }

        private void OnTransportConnected(object? sender, EventArgs e)
        {
            // State transitions to Handshaking in ConnectAsync once the header
            // frame goes out.
        }

        private void OnDataReceived(object? sender, DataReceivedEventArgs e)
        {
            _receiver.Append(e.Data, e.Offset, e.Count);

            while (_receiver.TryTakeFrame(out byte[]? frame))
            {
                if (frame != null)
                {
                    DispatchFrame(frame);
                }
            }
        }

        private void DispatchFrame(byte[] frame)
        {
            IReadOnlyList<MessageRecord> records = BundleReader.Parse(frame, 0, frame.Length, _serverToClient);
            foreach (MessageRecord record in records)
            {
                DispatchRecord(record);
            }
        }

        private void DispatchRecord(MessageRecord record)
        {
            switch (record.MessageId)
            {
                case (byte)0xFF:
                    // Replies are not part of the login flow; surface as unhandled.
                    UnhandledMessage?.Invoke(this, new MessageEventArgs(record));
                    return;

                case byte id when id == _ids.OnLoginCheckStatus.Id:
                {
                    BwReader reader = BundleReader.PayloadReader(record);
                    int status = reader.ReadInt32();
                    // Transition first so a subscriber observes the new state.
                    State = status == 0
                        ? ConnectionState.AwaitingEntities
                        : ConnectionState.LoggedOff;
                    LoginCheckStatus?.Invoke(this, new LoginStatusEventArgs(
                        status == 0, status, reader.Remaining > 0
                            ? reader.ReadBytesRaw(reader.Remaining)
                            : Array.Empty<byte>()));
                    return;
                }

                case byte id when id == _ids.CreateEntity.Id:
                {
                    BwReader reader = BundleReader.PayloadReader(record);
                    int entityId = reader.ReadInt32();
                    ushort entityTypeId = reader.ReadUInt16();
                    State = ConnectionState.Connected;
                    EntityCreated?.Invoke(this, new EntityCreatedEventArgs(entityId, entityTypeId, reader));
                    return;
                }

                case byte id when id == _ids.LoggedOff.Id:
                {
                    State = ConnectionState.LoggedOff;
                    return;
                }

                default:
                    UnhandledMessage?.Invoke(this, new MessageEventArgs(record));
                    return;
            }
        }

        private void OnTransportClosed(object? sender, TransportClosedEventArgs e)
        {
            if (State != ConnectionState.LoggedOff)
            {
                State = ConnectionState.Disconnected;
            }

            Closed?.Invoke(this, e.Reason ?? "connection closed");
        }

        public void Dispose()
        {
            _transport.Connected -= OnTransportConnected;
            _transport.DataReceived -= OnDataReceived;
            _transport.Closed -= OnTransportClosed;
            _transport.Dispose();
        }
    }
}