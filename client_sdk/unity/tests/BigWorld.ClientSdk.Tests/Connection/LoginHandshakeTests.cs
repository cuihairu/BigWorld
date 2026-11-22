using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using BigWorld.ClientSdk.Connection;
using BigWorld.ClientSdk.Protocol;
using BigWorld.ClientSdk.Transport;
using Xunit;

namespace BigWorld.ClientSdk.Tests.Connection
{
    /// <summary>
    /// End-to-end login handshake against an in-process mock baseapp speaking
    /// the Mercury wire format. Covers the CL-1 acceptance path:
    /// login → create → onLoginCheckStatus → entity creation.
    ///
    /// The mock asserts the client's first two frames byte-for-byte (header
    /// frame "TCP\r\n\r\n", then the authenticate bundle) so the test is also a
    /// wire-format oracle, not just a round trip.
    /// </summary>
    public sealed class LoginHandshakeTests
    {
        private const uint SessionKey = 0x11223344u;
        private const int EntityId = 1000;
        private const ushort EntityTypeId = 2;

        // 0xCAFEBABE seen as a negative int32.
        private const int ReplyIdValue = unchecked((int)0xCAFEBABEu);

        // LGN-1: happy path — status 0 then createEntity reaches Connected.
        [Fact]
        public async Task Login_Succeeds_AndCreatesEntity()
        {
            using var server = new MockBaseApp(async stream =>
            {
                AssertClientAuthenticate(await ReadHeaderAndAuthenticate(stream));

                WriteFrame(stream, OnLoginCheckStatus(status: 0));
                WriteFrame(stream, CreateEntity(EntityId, EntityTypeId, extraInt32: 42));

                await DrainUntilClosed(stream);
            });

            using var connection = new ClientConnection(new TcpTransport());
            var created = new TaskCompletionSource<EntityCreatedEventArgs>();
            connection.EntityCreated += (_, e) => created.TrySetResult(e);

            await connection.ConnectAsync("127.0.0.1", server.Port, SessionKey);

            EntityCreatedEventArgs args = await Race(created.Task, server);
            Assert.Equal(EntityId, args.EntityId);
            Assert.Equal(EntityTypeId, args.EntityTypeId);
            Assert.Equal(42, args.Payload.ReadInt32());
            Assert.Equal(ConnectionState.Connected, connection.State);
        }

        // LGN-2: a non-zero login status fails the handshake.
        [Fact]
        public async Task Login_NonZeroStatus_LogsOff()
        {
            using var server = new MockBaseApp(async stream =>
            {
                AssertClientAuthenticate(await ReadHeaderAndAuthenticate(stream));
                WriteFrame(stream, OnLoginCheckStatus(status: 1));
                await DrainUntilClosed(stream);
            });

            using var connection = new ClientConnection(new TcpTransport());
            var statusEvent = new TaskCompletionSource<LoginStatusEventArgs>();
            connection.LoginCheckStatus += (_, e) => statusEvent.TrySetResult(e);

            await connection.ConnectAsync("127.0.0.1", server.Port, SessionKey);

            LoginStatusEventArgs status = await Race(statusEvent.Task, server);
            Assert.False(status.Ok);
            Assert.Equal(1, status.StatusCode);
            Assert.Equal(ConnectionState.LoggedOff, connection.State);
        }

        // LGN-3: loggedOff after a successful login terminates the session.
        [Fact]
        public async Task Login_ThenLoggedOff_Terminates()
        {
            using var server = new MockBaseApp(async stream =>
            {
                AssertClientAuthenticate(await ReadHeaderAndAuthenticate(stream));
                WriteFrame(stream, OnLoginCheckStatus(status: 0));
                WriteFrame(stream, LoggedOff(reason: 0));
                await DrainUntilClosed(stream);
            });

            using var connection = new ClientConnection(new TcpTransport());
            await connection.ConnectAsync("127.0.0.1", server.Port, SessionKey);
            await Race(WaitForState(connection, ConnectionState.LoggedOff), server);

            Assert.Equal(ConnectionState.LoggedOff, connection.State);
        }

        // LGN-4: a reply (id 0xFF) is not part of the login flow and surfaces as
        // an unhandled inbound message.
        [Fact]
        public async Task Login_ReplySurfacesAsUnhandled()
        {
            using var server = new MockBaseApp(async stream =>
            {
                AssertClientAuthenticate(await ReadHeaderAndAuthenticate(stream));
                WriteFrame(stream, OnLoginCheckStatus(status: 0));
                WriteFrame(stream, Reply(ReplyIdValue));
                await DrainUntilClosed(stream);
            });

            using var connection = new ClientConnection(new TcpTransport());
            var unhandled = new TaskCompletionSource<MessageEventArgs>();
            connection.UnhandledMessage += (_, e) => unhandled.TrySetResult(e);

            await connection.ConnectAsync("127.0.0.1", server.Port, SessionKey);

            MessageEventArgs args = await Race(unhandled.Task, server);
            Assert.Equal(0xFF, args.Record.MessageId);
            Assert.Equal(ReplyIdValue, BundleReader.PayloadReader(args.Record).ReadInt32());
        }

        // --- wire oracle helpers ---------------------------------------------

        private const byte IdOnLoginCheckStatus = 0x21;
        private const byte IdCreateEntity = 0x22;
        private const byte IdLoggedOff = 0x23;

        /// <summary>
        /// Read the client's first two frames and assert they are exactly the
        /// header frame then the authenticate bundle. Returns the authenticate
        /// bundle so the caller can further assert its payload.
        /// </summary>
        private static async Task<byte[]> ReadHeaderAndAuthenticate(NetworkStream stream)
        {
            // Header frame: [uint16 7]["TCP\r\n\r\n"].
            byte[] headerLen = await ReadExactAsync(stream, 2);
            Assert.Equal(new byte[] { 0x07, 0x00 }, headerLen);
            byte[] header = await ReadExactAsync(stream, 7);
            Assert.Equal("TCP\r\n\r\n", Encoding.ASCII.GetString(header));

            // Authenticate frame: [uint16 6][flags 0][id 0][key uint32 LE].
            byte[] authLen = await ReadExactAsync(stream, 2);
            Assert.Equal(new byte[] { 0x06, 0x00 }, authLen);
            byte[] auth = await ReadExactAsync(stream, 6);
            return auth;
        }

        private static void AssertClientAuthenticate(byte[] auth)
        {
            Assert.Equal(0x00, auth[0]); // flags: no requests
            Assert.Equal(0x00, auth[1]); // authenticate id (first declared message)
            uint key = (uint)(auth[2] | (auth[3] << 8) | (auth[4] << 16) | (auth[5] << 24));
            Assert.Equal(SessionKey, key);
        }

        private static byte[] OnLoginCheckStatus(int status)
        {
            // [flags 0][id 0x21][len 4][status int32 LE]
            return new byte[]
            {
                0x00, IdOnLoginCheckStatus, 0x04, 0x00,
                (byte)status, (byte)(status >> 8), (byte)(status >> 16), (byte)(status >> 24)
            };
        }

        private static byte[] CreateEntity(int entityId, ushort entityTypeId, int extraInt32)
        {
            // [flags 0][id 0x22][len 10][entityId int32][entityTypeId uint16][extra int32]
            return new byte[]
            {
                0x00, IdCreateEntity, 0x0A, 0x00,
                (byte)entityId, (byte)(entityId >> 8), (byte)(entityId >> 16), (byte)(entityId >> 24),
                (byte)entityTypeId, (byte)(entityTypeId >> 8),
                (byte)extraInt32, (byte)(extraInt32 >> 8), (byte)(extraInt32 >> 16), (byte)(extraInt32 >> 24)
            };
        }

        private static byte[] LoggedOff(byte reason)
        {
            // [flags 0][id 0x23][reason uint8]
            return new byte[] { 0x00, IdLoggedOff, reason };
        }

        private static byte[] Reply(int replyId)
        {
            // [flags 0][id 0xFF][len 4 bytes][replyId int32 LE]
            return new byte[]
            {
                0x00, 0xFF, 0x04, 0x00, 0x00, 0x00,
                (byte)replyId, (byte)(replyId >> 8), (byte)(replyId >> 16), (byte)(replyId >> 24)
            };
        }

        private static void WriteFrame(NetworkStream stream, byte[] payload)
        {
            byte[] prefix = TcpFraming.EncodeFramePrefix(payload.Length);
            stream.Write(prefix, 0, prefix.Length);
            stream.Write(payload, 0, payload.Length);
            stream.Flush();
        }

        private static async Task<byte[]> ReadExactAsync(NetworkStream stream, int count)
        {
            var buffer = new byte[count];
            int read = 0;
            while (read < count)
            {
                int n = await stream.ReadAsync(buffer, read, count - read).ConfigureAwait(false);
                if (n == 0)
                {
                    throw new EndOfStreamException($"server closed after {read}/{count} bytes");
                }

                read += n;
            }

            return buffer;
        }

        private static async Task DrainUntilClosed(NetworkStream stream)
        {
            var buffer = new byte[4096];
            while (await stream.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false) > 0)
            {
            }
        }

        /// <summary>Signal when the connection reaches state.</summary>
        private static Task WaitForState(ClientConnection connection, ConnectionState state)
        {
            var tcs = new TaskCompletionSource();
            connection.StateChanged += (_, s) =>
            {
                if (s == state)
                {
                    tcs.TrySetResult();
                }
            };

            if (connection.State == state)
            {
                tcs.TrySetResult();
            }

            return tcs.Task;
        }

        /// <summary>
        /// Await the expected event, but surface the mock server's own failure if
        /// the responder faults first (e.g. a wire-oracle assertion inside it).
        /// Without this the client would just time out with no clue.
        /// </summary>
        private static async Task Race(Task expected, MockBaseApp server)
        {
            Task winner = await Task.WhenAny(expected, server.Completion).ConfigureAwait(false);
            if (winner == server.Completion)
            {
                await server.Completion.ConfigureAwait(false); // rethrows the server fault
                throw new Exception("server finished without the expected client event");
            }

            await expected.ConfigureAwait(false);
        }

        private static async Task<T> Race<T>(Task<T> expected, MockBaseApp server)
        {
            Task winner = await Task.WhenAny(expected, server.Completion).ConfigureAwait(false);
            if (winner == server.Completion)
            {
                await server.Completion.ConfigureAwait(false); // rethrows the server fault
                throw new Exception("server finished without the expected client event");
            }

            return await expected.ConfigureAwait(false);
        }
        private sealed class MockBaseApp : IDisposable
        {
            private readonly TcpListener _listener;
            private readonly Task _serveTask;

            public MockBaseApp(Func<NetworkStream, Task> respond)
            {
                _listener = new TcpListener(IPAddress.Loopback, 0);
                _listener.Start();
                Port = ((IPEndPoint)_listener.LocalEndpoint).Port;

                _serveTask = Task.Run(async () =>
                {
                    using TcpClient client = await _listener.AcceptTcpClientAsync().ConfigureAwait(false);
                    await respond(client.GetStream()).ConfigureAwait(false);
                });
            }

            public int Port { get; }

            /// <summary>Completes when the responder finishes; faults if the
            /// responder (or a wire-oracle assertion inside it) throws.</summary>
            public Task Completion => _serveTask;

            public void Dispose()
            {
                _listener.Stop();
                try
                {
                    _serveTask.GetAwaiter().GetResult();
                }
                catch
                {
                    // The responder may assert-fail; the test surfaces it.
                }
            }
        }
    }
}
