// test_request_failure.cpp
//
// Fault injection for the DBApp query path (see todo.md: 网络故障注入扩展).
//
// A caller that issues a Mercury request has to survive two distinct peer
// failures:
//
//  1. The peer stops answering. The request's timeout expires and
//     ReplyMessageHandler::handleException() is invoked with
//     REASON_TIMER_EXPIRED. This is what a BaseApp does when DBApp hangs.
//  2. The channel to the peer is lost. The channel is condemned (or the peer
//     disappears outright) and every pending request fails with
//     REASON_CHANNEL_LOST.
//
// Neither path was covered by the suite: the existing request tests either
// get an immediate reply, or install a handleException() that only logs.
//
// Note that requests deliberately never timeout on channels - see the
// WARNING_MSG in UDPBundle::startRequest("Requests never timeout on
// channels."). The timeout scenario below therefore issues a one-off request
// rather than a channel request, which is also the shape
// DBAppInterfaceUtils::executeRawDatabaseCommand() uses.
//
// All three scenarios run on a ManualClock (see test_virtual_clock.cpp): the
// clock stands still while loopback packets are delivered, and expiry is
// driven by advancing it. NetworkInterface's internal dispatcher follows the
// main dispatcher's clock source (see network_interface.cpp), so the request
// timeout timer sits on the same injected clock. Nothing in this file waits
// in wall time.

#include "pch.hpp"
#include "CppUnitLite2/src/CppUnitLite2.h"

#include "network/channel_finder.hpp"
#include "network/event_dispatcher.hpp"
#include "network/network_interface.hpp"
#include "network/nub_exception.hpp"
#include "network/udp_channel.hpp"
#include "network/interfaces.hpp"

#include "common_interface.hpp"

#include "manual_clock.hpp"

#include "unit_test_lib/unit_test.hpp"

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

const Mercury::ChannelID CHANNEL_ID = 1;

// Long enough that a healthy reply would arrive well before it, short enough
// to keep the suite fast.
const int SHORT_REQUEST_TIMEOUT = 100000;

// The timeout in ManualClock::advanceMs() units (milliseconds).
const int SHORT_REQUEST_TIMEOUT_MS = SHORT_REQUEST_TIMEOUT / 1000;

// Bound for the non-idling delivery pumps below. Loopback delivery takes a
// handful of polls; this only has to stop a wedged endpoint from hanging
// the suite.
const int MAX_DELIVERY_PUMPS = 10000;


/**
 *	Records what happened to a single Mercury request.
 */
class RequestProbe : public Mercury::ReplyMessageHandler
{
public:
	RequestProbe() :
		numMessages_( 0 ),
		numExceptions_( 0 ),
		lastReason_( Mercury::REASON_SUCCESS )
	{}

	int numMessages() const				{ return numMessages_; }
	int numExceptions() const			{ return numExceptions_; }
	Mercury::Reason lastReason() const	{ return lastReason_; }

private:
	virtual void handleMessage( const Mercury::Address & /*source*/,
			Mercury::UnpackedMessageHeader & /*header*/,
			BinaryIStream & /*data*/, void * /*arg*/ )
	{
		++numMessages_;
	}

	virtual void handleException( const Mercury::NubException & exception,
			void * /*arg*/ )
	{
		++numExceptions_;
		lastReason_ = exception.reason();
	}

	int numMessages_;
	int numExceptions_;
	Mercury::Reason lastReason_;
};


/**
 *	Pumps the dispatcher without idling until the predicate holds or the
 *	delivery budget runs out. Every pump is a non-blocking poll, so no
 *	clock - real or injected - moves during delivery.
 */
template< class PREDICATE >
void pumpUntil( Mercury::EventDispatcher & dispatcher, PREDICATE predicate )
{
	for (int i = 0; i < MAX_DELIVERY_PUMPS && !predicate(); ++i)
	{
		dispatcher.processOnce( /*shouldIdle:*/ false );
	}
}


/**
 *	An in-process Mercury endpoint. With shouldEcho set it answers every
 *	query, which stands in for a healthy DBApp; with it clear it swallows
 *	every query, which stands in for one that has hung.
 */
class Endpoint : public CommonHandler
{
public:
	Endpoint( Mercury::EventDispatcher & dispatcher, bool shouldEcho ) :
		interface_( &dispatcher, Mercury::NETWORK_INTERFACE_INTERNAL ),
		channelFinder_( pChannel_ ),
		pChannel_( NULL ),
		shouldEcho_( shouldEcho ),
		numReceived_( 0 )
	{
		CommonInterface::registerWithInterface( interface_ );
		interface_.pExtensionData( this );
		interface_.registerChannelFinder( &channelFinder_ );
	}

	~Endpoint()
	{
		if (pChannel_)
		{
			pChannel_->destroy();
			pChannel_ = NULL;
		}
	}

	Mercury::NetworkInterface & interface()
	{
		return interface_;
	}

	Mercury::Address address() const
	{
		return interface_.address();
	}

	int numReceived() const		{ return numReceived_; }

	// Gives this endpoint a channel to the given peer, so the two endpoints
	// can ack each other's packets. Mirrors App::initChannel() in
	// test_baseapp_death.cpp.
	void initChannel( const Mercury::Address & addr )
	{
		MF_ASSERT( !pChannel_ );
		pChannel_ = new Mercury::UDPChannel( interface_, addr,
			Mercury::UDPChannel::INTERNAL, 1.f, NULL, CHANNEL_ID );
		pChannel_->isLocalRegular( false );
		pChannel_->isRemoteRegular( false );
	}

	// Access to the channel set up by initChannel().
	Mercury::Bundle & channelBundle()		{ return pChannel_->bundle(); }
	void sendOnChannel()					{ pChannel_->send(); }
	// Channel::destroy() notifies NetworkInterface::onChannelGone()
	// synchronously, which is what tears down the pending requests. condemn()
	// on its own would defer the deletion to CondemnedChannels' AGE_LIMIT
	// (60s), far past this test's window.
	void destroyChannel()					{ pChannel_->destroy(); pChannel_ = NULL; }

	// CommonHandler override. This is the "database query" handler: it is the
	// point at which a healthy DBApp answers and a hung one does not.
	void on_msg1( const Mercury::Address & srcAddr,
			const CommonInterface::msg1Args & args )
	{
		++numReceived_;

		if (shouldEcho_)
		{
			Mercury::UDPBundle bundle;
			CommonInterface::msg1Args & outArgs =
				CommonInterface::msg1Args::start( bundle );
			outArgs.seq = args.seq;
			outArgs.data = 1;
			interface_.send( srcAddr, bundle );
		}
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
			if (id == CHANNEL_ID)
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
	bool						shouldEcho_;
	int							numReceived_;
};


/**
 *	A one-off request that is never answered must fail with
 *	REASON_TIMER_EXPIRED rather than hanging the caller.
 */
TEST( RequestFail_hungPeerTimesOut )
{
	ManualClock clock;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );
	Endpoint dbApp( dispatcher, /*shouldEcho: */false );
	Endpoint client( dispatcher, /*shouldEcho: */false );

	RequestProbe probe;

	Mercury::UDPBundle bundle;
	CommonInterface::msg1Args::startRequest( bundle, &probe, NULL,
		SHORT_REQUEST_TIMEOUT );
	client.interface().send( dbApp.address(), bundle );

	// The query travels over loopback while the clock stands still.
	pumpUntil( dispatcher, [&]() { return dbApp.numReceived() == 1; } );
	CHECK( dbApp.numReceived() == 1 );

	// Advance past the request timeout and the caller is told, instead of
	// blocking forever.
	clock.advanceMs( SHORT_REQUEST_TIMEOUT_MS + 1 );
	pumpUntil( dispatcher, [&]() { return probe.numExceptions() == 1; } );

	CHECK( probe.numMessages() == 0 );
	CHECK( probe.numExceptions() == 1 );
	CHECK( probe.lastReason() == Mercury::REASON_TIMER_EXPIRED );
}


/**
 *	A clock jump far past the request deadline must deliver the one-off
 *	expiry exactly once - the queue must not spin over the skipped span.
 */
TEST( RequestFail_clockJumpForwardTimesOutOnce )
{
	ManualClock clock;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );
	Endpoint dbApp( dispatcher, /*shouldEcho: */false );
	Endpoint client( dispatcher, /*shouldEcho: */false );

	RequestProbe probe;

	Mercury::UDPBundle bundle;
	CommonInterface::msg1Args::startRequest( bundle, &probe, NULL,
		SHORT_REQUEST_TIMEOUT );
	client.interface().send( dbApp.address(), bundle );

	pumpUntil( dispatcher, [&]() { return dbApp.numReceived() == 1; } );
	CHECK( dbApp.numReceived() == 1 );

	// One huge jump, then the expiry arrives exactly once.
	clock.advanceMs( 10 * SHORT_REQUEST_TIMEOUT_MS );
	pumpUntil( dispatcher, [&]() { return probe.numExceptions() == 1; } );

	CHECK( probe.numMessages() == 0 );
	CHECK( probe.numExceptions() == 1 );
	CHECK( probe.lastReason() == Mercury::REASON_TIMER_EXPIRED );
}


/**
 *	A backward clock jump must not fire a pending request early, and the
 *	request must still expire once the clock crosses its deadline again.
 */
TEST( RequestFail_clockBackwardJumpHoldsDeadline )
{
	ManualClock clock;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );
	Endpoint dbApp( dispatcher, /*shouldEcho: */false );
	Endpoint client( dispatcher, /*shouldEcho: */false );

	RequestProbe probe;

	Mercury::UDPBundle bundle;
	CommonInterface::msg1Args::startRequest( bundle, &probe, NULL,
		SHORT_REQUEST_TIMEOUT );
	client.interface().send( dbApp.address(), bundle );

	pumpUntil( dispatcher, [&]() { return dbApp.numReceived() == 1; } );
	CHECK( dbApp.numReceived() == 1 );

	// Jump behind the deadline and pump a little: nothing may fire early.
	clock.rewindMs( 5000 );
	for (int i = 0; i < 10; ++i)
	{
		dispatcher.processOnce( /*shouldIdle:*/ false );
	}
	CHECK( probe.numExceptions() == 0 );

	// Cross the deadline again and the expiry arrives.
	clock.advanceMs( 5000 + SHORT_REQUEST_TIMEOUT_MS + 1 );
	pumpUntil( dispatcher, [&]() { return probe.numExceptions() == 1; } );

	CHECK( probe.numMessages() == 0 );
	CHECK( probe.numExceptions() == 1 );
	CHECK( probe.lastReason() == Mercury::REASON_TIMER_EXPIRED );
}


/**
 *	The control case for RequestFail_hungPeerTimesOut: the same one-off
 *	message to an endpoint that does answer is delivered in both directions.
 *
 *	This deliberately sends a plain message rather than a request, so that no
 *	reply is expected and the test only asserts delivery.
 */
TEST( RequestFail_responsivePeerEchoes )
{
	Mercury::EventDispatcher dispatcher;
	Endpoint dbApp( dispatcher, /*shouldEcho: */true );
	Endpoint client( dispatcher, /*shouldEcho: */false );

	Mercury::UDPBundle bundle;
	CommonInterface::msg1Args::start( bundle );
	client.interface().send( dbApp.address(), bundle );

	pumpUntil( dispatcher, [&]()
		{ return dbApp.numReceived() == 1 && client.numReceived() == 1; } );

	CHECK( dbApp.numReceived() == 1 );
	CHECK( client.numReceived() == 1 );
}


/**
 *	Destroying the channel a request is pending on must fail that request
 *	with REASON_CHANNEL_LOST, which is the signal a caller sees when the
 *	network to DBApp is partitioned.
 */
TEST( RequestFail_channelLost )
{
	ManualClock clock;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );
	Endpoint dbApp( dispatcher, /*shouldEcho: */false );
	Endpoint client( dispatcher, /*shouldEcho: */false );

	// Both endpoints need a channel so that packets can be acked; without
	// the peer side the client channel would just keep resending.
	client.initChannel( dbApp.address() );
	dbApp.initChannel( client.address() );

	RequestProbe probe;
	Mercury::Bundle & bundle = client.channelBundle();
	CommonInterface::msg1Args::startRequest( bundle, &probe, NULL );
	client.sendOnChannel();

	// Wait for the query to reach DBApp before the partition. The frozen
	// clock keeps the channel's resend timers out of the picture.
	pumpUntil( dispatcher, [&]() { return dbApp.numReceived() == 1; } );
	CHECK( dbApp.numReceived() == 1 );

	// Tear the channel out from under the pending request. This runs
	// NetworkInterface::onChannelGone() synchronously, so the failure is
	// already recorded when destroy() returns.
	client.destroyChannel();

	CHECK( probe.numMessages() == 0 );
	CHECK( probe.numExceptions() == 1 );
	CHECK( probe.lastReason() == Mercury::REASON_CHANNEL_LOST );
}

} // end anonymous namespace

BW_END_NAMESPACE

// test_request_failure.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
