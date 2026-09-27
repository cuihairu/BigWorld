#include "pch.hpp"

#include "cstdmf/memory_stream.hpp"
#include "cstdmf/watcher.hpp"

#include "connection/login_challenge.hpp"
#include "connection/login_challenge_factory.hpp"

#include "test_login_challenge_interfaces.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	A challenge with a recognisable payload, used to prove that
 *	createChallenge() goes through the factory that was asked for.
 */
class CountingLoginChallenge : public LoginChallenge
{
public:
	explicit CountingLoginChallenge( int & creations ) :
		creations_( creations )
	{
		++creations_;
	}

	virtual bool writeChallengeToStream( BinaryOStream & data )
	{
		data << CHALLENGE_MAGIC;
		return true;
	}

	virtual bool readChallengeFromStream( BinaryIStream & data )
	{
		uint32 magic = 0;
		data >> magic;
		return !data.error() && magic == CHALLENGE_MAGIC;
	}

	virtual bool writeResponseToStream( BinaryOStream & data )
	{
		data << RESPONSE_MAGIC;
		return true;
	}

	virtual bool readResponseFromStream( BinaryIStream & data )
	{
		uint32 magic = 0;
		data >> magic;
		return !data.error() && magic == RESPONSE_MAGIC;
	}

private:
	static const uint32 CHALLENGE_MAGIC = 0xABCD1234u;
	static const uint32 RESPONSE_MAGIC = 0x1234ABCDu;

	int & creations_;
};


/**
 *	A factory that hands out CountingLoginChallenges and counts them, so a
 *	test can tell which factory createChallenge() went to.
 */
class CountingLoginChallengeFactory : public LoginChallengeFactory
{
public:
	CountingLoginChallengeFactory() :
		creations_( 0 )
	{}

	virtual LoginChallengePtr create()
	{
		return new CountingLoginChallenge( creations_ );
	}

	int creations() const	{ return creations_; }

private:
	int creations_;
};


/**
 *	Reads the single float a DelayLoginChallenge puts on the wire.
 */
float delayOf( LoginChallenge & challenge )
{
	MemoryOStream data;
	if (!challenge.writeChallengeToStream( data ))
	{
		return -1.f;
	}

	MemoryIStream in( data.data(), data.size() );
	float duration = 0.f;
	in >> duration;
	return in.error() ? -1.f : duration;
}

} // end namespace (anonymous)


// The container registers the three challenge types the engine ships with,
// and refuses names it does not know about.
TEST( LoginChallengeFactories_defaultRegistrations )
{
	LoginChallengeFactories factories;

	CHECK( factories.getFactory( "delay" ).hasObject() );
	CHECK( factories.getFactory( "fail" ).hasObject() );
	CHECK( factories.getFactory( "cuckoo_cycle" ).hasObject() );
	CHECK( !factories.getFactory( "nonesuch" ).hasObject() );

	// "fail" is the only default challenge that is cheap to run here;
	// "delay" and "cuckoo_cycle" have tests of their own.
	CHECK( factories.createChallenge( "fail" ).hasObject() );
	CHECK( !factories.createChallenge( "nonesuch" ).hasObject() );
}


// A registered factory is the one that gets asked to create challenges;
// re-registering a name swaps the entry and deregistering removes it.
TEST( LoginChallengeFactories_registerAndDeregister )
{
	LoginChallengeFactories factories;

	CountingLoginChallengeFactory * pFactory =
		new CountingLoginChallengeFactory();
	factories.registerFactory( "counting", pFactory );
	LoginChallengeFactoryPtr pFound = factories.getFactory( "counting" );
	CHECK( pFound.hasObject() );
	CHECK_EQUAL( static_cast< LoginChallengeFactory * >( pFactory ),
		pFound.get() );

	// createChallenge() really goes through that factory...
	LoginChallengePtr pChallenge = factories.createChallenge( "counting" );
	CHECK( pChallenge.hasObject() );
	CHECK_EQUAL( 1, pFactory->creations() );

	// ... and the challenge it made round trips its own payload.
	MemoryOStream challenge;
	CHECK( pChallenge->writeChallengeToStream( challenge ) );
	CHECK_EQUAL( 4, challenge.size() );
	MemoryOStream response;
	CHECK( pChallenge->writeResponseToStream( response ) );
	CHECK_EQUAL( 4, response.size() );

	MemoryIStream challengeIn( challenge.data(), challenge.size() );
	CHECK( pChallenge->readChallengeFromStream( challengeIn ) );
	MemoryIStream responseIn( response.data(), response.size() );
	CHECK( pChallenge->readResponseFromStream( responseIn ) );

	// Drop our reference before the factory is swapped out from under us.
	pChallenge = NULL;

	// Registering the same name again replaces the entry rather than
	// adding a second one.
	CountingLoginChallengeFactory * pReplacement =
		new CountingLoginChallengeFactory();
	factories.registerFactory( "counting", pReplacement );
	CHECK_EQUAL( static_cast< LoginChallengeFactory * >( pReplacement ),
		factories.getFactory( "counting" ).get() );
	CHECK( factories.createChallenge( "counting" ).hasObject() );
	CHECK_EQUAL( 1, pReplacement->creations() );
	CHECK_EQUAL( 1, pFactory->creations() );

	factories.deregisterFactory( "counting" );
	CHECK( !factories.getFactory( "counting" ).hasObject() );
	CHECK( !factories.createChallenge( "counting" ).hasObject() );
}


// The delay challenge carries a float to the client and expects the same
// float back; the delay itself is the factory's configured duration.
TEST( LoginChallengeFactories_configuresDelayChallenge )
{
	LoginChallengeFactories factories;

	// Configure the delay down to something a unit test can afford to
	// sleep for. The other two challenge types get no child config at all
	// and must be left alone rather than dropped. The root ends up in a
	// smart pointer - network_test crashes on leaked allocations at exit.
	TestChallengeConfig * pRootConfig = new TestChallengeConfig();
	TestChallengeConfig * pDelay = new TestChallengeConfig();
	pDelay->setDouble( "duration", 0.01 );
	pRootConfig->setChild( "delay", pDelay );
	LoginChallengeConfigPtr pRoot( pRootConfig );

	CHECK( factories.configureFactories( *pRoot ) );
	CHECK( factories.getFactory( "fail" ).hasObject() );
	CHECK( factories.getFactory( "cuckoo_cycle" ).hasObject() );

	LoginChallengePtr pServerChallenge = factories.createChallenge( "delay" );
	CHECK( pServerChallenge.hasObject() );
	CHECK_EQUAL( 0.01f, delayOf( *pServerChallenge ) );

	// A second challenge stands in for the client's copy: it is told the
	// duration, sleeps for it and echoes the value back.
	LoginChallengePtr pClientChallenge = factories.createChallenge( "delay" );

	MemoryOStream challenge;
	CHECK( pServerChallenge->writeChallengeToStream( challenge ) );
	CHECK_EQUAL( 4, challenge.size() );

	MemoryIStream challengeIn( challenge.data(), challenge.size() );
	CHECK( pClientChallenge->readChallengeFromStream( challengeIn ) );

	MemoryOStream response;
	CHECK( pClientChallenge->writeResponseToStream( response ) );
	CHECK_EQUAL( 4, response.size() );

	MemoryIStream responseIn( response.data(), response.size() );
	CHECK( pServerChallenge->readResponseFromStream( responseIn ) );

	// A response carrying some other duration is rejected.
	MemoryOStream wrong;
	wrong << 0.5f;
	MemoryIStream wrongIn( wrong.data(), wrong.size() );
	CHECK( !pServerChallenge->readResponseFromStream( wrongIn ) );
}


// A non-positive duration is refused, the previous value survives, and
// configureFactories() drops the factory that failed to configure.
TEST( LoginChallengeFactories_rejectsBadDelay )
{
	LoginChallengeFactories factories;
	LoginChallengeFactoryPtr pFactory = factories.getFactory( "delay" );
	CHECK( pFactory.hasObject() );

	// The factory keeps its default duration when configuration fails.
	TestChallengeConfig zero;
	zero.setDouble( "duration", 0.0 );
	CHECK( !pFactory->configure( zero ) );

	TestChallengeConfig negative;
	negative.setDouble( "duration", -1.0 );
	CHECK( !pFactory->configure( negative ) );

	LoginChallengePtr pChallenge = factories.createChallenge( "delay" );
	CHECK( pChallenge.hasObject() );
	CHECK_EQUAL( 1.f, delayOf( *pChallenge ) );

	// The same failure through the container removes the registration
	// entirely - the challenge type becomes unavailable. The root ends up
	// in a smart pointer - network_test crashes on leaked allocations at
	// exit.
	TestChallengeConfig * pRootConfig = new TestChallengeConfig();
	TestChallengeConfig * pBadDelay = new TestChallengeConfig();
	pBadDelay->setDouble( "duration", 0.0 );
	pRootConfig->setChild( "delay", pBadDelay );
	LoginChallengeConfigPtr pRoot( pRootConfig );
	CHECK( !factories.configureFactories( *pRoot ) );
	CHECK( !factories.getFactory( "delay" ).hasObject() );
	CHECK( !factories.createChallenge( "delay" ).hasObject() );

	// The survivors configure cleanly on a second pass.
	CHECK( factories.configureFactories( *pRoot ) );
}


// The "fail" challenge writes nothing at all and always refuses the
// response - which is the whole point of it.
TEST( LoginChallengeFactories_alwaysFailChallenge )
{
	LoginChallengeFactories factories;
	LoginChallengePtr pChallenge = factories.createChallenge( "fail" );
	CHECK( pChallenge.hasObject() );

	MemoryOStream nothing;
	CHECK( pChallenge->writeChallengeToStream( nothing ) );
	CHECK_EQUAL( 0, nothing.size() );
	CHECK( pChallenge->writeResponseToStream( nothing ) );
	CHECK_EQUAL( 0, nothing.size() );

	// Both read paths return success without consuming anything, so an
	// empty stream is enough to drive them.
	const char EMPTY[] = "";
	MemoryIStream empty( EMPTY, 0 );
	CHECK( pChallenge->readChallengeFromStream( empty ) );
	CHECK( !pChallenge->readResponseFromStream( empty ) );
}


// Every registered factory contributes a child to the watcher tree, with
// the ones that have no settings of their own as empty directories - the
// directory listing is how the supported challenge types are enumerated.
TEST( LoginChallengeFactories_addWatchers )
{
	LoginChallengeFactories factories;

	WatcherPtr pRoot = new DirectoryWatcher();
	factories.addWatchers( pRoot );

	CHECK( pRoot->getChild( "delay" ).hasObject() );
	CHECK( pRoot->getChild( "fail" ).hasObject() );
	CHECK( pRoot->getChild( "cuckoo_cycle" ).hasObject() );
	CHECK( !pRoot->getChild( "nonesuch" ).hasObject() );

	// "delay" and "cuckoo_cycle" expose their setting as a leaf; "fail"
	// has nothing to configure, so it gets an empty directory.
	CHECK( pRoot->getChild( "delay" )->getChild( "duration" ).hasObject() );
	CHECK( pRoot->getChild( "cuckoo_cycle" )->getChild( "easiness" ).hasObject() );
	CHECK( !pRoot->getChild( "fail" )->getChild( "duration" ).hasObject() );
}

BW_END_NAMESPACE

// test_login_challenge_factories.cpp
