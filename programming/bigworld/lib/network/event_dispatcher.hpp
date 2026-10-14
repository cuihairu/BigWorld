#ifndef EVENT_DISPATCHER_HPP
#define EVENT_DISPATCHER_HPP

#ifdef _WIN32
	#ifdef _XBOX360
		#include <winsockx.h>
	#else
		#include <Winsock2.h> // for fdset
	#endif
#endif

#include "interfaces.hpp"

#include "cstdmf/bw_vector.hpp"
#include "cstdmf/timestamp.hpp"
#include "cstdmf/watcher.hpp"

#include <functional>

// #include "cstdmf/time_queue.hpp"

BW_BEGIN_NAMESPACE

template <class TYPE> class TimeQueueT;
typedef TimeQueueT< uint64 > TimeQueue64;

namespace Mercury
{

class DispatcherCoupling;
class ErrorReporter;
class EventPoller;
class FrequentTask;
class FrequentTasks;


/**
 *	This class is responsible for dispatching events. These include timers,
 *	network activity and file activity events.
 */
class EventDispatcher
{
public:
	/**
	 *	A source of TimeStamp values driving this dispatcher's timers and
	 *	wait computations. The default is the real-time clock (timestamp());
	 *	tests can inject a manual clock to drive timers without sleeping
	 *	(see unit_test/test_virtual_clock.cpp).
	 */
	typedef std::function< uint64() > ClockFn;

	/// The default clock source: the real-time stamps of timestamp().
	static uint64 realClock();

	/// The clock source driving this dispatcher (see ClockFn).
	const ClockFn & clock() const	{ return clock_; }

	EventDispatcher();
	explicit EventDispatcher( const ClockFn & clock );
	~EventDispatcher();

	void prepareForShutdown();

	void processContinuously();
	int  processOnce( bool shouldIdle = false );
	void processUntilBreak();
	void processUntilSignalled( bool & signal );

	void breakProcessing( bool breakState = true );
	bool processingBroken() const;

	void attach( EventDispatcher & childDispatcher );
	void detach( EventDispatcher & childDispatcher );

	bool registerFileDescriptor( int fd,
		InputNotificationHandler * handler,
		const char * name = "NotNamed" );
	bool deregisterFileDescriptor( int fd );
	
	bool registerWriteFileDescriptor( int fd,
		InputNotificationHandler * handler,
		const char * name = "NotNamed" );
	bool deregisterWriteFileDescriptor( int fd );

	INLINE TimerHandle addTimer( int64 microseconds, 
		TimerHandler * handler, void * arg = NULL, 
		const char * name = "UnnamedTimer" );
	INLINE TimerHandle addOnceOffTimer( int64 microseconds,
		TimerHandler * handler, void * arg = NULL,
		const char * name = "UnnamedTimer" );

	void addFrequentTask( FrequentTask * pTask );
	bool cancelFrequentTask( FrequentTask * pTask );

	uint64 timerDeliveryTime( TimerHandle handle ) const;
	uint64 timerIntervalTime( TimerHandle handle ) const;
	uint64 & timerIntervalTime( TimerHandle handle );

	uint64 getSpareTime() const;
	void clearSpareTime();
	double proportionalSpareTime() const;

	/**
	 *	The greatest lateness this dispatcher's timer queue has seen, in
	 *	stamps: how far past its deadline the worst late timer actually ran.
	 *	Zero means every timer so far has been delivered on time.
	 *
	 *	This is the event loop stall signal for a dispatcher: a background
	 *	task that hogs the thread (a long script callback, a blocking I/O
	 *	call, a starved thread) delays this dispatcher's timers, and the
	 *	delay shows up here. It is what todo.md's GIL starvation scenario
	 *	asserts on - see unit_test/test_event_stall.cpp.
	 */
	TimeStamp maxTimerLateness() const;

	/**
	 *	How many timers this dispatcher has run late, since construction or
	 *	since the last resetTimerLateness().
	 */
	uint32 numLateTimers() const;

	/**
	 *	Discards the accumulated lateness so the next reading covers only
	 *	what happens from here on. Pending timers are unaffected.
	 */
	void resetTimerLateness();

	/**
	 *	The dispatcher's clock source. Lets a caller see the same stamps the
	 *	dispatcher's timers are being scheduled against, whether that is the
	 *	real clock or one a test injected.
	 */
	INLINE TimeStamp now() const;

	INLINE double maxWait() const;
	INLINE void maxWait( double seconds );

	ErrorReporter & errorReporter()	{ return *pErrorReporter_; }
	
	
	int numChildren() const
	{
		return static_cast<int>( childDispatchers_.size() );
	}

#if ENABLE_WATCHERS
	static WatcherPtr pTimingWatcher();
	static WatcherPtr pWatcher();

	/**
	 *	Watchable form of maxTimerLateness(), in seconds.
	 */
	double maxTimerLatenessSeconds() const;

	/**
	 *	Watchable form of numLateTimers().
	 */
	uint32 lateTimerCount() const;
#endif

private:
	TimerHandle addTimerCommon( int64 microseconds,
		TimerHandler * handler,
		void * arg,
		bool recurrent,
		const char * name );

	void processFrequentTasks();
	void processTimers();
	void processStats();
	int processNetwork( bool shouldIdle );

	double calculateWait() const;

	void attachTo( EventDispatcher & parentDispatcher );
	void detachFrom( EventDispatcher & parentDispatcher );

	bool breakProcessing_;

	/**
	 *	Clock source driving processTimers(), processStats(), timer
	 *	registration and calculateWait(). prepareForShutdown() deliberately
	 *	stays on the real clock so shutdown always drains in wall time.
	 */
	ClockFn clock_;

	TimeQueue64 * pTimeQueue_;

	FrequentTasks * pFrequentTasks_;

	// Statistics
	TimeStamp		accSpareTime_;
	TimeStamp		oldSpareTime_;
	TimeStamp		totSpareTime_;
	TimeStamp		lastStatisticsGathered_;

	uint32 numTimerCalls_;

	double maxWait_;

	DispatcherCoupling * pCouplingToParent_;

	typedef BW::vector< EventDispatcher * > ChildDispatchers;
	ChildDispatchers childDispatchers_;

	EventPoller * pPoller_;

	ErrorReporter * pErrorReporter_;
};

} // namespace Mercury

#ifdef CODE_INLINE
#include "event_dispatcher.ipp"
#endif

BW_END_NAMESPACE

#endif // EVENT_DISPATCHER_HPP
