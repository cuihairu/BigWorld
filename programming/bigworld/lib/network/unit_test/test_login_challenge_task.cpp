#include "pch.hpp"

#include "cstdmf/memory_stream.hpp"

#include "connection/login_challenge.hpp"
#include "connection/login_challenge_task.hpp"
#include "connection/login_handler.hpp"
#include "connection/log_on_status.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	A trivial challenge that answers with a fixed payload, so that
 *	perform() can be run without mining a real proof of work.
 */
class TestLoginChallenge : public LoginChallenge
{
public:
	/* Override from LoginChallenge. */
	virtual bool writeChallengeToStream( BinaryOStream & data )
	{
		data << BW::string( "test-challenge" );
		return true;
	}

	/* Override from LoginChallenge. */
	virtual bool readChallengeFromStream( BinaryIStream & data )
	{
		BW::string payload;
		data >> payload;
		return !data.error() && (payload == "test-challenge");
	}

	/* Override from LoginChallenge. */
	virtual bool writeResponseToStream( BinaryOStream & data )
	{
		++responseWrites_;
		data << BW::string( "test-response" );
		return true;
	}

	/* Override from LoginChallenge. */
	virtual bool readResponseFromStream( BinaryIStream & data )
	{
		BW::string payload;
		data >> payload;
		return !data.error() && (payload == "test-response" );
	}

	int responseWrites() const	{ return responseWrites_; }

private:
	int responseWrites_;
};


/**
 *	A LoginHandler built purely to be a reference-counted anchor for the
 *	task. It is constructed with a non-NOT_SET status so that it counts
 *	as finished and its destructor does not try to drive a login
 *	through a NULL ServerConnection.
 */
LoginHandlerPtr newIdleHandler()
{
	return new LoginHandler( /* pServerConnection */ NULL,
		LogOnStatus( LogOnStatus::LOGGED_ON ) );
}

} // end namespace (anonymous)


// perform() runs the challenge exactly once, records how long it took,
// flips the finished flag and leaves the response in data() where the
// request can pick it up.
TEST( LoginChallengeTask_performWritesResponse )
{
	LoginHandlerPtr pHandler = newIdleHandler();
	TestLoginChallenge * pChallenge = new TestLoginChallenge();
	LoginChallengePtr pChallengeHolder( pChallenge );

	LoginChallengeTask task( *pHandler, pChallenge );

	// Nothing has run yet.
	CHECK( !task.isFinished() );
	CHECK_EQUAL( 0, int( task.data().size() ) );
	CHECK_EQUAL( 0.f, task.calculationDuration() );

	task.perform();

	CHECK( task.isFinished() );
	CHECK_EQUAL( 1, pChallenge->responseWrites() );

	// The response the challenge wrote is readable back out of data(),
	// and the duration was measured (a float, so only the sign is
	// meaningful for a challenge this fast).
	CHECK( task.data().size() > 0 );
	CHECK( task.calculationDuration() >= 0.f );

	pChallengeHolder->readResponseFromStream( task.data() );
}


// A second perform() is refused: the challenge is not asked again, the
// response is not appended to, and the recorded duration is left alone.
TEST( LoginChallengeTask_performIsNotRepeatable )
{
	LoginHandlerPtr pHandler = newIdleHandler();
	TestLoginChallenge * pChallenge = new TestLoginChallenge();
	LoginChallengePtr pChallengeHolder( pChallenge );

	LoginChallengeTask task( *pHandler, pChallenge );

	task.perform();

	const int sizeAfterFirst = int( task.data().size() );
	const float durationAfterFirst = task.calculationDuration();

	// The refusal itself is an ERROR_MSG, which the unit test's
	// DebugFilter swallows; what is observable is that nothing moved.
	task.perform();

	CHECK_EQUAL( 1, pChallenge->responseWrites() );
	CHECK_EQUAL( sizeAfterFirst, int( task.data().size() ) );
	CHECK_EQUAL( durationAfterFirst, task.calculationDuration() );
	CHECK( task.isFinished() );
}


// disassociate() drops the handler reference the task holds; the task
// stays usable (its own state and response are untouched), it simply no
// longer has anywhere to report completion to.
TEST( LoginChallengeTask_disassociateDropsHandler )
{
	LoginHandlerPtr pHandler = newIdleHandler();
	TestLoginChallenge * pChallenge = new TestLoginChallenge();
	LoginChallengePtr pChallengeHolder( pChallenge );

	LoginChallengeTask task( *pHandler, pChallenge );
	task.disassociate();
	task.perform();

	CHECK( task.isFinished() );
	CHECK_EQUAL( 1, pChallenge->responseWrites() );
	CHECK( task.data().size() > 0 );
}

BW_END_NAMESPACE

// test_login_challenge_task.cpp
