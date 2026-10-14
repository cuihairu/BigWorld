// manual_clock.hpp
//
// A scriptable stamp source for EventDispatcher's injectable clock
// (EventDispatcher::ClockFn, see todo.md: 虚拟时钟注入). Time stands still
// until advanceMs() is called, so a test can fire timers by advancing the
// clock between dispatcher pumps instead of sleeping in wall time.

#ifndef MANUAL_CLOCK_HPP
#define MANUAL_CLOCK_HPP

#include "cstdmf/timestamp.hpp"

BW_BEGIN_NAMESPACE

class ManualClock
{
public:
	ManualClock() : now_( timestamp() ) {}

	/// The clock function to hand to EventDispatcher's constructor.
	uint64 operator()() const	{ return now_; }

	uint64 now() const			{ return now_; }

	/// Moves the clock forward by the given number of milliseconds.
	void advanceMs( int64 milliseconds )
	{
		now_ += TimeStamp::fromSeconds( milliseconds / 1000.0 );
	}

private:
	uint64 now_;
};

BW_END_NAMESPACE

#endif // MANUAL_CLOCK_HPP

// manual_clock.hpp
