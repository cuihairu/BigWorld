#include "pch.hpp"

#include "network/machine_guard.hpp"
#include "network/basictypes.hpp"

#include "cstdmf/memory_stream.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

uint32 readLE32( const uint8 * p )
{
	return uint32( p[0] ) | (uint32( p[1] ) << 8) |
		(uint32( p[2] ) << 16) | (uint32( p[3] ) << 24);
}


uint16 readLE16( const uint8 * p )
{
	return uint16( p[0] ) | (uint16( p[1] ) << 8);
}


/**
 *	Builds a hand-rolled QueryInterfaceMessage wire body:
 *	[message=12][flags][seq lo][seq hi][address le32].
 */
void buildQueryInterfaceWire( uint8 * p, uint8 flags, uint16 seq,
		uint32 address )
{
	p[0] = MachineGuardMessage::QUERY_INTERFACE_MESSAGE;
	p[1] = flags;
	p[2] = uint8( seq & 0xff );
	p[3] = uint8( seq >> 8 );
	p[4] = uint8( address & 0xff );
	p[5] = uint8( (address >> 8) & 0xff );
	p[6] = uint8( (address >> 16) & 0xff );
	p[7] = uint8( (address >> 24) & 0xff );
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: MGMPacket framing
// -----------------------------------------------------------------------------

// An empty packet serialises to exactly the 5-byte header:
// [flags][buddy le32], and reads back intact.
TEST( MGM_packetEmptyRoundTrip )
{
	MGMPacket packet;
	packet.flags_ = MGMPacket::PACKET_STAGGER_REPLIES;
	packet.buddy_ = 0x01020304;

	MemoryOStream os;
	CHECK( packet.write( os ) );
	CHECK_EQUAL( 5, os.size() );

	const uint8 * wire = static_cast< const uint8 * >( os.data() );
	CHECK_EQUAL( MGMPacket::PACKET_STAGGER_REPLIES, int( wire[ 0 ] ) );
	CHECK_EQUAL( uint32( 0x01020304 ), readLE32( wire + 1 ) );

	// And it reads back: stagger flag, buddy, no messages.
	MemoryIStream is( os.data(), os.size() );
	MGMPacket roundTrip( is );
	CHECK( !is.error() );
	CHECK( roundTrip.shouldStaggerReply() );
	CHECK_EQUAL( uint32( 0x01020304 ), roundTrip.buddy_ );
	CHECK_EQUAL( 0, int( roundTrip.messages_.size() ) );
	CHECK( !roundTrip.hasError() );
}


// A packet carrying a QueryInterfaceMessage round-trips: the message comes
// back with its type and payload intact.
TEST( MGM_packetMessageRoundTrip )
{
	MGMPacket packet;
	QueryInterfaceMessage msg;
	msg.address_ = 0x0A0000FE;
	packet.append( msg );

	MemoryOStream os;
	CHECK( packet.write( os ) );
	// 5 header + 2 length prefix + 4 header + 4 address.
	CHECK_EQUAL( 15, os.size() );

	MemoryIStream is( os.data(), os.size() );
	MGMPacket roundTrip( is );
	CHECK( !is.error() );
	CHECK( !roundTrip.hasError() );
	CHECK_EQUAL( 1, int( roundTrip.messages_.size() ) );

	// MGMPacket::read appends with shouldDelete=true, so the round-trip
	// packet owns its message.
	QueryInterfaceMessage * pRoundTrip =
		dynamic_cast< QueryInterfaceMessage * >( roundTrip.messages_[ 0 ] );
	CHECK( pRoundTrip != NULL );

	if (pRoundTrip != NULL)
	{
		CHECK_EQUAL( MachineGuardMessage::QUERY_INTERFACE_MESSAGE,
			int( pRoundTrip->message_ ) );
		CHECK_EQUAL( uint32( 0x0A0000FE ), pRoundTrip->address_ );
	}
}


// The per-message length prefix is a u16 stored little-endian (BW_HTONS is
// the identity on this platform - binary_stream.ipp:100-103).
TEST( MGM_packetLengthPrefixLittleEndian )
{
	MGMPacket packet;
	QueryInterfaceMessage msg;
	packet.append( msg );

	MemoryOStream os;
	CHECK( packet.write( os ) );

	const uint8 * wire = static_cast< const uint8 * >( os.data() );
	// A bare QueryInterfaceMessage body is 8 bytes (4 header + 4 address).
	CHECK_EQUAL( 8, int( readLE16( wire + 5 ) ) );
}


// A length prefix that runs past the end of the stream sets hasError_
// (the explicit "not enough bytes" arm in MGMPacket::read) without ever
// reading past the buffer.
TEST( MGM_packetTruncatedTailFlagsError )
{
	uint8 buf[ 10 ];
	buf[ 0 ] = 0;					// flags
	buf[ 1 ] = 0x78;				// buddy...
	buf[ 2 ] = 0x56;
	buf[ 3 ] = 0x34;
	buf[ 4 ] = 0x12;
	buf[ 5 ] = 100;					// length prefix claims 100 bytes
	buf[ 6 ] = 0;
	buf[ 7 ] = MachineGuardMessage::QUERY_INTERFACE_MESSAGE;	// 3 real bytes
	buf[ 8 ] = 0;
	buf[ 9 ] = 0;

	MemoryIStream is( buf, sizeof( buf ) );
	MGMPacket packet( is );
	CHECK( !is.error() );
	CHECK( packet.hasError() );
	CHECK_EQUAL( 0, int( packet.messages_.size() ) );
}


// A zero length prefix neither crashes nor sets hasError_: the message
// stream is empty, create() peeks an exhausted stream and returns NULL, and
// MGMPacket::read silently drops it (the "Invalid message" arm).
TEST( MGM_packetZeroLenMessageDropped )
{
	uint8 buf[ 7 ];
	memset( buf, 0, sizeof( buf ) );
	buf[ 5 ] = 0;					// zero length prefix
	buf[ 6 ] = 0;

	MemoryIStream is( buf, sizeof( buf ) );
	MGMPacket packet( is );
	CHECK( !is.error() );
	CHECK( !packet.hasError() );
	CHECK_EQUAL( 0, int( packet.messages_.size() ) );
}


// While s_buddy_ is set (non-BROADCAST) it overrides every written packet's
// buddy field - the static is process-wide, so it is restored before the
// test ends.
TEST( MGM_packetSetBuddyOverrides )
{
	const uint32 broadcast = BROADCAST;

	MGMPacket::setBuddy( 0x0A0B0C0D );

	MGMPacket packet;
	packet.buddy_ = 0x01020304;		// would win if s_buddy_ were BROADCAST

	MemoryOStream os;
	CHECK( packet.write( os ) );
	const uint8 * wire = static_cast< const uint8 * >( os.data() );
	CHECK_EQUAL( uint32( 0x0A0B0C0D ), readLE32( wire + 1 ) );

	// Restore the process-wide static before any assertions could... they
	// do not abort, so this line always runs.
	MGMPacket::setBuddy( broadcast );
	CHECK_EQUAL( broadcast, MGMPacket::s_buddy_ );

	// And reading the packet back sees the overridden buddy.
	MemoryIStream is( os.data(), os.size() );
	MGMPacket roundTrip( is );
	CHECK_EQUAL( uint32( 0x0A0B0C0D ), roundTrip.buddy_ );
}


// A packet whose single message exceeds MGMPacket::MAX_SIZE fails to write
// (the fragmentation-needed arm) - the bytes are still emitted, but the
// caller is told so.
TEST( MGM_packetOversizeReturnsFalse )
{
	MGMPacket packet;
	ErrorMessage msg;
	msg.severity_ = MESSAGE_PRIORITY_WARNING;
	msg.msg_ = BW::string( 33000, 'x' );
	packet.append( msg );

	MemoryOStream os;
	CHECK( !packet.write( os ) );
	// 5 header + 2 prefix + 4 hdr + 1 severity + 4 packed length + 33000.
	CHECK_EQUAL( 33016, os.size() );

	// The error is only advisory: hasError_ is a read-side flag.
	CHECK( !packet.hasError() );
}


// -----------------------------------------------------------------------------
// Section: MachineGuardMessage::create dispatch
// -----------------------------------------------------------------------------

// create() peeks the first byte and instantiates the matching concrete
// message, which streams itself off the remainder.
TEST( MGM_createByFirstByteDispatch )
{
	uint8 buf[ 8 ];
	buildQueryInterfaceWire( buf, 0, 0x0102, 0xC0A80166 );

	MachineGuardMessage * pMsg = MachineGuardMessage::create( buf, 8 );
	CHECK( pMsg != NULL );

	QueryInterfaceMessage * pQuery =
		dynamic_cast< QueryInterfaceMessage * >( pMsg );
	CHECK( pQuery != NULL );

	if (pQuery != NULL)
	{
		CHECK_EQUAL( MachineGuardMessage::QUERY_INTERFACE_MESSAGE,
			int( pQuery->message_ ) );
		CHECK_EQUAL( uint32( 0xC0A80166 ), pQuery->address_ );

		// seq_ is private; observe it through a write-back (seqSent_ is
		// false on a freshly-read message, so the seq is emitted as-is).
		MemoryOStream back;
		pQuery->write( back );
		CHECK_EQUAL( 8, back.size() );
		const uint8 * backWire = static_cast< const uint8 * >( back.data() );
		CHECK_EQUAL( 0x0c, int( backWire[ 0 ] ) );
		CHECK_EQUAL( 0, int( backWire[ 1 ] ) );
		CHECK_EQUAL( 0x0102, int( readLE16( backWire + 2 ) ) );
		CHECK_EQUAL( uint32( 0xC0A80166 ), readLE32( backWire + 4 ) );
	}

	delete pMsg;
}


// An unrecognised first byte produces an UnknownMessage: the body is
// swallowed raw, MESSAGE_NOT_UNDERSTOOD is flagged, and writing it back
// emits the header followed by the body as a packed-length string.
TEST( MGM_createUnknownEchoes )
{
	uint8 buf[ 7 ] = { 200, 0, 0x01, 0x02, 'a', 'b', 'c' };

	MachineGuardMessage * pMsg = MachineGuardMessage::create( buf, 7 );
	CHECK( pMsg != NULL );

	UnknownMessage * pUnknown = dynamic_cast< UnknownMessage * >( pMsg );
	CHECK( pUnknown != NULL );

	if (pUnknown != NULL)
	{
		CHECK_EQUAL( 200, int( pUnknown->message_ ) );
		const int notUnderstood =
			MachineGuardMessage::MESSAGE_NOT_UNDERSTOOD;
		CHECK_EQUAL( notUnderstood, int( pUnknown->flags_ & notUnderstood ) );

		MemoryOStream os;
		pUnknown->write( os );
		CHECK_EQUAL( 8, os.size() );
		const uint8 * wire = static_cast< const uint8 * >( os.data() );
		CHECK_EQUAL( 200, int( wire[ 0 ] ) );
		// flags gained the NOT_UNDERSTOOD bit on read.
		const int notUnderstood2 =
			MachineGuardMessage::MESSAGE_NOT_UNDERSTOOD;
		CHECK_EQUAL( notUnderstood2, int( wire[ 1 ] & notUnderstood2 ) );
		// The original seq passes through untouched (the raw bytes 01 02
		// read little-endian are 0x0201).
		CHECK_EQUAL( 0x0201, int( readLE16( wire + 2 ) ) );
		// writePackedInt(3) is a single byte below 255.
		CHECK_EQUAL( 3, int( wire[ 4 ] ) );
		CHECK( memcmp( wire + 5, "abc", 3 ) == 0 );
	}

	delete pMsg;
}


// The empty-stream create() path: peek sets error_ and create returns NULL
// (this is what MGMPacket::read's zero-length arm feeds).
TEST( MGM_createEmptyStreamReturnsNull )
{
	uint8 scratch = 0;
	MemoryIStream is( &scratch, 0 );
	CHECK( MachineGuardMessage::create( is ) == NULL );
}


// -----------------------------------------------------------------------------
// Section: concrete message streaming
// -----------------------------------------------------------------------------

// QueryInterfaceMessage defaults: message tag 12, INTERNAL address, and the
// typeStr mapping for its case.
TEST( MGM_queryInterfaceDefaults )
{
	QueryInterfaceMessage msg;
	CHECK_EQUAL( MachineGuardMessage::QUERY_INTERFACE_MESSAGE,
		int( msg.message_ ) );
	CHECK_EQUAL( 0, int( msg.address_ ) );
	CHECK( strcmp( msg.typeStr(), "QUERY_INTERFACE" ) == 0 );
	CHECK( strcmp( msg.c_str(), "QueryInterfaceMessage" ) == 0 );
}


// A full ProcessMessage round-trips through an MGMPacket, including the
// writeExtra/readExtra forward-compatibility section.
TEST( MGM_processMessageFullRoundTrip )
{
	ProcessMessage msg;
	msg.param_ = ProcessMessage::PARAM_USE_NAME |
		ProcessMessage::PARAM_IS_MSGTYPE;
	msg.category_ = ProcessMessage::SERVER_COMPONENT;
	msg.uid_ = 1234;
	msg.pid_ = 5678;
	msg.port_ = 9090;
	msg.id_ = 0x2A;
	msg.name_ = "cellapp";
	msg.majorVersion_ = 2;
	msg.minorVersion_ = 7;
	msg.patchVersion_ = 1;
	msg.username_ = "bigworld";
	msg.defDigest_ = "deadbeefdeadbeef";

	MGMPacket packet;
	packet.append( msg );

	MemoryOStream os;
	CHECK( packet.write( os ) );

	MemoryIStream is( os.data(), os.size() );
	MGMPacket roundTrip( is );
	CHECK( !is.error() );
	CHECK( !roundTrip.hasError() );
	CHECK_EQUAL( 1, int( roundTrip.messages_.size() ) );

	ProcessMessage * pBack =
		dynamic_cast< ProcessMessage * >( roundTrip.messages_[ 0 ] );
	CHECK( pBack != NULL );

	if (pBack != NULL)
	{
		CHECK_EQUAL( MachineGuardMessage::PROCESS_MESSAGE,
			int( pBack->message_ ) );
		CHECK_EQUAL( int( ProcessMessage::PARAM_USE_NAME |
			ProcessMessage::PARAM_IS_MSGTYPE ), int( pBack->param_ ) );
		CHECK_EQUAL( int( ProcessMessage::SERVER_COMPONENT ),
			int( pBack->category_ ) );
		CHECK_EQUAL( 1234, int( pBack->uid_ ) );
		CHECK_EQUAL( 5678, int( pBack->pid_ ) );
		CHECK_EQUAL( 9090, int( pBack->port_ ) );
		CHECK_EQUAL( 0x2A, int( pBack->id_ ) );
		CHECK( pBack->name_ == "cellapp" );
		CHECK_EQUAL( 2, int( pBack->majorVersion_ ) );
		CHECK_EQUAL( 7, int( pBack->minorVersion_ ) );
		CHECK_EQUAL( 1, int( pBack->patchVersion_ ) );
		CHECK( pBack->username_ == "bigworld" );
		CHECK( pBack->defDigest_ == "deadbeefdeadbeef" );
		CHECK( strcmp( pBack->c_str(), "cellapp (SERVER_COMPONENT) (pid:5678)" ) == 0 );
	}
}


// A wire body cut right after the readImpl section (the pre-extra legacy
// format) still parses: readExtra's remainingLength()==0 arm tolerates it
// and the version fields keep their defaults.
TEST( MGM_processMessageLegacyBodyWithoutExtra )
{
	// [12][flags=0][seq=0x0102][param=0x20][category=0][uid=1000]
	// [pid=2000][port=0][id=0x30][nameLen=5]["bwapp"] - 20 bytes, no extra.
	uint8 buf[ 20 ];
	buf[ 0 ] = MachineGuardMessage::PROCESS_MESSAGE;
	buf[ 1 ] = 0;
	buf[ 2 ] = 0x02;
	buf[ 3 ] = 0x01;
	buf[ 4 ] = ProcessMessage::PARAM_USE_NAME;
	buf[ 5 ] = ProcessMessage::SERVER_COMPONENT;
	buf[ 6 ] = 1000 & 0xff;
	buf[ 7 ] = 1000 >> 8;
	buf[ 8 ] = 2000 & 0xff;
	buf[ 9 ] = 2000 >> 8;
	buf[ 10 ] = 0;
	buf[ 11 ] = 0;
	buf[ 12 ] = 0x30;
	buf[ 13 ] = 0;
	buf[ 14 ] = 5;
	memcpy( buf + 15, "bwapp", 5 );

	MachineGuardMessage * pMsg = MachineGuardMessage::create( buf, 20 );
	CHECK( pMsg != NULL );

	ProcessMessage * pProc = dynamic_cast< ProcessMessage * >( pMsg );
	CHECK( pProc != NULL );

	if (pProc != NULL)
	{
		CHECK_EQUAL( 0x20, int( pProc->param_ ) );
		CHECK_EQUAL( 1000, int( pProc->uid_ ) );
		CHECK_EQUAL( 2000, int( pProc->pid_ ) );
		CHECK_EQUAL( 0x30, int( pProc->id_ ) );
		CHECK( pProc->name_ == "bwapp" );
		CHECK_EQUAL( 0, int( pProc->majorVersion_ ) );
		// The extra section keeps its constructor defaults: username_ was
		// seeded from getUsername() and the absent wire extra does not
		// overwrite it.
		CHECK( !pProc->username_.empty() );
	}

	delete pMsg;
}


// ErrorMessage round-trips its severity and packed-length string payload.
TEST( MGM_errorMessageRoundTrip )
{
	ErrorMessage msg;
	msg.severity_ = MESSAGE_PRIORITY_WARNING;
	msg.msg_ = "boom";

	MGMPacket packet;
	packet.append( msg );

	MemoryOStream os;
	CHECK( packet.write( os ) );

	MemoryIStream is( os.data(), os.size() );
	MGMPacket roundTrip( is );
	CHECK( !is.error() );
	CHECK_EQUAL( 1, int( roundTrip.messages_.size() ) );

	ErrorMessage * pBack =
		dynamic_cast< ErrorMessage * >( roundTrip.messages_[ 0 ] );
	CHECK( pBack != NULL );

	if (pBack != NULL)
	{
		CHECK_EQUAL( MESSAGE_PRIORITY_WARNING, int( pBack->severity_ ) );
		CHECK( pBack->msg_ == "boom" );
	}
}


// refreshSeq() keeps the ticker moving, observed through write(): a zero
// seq is refreshed at construction into something nonzero, and because the
// first write marks the message as sent, a second write refreshes to a new
// nonzero value (the production "reuse a sent message" path).
TEST( MGM_refreshSeqAdvances )
{
	MachineGuardMessage msg( MachineGuardMessage::RESET_MESSAGE );

	MemoryOStream first;
	msg.write( first );
	CHECK_EQUAL( 4, first.size() );
	const uint8 * firstWire =
		static_cast< const uint8 * >( first.data() );
	CHECK_EQUAL( MachineGuardMessage::RESET_MESSAGE, int( firstWire[ 0 ] ) );

	// Construction refreshed the zero seq to a nonzero one, and the first
	// write (seqSent_ false) emitted it untouched.
	uint16 firstSeq = readLE16( firstWire + 2 );
	CHECK( firstSeq != 0 );

	// The second write re-sends a consumed message: refreshSeq must run and
	// produce a different nonzero seq.
	MemoryOStream second;
	msg.write( second );
	const uint8 * secondWire =
		static_cast< const uint8 * >( second.data() );
	uint16 secondSeq = readLE16( secondWire + 2 );
	CHECK( secondSeq != 0 );
	CHECK( secondSeq != firstSeq );
}


// typeStr() has no case for ERROR_MESSAGE, so it reports the fallback -
// recorded as-is (log-readability nit, not fixed here).
TEST( MGM_typeStrFallbacks )
{
	ErrorMessage msg;
	CHECK( strcmp( msg.typeStr(), "** UNKNOWN **" ) == 0 );

	// An UnknownMessage's type string falls back the same way.
	UnknownMessage unknown( MachineGuardMessage::Message( 200 ) );
	CHECK( strcmp( unknown.typeStr(), "** UNKNOWN **" ) == 0 );
}


// test_machine_guard.cpp
BW_END_NAMESPACE
