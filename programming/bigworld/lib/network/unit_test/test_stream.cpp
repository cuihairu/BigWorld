#include "pch.hpp"

#include "CppUnitLite2/src/CppUnitLite2.h"
#include "common_interface.hpp"

#include "network/event_dispatcher.hpp"
#include "network/network_interface.hpp"
#include "network/channel_finder.hpp"
#include "network/udp_channel.hpp"


BW_BEGIN_NAMESPACE

Mercury::UDPChannel * pFromChannel;
Mercury::UDPChannel * pToChannel;

int g_numSent = 0;
const int NUM_SENDS = 1000;

namespace
{

class LocalHandler : public CommonHandler, public TimerHandler
{
public:
	LocalHandler( Mercury::EventDispatcher & dispatcher ) :
		dispatcher_( dispatcher )
	{
	}

protected:
	virtual void on_msg1( const Mercury::Address & srcAddr,
			const CommonInterface::msg1Args & args )
	{
		if (args.data != 0)
		{
			dispatcher_.breakProcessing();
		}
	}

	virtual void handleTimeout( TimerHandle handle, void * arg )
	{
		if (arg)
		{
			this->recreateChannel( handle, 
								   static_cast<Mercury::UDPChannel **>(arg) );
		}
		else
		{
			this->sendMsg1();
		}
	}

	void recreateChannel( TimerHandle handle, Mercury::UDPChannel ** ppChannel )
	{	
		if (g_numSent == NUM_SENDS)
		{
			// If we don't stop recreating the channels, resends don't get
			// processed because the last send time of unacked packets is reset
			// each time we initFromStream.
			return;
		}

		Mercury::UDPChannel * pOldChannel = *ppChannel;
		Mercury::UDPChannel * pNewChannel =
			new Mercury::UDPChannel( pOldChannel->networkInterface(), 
								  pOldChannel->addr(),
								  Mercury::UDPChannel::INTERNAL, 1.f, NULL, 1 );
		MemoryOStream mos;
		pOldChannel->addToStream( mos );
		pOldChannel->destroy();

		pNewChannel->initFromStream( mos, pNewChannel->addr() );
		pNewChannel->isLocalRegular( false );
		pNewChannel->isRemoteRegular( false );

		*ppChannel = pNewChannel;
	}

	void sendMsg1()
	{
		if (g_numSent < NUM_SENDS)
		{
			Mercury::Bundle & bundle = pFromChannel->bundle();
			CommonInterface::msg1Args & args =
				CommonInterface::msg1Args::start( bundle );
			args.seq = g_numSent;
			// The last one indicates "disconnect".
			args.data = (g_numSent == NUM_SENDS - 1);
			g_numSent++;
			pFromChannel->send();
		}
	}

private:
	Mercury::EventDispatcher & dispatcher_;
};


class MyChannelFinder : public Mercury::ChannelFinder
{
public:
	MyChannelFinder ( Mercury::UDPChannel*& myChannel ):
		myChannel_( myChannel )
	{
	}

	Mercury::UDPChannel* find( Mercury::ChannelID id, 
							const Mercury::Address & srcAddr,
							const Mercury::Packet * pPacket,
							bool & rHasBeenHandled )
	{
		if ( id == 1 )
		{
			rHasBeenHandled = false;
			return myChannel_;
		}
		return NULL;
	}

	Mercury::UDPChannel *& myChannel_;
};


class Alarm : public TimerHandler
{
public:
	Alarm( Mercury::EventDispatcher & dispatcher ) :
			TimerHandler(),
			dispatcher_( dispatcher ),
			didTimeout_( false )
	{}

	virtual ~Alarm()
	{}

	bool didTimeout() const { return didTimeout_; }

	virtual void handleTimeout( TimerHandle handle, void * arg )
	{
		didTimeout_ = true;
		dispatcher_.breakProcessing();
	}

private:
	Mercury::EventDispatcher & dispatcher_;
	bool didTimeout_;
};

} // anonymous namespace



TEST( Channel_stream )
{
	Mercury::EventDispatcher dispatcher;

	Mercury::NetworkInterface fromInterface( &dispatcher,
		Mercury::NETWORK_INTERFACE_INTERNAL );
	Mercury::NetworkInterface toInterface( &dispatcher,
		Mercury::NETWORK_INTERFACE_INTERNAL );

	fromInterface.setIrregularChannelsResendPeriod( 0.15 );
	toInterface.setIrregularChannelsResendPeriod( 0.15 );

	LocalHandler handler( dispatcher );

	fromInterface.pExtensionData( &handler );
	toInterface.pExtensionData( &handler );

	MyChannelFinder fromChannelFinder ( pFromChannel );
	MyChannelFinder toChannelFinder ( pToChannel );
	fromInterface.registerChannelFinder( &fromChannelFinder );
	toInterface.registerChannelFinder( &toChannelFinder );

	CommonInterface::registerWithInterface( fromInterface );
	CommonInterface::registerWithInterface( toInterface );

	pFromChannel = 
		new Mercury::UDPChannel( fromInterface, toInterface.address(),
							  Mercury::UDPChannel::INTERNAL, 1.f, NULL, 1 );
	pFromChannel->isLocalRegular( false );
	pFromChannel->isRemoteRegular( false );

	pToChannel = 
		new Mercury::UDPChannel( toInterface, fromInterface.address(),
							  Mercury::UDPChannel::INTERNAL, 1.f, NULL, 1 );
	pToChannel->isLocalRegular( false );
	pToChannel->isRemoteRegular( false );
	TimerHandle h1 = dispatcher.addTimer( 3000, &handler, &pToChannel );
	TimerHandle h2 = dispatcher.addTimer( 5000, &handler, &pFromChannel );
	TimerHandle h3 = dispatcher.addTimer( 7000, &handler, NULL );

	fromInterface.setLossRatio( 0.1 );

	Alarm alarm( dispatcher );
	TimerHandle h4 = dispatcher.addOnceOffTimer( 60000000, &alarm, NULL );

	dispatcher.processUntilBreak();

	CHECK( !alarm.didTimeout() );

	h1.cancel();
	h2.cancel();
	h3.cancel();
	h4.cancel();

	pFromChannel->destroy();
	pToChannel->destroy();

}


/**
 *	initFromStream() used to loop until numToReceive reached zero, but its
 *	slot supply can end before that happens (a truncated or corrupt stream):
 *	every slot that reads back as a hole left numToReceive untouched, so it
 *	span forever on createFromStream() NULLs. The loop is now bounded by the
 *	receive window's slot count.
 *
 *	The stream below declares a thousand real buffered receives but a window
 *	of four slots and supplies exactly four hole markers; the window bound
 *	must stop the loop there. Everything after the buffered-receive section is
 *	normal, so no read in this test may ever run past the end of the stream -
 *	that keeps the overread assertion out of the picture in assert builds
 *	(CR-P2-16).
 */
TEST( Channel_streamWindowBoundsBufferedReceives )
{
	Mercury::EventDispatcher dispatcher;

	Mercury::NetworkInterface interface( &dispatcher,
		Mercury::NETWORK_INTERFACE_INTERNAL );

	const Mercury::Address addr = interface.address();

	Mercury::UDPChannel * pChannel =
		new Mercury::UDPChannel( interface, addr,
			Mercury::UDPChannel::INTERNAL, 1.f, NULL, 1 );

	MemoryOStream os;

	// version, smallOutSeqAt_, largeOutSeqAt_, oldestUnackedSeq_ (SEQ_NULL so
	// the unacked pass is skipped) and the highestAck_ matching that.
	os << uint32( 1 ) << uint32( 1 ) << uint32( 1 ) << Mercury::SEQ_NULL
		<< uint32( 0 );

	// inSeqAt_, numBufferedReceives_ declaring far more real packets than the
	// window below has slots for, and the window size itself.
	os << uint32( 1 ) << uint32( 1000 ) << uint32( 4 );

	// The four slots of that window, all of them holes.
	os << uint8( 0 ) << uint8( 0 ) << uint8( 0 ) << uint8( 0 );

	// No chained fragments, no critical send pending, no first packet owed.
	os << uint16( 0 ) << Mercury::SEQ_NULL << uint8( 0 );

	// The trailing counters.
	os << uint32( 0 ) << uint32( 0 ) << uint32( 0 ) << uint32( 0 )
		<< uint32( 0 );

	pChannel->initFromStream( os, addr );

	// Everything the stream declared was consumed by the reads above - if the
	// loop had run past the window it would have eaten into these bytes or
	// overread the stream outright.
	CHECK( os.remainingLength() == 0 );

	pChannel->destroy();

}


/**
 *	createFromStream() of the chained-fragment section used to hand a NULL
 *	packet straight to FragmentedBundle's constructor (or addPacket), both of
 *	which dereference it. A corrupt stream - here a fragment record whose slot
 *	reads back empty - now stops the loop instead of crashing (CR-P2-17).
 */
TEST( Channel_streamEmptyChainedFragmentStops )
{
	Mercury::EventDispatcher dispatcher;

	Mercury::NetworkInterface interface( &dispatcher,
		Mercury::NETWORK_INTERFACE_INTERNAL );

	const Mercury::Address addr = interface.address();

	Mercury::UDPChannel * pChannel =
		new Mercury::UDPChannel( interface, addr,
			Mercury::UDPChannel::INTERNAL, 1.f, NULL, 1 );

	MemoryOStream os;

	// version, smallOutSeqAt_, largeOutSeqAt_, oldestUnackedSeq_ (SEQ_NULL so
	// the unacked pass is skipped) and the highestAck_ matching that.
	os << uint32( 1 ) << uint32( 1 ) << uint32( 1 ) << Mercury::SEQ_NULL
		<< uint32( 0 );

	// No buffered receives at all, then one declared chained fragment whose
	// slot reads back as empty: the packet comes back NULL.
	os << uint32( 1 ) << uint32( 0 ) << uint32( 0 );
	os << uint16( 1 ) << uint8( 0 );

	// No critical send pending, no first packet owed, and the trailing
	// counters.
	os << Mercury::SEQ_NULL << uint8( 0 );
	os << uint32( 0 ) << uint32( 0 ) << uint32( 0 ) << uint32( 0 )
		<< uint32( 0 );

	pChannel->initFromStream( os, addr );

	CHECK( os.remainingLength() == 0 );

	pChannel->destroy();

}

BW_END_NAMESPACE

// test_stream.cpp
