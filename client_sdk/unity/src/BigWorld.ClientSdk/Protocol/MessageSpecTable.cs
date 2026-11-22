using System;
using System.Collections.Generic;
using System.Linq;

namespace BigWorld.ClientSdk.Protocol
{
    /// <summary>
    /// Immutable lookup table from message id to <see cref="MessageSpec"/>.
    /// Analogue of Mercury::InterfaceTable.
    /// </summary>
    public sealed class MessageSpecTable
    {
        private readonly MessageSpec[] _byId = new MessageSpec[256];

        private MessageSpecTable()
        {
        }

        /// <summary>Build from an ordered list of specs. IDs must be unique.</summary>
        public static MessageSpecTable FromSpecs(IEnumerable<MessageSpec> specs)
        {
            var table = new MessageSpecTable();
            foreach (MessageSpec spec in specs)
            {
                if (table._byId[spec.Id] != null)
                {
                    throw new ArgumentException($"duplicate message id {spec.Id}");
                }

                table._byId[spec.Id] = spec;
            }

            return table;
        }

        public bool TryGet(byte id, out MessageSpec? spec)
        {
            spec = _byId[id];
            return spec != null;
        }

        public MessageSpec? Get(byte id) => _byId[id];

        /// <summary>
        /// Message IDs are assigned by declaration order via
        /// Mercury::InterfaceMinder::add (programming/bigworld/lib/network/interface_minder.cpp:38-49):
        /// the nth declared message (0-based) gets id n, and addRange reserves a
        /// block of ids for ranged entity messages
        /// (interface_minder.cpp:51-70).
        /// </summary>
        public static byte IdByDeclarationOrder(int declarationIndex)
        {
            if (declarationIndex < 0 || declarationIndex > 254)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(declarationIndex), "interface has at most 255 message ids");
            }

            return (byte)declarationIndex;
        }

        public IEnumerable<MessageSpec> All => _byId.Where(s => s != null);
    }
}