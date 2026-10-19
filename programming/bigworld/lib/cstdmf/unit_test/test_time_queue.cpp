#include "pch.hpp"

#include "cstdmf/time_queue.hpp"


BW_BEGIN_NAMESPACE

class Handler : public TimerHandler
{
protected:
	virtual void handleTimeout( TimerHandle handle, void * pUser )
	{
	}

	virtual void onRelease( TimerHandle handle, void * pUser )
	{
		delete this;
	}
};

TEST( Purge )
{
	BW::vector< TimerHandle > handles;

	TimeQueue timeQueue;

	const uint NUM_TIMERS = 50;

	for (uint i = 0; i < NUM_TIMERS; ++i)
	{
		handles.push_back( timeQueue.add( i, 0, new Handler, NULL, "Handler" ) );
	}

	CHECK( timeQueue.size() == NUM_TIMERS );

	// Cancel more than half
	uint numToCancel = 2 * NUM_TIMERS / 3;

	for (uint i = 0; i < numToCancel; ++i)
	{
		handles[i].cancel();
	}

	// If more than half of the timers are cancelled, they should be purged
	// immediately.
	CHECK( timeQueue.size() < NUM_TIMERS/2 );
}

TEST( BadPurge )
{
	// Check the case where purge is called while a once-off timer is being
	// cancelled.
	TimeQueue timeQueue;
	timeQueue.add( 1, 0, new Handler, NULL, "Handler" );
	timeQueue.process( 2 );
}


/**
 *	Records the stamps at which it was called, so a test can tell how late
 *	the queue actually got round to firing a timer.
 */
class DeadlineHandler : public TimerHandler
{
public:
	DeadlineHandler() : calls_( 0 ) {}

	int calls() const	{ return calls_; }

	// The queue's 'now' when the timer fired, per call.
	int callStamps() const	{ return callStamps_; }

	void setNow( int now )		{ now_ = now; }

private:
	virtual void handleTimeout( TimerHandle /*handle*/, void * /*pUser*/ )
	{
		++calls_;
		callStamps_ = now_;
	}

	virtual void onRelease( TimerHandle /*handle*/, void * /*pUser*/ )
	{
	}

	int calls_;
	int callStamps_;
	int now_;
};


/**
 *	A timer fired exactly on its deadline is not late, and a queue that has
 *	never been stalled must report no lateness at all.
 */
TEST( TimeQueue_latenessIsZeroOnTimeDelivery )
{
	TimeQueue timeQueue;
	DeadlineHandler * pHandler = new DeadlineHandler();

	timeQueue.add( 100, 0, pHandler, NULL, "OnTime" );
	pHandler->setNow( 100 );

	CHECK_EQUAL( 1, timeQueue.process( 100 ) );
	CHECK_EQUAL( 1, pHandler->calls() );
	CHECK_EQUAL( 100, pHandler->callStamps() );

	CHECK_EQUAL( 0U, timeQueue.maxLateness() );
	CHECK_EQUAL( 0U, timeQueue.numLateTimers() );
}


/**
 *	The queue reports how far past its deadline each late timer ran: this is
 *	the event loop stall signal - a timer that was 500 stamps overdue was
 *	delayed by 500 stamps of work the loop did not get to.
 */
TEST( TimeQueue_latenessReportsOverdueDelivery )
{
	TimeQueue timeQueue;
	DeadlineHandler * pHandler = new DeadlineHandler();

	// Due at 100, but the loop only comes back at 600: 500 late.
	timeQueue.add( 100, 0, pHandler, NULL, "Late" );
	pHandler->setNow( 600 );

	CHECK_EQUAL( 1, timeQueue.process( 600 ) );
	CHECK_EQUAL( 600, pHandler->callStamps() );

	CHECK_EQUAL( 500U, timeQueue.maxLateness() );
	CHECK_EQUAL( 1U, timeQueue.numLateTimers() );
}


/**
 *	Several timers can come due in one pass. The worst of them sets
 *	maxLateness, and every one of them counts as late.
 */
TEST( TimeQueue_latenessTracksWorstCaseAcrossBatch )
{
	TimeQueue timeQueue;
	DeadlineHandler * pFirst = new DeadlineHandler();
	DeadlineHandler * pSecond = new DeadlineHandler();
	DeadlineHandler * pThird = new DeadlineHandler();

	// Deadlines 10, 20, 30 processed in one pass at now == 60, so the
	// latenesses are 50, 40 and 30.
	timeQueue.add( 10, 0, pFirst, NULL, "First" );
	timeQueue.add( 20, 0, pSecond, NULL, "Second" );
	timeQueue.add( 30, 0, pThird, NULL, "Third" );

	CHECK_EQUAL( 3, timeQueue.process( 60 ) );

	CHECK_EQUAL( 50U, timeQueue.maxLateness() );
	CHECK_EQUAL( 3U, timeQueue.numLateTimers() );
}


/**
 *	A repeating timer that has fallen behind catches up over every missed
 *	deadline in one pass, and each firing is measured against its own
 *	deadline - not against the first missed one, which would compound.
 */
TEST( TimeQueue_repeatingTimerLatenessIsPerDeadline )
{
	TimeQueue timeQueue;
	DeadlineHandler * pHandler = new DeadlineHandler();

	TimerHandle handle =
		timeQueue.add( 100, 10, pHandler, NULL, "Repeating" );

	// First pass at 130: the timer catches up over deadlines 100, 110, 120
	// and 130 - four firings, latenesses 30, 20, 10 and 0.
	CHECK_EQUAL( 4, timeQueue.process( 130 ) );
	CHECK_EQUAL( 30U, timeQueue.maxLateness() );
	CHECK_EQUAL( 3U, timeQueue.numLateTimers() );

	// Second pass at 140: the next deadline is 140 itself, so zero lateness.
	// Measuring against the first missed deadline would say 40.
	CHECK_EQUAL( 1, timeQueue.process( 140 ) );
	CHECK_EQUAL( 30U, timeQueue.maxLateness() );
	CHECK_EQUAL( 3U, timeQueue.numLateTimers() );

	handle.cancel();
}


/**
 *	resetLateness() clears the stall record without disturbing the queue's
 *	contents, so a caller can measure one window (a second, a tick) at a
 *	time.
 */
TEST( TimeQueue_resetLatenessKeepsTimers )
{
	TimeQueue timeQueue;
	DeadlineHandler * pHandler = new DeadlineHandler();

	timeQueue.add( 100, 0, pHandler, NULL, "Late" );
	timeQueue.process( 300 );

	CHECK_EQUAL( 200U, timeQueue.maxLateness() );

	timeQueue.resetLateness();

	CHECK_EQUAL( 0U, timeQueue.maxLateness() );
	CHECK_EQUAL( 0U, timeQueue.numLateTimers() );

	// The queue is untouched: the timer already fired, and it is not
	// resurrected by the reset.
	CHECK_EQUAL( 0, timeQueue.process( 400 ) );
}

BW_END_NAMESPACE

// test_time_queue.cpp
