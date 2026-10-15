#include "pch.hpp"

#include "cstdmf/bw_map.hpp"

#include "network_app.hpp"
#include "common_interface.hpp"
#include "test_channel_interfaces.hpp"

#include "network/channel_finder.hpp"
#include "network/channel_owner.hpp"
#include "network/endpoint.hpp"
#include "network/network_interface.hpp"
#include "network/udp_channel.hpp"

#include "cstdmf/debug_filter.hpp"
#include "cstdmf/debug_message_callbacks.hpp"
#include "cstdmf/log_meta_data.hpp"
#include "cstdmf/memory_stream.hpp"

#if defined( _WIN32 )
// MSVC's SDK leaves `#define interface struct` (rpcndr.h via d3d9.h / shlobj /
// shlwapi / d3d10) live for the rest of the TU, which rewrites the
// `interface`-named declarations in this file into syntax errors (C2332/C2027
// in the root superproject build). Off for this file only; the pop below
// restores it for whatever SDK COM header comes later in the TU.
#pragma push_macro( "interface" )
#undef interface
#endif


BW_BEGIN_NAMESPACE


namespace
{
uint g_tickRate = 1000; // 1ms

// The number of messages the client should send to the server.  Also the
// maximum number of messages the server will send to the client.
unsigned NUM_ITERATIONS = 100;
}

// -----------------------------------------------------------------------------
// Section: Peer
// -----------------------------------------------------------------------------

/**
 *  Someone the app is talking to.  Servers can have more than one of these,
 *  clients should only have one.
 */
class Peer : public Mercury::ChannelOwner, public SafeReferenceCount
{
public:
	Peer( Mercury::NetworkInterface & networkInterface,
			const Mercury::Address & addr,
			Mercury::UDPChannel::Traits traits ) :
		Mercury::ChannelOwner( networkInterface, addr, traits ),
		timerHandle_(),
		inSeq_( 0 ),
		outSeq_( 0 )
	{}

	~Peer()
	{
		timerHandle_.cancel();
	}

	Mercury::EventDispatcher & dispatcher()
	{
		return this->channel().networkInterface().mainDispatcher();
	}

	void startTimer( Mercury::EventDispatcher & mainDispatcher, uint tickRate,
			TimerHandler * pHandler )
	{
		timerHandle_ = this->dispatcher().addTimer( tickRate, pHandler, this );
	}

	void sendNextMessage()
	{
		ClientChannelTestInterface::msg1Args & args =
			ClientChannelTestInterface::msg1Args::start( this->bundle() );

		args.seq = outSeq_++;
		args.data = 0;

		if (outSeq_ == NUM_ITERATIONS)
		{
			timerHandle_.cancel();
			this->channel().isLocalRegular( false );
			this->channel().isRemoteRegular( false );
		}

		this->send();
	}

	void receiveMessage( uint32 seq, uint32 data );
	void disconnect( uint32 seq );

private:
	TimerHandle timerHandle_;
	uint32 inSeq_;
	uint32 outSeq_;
};

typedef SmartPointer< Peer > PeerPtr;

// -----------------------------------------------------------------------------
// Section: ChannelServerApp
// -----------------------------------------------------------------------------

class ChannelServerApp : public NetworkApp
{
public:
	ChannelServerApp( Mercury::EventDispatcher & mainDispatcher, TEST_ARGS_PROTO ) :
		NetworkApp( mainDispatcher, Mercury::NETWORK_INTERFACE_INTERNAL, 
			TEST_ARGS )
	{
		// Dodgy singleton code
		MF_ASSERT( s_pInstance == NULL );
		s_pInstance = this;

		ServerChannelTestInterface::registerWithInterface(
			this->networkInterface() );
	}

	~ChannelServerApp()
	{
		MF_ASSERT( s_pInstance == this );
		s_pInstance = NULL;
	}

	void disconnect( const Mercury::Address & srcAddr,
			const ServerChannelTestInterface::disconnectArgs & args );

	void msg1( const Mercury::Address & srcAddr,
			const ServerChannelTestInterface::msg1Args & args );

	static ChannelServerApp & instance()
	{
		MF_ASSERT( s_pInstance != NULL );
		return *s_pInstance;
	}

	typedef BW::map< Mercury::Address, PeerPtr > Peers;

protected:
	void handleTimeout( TimerHandle handle, void * arg )
	{
		PeerPtr pPeer = (Peer*)arg;
		pPeer->sendNextMessage();
	}

private:
	PeerPtr startChannel( const Mercury::Address & addr,
		Mercury::UDPChannel::Traits traits );

	Peers peers_;

	static ChannelServerApp * s_pInstance;
};

ChannelServerApp * ChannelServerApp::s_pInstance = NULL;


/**
 *	Class for struct-style Mercury message handler objects.
 */
template <class ARGS> class ServerStructMessageHandler :
	public Mercury::InputMessageHandler
{
public:
	typedef void (ChannelServerApp::*Handler)(
			const Mercury::Address & srcAddr,
			const ARGS & args );

	ServerStructMessageHandler( Handler handler ) :
		handler_( handler )
	{}

private:
	virtual void handleMessage( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header, BinaryIStream & data )
	{
		ARGS * pArgs = (ARGS*)data.retrieve( sizeof(ARGS) );
		(ChannelServerApp::instance().*handler_)( srcAddr, *pArgs );
	}

	Handler handler_;
};


PeerPtr ChannelServerApp::startChannel( const Mercury::Address & addr,
	Mercury::UDPChannel::Traits traits )
{
	INFO_MSG( "Creating channel to %s\n", addr.c_str() );

	PeerPtr pPeer = new Peer( this->networkInterface(), addr, traits );
	peers_[ addr ] = pPeer;

	pPeer->startTimer( mainDispatcher_, g_tickRate, this );

	return pPeer;
}


// -----------------------------------------------------------------------------
// Section: ChannelServerApp Message Handlers
// -----------------------------------------------------------------------------

void ChannelServerApp::msg1( const Mercury::Address & srcAddr,
		const ServerChannelTestInterface::msg1Args & args )
{
	PeerPtr pPeer = peers_[ srcAddr ];

	// If this is the first message from this client, connect him now.
	if (pPeer == NULL)
	{
		pPeer = this->startChannel( srcAddr, args.traits );
	}

	pPeer->receiveMessage( args.seq, args.data );
}


void ChannelServerApp::disconnect( const Mercury::Address & srcAddr,
		const ServerChannelTestInterface::disconnectArgs & args )
{
	Peers::iterator peerIter = peers_.find( srcAddr );

	if (peerIter != peers_.end())
	{
		peerIter->second->disconnect( args.seq );
		peers_.erase( peerIter );

		if (peers_.empty())
		{
			this->dispatcher().breakProcessing();
		}
	}
	else
	{
		ERROR_MSG( "ChannelServerApp::disconnectArgs: "
				"Got message from unknown peer at %s\n",
			srcAddr.c_str() );
	}
}


// -----------------------------------------------------------------------------
// Section: ChannelClientApp
// -----------------------------------------------------------------------------

class ChannelClientApp : public NetworkApp
{
public:
	ChannelClientApp( Mercury::EventDispatcher & mainDispatcher,
			TEST_ARGS_PROTO,
			const Mercury::Address & dstAddr ) :
		NetworkApp( mainDispatcher, Mercury::NETWORK_INTERFACE_INTERNAL, 
			TEST_ARGS ),
		outSeq_( 0 ),
		numToSend_( NUM_ITERATIONS ),
		pChannel_( NULL )
	{
		pChannel_ =
			new Mercury::UDPChannel( this->networkInterface(), dstAddr, 
				Mercury::UDPChannel::INTERNAL );

		// Dodgy singleton code
		MF_ASSERT( s_pInstance == NULL );
		s_pInstance = this;

		// masterNub.add( interface_ );

		ClientChannelTestInterface::registerWithInterface(
			this->networkInterface() );
	}

	~ChannelClientApp()
	{
		pChannel_->destroy();
		pChannel_ = NULL;
		MF_ASSERT( s_pInstance == this );
		s_pInstance = NULL;
	}

	void startTest();

	void handleTimeout( TimerHandle handle, void * arg );

	void msg1( const Mercury::Address & srcAddr,
			const ClientChannelTestInterface::msg1Args & args );

	static ChannelClientApp & instance()
	{
		MF_ASSERT( s_pInstance != NULL );
		return *s_pInstance;
	}

private:
	uint32 outSeq_;
	uint32 numToSend_;
	Mercury::UDPChannel * pChannel_;

	static ChannelClientApp * s_pInstance;
};

ChannelClientApp * ChannelClientApp::s_pInstance = NULL;


/**
 *	Class for struct-style Mercury message handler objects.
 */
template <class ARGS> class ClientStructMessageHandler :
	public Mercury::InputMessageHandler
{
public:
	typedef void (ChannelClientApp::*Handler)(
			const Mercury::Address & srcAddr,
			const ARGS & args );

	ClientStructMessageHandler( Handler handler ) :
		handler_( handler )
	{}

private:
	virtual void handleMessage( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header, BinaryIStream & data )
	{
		ARGS * pArgs = (ARGS*)data.retrieve( sizeof(ARGS) );
		(ChannelClientApp::instance().*handler_)( srcAddr, *pArgs );
	}

	Handler handler_;
};


void ChannelClientApp::startTest()
{
	this->startTimer( g_tickRate );
}

void ChannelClientApp::handleTimeout( TimerHandle handle, void * arg )
{
	ServerChannelTestInterface::msg1Args & args =
		ServerChannelTestInterface::msg1Args::start( pChannel_->bundle() );

	args.traits = pChannel_->traits();
	args.seq = outSeq_++;
	args.data = 0;

	if (outSeq_ == numToSend_)
	{
		ServerChannelTestInterface::disconnectArgs & args =
			ServerChannelTestInterface::disconnectArgs::start( pChannel_->bundle() );

		args.seq = outSeq_;
		this->stopTimer();
		pChannel_->isLocalRegular( false );
		pChannel_->isRemoteRegular( false );
	}

	pChannel_->send();
}

void ChannelClientApp::msg1( const Mercury::Address & srcAddr,
			const ClientChannelTestInterface::msg1Args & args )
{
}


// -----------------------------------------------------------------------------
// Section: Peer
// -----------------------------------------------------------------------------

void Peer::receiveMessage( uint32 seq, uint32 data )
{
	MF_ASSERT( inSeq_ == seq );
	inSeq_ = seq + 1;
}

void Peer::disconnect( uint32 seq )
{
	MF_ASSERT( inSeq_ == seq );
}


/**
 *	This method is a simple channel test.
 */
TEST( Channel_testSimpleChannel )
{
	Mercury::EventDispatcher mainDispatcher;
	ChannelServerApp serverApp( mainDispatcher, TEST_ARGS );
	ChannelClientApp clientApp( mainDispatcher, TEST_ARGS, serverApp.networkInterface().address() );

	clientApp.startTest();
	serverApp.run();
}

/**
 *	This method is a channel test with loss.
 */
// This tests fails under windows: http://bugzilla/show_bug.cgi?id=14203
#if 0
#ifdef MF_SERVER
TEST( Channel_testLoss )
{
	float LOSS_RATIO = 0.1f;

	ChannelServerApp serverApp;
	ChannelClientApp clientApp( serverApp.networkInterface().address(),
			serverApp.mainDispatcher() );

	serverApp.networkInterface().setLossRatio( LOSS_RATIO );
	clientApp.networkInterface().setLossRatio( LOSS_RATIO );

	clientApp.startTest();
	serverApp.run();
}
#endif
#endif


// -----------------------------------------------------------------------------
// Section: Log correlation
// -----------------------------------------------------------------------------

namespace
{

// The explicit id both sides give their channel, so the receiving channel's
// id() is known when asserting the correlation field.
const Mercury::ChannelID CORR_CHANNEL_ID = 42;

// Bounds the non-idling delivery pump below; loopback delivery takes a
// handful of polls.
const int MAX_CORR_DELIVERY_PUMPS = 10000;

static const char s_channelMarker[] = "Channel log correlation marker";


/**
 *	Captures the metadata of marker messages so tests can assert which
 *	correlation fields a message dispatched on a channel carried.
 */
class ChannelCorrCapture : public DebugMessageCallback
{
public:
	ChannelCorrCapture() : sawMetaData_( false ) {}

	virtual bool handleMessage( DebugMessagePriority /*messagePriority*/,
			const char * /*pCategory*/, DebugMessageSource /*messageSource*/,
			const LogMetaData & metaData, const char * pFormat,
			va_list /*argPtr*/ )
	{
		if (strstr( pFormat, s_channelMarker ) == NULL)
		{
			return false;
		}

		const MemoryOStream * pStream = metaData.pData();
		if (pStream == NULL)
		{
			return true;
		}

		MemoryIStream is( const_cast< MemoryOStream * >( pStream )->data(),
			pStream->size() );

		while (is.remainingLength() > 0)
		{
			LogMetaData::MessageLoggerValueType keyType;
			LogMetaData::MessageLoggerValueType valueType;
			is >> keyType >> key_ >> valueType >> value_;

			if ((keyType == LogMetaData::MLVT_STRING) &&
					(valueType == LogMetaData::MLVT_LONG_LONG_INT))
			{
				sawMetaData_ = true;
			}
		}

		return true;
	}

	bool sawMetaData() const				{ return sawMetaData_; }
	const BW::string & key() const			{ return key_; }
	long long value() const					{ return value_; }

private:
	bool sawMetaData_;
	BW::string key_;
	long long value_;
};


/**
 *	A minimal in-process Mercury endpoint whose channel messages are
 *	dispatched with a handler that logs a marker (mirrors the Endpoint
 * pattern in test_request_failure.cpp).
 */
class CorrEndpoint : public CommonHandler
{
public:
	CorrEndpoint( Mercury::EventDispatcher & dispatcher ) :
		interface_( &dispatcher, Mercury::NETWORK_INTERFACE_INTERNAL ),
		channelFinder_( pChannel_ ),
		pChannel_( NULL ),
		numReceived_( 0 )
	{
		CommonInterface::registerWithInterface( interface_ );
		interface_.pExtensionData( this );
		interface_.registerChannelFinder( &channelFinder_ );
	}

	~CorrEndpoint()
	{
		if (pChannel_)
		{
			pChannel_->destroy();
			pChannel_ = NULL;
		}
	}

	Mercury::NetworkInterface & interface()	{ return interface_; }
	Mercury::Address address() const		{ return interface_.address(); }
	int numReceived() const					{ return numReceived_; }

	void initChannel( const Mercury::Address & addr )
	{
		MF_ASSERT( !pChannel_ );
		pChannel_ = new Mercury::UDPChannel( interface_, addr,
			Mercury::UDPChannel::INTERNAL, 1.f, NULL, CORR_CHANNEL_ID );
		pChannel_->isLocalRegular( false );
		pChannel_->isRemoteRegular( false );
	}

	Mercury::Bundle & channelBundle()		{ return pChannel_->bundle(); }
	void sendOnChannel()					{ pChannel_->send(); }

	// CommonHandler override: logs the marker inside the dispatch of a
	// channel message. The marker must be the format string itself so that
	// the capture callback sees it in pFormat.
	void on_msg1( const Mercury::Address & /*srcAddr*/,
			const CommonInterface::msg1Args & /*args*/ )
	{
		++numReceived_;
		INFO_MSG( s_channelMarker );
	}

private:
	class ChannelFinder : public Mercury::ChannelFinder
	{
	public:
		ChannelFinder( Mercury::UDPChannel *& rpChannel ):
			rpChannel_( rpChannel )
		{}

		virtual Mercury::UDPChannel * find( Mercury::ChannelID id,
				const Mercury::Address & /*srcAddr*/,
				const Mercury::Packet * /*pPacket*/,
				bool & rHasBeenHandled )
		{
			if (id == CORR_CHANNEL_ID)
			{
				rHasBeenHandled = false;
				return rpChannel_;
			}

			return NULL;
		}

	private:
		Mercury::UDPChannel *& rpChannel_;
	};

	Mercury::NetworkInterface	interface_;
	ChannelFinder				channelFinder_;
	Mercury::UDPChannel *		pChannel_;
	int							numReceived_;
};


/**
 *	Every log written while a channel message is being dispatched carries the
 *	id of the channel it arrived on.
 */
TEST( Channel_logCorrelation )
{
	Mercury::EventDispatcher dispatcher;
	CorrEndpoint server( dispatcher );
	CorrEndpoint client( dispatcher );

	// Both endpoints need a channel so that packets can be acked.
	client.initChannel( server.address() );
	server.initChannel( client.address() );

	ChannelCorrCapture capture;
	DebugFilter::instance().addMessageCallback( &capture );

	CommonInterface::msg1Args::start( client.channelBundle() );
	client.sendOnChannel();

	for (int i = 0; i < MAX_CORR_DELIVERY_PUMPS && server.numReceived() == 0;
			++i)
	{
		dispatcher.processOnce( /*shouldIdle:*/ false );
	}

	DebugFilter::instance().deleteMessageCallback( &capture );

	CHECK_EQUAL( 1, server.numReceived() );

	CHECK( capture.sawMetaData() );
	CHECK_EQUAL( 0, strcmp( "channelID", capture.key().c_str() ) );
	CHECK_EQUAL( (long long)CORR_CHANNEL_ID, capture.value() );
}

} // end anonymous namespace

BW_END_NAMESPACE


#define DEFINE_SERVER_HERE
#include "test_channel_interfaces.hpp"

// test_channel.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
