#include "pch.hpp"

#include "cstdmf/checksum_stream.hpp"
#include "cstdmf/md5.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/bgtask_manager.hpp"

#include "connection/replay_controller.hpp"
#include "connection/replay_data.hpp"
#include "connection/replay_header.hpp"
#include "connection/replay_metadata.hpp"
#include "connection/replay_tick_loader.hpp"

#include <stdio.h>
#include <string.h>
#include <unistd.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

const char REPLAY_FILE_PATH[] = "/tmp/bw_replay_tick_loader_test.replay";
const size_t SIGNATURE_LENGTH = 16; // MD5SumScheme stream size


/**
 *	This class records the ReplayTickLoader callbacks handed to it, and owns
 *	any tick data lists it receives.
 */
class TestTickLoaderListener : public IReplayTickLoaderListener
{
public:
	TestTickLoaderListener() :
		gotHeader_( false ),
		firstGameTime_( 0 ),
		gameUpdateFrequency_( 0 ),
		numAppendTicks_( 0 ),
		pAppendStart_( NULL ),
		pAppendEnd_( NULL ),
		errorType_( ReplayTickLoader::ERROR_NONE )
	{}

	~TestTickLoaderListener()
	{
		while (pAppendStart_ != NULL)
		{
			ReplayTickData * pNext = pAppendStart_->pNext();
			delete pAppendStart_;
			pAppendStart_ = pNext;
		}
	}

	/* Override from IReplayTickLoaderListener. */
	virtual void onReplayTickLoaderReadHeader( const ReplayHeader & header,
		GameTime firstGameTime )
	{
		gotHeader_ = true;
		firstGameTime_ = firstGameTime;
		gameUpdateFrequency_ = header.gameUpdateFrequency();
	}

	/* Override from IReplayTickLoaderListener. */
	virtual void onReplayTickLoaderPrependTicks( ReplayTickData * pStart,
		ReplayTickData * pEnd, uint totalTicks )
	{
	}

	/* Override from IReplayTickLoaderListener. */
	virtual void onReplayTickLoaderAppendTicks( ReplayTickData * pStart,
		ReplayTickData * pEnd, uint totalTicks )
	{
		numAppendTicks_ = totalTicks;
		pAppendStart_ = pStart;
		pAppendEnd_ = pEnd;
	}

	/* Override from IReplayTickLoaderListener. */
	virtual void onReplayTickLoaderError(
		ReplayTickLoader::ErrorType errorType,
		const BW::string & errorString )
	{
		errorType_ = errorType;
		errorString_ = errorString;
	}

	bool gotHeader_;
	GameTime firstGameTime_;
	uint gameUpdateFrequency_;
	uint numAppendTicks_;
	ReplayTickData * pAppendStart_;
	ReplayTickData * pAppendEnd_;
	ReplayTickLoader::ErrorType errorType_;
	BW::string errorString_;

public:
	// Predicates for waiting on the background task's main thread callback.
	bool gotHeader() const 		{ return gotHeader_; }
	bool hasAppendTicks() const { return (numAppendTicks_ > 0); }
	bool hasError() const
	{
		return (errorType_ != ReplayTickLoader::ERROR_NONE);
	}
};


/**
 *	This helper writes a minimal replay file: a signed header, a signed
 *	meta-data block, then numTicks tick chunks. The tick payloads are opaque
 *	to the loader (it reads without decompressing), so they are plain bytes.
 *	The signatures use MD5 placeholders because the tests read with either
 *	no verifying key or a bad one, so nothing is verified.
 */
bool writeReplayFile( const char * path, int numTicks,
	GameTime firstGameTime )
{
	FILE * fp = fopen( path, "wb" );

	if (fp == NULL)
	{
		return false;
	}

	ChecksumSchemePtr pScheme = MD5SumScheme::create();
	MemoryOStream file;

	// Header.
	MD5 md5;
	md5.append( "BigWorld replay tick loader test", 32 );
	MD5::Digest digest;
	md5.getDigest( digest );

	ReplayHeader header( pScheme, digest, 10, "12345678" );
	header.addToStream( file, NULL );

	// Meta-data.
	ReplayMetaData metaData;
	metaData.init( pScheme );
	metaData.add( "recording", "test" );
	metaData.addToStream( file );

	// Tick chunks: signed chunk length, payload, signature placeholder.
	for (int i = 0; i < numTicks; ++i)
	{
		MemoryOStream payload;
		static const char TICK_DATA[] = "tick-payload";
		payload << GameTime( firstGameTime + i ) <<
			ReplayData::TickDataLength( sizeof( TICK_DATA ) - 1 );
		payload.addBlob( TICK_DATA, sizeof( TICK_DATA ) - 1 );

		file << ReplayData::SignedChunkLength( payload.size() );
		file.addBlob( payload.data(), payload.size() );

		char signature[ SIGNATURE_LENGTH ];
		memset( signature, 0, sizeof( signature ) );
		file.addBlob( signature, sizeof( signature ) );
	}

	const bool ok =
		(fwrite( file.data(), 1, file.size(), fp ) ==
			size_t( file.size() ));
	fclose( fp );

	return ok;
}


/**
 *	This helper pumps the task manager until the predicate becomes true or
 *	the bounded wait (10s) expires. Returns the final predicate value.
 */
bool waitFor( TaskManager & taskManager, bool (TestTickLoaderListener::*predicate)() const,
	TestTickLoaderListener & listener )
{
	static const int MAX_ITERATIONS = 1000;

	int iterations = 0;
	while (!(listener.*predicate)() && (iterations < MAX_ITERATIONS))
	{
		taskManager.tick();
		usleep( 10000 );
		++iterations;
	}

	return (listener.*predicate)();
}


} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: ReplayTickLoader
// -----------------------------------------------------------------------------

// A READ_HEADER request reads the header and the first tick (to establish
// the first game time) and reports both to the listener.
TEST( ReplayTickLoader_readHeader )
{
	CHECK( writeReplayFile( REPLAY_FILE_PATH, 1, 1000 ) );

	TaskManager taskManager;
	taskManager.startThreads( 1 );

	// ReplayTickLoader is refcounted and deleted by its loader tasks'
	// ReplayTickLoaderPtr when the last reference drops, so it must be
	// heap allocated (as ReplayController does).
	TestTickLoaderListener listener;
	ReplayTickLoaderPtr pLoader = new ReplayTickLoader( taskManager,
		&listener, REPLAY_FILE_PATH, "" );
	pLoader->addHeaderRequest();

	bool done = waitFor( taskManager,
		&TestTickLoaderListener::gotHeader, listener );
	taskManager.stopAll();

	unlink( REPLAY_FILE_PATH );

	CHECK( done );
	CHECK( listener.gotHeader_ );
	CHECK_EQUAL( 1000, listener.firstGameTime_ );
	CHECK_EQUAL( 10, listener.gameUpdateFrequency_ );
	CHECK( listener.errorType_ == ReplayTickLoader::ERROR_NONE );
}


// An APPEND request retrieves the tick range [start, end) as a linked list.
TEST( ReplayTickLoader_appendTickRange )
{
	CHECK( writeReplayFile( REPLAY_FILE_PATH, 3, 1000 ) );

	TaskManager taskManager;
	taskManager.startThreads( 1 );

	TestTickLoaderListener listener;
	ReplayTickLoaderPtr pLoader = new ReplayTickLoader( taskManager,
		&listener, REPLAY_FILE_PATH, "" );
	pLoader->addRequest( ReplayTickLoader::APPEND, 1, 3 );

	// The listener signals completion via a non-zero tick count.
	bool done = waitFor( taskManager,
		&TestTickLoaderListener::hasAppendTicks, listener );
	taskManager.stopAll();

	unlink( REPLAY_FILE_PATH );

	CHECK( done );
	CHECK_EQUAL( 2, listener.numAppendTicks_ );
	CHECK( listener.pAppendStart_ != NULL );
	CHECK( listener.pAppendStart_->pNext() ==
		listener.pAppendEnd_ );
	CHECK( listener.errorType_ == ReplayTickLoader::ERROR_NONE );
}


// A missing replay file is reported as ERROR_FILE_MISSING.
TEST( ReplayTickLoader_missingFile )
{
	TaskManager taskManager;
	taskManager.startThreads( 1 );

	TestTickLoaderListener listener;
	ReplayTickLoaderPtr pLoader = new ReplayTickLoader( taskManager,
		&listener,
		"/tmp/bw_replay_tick_loader_test_does_not_exist.replay", "" );
	pLoader->addHeaderRequest();

	bool done = waitFor( taskManager,
		&TestTickLoaderListener::hasError, listener );
	taskManager.stopAll();

	CHECK( done );
	CHECK( listener.errorType_ == ReplayTickLoader::ERROR_FILE_MISSING );
	CHECK( !listener.errorString_.empty() );
}


// A malformed verifying key surfaces the scheme's key error through the
// error path.
TEST( ReplayTickLoader_badVerifyingKey )
{
	CHECK( writeReplayFile( REPLAY_FILE_PATH, 1, 1000 ) );

	TaskManager taskManager;
	taskManager.startThreads( 1 );

	TestTickLoaderListener listener;
	ReplayTickLoaderPtr pLoader = new ReplayTickLoader( taskManager,
		&listener, REPLAY_FILE_PATH, "not-a-valid-key" );
	pLoader->addHeaderRequest();

	bool done = waitFor( taskManager,
		&TestTickLoaderListener::hasError, listener );
	taskManager.stopAll();

	unlink( REPLAY_FILE_PATH );

	CHECK( done );
	// A malformed PEM key leaves the scheme chain nominally "good" (the
	// chained scheme does not adopt the EC child's error), so the failure
	// surfaces when the header's signature is checked rather than as a key
	// error up front.
	CHECK( listener.errorType_ == ReplayTickLoader::ERROR_FILE_CORRUPTED );
	CHECK( !listener.errorString_.empty() );
	CHECK( listener.errorString_.find( "Failed to read header" ) !=
		BW::string::npos );
}


// Requests expose their parameters while queued and reset after completion;
// a header request is an APPEND-style request of the first tick.
TEST( ReplayTickLoader_requestBookkeeping )
{
	TaskManager taskManager;

	TestTickLoaderListener listener;
	ReplayTickLoaderPtr pLoader = new ReplayTickLoader( taskManager,
		&listener, REPLAY_FILE_PATH, "" );

	CHECK_EQUAL( BW::string( REPLAY_FILE_PATH ), pLoader->replayFilePath() );
	CHECK( pLoader->nextQueued() == ReplayTickLoader::NO_REQUEST );

	// No threads are started, so the request stays queued.
	pLoader->addHeaderRequest();
	CHECK( pLoader->nextQueued() == ReplayTickLoader::READ_HEADER );

	pLoader->onListenerDestroyed( &listener );

	taskManager.stopAll();
}


BW_END_NAMESPACE

// test_replay_tick_loader.cpp
