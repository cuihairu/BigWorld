#include "pch.hpp"

#include "network/machine_guard.hpp"
#include "network/basictypes.hpp"

#include "cstdmf/memory_stream.hpp"

#include <pwd.h>
#include <signal.h>
#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	Round-trips a message through its own write/create pair and returns the
 *	newly allocated message. The stream is checked for errors by the caller.
 */
template <class MSG>
MSG * roundTripMessage( MSG & msg )
{
	MemoryOStream os;
	msg.write( os );

	MemoryIStream is( os.data(), os.size() );
	MachineGuardMessage * pCreated = MachineGuardMessage::create( is );

	if (!pCreated)
	{
		return NULL;
	}

	if (is.error())
	{
		delete pCreated;
		return NULL;
	}

	return static_cast< MSG * >( pCreated );
}


/**
 *	Offsets of the ProcessMessage wire, given a streamed name of
 *	calculateStreamedSizeOfString( nameLength ) bytes.
 */
uint32 processExtraDataOffset( int nameLength )
{
	// [message][flags][seq lo][seq hi] +
	// [param][category][uid lo][uid hi][pid lo][pid hi][port lo][port hi]
	// [id lo][id hi] + streamed name.
	return 4 + 10 +
		BinaryOStream::calculateStreamedSizeOfString( nameLength );
}


uint32 readLE32( const uint8 * p )
{
	return uint32( p[0] ) | (uint32( p[1] ) << 8) |
		(uint32( p[2] ) << 16) | (uint32( p[3] ) << 24);
}


void patchLE32( uint8 * p, uint32 value )
{
	p[0] = uint8( value & 0xff );
	p[1] = uint8( (value >> 8) & 0xff );
	p[2] = uint8( (value >> 16) & 0xff );
	p[3] = uint8( (value >> 24) & 0xff );
}


/**
 *	Reply handler that records which handler was reached by handle().
 */
class RecordingReplyHandler : public MachineGuardMessage::ReplyHandler
{
public:
	RecordingReplyHandler() : last_( "none" ) {}

	bool dispatch( MachineGuardMessage & mgm )
	{
		last_ = "none";
		return this->handle( mgm, 0x7f000001 );
	}

	BW::string last_;

	virtual bool onUnhandledMsg( MachineGuardMessage & /*mgm*/, uint32 )
	{
		last_ = "unhandled";
		return true;
	}

	virtual bool onHighPrecisionMachineMessage(
		HighPrecisionMachineMessage &, uint32 )
	{
		last_ = "highPrecision";
		return true;
	}

	virtual bool onWholeMachineMessage( WholeMachineMessage &, uint32 )
	{
		last_ = "wholeMachine";
		return true;
	}

	virtual bool onProcessMessage( ProcessMessage &, uint32 )
	{
		last_ = "process";
		return true;
	}

	virtual bool onProcessStatsMessage( ProcessStatsMessage &, uint32 )
	{
		last_ = "processStats";
		return true;
	}

	virtual bool onListenerMessage( ListenerMessage &, uint32 )
	{
		last_ = "listener";
		return true;
	}

	virtual bool onCreateMessage( CreateMessage &, uint32 )
	{
		last_ = "create";
		return true;
	}

	virtual bool onSignalMessage( SignalMessage &, uint32 )
	{
		last_ = "signal";
		return true;
	}

	virtual bool onTagsMessage( TagsMessage &, uint32 )
	{
		last_ = "tags";
		return true;
	}

	virtual bool onUserMessage( UserMessage &, uint32 )
	{
		last_ = "user";
		return true;
	}

	virtual bool onPidMessage( PidMessage &, uint32 )
	{
		last_ = "pid";
		return true;
	}

	virtual bool onResetMessage( ResetMessage &, uint32 )
	{
		last_ = "reset";
		return true;
	}

	virtual bool onErrorMessage( ErrorMessage &, uint32 )
	{
		last_ = "error";
		return true;
	}

	virtual bool onMachinedAnnounceMessage( MachinedAnnounceMessage &, uint32 )
	{
		last_ = "machinedAnnounce";
		return true;
	}

	virtual bool onQueryInterfaceMessage( QueryInterfaceMessage &, uint32 )
	{
		last_ = "queryInterface";
		return true;
	}

	virtual bool onCreateWithArgsMessage( CreateWithArgsMessage &, uint32 )
	{
		last_ = "createWithArgs";
		return true;
	}

	virtual bool onMachinePlatformMessage( MachinePlatformMessage &, uint32 )
	{
		last_ = "machinePlatform";
		return true;
	}
};

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: machine statistics messages
// -----------------------------------------------------------------------------

TEST( MGMMSG_highPrecisionMachineRoundTrip )
{
	HighPrecisionMachineMessage msg;

	msg.hostname_ = "batch30-host";
	msg.cpuSpeed_ = 2400;
	msg.version_ = 9;
	msg.mem_ = 77;
	msg.inDiscards_ = 1;
	msg.outDiscards_ = 2;
	msg.ioWaitTime_ = 5;
	msg.setNCpus( 2 );
	msg.cpuLoads_[0] = 10;
	msg.cpuLoads_[1] = 90;
	msg.setNInterfaces( 1 );
	msg.ifStats_[0].name_ = "eth0";
	msg.ifStats_[0].bitsIn_ = 0x01020304;
	msg.ifStats_[0].bitsOut_ = 0x05060708;
	msg.ifStats_[0].packIn_ = 11;
	msg.ifStats_[0].packOut_ = 12;

	HighPrecisionMachineMessage * pRoundTrip =
		roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::HIGH_PRECISION_MACHINE_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK( strcmp( "batch30-host", pRoundTrip->hostname_.c_str() ) == 0 );
	CHECK_EQUAL( 2400, int( pRoundTrip->cpuSpeed_ ) );
	CHECK_EQUAL( 2, int( pRoundTrip->nCpus_ ) );
	CHECK_EQUAL( 10, int( pRoundTrip->cpuLoads_[0] ) );
	CHECK_EQUAL( 90, int( pRoundTrip->cpuLoads_[1] ) );
	CHECK_EQUAL( 5, int( pRoundTrip->ioWaitTime_ ) );
	CHECK_EQUAL( 77, int( pRoundTrip->mem_ ) );
	CHECK_EQUAL( 9, int( pRoundTrip->version_ ) );
	CHECK_EQUAL( 1, int( pRoundTrip->inDiscards_ ) );
	CHECK_EQUAL( 2, int( pRoundTrip->outDiscards_ ) );
	CHECK_EQUAL( 1, int( pRoundTrip->nInterfaces_ ) );
	CHECK( strcmp( "eth0", pRoundTrip->ifStats_[0].name_.c_str() ) == 0 );
	CHECK_EQUAL( uint32( 0x01020304 ), pRoundTrip->ifStats_[0].bitsIn_ );
	CHECK_EQUAL( uint32( 0x05060708 ), pRoundTrip->ifStats_[0].bitsOut_ );
	CHECK_EQUAL( 11, int( pRoundTrip->ifStats_[0].packIn_ ) );
	CHECK_EQUAL( 12, int( pRoundTrip->ifStats_[0].packOut_ ) );

	delete pRoundTrip;
}


TEST( MGMMSG_wholeMachineRoundTrip )
{
	WholeMachineMessage msg;

	msg.hostname_ = "batch30-whole";
	msg.cpuSpeed_ = 1800;
	msg.version_ = 7;
	msg.mem_ = 40;
	msg.setNCpus( 1 );
	msg.cpuLoads_[0] = 55;
	msg.setNInterfaces( 1 );
	msg.ifStats_[0].name_ = "lo";
	// LowPrecisionInterfaceStats carries 8-bit counters.
	msg.ifStats_[0].bitsIn_ = 200;
	msg.ifStats_[0].bitsOut_ = 201;
	msg.ifStats_[0].packIn_ = 202;
	msg.ifStats_[0].packOut_ = 203;

	WholeMachineMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::WHOLE_MACHINE_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK( strcmp( "batch30-whole", pRoundTrip->hostname_.c_str() ) == 0 );
	CHECK_EQUAL( 1800, int( pRoundTrip->cpuSpeed_ ) );
	CHECK_EQUAL( 55, int( pRoundTrip->cpuLoads_[0] ) );
	CHECK_EQUAL( 40, int( pRoundTrip->mem_ ) );
	CHECK_EQUAL( 7, int( pRoundTrip->version_ ) );
	CHECK( strcmp( "lo", pRoundTrip->ifStats_[0].name_.c_str() ) == 0 );
	CHECK_EQUAL( 200, int( pRoundTrip->ifStats_[0].bitsIn_ ) );
	CHECK_EQUAL( 203, int( pRoundTrip->ifStats_[0].packOut_ ) );

	delete pRoundTrip;
}


TEST( MGMMSG_machineStatCStr )
{
	HighPrecisionMachineMessage hpmm;
	CHECK( strcmp( hpmm.c_str(), "HighPrecisionMachineMessage" ) == 0 );

	WholeMachineMessage wmm;
	CHECK( strcmp( wmm.c_str(), "WholeMachineMessage" ) == 0 );
}


// -----------------------------------------------------------------------------
// Section: process family messages
// -----------------------------------------------------------------------------

TEST( MGMMSG_processRoundTripWithExtra )
{
	ProcessMessage msg;

	msg.param_ = ProcessMessage::PARAM_USE_PID |
		ProcessMessage::PARAM_USE_NAME;
	msg.category_ = ProcessMessage::WATCHER_NUB;
	msg.uid_ = 1234;
	msg.pid_ = 4321;
	msg.port_ = 20901;
	msg.id_ = 77;
	msg.name_ = "batch30app";
	msg.majorVersion_ = 2;
	msg.minorVersion_ = 9;
	msg.patchVersion_ = 1;
	msg.interfaceVersion_ = 5;
	msg.username_ = "cui";
	msg.defDigest_ = "digest-abcd";

	ProcessMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::PROCESS_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK_EQUAL( int( msg.param_ ), int( pRoundTrip->param_ ) );
	CHECK_EQUAL( int( ProcessMessage::WATCHER_NUB ),
		int( pRoundTrip->category_ ) );
	CHECK_EQUAL( 1234, int( pRoundTrip->uid_ ) );
	CHECK_EQUAL( 4321, int( pRoundTrip->pid_ ) );
	CHECK_EQUAL( 20901, int( pRoundTrip->port_ ) );
	CHECK_EQUAL( 77, int( pRoundTrip->id_ ) );
	CHECK( strcmp( "batch30app", pRoundTrip->name_.c_str() ) == 0 );
	// The extra block rides behind the impl block.
	CHECK_EQUAL( 2, int( pRoundTrip->majorVersion_ ) );
	CHECK_EQUAL( 9, int( pRoundTrip->minorVersion_ ) );
	CHECK_EQUAL( 1, int( pRoundTrip->patchVersion_ ) );
	CHECK_EQUAL( 5, int( pRoundTrip->interfaceVersion_ ) );
	CHECK( strcmp( "cui", pRoundTrip->username_.c_str() ) == 0 );
	CHECK( strcmp( "digest-abcd", pRoundTrip->defDigest_.c_str() ) == 0 );

	delete pRoundTrip;
}


TEST( MGMMSG_processExtraSkipsFutureBytes )
{
	ProcessMessage msg;
	msg.name_ = "batch30app";
	msg.username_ = "cui";

	MemoryOStream os;
	msg.write( os );

	// Declare four extra future bytes and append them.
	const uint32 offset = processExtraDataOffset( msg.name_.size() );
	uint8 * pExtraData = static_cast< uint8 * >( os.data() ) + offset;
	patchLE32( pExtraData, readLE32( pExtraData ) + 4 );
	os << uint32( 0xDEADBEEF );

	MemoryIStream is( os.data(), os.size() );
	MachineGuardMessage * pCreated = MachineGuardMessage::create( is );

	CHECK( pCreated != NULL );
	if (!pCreated)
	{
		return;
	}

	// The future bytes were consumed, no error and no leftovers.
	CHECK( !is.error() );
	CHECK_EQUAL( 0, is.remainingLength() );
	CHECK( strcmp( "batch30app", static_cast< ProcessMessage * >( pCreated )->name_.c_str() ) == 0 );

	delete pCreated;
}


TEST( MGMMSG_processExtraUndersizedDeclarationErrors )
{
	ProcessMessage msg;
	msg.name_ = "batch30app";

	MemoryOStream os;
	msg.write( os );

	// Declare less extra data than the fields actually consume: the
	// declaration goes negative and the stream is flagged in error.
	const uint32 offset = processExtraDataOffset( msg.name_.size() );
	patchLE32( static_cast< uint8 * >( os.data() ) + offset, 1 );

	MemoryIStream is( os.data(), os.size() );
	MachineGuardMessage * pCreated = MachineGuardMessage::create( is );

	CHECK( pCreated != NULL );
	if (!pCreated)
	{
		return;
	}

	CHECK( is.error() );

	delete pCreated;
}


TEST( MGMMSG_processMatchesPerParamBit )
{
	ProcessMessage self;
	self.category_ = ProcessMessage::SERVER_COMPONENT;
	self.uid_ = 100;
	self.pid_ = 200;
	self.port_ = 300;
	self.id_ = 400;
	self.name_ = "batch30app";

	// The query mirrors every tracked field and asks about every bit.
	ProcessMessage query;
	query.param_ = ProcessMessage::PARAM_USE_CATEGORY |
		ProcessMessage::PARAM_USE_UID | ProcessMessage::PARAM_USE_PID |
		ProcessMessage::PARAM_USE_PORT | ProcessMessage::PARAM_USE_ID |
		ProcessMessage::PARAM_USE_NAME;
	query.category_ = self.category_;
	query.uid_ = self.uid_;
	query.pid_ = self.pid_;
	query.port_ = self.port_;
	query.id_ = self.id_;
	query.name_ = self.name_;

	// Matching on every bit passes.
	CHECK( self.matches( query ) );

	// Flipping each tracked field breaks the match in turn.
	ProcessMessage probe = query;

	probe = query;
	probe.category_ = ProcessMessage::WATCHER_NUB;
	CHECK( !self.matches( probe ) );

	probe = query;
	probe.uid_ = 999;
	CHECK( !self.matches( probe ) );

	probe = query;
	probe.pid_ = 999;
	CHECK( !self.matches( probe ) );

	probe = query;
	probe.port_ = 999;
	CHECK( !self.matches( probe ) );

	probe = query;
	probe.id_ = 999;
	CHECK( !self.matches( probe ) );

	probe = query;
	probe.name_ = "other";
	CHECK( !self.matches( probe ) );

	// Untracked fields are ignored: param drops every USE bit.
	ProcessMessage untracked;
	untracked.param_ = 0;
	untracked.uid_ = 999;
	untracked.pid_ = 999;
	CHECK( self.matches( untracked ) );
}


TEST( MGMMSG_processCategoryToString )
{
	CHECK( strcmp( ProcessMessage::categoryToString(
		ProcessMessage::SERVER_COMPONENT ), "SERVER_COMPONENT" ) == 0 );
	CHECK( strcmp( ProcessMessage::categoryToString(
		ProcessMessage::WATCHER_NUB ), "WATCHER_NUB" ) == 0 );
	CHECK( strcmp( ProcessMessage::categoryToString( 42 ), "UNKNOWN" ) == 0 );

	ProcessMessage msg;
	msg.name_ = "batch30app";
	msg.category_ = ProcessMessage::WATCHER_NUB;
	msg.pid_ = 4321;

	// c_str embeds the category string and the pid.
	CHECK( strstr( msg.c_str(), "batch30app" ) != NULL );
	CHECK( strstr( msg.c_str(), "WATCHER_NUB" ) != NULL );
	CHECK( strstr( msg.c_str(), "4321" ) != NULL );
}


TEST( MGMMSG_processStatsRoundTrip )
{
	ProcessStatsMessage msg;

	msg.param_ = ProcessMessage::PARAM_USE_PID;
	msg.pid_ = 555;
	msg.name_ = "batch30stats";
	msg.cpu_ = 30;
	msg.mem_ = 60;

	ProcessStatsMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::PROCESS_STATS_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK( strcmp( "batch30stats", pRoundTrip->name_.c_str() ) == 0 );
	CHECK_EQUAL( 555, int( pRoundTrip->pid_ ) );
	CHECK_EQUAL( 30, int( pRoundTrip->cpu_ ) );
	CHECK_EQUAL( 60, int( pRoundTrip->mem_ ) );

	// Its c_str carries the name and pid but not the category.
	CHECK( strstr( pRoundTrip->c_str(), "batch30stats" ) != NULL );
	CHECK( strstr( pRoundTrip->c_str(), "555" ) != NULL );

	delete pRoundTrip;
}


TEST( MGMMSG_listenerRoundTrip )
{
	ListenerMessage msg;

	msg.param_ = ProcessMessage::PARAM_IS_MSGTYPE |
		ListenerMessage::ADD_BIRTH_LISTENER;
	msg.uid_ = 0xffff;
	msg.name_ = "batch30listener";
	msg.preAddr_ = "1.2.3.4:8000";
	msg.postAddr_ = "5.6.7.8:9000";

	ListenerMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::LISTENER_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK( strcmp( "batch30listener", pRoundTrip->name_.c_str() ) == 0 );
	CHECK( strcmp( "1.2.3.4:8000", pRoundTrip->preAddr_.c_str() ) == 0 );
	CHECK( strcmp( "5.6.7.8:9000", pRoundTrip->postAddr_.c_str() ) == 0 );

	// c_str picks the registration label from the param byte.
	CHECK( strstr( pRoundTrip->c_str(), "RegBirthL:" ) != NULL );

	msg.param_ = ProcessMessage::PARAM_IS_MSGTYPE |
		ListenerMessage::ADD_DEATH_LISTENER;
	CHECK( strstr( msg.c_str(), "RegDeathL:" ) != NULL );

	delete pRoundTrip;
}


TEST( MGMMSG_signalRoundTripAndSetters )
{
	SignalMessage msg;

	msg.pid_ = 8080;
	msg.signal_ = 15;

	SignalMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::SIGNAL_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK_EQUAL( 15, int( pRoundTrip->signal_ ) );
	CHECK( strstr( pRoundTrip->c_str(), "SignalMessage: 15" ) != NULL );

	delete pRoundTrip;

	// The convenience setters map onto the real signal numbers.
	SignalMessage controlled;
	controlled.setControlledShutdown();
	CHECK_EQUAL( SIGUSR1, int( controlled.signal_ ) );

	SignalMessage killed;
	killed.setKill();
	CHECK_EQUAL( SIGINT, int( killed.signal_ ) );

	SignalMessage hardKilled;
	hardKilled.setHardKill();
	CHECK_EQUAL( SIGQUIT, int( hardKilled.signal_ ) );
}


// -----------------------------------------------------------------------------
// Section: creation messages
// -----------------------------------------------------------------------------

TEST( MGMMSG_createRoundTrip )
{
	CreateMessage msg;

	msg.name_ = "cellapp";
	msg.config_ = "hybrid";
	msg.uid_ = 777;
	msg.recover_ = 1;
	msg.fwdIp_ = 0x0A000001;
	msg.fwdPort_ = 12345;

	CreateMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::CREATE_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK( strcmp( "cellapp", pRoundTrip->name_.c_str() ) == 0 );
	CHECK( strcmp( "hybrid", pRoundTrip->config_.c_str() ) == 0 );
	CHECK_EQUAL( 777, int( pRoundTrip->uid_ ) );
	CHECK_EQUAL( 1, int( pRoundTrip->recover_ ) );
	CHECK_EQUAL( uint32( 0x0A000001 ), pRoundTrip->fwdIp_ );
	CHECK_EQUAL( 12345, int( pRoundTrip->fwdPort_ ) );

	// c_str and c_str_name shape the config/name/uid triple.
	CHECK( strstr( pRoundTrip->c_str(), "CreateMessage" ) != NULL );
	CHECK( strstr( pRoundTrip->c_str(), "hybrid/cellapp" ) != NULL );
	CHECK( strstr( pRoundTrip->c_str(), "(uid:777)" ) != NULL );

	delete pRoundTrip;
}


TEST( MGMMSG_createWithArgsRoundTrip )
{
	CreateWithArgsMessage msg;

	msg.name_ = "dbapp";
	msg.config_ = "release";
	msg.uid_ = 888;
	msg.args_.push_back( "--res" );
	msg.args_.push_back( "/game/res" );
	msg.args_.push_back( "-verbose" );

	CreateWithArgsMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::CREATE_WITH_ARGS_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK( strcmp( "dbapp", pRoundTrip->name_.c_str() ) == 0 );
	CHECK_EQUAL( 3, int( pRoundTrip->args_.size() ) );
	CHECK( strcmp( "--res", pRoundTrip->args_[0].c_str() ) == 0 );
	CHECK( strcmp( "/game/res", pRoundTrip->args_[1].c_str() ) == 0 );
	CHECK( strcmp( "-verbose", pRoundTrip->args_[2].c_str() ) == 0 );

	// c_str appends the argument list.
	CHECK( strstr( pRoundTrip->c_str(), "CreateWithArgsMessage" ) != NULL );
	CHECK( strstr( pRoundTrip->c_str(),
		"args=[--res,/game/res,-verbose]" ) != NULL );

	delete pRoundTrip;
}


TEST( MGMMSG_createWithArgsEmptyCStr )
{
	CreateWithArgsMessage msg;
	msg.name_ = "bare";
	msg.config_ = "hybrid";

	// No arguments renders an empty bracket pair.
	CHECK( strstr( msg.c_str(), "args=[]" ) != NULL );
}


// -----------------------------------------------------------------------------
// Section: misc messages
// -----------------------------------------------------------------------------

TEST( MGMMSG_tagsRoundTrip )
{
	TagsMessage msg;

	msg.exists_ = 1;
	msg.tags_.push_back( "production" );
	msg.tags_.push_back( "eu-west" );

	TagsMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::TAGS_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK_EQUAL( 1, int( pRoundTrip->exists_ ) );
	CHECK_EQUAL( 2, int( pRoundTrip->tags_.size() ) );
	CHECK( strcmp( "production", pRoundTrip->tags_[0].c_str() ) == 0 );
	CHECK( strcmp( "eu-west", pRoundTrip->tags_[1].c_str() ) == 0 );
	CHECK( strcmp( pRoundTrip->c_str(), "TagsMessage" ) == 0 );

	delete pRoundTrip;
}


TEST( MGMMSG_userRoundTripCoredumps )
{
	UserMessage msg;

	msg.param_ = UserMessage::PARAM_CHECK_COREDUMPS;
	msg.uid_ = 4242;
	msg.username_ = "cui";
	msg.fullname_ = "Cui Hairu";
	msg.home_ = "/home/cui";
	msg.mfroot_ = "/opt/bw";
	msg.bwrespath_ = "/game/res";
	UserMessage::CoreDump dump;
	dump.filename_ = "/tmp/core.1234";
	dump.assert_ = "assert text";
	dump.time_ = 1728999999;
	msg.coredumps_.push_back( dump );

	UserMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::USER_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK_EQUAL( int( UserMessage::PARAM_CHECK_COREDUMPS ),
		int( pRoundTrip->param_ ) );
	CHECK_EQUAL( 4242, int( pRoundTrip->uid_ ) );
	CHECK( strcmp( "cui", pRoundTrip->username_.c_str() ) == 0 );
	CHECK( strcmp( "/home/cui", pRoundTrip->home_.c_str() ) == 0 );
	CHECK_EQUAL( 1, int( pRoundTrip->coredumps_.size() ) );
	CHECK( strcmp( "/tmp/core.1234", pRoundTrip->coredumps_[0].filename_.c_str() ) == 0 );
	CHECK( strcmp( "assert text", pRoundTrip->coredumps_[0].assert_.c_str() ) == 0 );
	CHECK_EQUAL( uint32( 1728999999 ), pRoundTrip->coredumps_[0].time_ );
	// Version string is only streamed when requested.
	CHECK( pRoundTrip->versionString_.empty() );

	delete pRoundTrip;
}


TEST( MGMMSG_userRoundTripVersionString )
{
	UserMessage msg;

	msg.param_ = UserMessage::PARAM_GET_VERSION;
	msg.username_ = "cui";
	msg.versionString_ = "2.9.0-batch30";

	UserMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( UserMessage::PARAM_GET_VERSION ),
		int( pRoundTrip->param_ ) );
	CHECK( strcmp( "2.9.0-batch30", pRoundTrip->versionString_.c_str() ) == 0 );
	CHECK_EQUAL( 0, int( pRoundTrip->coredumps_.size() ) );
	CHECK( strcmp( pRoundTrip->c_str(), "UserMessage: cui:0" ) == 0 );

	delete pRoundTrip;
}


TEST( MGMMSG_userInitAndConfFilename )
{
	UserMessage msg;

	struct passwd pwent;
	memset( &pwent, 0, sizeof( pwent ) );
	pwent.pw_uid = 4242;
	pwent.pw_gid = 4243;
	pwent.pw_name = const_cast< char * >( "cui" );
	pwent.pw_gecos = const_cast< char * >( "Cui Hairu" );
	pwent.pw_dir = const_cast< char * >( "/home/cui" );

	msg.init( pwent );

	CHECK_EQUAL( 4242, int( msg.uid_ ) );
	CHECK_EQUAL( 4243, int( msg.gid_ ) );
	CHECK( strcmp( "cui", msg.username_.c_str() ) == 0 );
	CHECK( strcmp( "Cui Hairu", msg.fullname_.c_str() ) == 0 );
	CHECK( strcmp( "/home/cui", msg.home_.c_str() ) == 0 );
	CHECK( strcmp( msg.getConfFilename(),
		"/home/cui/.bwmachined.conf" ) == 0 );
}


TEST( MGMMSG_userMatchesPerParamBit )
{
	UserMessage self;
	self.param_ = 0;
	self.uid_ = 4242;
	self.username_ = "cui";

	UserMessage query;
	query.param_ = UserMessage::PARAM_USE_UID |
		UserMessage::PARAM_USE_NAME;
	query.uid_ = self.uid_;
	query.username_ = self.username_;

	CHECK( self.matches( query ) );

	query.uid_ = 1;
	CHECK( !self.matches( query ) );

	query = UserMessage();
	query.param_ = UserMessage::PARAM_USE_UID |
		UserMessage::PARAM_USE_NAME;
	query.username_ = "other";
	CHECK( !self.matches( query ) );

	// Neither bit set: always a match.
	UserMessage untracked;
	untracked.param_ = 0;
	untracked.uid_ = 1;
	untracked.username_ = "other";
	CHECK( self.matches( untracked ) );
}


TEST( MGMMSG_resetRoundTrip )
{
	ResetMessage msg;

	ResetMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::RESET_MESSAGE ),
		int( pRoundTrip->message_ ) );

	delete pRoundTrip;
}


TEST( MGMMSG_pidRoundTrip )
{
	PidMessage msg;

	msg.pid_ = 9090;
	msg.running_ = 1;

	PidMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::PID_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK_EQUAL( 9090, int( pRoundTrip->pid_ ) );
	CHECK_EQUAL( 1, int( pRoundTrip->running_ ) );
	CHECK( strstr( pRoundTrip->c_str(), "PidMessage: 9090 1" ) != NULL );

	delete pRoundTrip;
}


TEST( MGMMSG_machinedAnnounceRoundTrip )
{
	MachinedAnnounceMessage birth;
	birth.type_ = MachinedAnnounceMessage::ANNOUNCE_BIRTH;
	birth.count_ = 5;

	MachinedAnnounceMessage * pBirth = roundTripMessage( birth );

	CHECK( pBirth != NULL );
	if (!pBirth)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::MACHINED_ANNOUNCE_MESSAGE ),
		int( pBirth->message_ ) );
	CHECK_EQUAL( int( MachinedAnnounceMessage::ANNOUNCE_BIRTH ),
		int( pBirth->type_ ) );
	CHECK_EQUAL( 5, int( pBirth->count_ ) );
	CHECK( strstr( pBirth->c_str(), "MachinedBirth: 5" ) != NULL );

	delete pBirth;

	// Death/exists shapes reuse the same bytes as addr_.
	MachinedAnnounceMessage death;
	death.type_ = MachinedAnnounceMessage::ANNOUNCE_DEATH;
	death.addr_ = 0x0A0000FE;

	MachinedAnnounceMessage * pDeath = roundTripMessage( death );

	CHECK( pDeath != NULL );
	if (!pDeath)
	{
		return;
	}

	CHECK_EQUAL( uint32( 0x0A0000FE ), pDeath->addr_ );
	CHECK( strstr( pDeath->c_str(), "MachinedDeath" ) != NULL );

	delete pDeath;
}


TEST( MGMMSG_platformRoundTrip )
{
	MachinePlatformMessage msg;

	msg.platformInfo_ = "Linux 7.0.0-38-generic";

	MachinePlatformMessage * pRoundTrip = roundTripMessage( msg );

	CHECK( pRoundTrip != NULL );
	if (!pRoundTrip)
	{
		return;
	}

	CHECK_EQUAL( int( MachineGuardMessage::MACHINE_PLATFORM_MESSAGE ),
		int( pRoundTrip->message_ ) );
	CHECK( strcmp( "Linux 7.0.0-38-generic", pRoundTrip->platformInfo_.c_str() ) == 0 );
	CHECK( strcmp( pRoundTrip->c_str(),
		"Linux 7.0.0-38-generic" ) == 0 );

	delete pRoundTrip;
}


TEST( MGMMSG_unknownEchosWithTypeFlag )
{
	// An unrecognised type id echoes back as UnknownMessage with the
	// not-understood flag set and the body preserved.
	MemoryOStream os;
	os << uint8( 0x55 ) << uint8( 0 );
	os << uint16( 7 );
	os << uint8( 'b' ) << uint8( 'w' ) << uint8( '3' ) << uint8( '0' );

	MemoryIStream is( os.data(), os.size() );
	MachineGuardMessage * pCreated = MachineGuardMessage::create( is );

	CHECK( pCreated != NULL );
	if (!pCreated)
	{
		return;
	}

	CHECK_EQUAL( 0x55, int( pCreated->message_ ) );
	CHECK( (pCreated->flags_ & MachineGuardMessage::MESSAGE_NOT_UNDERSTOOD )
		!= 0 );
	CHECK( strstr( pCreated->c_str(), "4 bytes long" ) != NULL );

	delete pCreated;
}


// -----------------------------------------------------------------------------
// Section: reply handler dispatch
// -----------------------------------------------------------------------------

TEST( MGMMSG_replyHandlerDispatchPerType )
{
	RecordingReplyHandler handler;

	{
		HighPrecisionMachineMessage msg;
		msg.message_ = MachineGuardMessage::HIGH_PRECISION_MACHINE_MESSAGE;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "highPrecision", handler.last_.c_str() ) == 0 );
	}
	{
		WholeMachineMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "wholeMachine", handler.last_.c_str() ) == 0 );
	}
	{
		ProcessMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "process", handler.last_.c_str() ) == 0 );
	}
	{
		ProcessStatsMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "processStats", handler.last_.c_str() ) == 0 );
	}
	{
		ListenerMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "listener", handler.last_.c_str() ) == 0 );
	}
	{
		CreateMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "create", handler.last_.c_str() ) == 0 );
	}
	{
		SignalMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "signal", handler.last_.c_str() ) == 0 );
	}
	{
		TagsMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "tags", handler.last_.c_str() ) == 0 );
	}
	{
		UserMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "user", handler.last_.c_str() ) == 0 );
	}
	{
		PidMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "pid", handler.last_.c_str() ) == 0 );
	}
	{
		ResetMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "reset", handler.last_.c_str() ) == 0 );
	}
	{
		ErrorMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "error", handler.last_.c_str() ) == 0 );
	}
	{
		MachinedAnnounceMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "machinedAnnounce", handler.last_.c_str() ) == 0 );
	}
	{
		QueryInterfaceMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "queryInterface", handler.last_.c_str() ) == 0 );
	}
	{
		CreateWithArgsMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "createWithArgs", handler.last_.c_str() ) == 0 );
	}
	{
		MachinePlatformMessage msg;
		CHECK( handler.dispatch( msg ) );
		CHECK( strcmp( "machinePlatform", handler.last_.c_str() ) == 0 );
	}
}


TEST( MGMMSG_replyHandlerDispatchesUnknownToUnhandled )
{
	RecordingReplyHandler handler;

	// A message id the switch does not know about hits its default arm.
	UnknownMessage unknown( (MachineGuardMessage::Message)0x55 );

	CHECK( handler.dispatch( unknown ) );
	CHECK( strcmp( "unhandled", handler.last_.c_str() ) == 0 );
}


TEST( MGMMSG_replyHandlerDefaultRoutesToUnhandled )
{
	MachineGuardMessage::ReplyHandler handler;

	// Every default handler falls through to onUnhandledMsg, which
	// returns true to keep processing.
	HighPrecisionMachineMessage highPrecision;
	CHECK( handler.handle( highPrecision, 0x7f000001 ) );

	WholeMachineMessage whole;
	CHECK( handler.handle( whole, 0x7f000001 ) );

	ProcessMessage process;
	CHECK( handler.handle( process, 0x7f000001 ) );

	ProcessStatsMessage processStats;
	CHECK( handler.handle( processStats, 0x7f000001 ) );

	ListenerMessage listener;
	CHECK( handler.handle( listener, 0x7f000001 ) );

	CreateMessage create;
	CHECK( handler.handle( create, 0x7f000001 ) );

	SignalMessage signal;
	CHECK( handler.handle( signal, 0x7f000001 ) );

	TagsMessage tags;
	CHECK( handler.handle( tags, 0x7f000001 ) );

	UserMessage user;
	CHECK( handler.handle( user, 0x7f000001 ) );

	PidMessage pid;
	CHECK( handler.handle( pid, 0x7f000001 ) );

	ResetMessage reset;
	CHECK( handler.handle( reset, 0x7f000001 ) );

	ErrorMessage error;
	CHECK( handler.handle( error, 0x7f000001 ) );

	MachinedAnnounceMessage announce;
	CHECK( handler.handle( announce, 0x7f000001 ) );

	QueryInterfaceMessage queryInterface;
	CHECK( handler.handle( queryInterface, 0x7f000001 ) );

	CreateWithArgsMessage createWithArgs;
	CHECK( handler.handle( createWithArgs, 0x7f000001 ) );

	MachinePlatformMessage platform;
	CHECK( handler.handle( platform, 0x7f000001 ) );
}

BW_END_NAMESPACE

// test_machine_guard_messages.cpp
