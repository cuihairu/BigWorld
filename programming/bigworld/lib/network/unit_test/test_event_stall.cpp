// test_event_stall.cpp
//
// Event loop stall metrics and the GIL-starvation fault injection they make
// observable (todo.md: 网络故障注入扩展 ③).
//
// When something hogs the thread running an EventDispatcher - a long script
// callback, a blocking I/O call, a Python thread starved of the GIL - the
// dispatcher's timers keep their deadlines but lose their delivery. The
// process() call that finally comes back to them delivers them late. This
// file pins down that signal: EventDispatcher::maxTimerLateness() /
// numLateTimers() (backed by TimeQueueT) must report the delay, so the
// GIL-starvation scenario has an assertion object.
//
// The same loop is also measured per tick for its network cost:
// maxNetworkTime() / numOverBudgetTicks() record what processNetwork()
// spends handling packets and socket events, with the poller's idle select
// wait excluded, against a configurable budget (todo.md: io_uring 评估的
// per-tick 网络耗时指标). Those tests use real sockets and the real clock -
// the measurement is deliberately wall-time and must not follow an injected
// clock.
//
// The stall is injected with a Mercury::FrequentTask that, on its first
// invocation, pushes the ManualClock far into the future - the task stands in
// for the hogging work and, because processOnce() runs frequent tasks before
// timers, the following processTimers() sees every timer that came due in
// the meantime as overdue. Nothing sleeps in wall time.

#include "pch.hpp"
#include "CppUnitLite2/src/CppUnitLite2.h"

#include "network/event_dispatcher.hpp"
#include "network/frequent_tasks.hpp"

#include "cstdmf/timer_handler.hpp"

#include <sys/socket.h>
#include <unistd.h>

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
 *	A frequent task that, the first time it runs, burns a large slice of
 *	clock by advancing it. This models the thing that starves an event loop:
 *	work that takes so long that timers due in the meantime cannot be
 *	delivered on time.
 */
class ClockHoggingTask : public Mercury::FrequentTask
{
public:
	ClockHoggingTask( ManualClock & clock, int64 hogMs ) :
		Mercury::FrequentTask( "ClockHoggingTask" ),
		clock_( clock ),
		hogMs_( hogMs ),
		numRuns_( 0 )
	{
	}

	int numRuns() const	{ return numRuns_; }

private:
	virtual void doTask()
	{
		++numRuns_;

		// Only hog once, so a later pump can be observed delivering on time.
		if (numRuns_ == 1)
		{
			clock_.advanceMs( hogMs_ );
		}
	}

	ManualClock & clock_;
	int64 hogMs_;
	int numRuns_;
};


/**
 *	An input handler that drains one datagram per delivery and counts them,
 *	so a test can see how many ticks the dispatcher spent in the handler.
 */
class ProbeHandler : public Mercury::InputNotificationHandler
{
public:
	ProbeHandler() : numReads_( 0 ) {}

	int numReads() const	{ return numReads_; }

private:
	virtual int handleInputNotification( int fd )
	{
		char buf[ 64 ];
		recv( fd, buf, sizeof( buf ), 0 );
		++numReads_;
		return 0;
	}

	int numReads_;
};


/**
 *	A fresh dispatcher has no stall history, and delivering a timer on its
 *	deadline does not create one.
 */
TEST( EventStall_noStallOnOnTimeDelivery )
{
	ManualClock clock;
	CountingHandler handler;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );

	// Pump once first so lastStatisticsGathered_ is populated and the
	// dispatcher is in its steady state.
	dispatcher.processOnce( /*shouldIdle:*/ false );

	TimerHandle handle = dispatcher.addOnceOffTimer( 1000000 /* 1s */,
		&handler, NULL );

	// Land exactly on the deadline.
	clock.advanceMs( 1000 );
	dispatcher.processOnce( /*shouldIdle:*/ false );

	CHECK( handler.count() == 1 );
	CHECK_EQUAL( 0U, dispatcher.maxTimerLateness() );
	CHECK_EQUAL( 0U, dispatcher.numLateTimers() );

	handle.cancel();
}


/**
 *	The core stall signal: a background task hogs the loop for 2s, and a
 *	timer that came due at +1s is delivered late as a result. This is the
 *	exact shape of GIL starvation - a timer due in the past, delivered only
 *	once the hogging work yields - and it must be assertable.
 */
TEST( EventStall_hoggingTaskMakesTimersLate )
{
	ManualClock clock;
	CountingHandler handler;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );

	// Pump once to get the dispatcher into steady state.
	dispatcher.processOnce( /*shouldIdle:*/ false );

	// A timer due at +1s.
	dispatcher.addOnceOffTimer( 1000000, &handler, NULL );

	// A background task that hogs 2s of clock on its first run.
	ClockHoggingTask hog( clock, 2000 /*ms*/ );
	dispatcher.addFrequentTask( &hog );

	// This single pump runs the hog (clock -> +2s) and then the timers,
	// so the timer due at +1s is delivered 1s late.
	dispatcher.processOnce( /*shouldIdle:*/ false );

	CHECK( handler.count() == 1 );

	// The timer was due at +1s but the loop only came back to it at +2s:
	// one second of lateness.
	const TimeStamp oneSecond = TimeStamp::fromSeconds( 1.0 );
	CHECK_EQUAL( oneSecond, dispatcher.maxTimerLateness() );
	CHECK_EQUAL( 1U, dispatcher.numLateTimers() );
}


/**
 *	An undisturbed dispatcher after a stall-free pump reports no lateness for
 *	timers that were given room to be delivered on time, confirming the
 *	metric isolates stalls rather than reporting them unconditionally.
 */
TEST( EventStall_recoveredLoopDeliversOnTime )
{
	ManualClock clock;
	CountingHandler handler;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );

	dispatcher.processOnce( /*shouldIdle:*/ false );

	// Stall the loop.
	dispatcher.addOnceOffTimer( 1000000, &handler, NULL );
	ClockHoggingTask hog( clock, 2000 );
	dispatcher.addFrequentTask( &hog );
	dispatcher.processOnce( /*shouldIdle:*/ false );
	CHECK( handler.count() == 1 );
	CHECK( dispatcher.maxTimerLateness() > 0 );

	// Reset the window and let a fresh timer land on time.
	dispatcher.resetTimerLateness();
	dispatcher.cancelFrequentTask( &hog );

	TimerHandle later = dispatcher.addOnceOffTimer( 1000000, &handler,
		NULL );

	// Pump with the clock 500ms short of the deadline, then step onto it.
	dispatcher.processOnce( /*shouldIdle:*/ false );
	CHECK( handler.count() == 1 );

	clock.advanceMs( 1000 );
	dispatcher.processOnce( /*shouldIdle:*/ false );

	CHECK( handler.count() == 2 );
	// No new stall accrued after the reset.
	CHECK_EQUAL( 0U, dispatcher.maxTimerLateness() );

	later.cancel();
}


/**
 *	now() reports the same stamps the dispatcher's timers are scheduled
 *	against, so a test can line a timer deadline up with the dispatcher's
 *	own notion of "now" without reaching for the clock object directly.
 */
TEST( EventStall_dispatcherNowMatchesItsClock )
{
	ManualClock clock;
	Mercury::EventDispatcher dispatcher( [&clock]() { return clock(); } );

	CHECK_EQUAL( clock.now(), dispatcher.now() );

	const uint64 start = clock.now();

	clock.advanceMs( 1234 );

	// now() moved by exactly the advance - the dispatcher's idea of "now"
	// is the clock it was built with, not a second source of time.
	CHECK_EQUAL( clock.now(), dispatcher.now() );
	CHECK( dispatcher.now() - start == TimeStamp::fromSeconds( 1.234 ) );
}

/**
 *	The per-tick network record: a datagram arrives on a registered socketpair
 *	end, and with the budget at zero the tick that handles it must show up in
 *	both the worst-tick cost and the over-budget count. The default clock is
 *	used - the measurement is wall time on purpose.
 */
TEST( EventStall_socketTickCountsAsNetworkTime )
{
	Mercury::EventDispatcher dispatcher;

	// Zero budget: any tick that handles a socket event is over budget.
	dispatcher.networkTimeBudget( 0.0 );
	CHECK_EQUAL( 0U, dispatcher.numOverBudgetTicks() );

	int fds[ 2 ] = { -1, -1 };
	CHECK( socketpair( AF_UNIX, SOCK_DGRAM, 0, fds ) == 0 );

	ProbeHandler handler;
	CHECK( dispatcher.registerFileDescriptor( fds[ 0 ], &handler, "probe" ) );

	// Send a datagram and pump until the handler has drained it.
	CHECK( send( fds[ 1 ], "ping", 4, 0 ) == 4 );
	for (int i = 0; i < 100 && handler.numReads() < 1; ++i)
	{
		dispatcher.processOnce( /*shouldIdle:*/ false );
	}

	CHECK( handler.numReads() == 1 );

	// Handling the datagram did real work: it lands in the worst-tick record
	// and, with a zero budget, in the over-budget count.
	CHECK( dispatcher.maxNetworkTime() > 0 );
	CHECK( dispatcher.numOverBudgetTicks() >= 1 );

	dispatcher.deregisterFileDescriptor( fds[ 0 ] );
	close( fds[ 0 ] );
	close( fds[ 1 ] );
}


/**
 *	Idle ticks do no network work: with a generous budget they stay inside
 *	it, and resetNetworkTimeStats() clears the record while leaving the
 *	budget setting in place.
 */
TEST( EventStall_idleTicksStayInBudgetAndResetClears )
{
	Mercury::EventDispatcher dispatcher;

	// One second a tick: idle ticks (nothing registered, no wait requested)
	// must not count as over budget.
	dispatcher.networkTimeBudget( 1.0 );

	for (int i = 0; i < 5; ++i)
	{
		dispatcher.processOnce( /*shouldIdle:*/ false );
	}

	CHECK_EQUAL( 0U, dispatcher.numOverBudgetTicks() );

	dispatcher.resetNetworkTimeStats();
	CHECK_EQUAL( 0U, dispatcher.maxNetworkTime() );
	CHECK_EQUAL( 0U, dispatcher.numOverBudgetTicks() );

	// Resetting the record keeps the budget itself.
	CHECK_EQUAL( 1.0, dispatcher.networkTimeBudget() );

	// The default budget is 10ms.
	Mercury::EventDispatcher defaultDispatcher;
	CHECK( defaultDispatcher.networkTimeBudget() > 0.0 );
	CHECK( defaultDispatcher.networkTimeBudget() < 0.011 );
}


} // end anonymous namespace

BW_END_NAMESPACE

// test_event_stall.cpp