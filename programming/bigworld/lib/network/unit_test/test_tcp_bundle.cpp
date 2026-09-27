#include "pch.hpp"

#include "network/tcp_bundle.hpp"
#include "network/tcp_channel.hpp"
#include "network/network_interface.hpp"
#include "network/event_dispatcher.hpp"
#include "network/endpoint.hpp"
#include "network/interface_element.hpp"

#include "cstdmf/bw_vector.hpp"
#include "cstdmf/memory_stream.hpp"

#include <string.h>
#include <sys/socket.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	A derived probe that exposes the request-order table that doFinalise()
 *	rewrites (protected in Bundle; the frame streams themselves are private
 *	in TCPBundle, so the wire is observed through the public data()).
 */
class ProbeTCPBundle : public Mercury::TCPBundle
{
public:
	ProbeTCPBundle( Mercury::TCPChannel & channel ) :
		Mercury::TCPBundle( channel )
	{
	}

	const Mercury::Bundle::ReplyOrders & replyOrders() const
	{
		return replyOrders_;
	}
};


uint32 readHost32( const uint8 * p )
{
	uint32 result;
	memcpy( &result, p, sizeof( result ) );
	return result;
}


/**
 *	An established loopback TCP pair with a server-side TCPChannel on it, so
 *	that the TCPChannel constructor (fd registration, socket options) and the
 *	MSS arithmetic (live TCP_INFO on an established socket) both work.
 *
 *	Lifetime notes, from the sources: TCPChannel must die through destroy()
 *	(~Channel asserts isDestroyed_), and destroy()'s decRef deletes the
 *	channel and with it the accepted endpoint (the channel's destructor takes
 *	ownership of the Endpoint& it was constructed with).
 */
class TCPBundleRig
{
public:
	TCPBundleRig() :
		dispatcher_(),
		interface_( &dispatcher_, Mercury::NETWORK_INTERFACE_INTERNAL ),
		pListener_( new Endpoint() ),
		pAccepted_( NULL ),
		pChannel_( NULL ),
		pBundle_( NULL ),
		isCreated_( false )
	{
		// No CHECK macros here: they only expand inside a TEST body. Every
		// step guards on the previous one, and tests assert isCreated()
		// first.
		pListener_->socket( SOCK_STREAM );

		if (0 != pListener_->bind( 0 ))
		{
			return;
		}

		Mercury::Address local = pListener_->getLocalAddress();

		if (0 != pListener_->listen( 5 ))
		{
			return;
		}

		client_.socket( SOCK_STREAM );

		if (0 != client_.connect( local.port, local.ip ))
		{
			return;
		}

		// Blocking accept; the loopback connect is already queued.
		pAccepted_ = pListener_->accept();

		if (pAccepted_ == NULL)
		{
			return;
		}

		// Server side: waits for a frame header, sends nothing by itself.
		pChannel_ = new Mercury::TCPChannel( interface_, *pAccepted_, true );
		pBundle_ = new ProbeTCPBundle( *pChannel_ );
		isCreated_ = true;
	}

	~TCPBundleRig()
	{
		delete pBundle_;

		if (pChannel_ != NULL)
		{
			// The channel's destructor takes the accepted endpoint with it.
			pChannel_->destroy();
		}
		else
		{
			delete pAccepted_;
		}

		delete pListener_;
	}

	bool isCreated() const { return isCreated_; }

	Mercury::TCPChannel & channel() { return *pChannel_; }
	ProbeTCPBundle & bundle() { return *pBundle_; }

	// The wire view of the bundle's frame: data() is the frame start, which
	// sits past the reserved 4-byte first-request-offset header until a
	// request drops frameStartOffset_ to zero.
	const uint8 * wire() const
	{
		return static_cast< const uint8 * >( pBundle_->data() );
	}

private:
	Mercury::EventDispatcher dispatcher_;
	Mercury::NetworkInterface interface_;
	Endpoint client_;
	Endpoint * pListener_;
	Endpoint * pAccepted_;
	Mercury::TCPChannel * pChannel_;
	ProbeTCPBundle * pBundle_;
	bool isCreated_;
};


// Test elements: one fixed-length, two variable-length widths.
const uint8 FIXED_MSG_ID = 0x11;
const uint8 VAR1_MSG_ID = 0x22;
const uint8 VAR2_MSG_ID = 0x33;
const uint8 VAR3_MSG_ID = 0x55;
const uint8 REQ_MSG_ID = 0x44;

Mercury::InterfaceElement fixedElement()
{
	return Mercury::InterfaceElement( "TestFixed", FIXED_MSG_ID,
		Mercury::FIXED_LENGTH_MESSAGE, 4 );
}

Mercury::InterfaceElement var1Element()
{
	return Mercury::InterfaceElement( "TestVar1", VAR1_MSG_ID,
		Mercury::VARIABLE_LENGTH_MESSAGE, 1 );
}

Mercury::InterfaceElement var2Element()
{
	return Mercury::InterfaceElement( "TestVar2", VAR2_MSG_ID,
		Mercury::VARIABLE_LENGTH_MESSAGE, 2 );
}

Mercury::InterfaceElement var3Element()
{
	return Mercury::InterfaceElement( "TestVar3", VAR3_MSG_ID,
		Mercury::VARIABLE_LENGTH_MESSAGE, 3 );
}

Mercury::InterfaceElement requestElement()
{
	return Mercury::InterfaceElement( "TestRequest", REQ_MSG_ID,
		Mercury::VARIABLE_LENGTH_MESSAGE, 1 );
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: frame layout
// -----------------------------------------------------------------------------

// A fresh bundle is one byte on the wire: the flags byte. size() = frame(5)
// + message(0) - frameStartOffset_(4) = 1, because the constructor reserves
// the invisible 4-byte first-request-offset header ahead of the wire view
// (tcp_bundle.cpp:26-41, 220-225).
TEST( TCPBundle_freshState )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();
	CHECK_EQUAL( 1, bundle.size() );
	CHECK_EQUAL( 0, bundle.numMessages() );

	// The flags byte starts at zero, and the wire pointer is live.
	CHECK_EQUAL( 0, int( rig.wire()[ 0 ] ) );
	CHECK( bundle.data() != NULL );
}


// A fixed-length message gains no length field on the wire: the frame is
// just [messageID][payload]. (compressLength() for FIXED_LENGTH_MESSAGE
// returns 0, so the 32-bit escape branch in finaliseCurrentMessage() never
// runs; the payload must be exactly lengthParam bytes or the element raises
// a critical - hence the exact-size fixture here.)
TEST( TCPBundle_fixedLengthMessageFrame )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startMessage( fixedElement() );
	const unsigned char payload[ 4 ] = { 0xde, 0xad, 0xbe, 0xef };
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();

	const uint8 * wire = rig.wire();
	// Wire: [flags][id][payload] - the flags byte leads every frame.
	CHECK_EQUAL( 0, int( wire[ 0 ] ) );
	CHECK_EQUAL( FIXED_MSG_ID, wire[ 1 ] );
	CHECK( memcmp( wire + 2, payload, sizeof( payload ) ) == 0 );

	// One flags byte + id + payload, and no other bytes.
	CHECK_EQUAL( 6, bundle.size() );
	CHECK_EQUAL( 1, bundle.numMessages() );
}


// A width-1 variable-length message carries its length in the byte after
// the id: [id][len][payload].
TEST( TCPBundle_variableLengthWidth1 )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startMessage( var1Element() );
	const unsigned char payload[ 3 ] = { 0xa1, 0xb2, 0xc3 };
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();

	const uint8 * wire = rig.wire();
	// [flags][id][len][payload].
	CHECK_EQUAL( VAR1_MSG_ID, wire[ 1 ] );
	CHECK_EQUAL( 3, int( wire[ 2 ] ) );
	CHECK( memcmp( wire + 3, payload, sizeof( payload ) ) == 0 );
	CHECK_EQUAL( 6, bundle.size() );
}


// The inline width-1 regime runs up to and including 0xfe; 0xff is the
// escape marker, so 254 bytes is the largest inline length
// (interface_element.cpp:318-322: "if (len < 0xff)").
TEST( TCPBundle_variableLengthWidth1Boundary )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startMessage( var1Element() );
	unsigned char payload[ 254 ];
	memset( payload, 0x5a, sizeof( payload ) );
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();

	const uint8 * wire = rig.wire();
	CHECK_EQUAL( VAR1_MSG_ID, wire[ 1 ] );
	CHECK_EQUAL( 254, int( wire[ 2 ] ) );
	CHECK_EQUAL( 257, bundle.size() );
}


// A width-2 variable-length message stores its length through BW_HTONS -
// which is the identity on little-endian hosts (binary_stream.ipp:100-103:
// "BigWorld uses little-endian over the network"), so 258 = 0x0102 comes out
// low byte first.
TEST( TCPBundle_variableLengthWidth2WireOrder )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startMessage( var2Element() );
	// 258 = 0x0102: little-endian on the wire, so 0x02 then 0x01.
	unsigned char payload[ 258 ];
	memset( payload, 0x77, sizeof( payload ) );
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();

	const uint8 * wire = rig.wire();
	CHECK_EQUAL( VAR2_MSG_ID, wire[ 1 ] );
	CHECK_EQUAL( 0x02, int( wire[ 2 ] ) );
	CHECK_EQUAL( 0x01, int( wire[ 3 ] ) );
	CHECK( memcmp( wire + 4, payload, sizeof( payload ) ) == 0 );
	CHECK_EQUAL( 262, bundle.size() );
}


// A payload that does not fit the inline width escapes: the length field is
// filled with 0xff and the real length follows as a 32-bit value written by
// operator<<, i.e. a raw host-order write (tcp_bundle.cpp:255-267 with the
// -1 return from compressLength at interface_element.cpp:358-371). The
// inline fields go through BW_HTON*, which is also the identity on this
// little-endian platform - so both paths come out little-endian here; the
// difference only shows on big-endian hosts.
TEST( TCPBundle_oversizeEscape )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startMessage( var1Element() );
	unsigned char payload[ 300 ];
	memset( payload, 0x99, sizeof( payload ) );
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();

	const uint8 * wire = rig.wire();
	CHECK_EQUAL( VAR1_MSG_ID, wire[ 1 ] );
	// Escape marker.
	CHECK_EQUAL( 0xff, int( wire[ 2 ] ) );
	// The real length, in host order (endian-agnostic read-back).
	CHECK_EQUAL( 300, int( readHost32( wire + 3 ) ) );
	CHECK( memcmp( wire + 7, payload, sizeof( payload ) ) == 0 );
	CHECK_EQUAL( 307, bundle.size() );
}


// A width-3 variable-length message stores its length through BW_PACK3 -
// plain little-endian 3-byte packing (interface_element.cpp:338-341), the
// one length style that never goes through the HTON macros at all.
TEST( TCPBundle_variableLengthWidth3Pack3 )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startMessage( var3Element() );
	// 300 = 0x012C packs as the three bytes 2C 01 00.
	unsigned char payload[ 300 ];
	memset( payload, 0x66, sizeof( payload ) );
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();

	const uint8 * wire = rig.wire();
	CHECK_EQUAL( VAR3_MSG_ID, wire[ 1 ] );
	CHECK_EQUAL( 0x2c, int( wire[ 2 ] ) );
	CHECK_EQUAL( 0x01, int( wire[ 3 ] ) );
	CHECK_EQUAL( 0x00, int( wire[ 4 ] ) );
	CHECK( memcmp( wire + 5, payload, sizeof( payload ) ) == 0 );
	// flags + id + 3 length bytes + payload.
	CHECK_EQUAL( 305, bundle.size() );
}


// The reply message is a width-4 variable-length element with the reply id
// as its payload: [0xFF][u32 length 4, little-endian][replyID]. No request
// flag is set.
TEST( TCPBundle_startReplyFrame )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startReply( 0xdeadbeef );
	bundle.doFinalise();

	const uint8 * wire = rig.wire();
	CHECK_EQUAL( 0, int( wire[ 0 ] & Mercury::TCPBundle::FLAG_HAS_REQUESTS ) );
	CHECK_EQUAL( Mercury::REPLY_MESSAGE_IDENTIFIER, wire[ 1 ] );
	CHECK_EQUAL( 4, int( readHost32( wire + 2 ) ) );
	CHECK_EQUAL( 0xdeadbeef, readHost32( wire + 6 ) );
	CHECK_EQUAL( 10, bundle.size() );
}


// -----------------------------------------------------------------------------
// Section: requests
// -----------------------------------------------------------------------------

// startRequest grows the frame down over the reserved 4-byte header
// (frameStartOffset_ drops to 0) and sets FLAG_HAS_REQUESTS. The header
// holds the first request's offset as a little-endian u32 (BW_HTONL is the
// identity on little-endian hosts); each request message embeds a ReplyID
// placeholder and the next-request offset (0xffffffff until a later request
// overwrites it). The payload length excludes the 8-byte request header
// (currentMessagePayloadLength, tcp_bundle.cpp:308-333).
TEST( TCPBundle_firstRequestFrame )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startRequest( requestElement(), NULL );
	const unsigned char payload[ 2 ] = { 0x0f, 0xf0 };
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();

	ProbeTCPBundle & probe = rig.bundle();
	const uint8 * wire = rig.wire();

	// The frame header: flags, then the first request offset. The request
	// message starts right after flags(1) + offset(4) = offset 5 - the wire
	// base has moved down over the reserved header (frameStartOffset_ = 0),
	// which is what makes offset 5 the message start on the wire.
	// (Local copy first: FLAG_HAS_REQUESTS has no out-of-class definition
	// and CHECK_EQUAL would ODR-use it.)
	const int hasRequests = Mercury::TCPBundle::FLAG_HAS_REQUESTS;
	CHECK_EQUAL( hasRequests, int( wire[ 0 ] ) );
	CHECK_EQUAL( 5, int( readHost32( wire + 1 ) ) );

	// The message: id, length, ReplyID placeholder, next-offset placeholder,
	// payload.
	CHECK_EQUAL( REQ_MSG_ID, wire[ 5 ] );
	CHECK_EQUAL( 2, int( wire[ 6 ] ) );
	CHECK_EQUAL( 0x12345678, readHost32( wire + 7 ) );
	CHECK_EQUAL( 0xffffffffU, readHost32( wire + 11 ) );
	CHECK( memcmp( wire + 15, payload, sizeof( payload ) ) == 0 );

	CHECK_EQUAL( 17, bundle.size() );
	CHECK_EQUAL( 1, bundle.numMessages() );

	// doFinalise resolved the stored offset into a pointer onto the
	// placeholder in the frame data (the ReplyIDOrOffset hack,
	// tcp_bundle.cpp:288-301). With frameStartOffset_ at zero, data() is
	// the frame base.
	CHECK_EQUAL( 1, int( probe.replyOrders().size() ) );
	const uint8 * pPlaceholder =
		reinterpret_cast< const uint8 * >( probe.replyOrders()[ 0 ].pReplyID );
	CHECK( pPlaceholder == static_cast< const uint8 * >( bundle.data() ) + 7 );
	CHECK_EQUAL( 0x12345678, readHost32( pPlaceholder ) );
}


// The second request's offset is written over the first request's
// next-request placeholder, chaining them; the last request keeps the
// 0xffffffff sentinel.
TEST( TCPBundle_requestChain )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startRequest( requestElement(), NULL );
	const unsigned char first[ 2 ] = { 0x01, 0x02 };
	bundle.addBlob( first, sizeof( first ) );

	bundle.startRequest( requestElement(), NULL );
	const unsigned char second[ 3 ] = { 0x03, 0x04, 0x05 };
	bundle.addBlob( second, sizeof( second ) );

	bundle.doFinalise();

	ProbeTCPBundle & probe = rig.bundle();
	const uint8 * wire = rig.wire();

	// Request 1 starts at 5; its message is 1(id) + 1(len) + 4(replyID) +
	// 4(next) + 2(payload) = 12 bytes, so request 2 starts at 17.
	const int hasRequests = Mercury::TCPBundle::FLAG_HAS_REQUESTS;
	CHECK_EQUAL( hasRequests, int( wire[ 0 ] ) );
	CHECK_EQUAL( 5, int( readHost32( wire + 1 ) ) );
	CHECK_EQUAL( 17, int( readHost32( wire + 11 ) ) );

	// Request 2's own next-offset placeholder keeps the sentinel; its length
	// counts only its 3-byte payload.
	CHECK_EQUAL( 0xffffffffU, readHost32( wire + 23 ) );
	CHECK_EQUAL( 3, int( wire[ 18 ] ) );

	CHECK_EQUAL( 2, int( probe.replyOrders().size() ) );

	// Both placeholders are on the frame at their recorded offsets: request
	// 1's at 7, request 2's at 17 + 2 = 19. With frameStartOffset_ at zero,
	// data() is the frame base.
	const uint8 * pFirst =
		reinterpret_cast< const uint8 * >( probe.replyOrders()[ 0 ].pReplyID );
	const uint8 * pSecond =
		reinterpret_cast< const uint8 * >( probe.replyOrders()[ 1 ].pReplyID );
	const uint8 * pFrameBase = static_cast< const uint8 * >( bundle.data() );
	CHECK( pFirst == pFrameBase + 7 );
	CHECK( pSecond == pFrameBase + 19 );
	CHECK_EQUAL( 0x12345678, readHost32( pFirst ) );
	CHECK_EQUAL( 0x12345678, readHost32( pSecond ) );
}


// clear() restores the fresh state: a one-byte wire of flags zero, no
// messages, and the reserved header hidden again - and the bundle then
// builds fresh frames.
TEST( TCPBundle_clearResets )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();

	bundle.startMessage( var1Element() );
	const unsigned char payload[ 3 ] = { 1, 2, 3 };
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();
	CHECK( bundle.size() > 1 );

	bundle.clear();

	CHECK_EQUAL( 1, bundle.size() );
	CHECK_EQUAL( 0, bundle.numMessages() );
	CHECK_EQUAL( 0, int( rig.wire()[ 0 ] ) );

	// And it is reusable.
	bundle.startMessage( var1Element() );
	bundle.addBlob( payload, sizeof( payload ) );
	bundle.doFinalise();

	const uint8 * wire = rig.wire();
	CHECK_EQUAL( VAR1_MSG_ID, wire[ 1 ] );
	CHECK_EQUAL( 3, int( wire[ 2 ] ) );
	CHECK_EQUAL( 6, bundle.size() );
}


// -----------------------------------------------------------------------------
// Section: segment arithmetic (live MSS on an established socket)
// -----------------------------------------------------------------------------

// numDataUnits() is ceil(size / mss) and freeBytesInLastDataUnit() is
// mss - (size % mss), with mss read live from TCP_INFO of the accepted
// socket. The bundle's size here is 4 + payload (flags byte + 2-byte length
// field + payload... via 5 + (3 + payload) - 4), so the payload counts are
// chosen to land exactly on and one past the segment boundary.
TEST( TCPBundle_numDataUnitsArithmetic )
{
	TCPBundleRig rig;
	CHECK( rig.isCreated() );

	ProbeTCPBundle & bundle = rig.bundle();
	Mercury::TCPChannel & channel = rig.channel();

	const int mss = channel.maxSegmentSize();
	CHECK( mss > 0 );

	// Fresh: size 1, still one data unit, mss-1 free bytes in it.
	CHECK_EQUAL( 1, bundle.size() );
	CHECK_EQUAL( 1, bundle.numDataUnits() );
	CHECK_EQUAL( mss - 1, bundle.freeBytesInLastDataUnit() );

	// Exactly one full segment: size = 4 + payload = mss.
	unsigned char filler = 0x42;
	BW::vector< unsigned char > exact( mss - 4, filler );
	bundle.startMessage( var2Element() );
	bundle.addBlob( &exact[ 0 ], int( exact.size() ) );
	CHECK_EQUAL( mss, bundle.size() );
	CHECK_EQUAL( 1, bundle.numDataUnits() );
	CHECK_EQUAL( mss, bundle.freeBytesInLastDataUnit() );

	// One byte over rolls onto a second unit with one byte free.
	const unsigned char extra = 0x43;
	bundle.addBlob( &extra, 1 );
	CHECK_EQUAL( mss + 1, bundle.size() );
	CHECK_EQUAL( 2, bundle.numDataUnits() );
	CHECK_EQUAL( mss - 1, bundle.freeBytesInLastDataUnit() );
}


// test_tcp_bundle.cpp
BW_END_NAMESPACE