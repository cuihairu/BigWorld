#include "pch.hpp"

#include "cstdmf/memory_stream.hpp"

#include "connection/server_finder.hpp"

#include "network/nub_exception.hpp"
#include "network/unpacked_message_header.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE


// -----------------------------------------------------------------------------
// Section: ServerInfo
// -----------------------------------------------------------------------------

// ServerInfo is a simple value object pairing an address with a uid.
TEST( ServerInfo_accessors )
{
	Mercury::Address addr( 0x01020304, 12345 );

	ServerInfo info( addr, 77 );
	CHECK( info.address().ip == addr.ip );
	CHECK_EQUAL( 12345, int( info.address().port ) );
	CHECK_EQUAL( 77, int( info.uid() ) );
}


// -----------------------------------------------------------------------------
// Section: ServerProbeHandler
// -----------------------------------------------------------------------------

namespace // (anonymous)
{

/**
 *	This class records the probe callbacks instead of deleting itself on
 *	finished, so the test can inspect them.
 */
class TestProbeHandler : public ServerProbeHandler
{
public:
	TestProbeHandler() :
		finished_( false ),
		succeeded_( false ),
		failed_( false ),
		numPairs_( 0 )
	{}

protected:
	/* Override from ServerProbeHandler. */
	virtual void onKeyValue( const BW::string & key,
		const BW::string & value )
	{
		++numPairs_;
		lastKey_ = key;
		lastValue_ = value;
	}

	/* Override from ServerProbeHandler. */
	virtual void onSuccess()
	{
		succeeded_ = true;
	}

	/* Override from ServerProbeHandler. */
	virtual void onFailure()
	{
		failed_ = true;
	}

	/* Override from ServerProbeHandler. */
	virtual void onFinished()
	{
		finished_ = true;
	}

public:
	bool finished_;
	bool succeeded_;
	bool failed_;
	int numPairs_;
	BW::string lastKey_;
	BW::string lastValue_;
};


} // end namespace (anonymous)


// A probe reply streams key/value string pairs; each pair is handed to
// onKeyValue and the handler reports success when the stream runs out.
TEST( ServerProbeHandler_keyValueProbe )
{
	MemoryOStream stream;
	stream << BW::string( "type" ) << BW::string( "baseapp" );
	stream << BW::string( "uid" ) << BW::string( "42" );

	Mercury::Address srcAddr( 0x01020304, 12345 );
	Mercury::UnpackedMessageHeader header;

	TestProbeHandler handler;
	handler.handleMessage( srcAddr, header, stream, /* arg */ NULL );

	CHECK( handler.succeeded_ );
	CHECK( handler.finished_ );
	CHECK( !handler.failed_ );
	CHECK_EQUAL( 2, handler.numPairs_ );
	CHECK_EQUAL( BW::string( "uid" ), handler.lastKey_ );
	CHECK_EQUAL( BW::string( "42" ), handler.lastValue_ );
}


// A probe that fails to get a reply reports failure.
TEST( ServerProbeHandler_exception )
{
	TestProbeHandler handler;
	handler.handleException(
		Mercury::NubException( Mercury::REASON_NO_SUCH_PORT ),
		/* arg */ NULL );

	CHECK( handler.failed_ );
	CHECK( handler.finished_ );
	CHECK( !handler.succeeded_ );
}


BW_END_NAMESPACE

// test_server_finder.cpp
