// test_virtual_clock.cpp
//
// Virtual clock injection for EventDispatcher (todo.md: 虚拟时钟注入).
//
// The dispatcher's timers and wait computations are driven from an
// injectable ClockFn instead of the global inline timestamp(), so timer
// behaviour can be exercised by advancing a ManualClock between
// processOnce() calls rather than by sleeping in wall time. The default
// constructor keeps the real clock; the last test pins that default down.

#include "pch.hpp"
#include "CppUnitLite2/src/CppUnitLite2.h"

#include "network/event_dispatcher.hpp"

#include "cstdmf/timer_handler.hpp"

#include "manual_clock.hpp"

#include "unit_test_lib/unit_test.hpp"

BW_BEGIN_NAMESPACE

namespace
{

/**
 *	Counts handleTimeout() invocations.
 */
class CountingHandler : public TimerHandler
{
public:
	CountingHandler() : count_( 0 ) {}

	int count() const	{ return count_; }

private:
	virtual void handleTimeout( TimerHandle /*handle*/, void * /*arg*/ )
	{
		++count_;
	}

	int count_;
};

/**
 *	A one-off timer registered under a manual clock must fire on process once
 *	the clock has crossed its deadline - without any real time passing - and
 *	only once.
 */
TEST( VirtualClock_oneOffTimerFiresWithoutSleeping )
{
	ManualClock clock;
	CountingHandler handler;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );

	dispatcher.addOnceOffTimer( 5000000 /* 5s */, &handler, NULL );

	// Far from the deadline: pumps must not fire it.
	clock.advanceMs( 4999 );
	dispatcher.processOnce( /*shouldIdle:*/ false );
	CHECK( handler.count() == 0 );

	// Crossing the deadline fires exactly once.
	clock.advanceMs( 2 );
	dispatcher.processOnce( false );
	CHECK( handler.count() == 1 );

	// And never again.
	clock.advanceMs( 1000 );
	dispatcher.processOnce( false );
	CHECK( handler.count() == 1 );
}

/**
 *	A repeating timer fires once per full advance of its interval, exactly at
 *	the virtual deadline.
 */
TEST( VirtualClock_repeatingTimerFiresPerAdvance )
{
	ManualClock clock;
	CountingHandler handler;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );

	TimerHandle handle = dispatcher.addTimer( 1000000 /* 1s */,
		&handler, NULL );

	for (int i = 0; i < 3; ++i)
	{
		CHECK( handler.count() == i );
		clock.advanceMs( 1000 );
		dispatcher.processOnce( false );
	}
	CHECK( handler.count() == 3 );

	handle.cancel();
}

/**
 *	Timer bookkeeping is expressed in virtual time: a timer scheduled for 2s
 *	reports its delivery time relative to the manual clock's registration
 *	time, not the wall clock.
 */
TEST( VirtualClock_timerDeliveryTimeUsesVirtualNow )
{
	ManualClock clock;
	CountingHandler handler;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );

	TimerHandle handle = dispatcher.addOnceOffTimer( 2000000, &handler, NULL );

	CHECK( dispatcher.timerDeliveryTime( handle ) ==
		clock.now() + TimeStamp::fromSeconds( 2.0 ) );

	handle.cancel();
}

/**
 *	The default-constructed dispatcher must keep using the real clock: a
 *	short one-off timer fires from real elapsed time. processOnce(true)
 *	idles in the poller until the timer is due; the spin bound keeps a
 *	pathological machine from hanging the suite.
 */
TEST( VirtualClock_defaultDispatcherUsesRealTime )
{
	Mercury::EventDispatcher dispatcher;

	CountingHandler handler;
	dispatcher.addOnceOffTimer( 30000 /* 30ms */, &handler, NULL );

	for (int i = 0; i < 100 && handler.count() == 0; ++i)
	{
		dispatcher.processOnce( /*shouldIdle:*/ true );
	}
	CHECK( handler.count() == 1 );
}

} // end anonymous namespace

BW_END_NAMESPACE

// test_virtual_clock.cpp
