#include "pch.hpp"

#include "network/event_poller.hpp"
#include "network/interfaces.hpp"

#include <string.h>
#include <unistd.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	A handler that counts its calls, with switchable error handling and
 *	shutdown readiness.
 */
class FakeHandler : public Mercury::InputNotificationHandler
{
public:
	FakeHandler() :
		inputCalls_( 0 ),
		errorCalls_( 0 ),
		errorResult_( false ),
		ready_( true )
	{}

	virtual int handleInputNotification( int )
	{
		++inputCalls_;
		return 0;
	}

	virtual bool handleErrorNotification( int )
	{
		++errorCalls_;
		return errorResult_;
	}

	virtual bool isReadyForShutdown() const
	{
		return ready_;
	}

	int inputCalls_;
	int errorCalls_;
	bool errorResult_;
	bool ready_;
};


/**
 *	Re-exposes the protected base-class machinery so the tests can drive
 *	trigger/triggerError/maxFD directly, without depending on a concrete
 *	polling backend.
 */
class StubPoller : public Mercury::EventPoller
{
public:
	using EventPoller::isRegistered;
	using EventPoller::triggerRead;
	using EventPoller::triggerWrite;
	using EventPoller::triggerError;

	// Not a using-declaration: maxFD has a private static overload, and
	// importing the name wholesale would be an access error.
	int exposedMaxFD() const { return this->maxFD(); }

	virtual int processPendingEvents( double maxWait )
	{
		++processCalls_;
		return 0;
	}

protected:
	virtual bool doRegisterForRead( int fd, const char * name )
	{
		++doRegisterCalls_;
		return doRegisterResult_;
	}

	virtual bool doRegisterForWrite( int fd, const char * name )
	{
		++doRegisterCalls_;
		return doRegisterResult_;
	}

	virtual bool doDeregisterForRead( int fd )
	{
		return true;
	}

	virtual bool doDeregisterForWrite( int fd )
	{
		return true;
	}

public:
	int processCalls_ = 0;
	int doRegisterCalls_ = 0;
	bool doRegisterResult_ = true;
};


/**
 *	Creates a pair of connected pipes, closing both ends on destruction.
 */
class PipePair
{
public:
	PipePair()
	{
		fds_[ 0 ] = fds_[ 1 ] = -1;

		if (pipe( fds_ ) != 0)
		{
			fds_[ 0 ] = fds_[ 1 ] = -1;
		}
	}

	~PipePair()
	{
		for (int i = 0; i < 2; ++i)
		{
			if (fds_[ i ] != -1)
			{
				close( fds_[ i ] );
			}
		}
	}

	bool good() const { return fds_[ 0 ] != -1 && fds_[ 1 ] != -1; }

	int readEnd() const { return fds_[ 0 ]; }
	int writeEnd() const { return fds_[ 1 ]; }

	/**
	 *	Closes the write end (hanging up the read end) and disarms the
	 *	destructor entry so the fd is not closed twice.
	 */
	void closeWriteEnd()
	{
		if (fds_[ 1 ] != -1)
		{
			close( fds_[ 1 ] );
			fds_[ 1 ] = -1;
		}
	}

private:
	int fds_[ 2 ];
};

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: base-class machinery (through StubPoller)
// -----------------------------------------------------------------------------

// The registration maps: doRegister failure must leave no map entry, and
// successful register/deregister round-trips update isRegistered.
TEST( EventPoller_registerMapsTrackDoRegisterResult )
{
	StubPoller poller;
	FakeHandler handler;

	// Failure from the backend must not register anything.
	poller.doRegisterResult_ = false;
	CHECK( !poller.registerForRead( 7, &handler, "bad" ) );
	CHECK( !poller.isRegistered( 7, true ) );
	CHECK( !poller.registerForWrite( 7, &handler, "bad" ) );
	CHECK( !poller.isRegistered( 7, false ) );

	// Success path.
	poller.doRegisterResult_ = true;
	CHECK( poller.registerForRead( 7, &handler, "reader" ) );
	CHECK( poller.isRegistered( 7, true ) );
	CHECK( !poller.isRegistered( 7, false ) );
	CHECK( poller.registerForWrite( 8, &handler, "writer" ) );
	CHECK( poller.isRegistered( 8, false ) );

	CHECK( poller.deregisterForRead( 7 ) );
	CHECK( !poller.isRegistered( 7, true ) );
	CHECK( poller.deregisterForWrite( 8 ) );
	CHECK( !poller.isRegistered( 8, false ) );
	CHECK_EQUAL( 4, poller.doRegisterCalls_ );
}


// maxFD() spans both handler maps and returns -1 when both are empty.
TEST( EventPoller_maxFDAcrossMaps )
{
	StubPoller poller;
	FakeHandler handler;

	CHECK_EQUAL( -1, poller.exposedMaxFD() );

	CHECK( poller.registerForRead( 5, &handler, "r" ) );
	CHECK_EQUAL( 5, poller.exposedMaxFD() );

	CHECK( poller.registerForWrite( 9, &handler, "w" ) );
	CHECK_EQUAL( 9, poller.exposedMaxFD() );

	CHECK( poller.deregisterForWrite( 9 ) );
	CHECK_EQUAL( 5, poller.exposedMaxFD() );

	CHECK( poller.deregisterForRead( 5 ) );
	CHECK_EQUAL( -1, poller.exposedMaxFD() );
}


// triggerRead/triggerWrite dispatch only to registered fds, forwarding to
// the recorded handler.
TEST( EventPoller_triggersDispatchToRegisteredHandler )
{
	StubPoller poller;
	FakeHandler handler;

	// Nothing registered: no dispatch.
	CHECK( !poller.triggerRead( 3 ) );
	CHECK( !poller.triggerWrite( 3 ) );

	CHECK( poller.registerForRead( 3, &handler, "r" ) );
	CHECK( poller.triggerRead( 3 ) );
	CHECK( !poller.triggerWrite( 3 ) );
	CHECK_EQUAL( 1, handler.inputCalls_ );

	CHECK( poller.registerForWrite( 4, &handler, "w" ) );
	CHECK( poller.triggerWrite( 4 ) );
	CHECK_EQUAL( 2, handler.inputCalls_ );

	// Deregistering stops the dispatch.
	CHECK( poller.deregisterForRead( 3 ) );
	CHECK( !poller.triggerRead( 3 ) );
	CHECK_EQUAL( 2, handler.inputCalls_ );
}


// triggerError prefers the read handler, consults handleErrorNotification
// and falls back to input notification when the error is unclaimed; a write
// handler claims responsibility when no read handler exists; unknown fds
// report false.
TEST( EventPoller_triggerErrorArms )
{
	StubPoller poller;
	FakeHandler handler;

	// No handler at all.
	CHECK( !poller.triggerError( 3 ) );

	// Read handler that claims the error: no input fallback.
	CHECK( poller.registerForRead( 3, &handler, "r" ) );
	handler.errorResult_ = true;
	CHECK( poller.triggerError( 3 ) );
	CHECK_EQUAL( 1, handler.errorCalls_ );
	CHECK_EQUAL( 0, handler.inputCalls_ );

	// Unclaimed error falls back to input notification.
	handler.errorResult_ = false;
	CHECK( poller.triggerError( 3 ) );
	CHECK_EQUAL( 2, handler.errorCalls_ );
	CHECK_EQUAL( 1, handler.inputCalls_ );

	// Write-handler-only fd: errors route there when no read handler exists.
	CHECK( poller.registerForWrite( 4, &handler, "w" ) );
	handler.errorResult_ = true;
	CHECK( poller.triggerError( 4 ) );
	CHECK_EQUAL( 3, handler.errorCalls_ );
}


// isReadyForShutdown is the conjunction over every registered handler's
// readiness, and an empty poller is always ready.
TEST( EventPoller_isReadyForShutdownConjunction )
{
	StubPoller poller;
	FakeHandler reader;
	FakeHandler writer;

	CHECK( poller.isReadyForShutdown() );

	CHECK( poller.registerForRead( 3, &reader, "r" ) );
	CHECK( poller.isReadyForShutdown() );

	CHECK( poller.registerForWrite( 4, &writer, "w" ) );
	writer.ready_ = false;
	CHECK( !poller.isReadyForShutdown() );

	reader.ready_ = false;
	CHECK( !poller.isReadyForShutdown() );

	CHECK( poller.deregisterForWrite( 4 ) );
	// Still blocked by the read handler.
	CHECK( !poller.isReadyForShutdown() );
	reader.ready_ = true;
	CHECK( poller.isReadyForShutdown() );
}


// EventPoller is itself an InputNotificationHandler: its
// handleInputNotification just runs a zero-timeout poll pass. A stub poller
// has no poll descriptor of its own (the base getFileDescriptor default).
TEST( EventPoller_selfHandlerRunsPollPass )
{
	StubPoller poller;
	CHECK_EQUAL( 0, poller.handleInputNotification( 99 ) );
	CHECK_EQUAL( 1, poller.processCalls_ );
	CHECK_EQUAL( -1, poller.getFileDescriptor() );

	// spare-time bookkeeping round-trips.
	poller.clearSpareTime();
	CHECK_EQUAL( 0, int( poller.spareTime() ) );
}


// The INFORM_COUNT reporting arm fires every 100000 triggers, for both
// input and error counting.
TEST( EventPoller_inputHandlerEntryTriggerCounting )
{
	FakeHandler handler;
	Mercury::InputHandlerEntry entry( &handler, "counted" );

	for (uint32 i = 0; i < 100000; ++i)
	{
		entry.handleInputNotification( 1 );
	}

	CHECK_EQUAL( 100000, handler.inputCalls_ );
	CHECK_EQUAL( 100000, int( entry.numTriggers() ) );
	CHECK( strcmp( entry.name(), "counted" ) == 0 );

	// Error counting is symmetric (and crosses the INFORM_COUNT boundary).
	for (uint32 i = 0; i < 100000; ++i)
	{
		entry.handleErrorNotification( 1 );
	}

	CHECK_EQUAL( 100000, handler.errorCalls_ );
	CHECK_EQUAL( 100000, int( entry.numErrors() ) );
}


// -----------------------------------------------------------------------------
// Section: the platform poller (EventPoller::create on Linux: EPoller)
// -----------------------------------------------------------------------------

// create() hands back a live poller whose epoll descriptor is valid.
TEST( EventPoller_createReturnsUsablePoller )
{
	Mercury::EventPoller * pPoller = Mercury::EventPoller::create();
	CHECK( pPoller != NULL );

	if (pPoller != NULL)
	{
		// EPoller on Linux: a real epoll fd; SelectPoller would return -1.
		CHECK( pPoller->getFileDescriptor() >= 0 );
		delete pPoller;
	}
}


// A registered read fd wakes the poll: the handler sees the event and the
// next pass is idle.
TEST( EventPoller_pollDispatchesReadEvent )
{
	PipePair pipes;
	CHECK( pipes.good() );

	Mercury::EventPoller * pPoller = Mercury::EventPoller::create();
	CHECK( pPoller != NULL );

	FakeHandler handler;
	CHECK( pPoller->registerForRead( pipes.readEnd(), &handler, "pipe-r" ) );

	const char byte = 'x';
	CHECK( sizeof( byte ) == write( pipes.writeEnd(), &byte, 1 ) );

	CHECK_EQUAL( 1, pPoller->processPendingEvents( 0.0 ) );
	CHECK_EQUAL( 1, handler.inputCalls_ );

	// Drain the byte: level-triggered epoll would report it again.
	char drain = 0;
	CHECK( 1 == read( pipes.readEnd(), &drain, 1 ) );
	CHECK_EQUAL( int( 'x' ), int( drain ) );

	// Drained: nothing pending any more.
	CHECK_EQUAL( 0, pPoller->processPendingEvents( 0.0 ) );
	CHECK_EQUAL( 1, handler.inputCalls_ );

	delete pPoller;
}


// A registered write fd is reported immediately (the socket is writable).
TEST( EventPoller_pollDispatchesWriteEvent )
{
	PipePair pipes;
	CHECK( pipes.good() );

	Mercury::EventPoller * pPoller = Mercury::EventPoller::create();
	CHECK( pPoller != NULL );

	FakeHandler handler;
	CHECK( pPoller->registerForWrite( pipes.writeEnd(), &handler, "pipe-w" ) );

	CHECK_EQUAL( 1, pPoller->processPendingEvents( 0.0 ) );
	CHECK_EQUAL( 1, handler.inputCalls_ );

	delete pPoller;
}


// Registering both directions on one fd keeps both dispatching, and
// deregistering one direction leaves the other live (the epoll_ctl MOD
// path).
TEST( EventPoller_bothDirectionsAndSelectiveRemoval )
{
	PipePair pipes;
	CHECK( pipes.good() );

	Mercury::EventPoller * pPoller = Mercury::EventPoller::create();
	CHECK( pPoller != NULL );

	FakeHandler readHandler;
	FakeHandler writeHandler;
	CHECK( pPoller->registerForRead( pipes.readEnd(), &readHandler, "r" ) );
	CHECK( pPoller->registerForWrite( pipes.readEnd(), &writeHandler, "w" ) );

	// The read end of a pipe is never writable; only the read event lands.
	const char byte = 'y';
	CHECK( sizeof( byte ) == write( pipes.writeEnd(), &byte, 1 ) );
	CHECK_EQUAL( 1, pPoller->processPendingEvents( 0.0 ) );
	CHECK_EQUAL( 1, readHandler.inputCalls_ );
	CHECK_EQUAL( 0, writeHandler.inputCalls_ );

	// Remove the read interest: further data raises nothing.
	CHECK( pPoller->deregisterForRead( pipes.readEnd() ) );
	char drain = 0;
	// Exactly one byte was written above; a single read drains it (a
	// drain-to-empty loop would block forever: pipe fds are blocking and
	// the write end stays open).
	CHECK( 1 == read( pipes.readEnd(), &drain, 1 ) );
	CHECK( sizeof( byte ) == write( pipes.writeEnd(), &byte, 1 ) );
	CHECK_EQUAL( 0, pPoller->processPendingEvents( 0.0 ) );
	CHECK_EQUAL( 1, readHandler.inputCalls_ );

	delete pPoller;
}


// Deregistering an fd the poller never saw runs the epoll_ctl ENOENT arm
// (WARNING + false) on both directions.
TEST( EventPoller_epollDeregisterUnknownFdFails )
{
	Mercury::EventPoller * pPoller = Mercury::EventPoller::create();
	CHECK( pPoller != NULL );

	if (pPoller != NULL)
	{
		CHECK( !pPoller->deregisterForRead( 9999 ) );
		CHECK( !pPoller->deregisterForWrite( 9999 ) );
		delete pPoller;
	}
}


// Registering an invalid fd fails through the epoll_ctl non-ENOENT (ERROR)
// arm and leaves nothing registered.
TEST( EventPoller_epollRegisterBadFdFails )
{
	Mercury::EventPoller * pPoller = Mercury::EventPoller::create();
	CHECK( pPoller != NULL );

	FakeHandler handler;

	if (pPoller != NULL)
	{
		CHECK( !pPoller->registerForRead( -1, &handler, "bad" ) );
		CHECK( !pPoller->registerForWrite( -1, &handler, "bad" ) );
		delete pPoller;
	}
}


// A hangup surfaces through triggerError: an unclaimed error falls back to
// input notification (the handler sees the EOF itself).
TEST( EventPoller_pollHangupRoutesToErrorHandler )
{
	PipePair pipes;
	CHECK( pipes.good() );

	Mercury::EventPoller * pPoller = Mercury::EventPoller::create();
	CHECK( pPoller != NULL );

	FakeHandler handler;
	CHECK( pPoller->registerForRead( pipes.readEnd(), &handler, "r" ) );

	// Closing the write end hangs up the read end.
	pipes.closeWriteEnd();

	CHECK_EQUAL( 1, pPoller->processPendingEvents( 0.0 ) );
	// The EPOLLERR|EPOLLHUP arm routes to the error handler first.
	CHECK_EQUAL( 1, handler.errorCalls_ );
	CHECK_EQUAL( 1, handler.inputCalls_ );

	delete pPoller;
}

// test_event_poller.cpp
BW_END_NAMESPACE
