#include "pch.hpp"

#include "cstdmf/binary_stream.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/watcher.hpp"

#include "connection/cuckoo_cycle_login_challenge_factory.hpp"
#include "connection/login_challenge.hpp"
#include "connection/login_challenge_factory.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	This class is a minimal LoginChallengeConfig implementation used to drive
 *	the configure() code paths without needing libresmgr.
 */
class TestLoginChallengeConfig : public LoginChallengeConfig
{
public:
	TestLoginChallengeConfig( double doubleValue, bool hasDoubleValue,
			bool hasChild = true ) :
		doubleValue_( doubleValue ),
		hasDoubleValue_( hasDoubleValue ),
		hasChild_( hasChild )
	{}

	/* Override from LoginChallengeConfig. */
	virtual LoginChallengeConfigPtr getChild(
		const BW::string & childName ) const
	{
		if (!hasChild_)
		{
			return NULL;
		}

		return new TestLoginChallengeConfig( doubleValue_, hasDoubleValue_,
			false );
	}

	/* Override from LoginChallengeConfig. */
	virtual const BW::string getString( const BW::string & path,
		BW::string defaultValue ) const
	{
		return defaultValue;
	}

	/* Override from LoginChallengeConfig. */
	virtual long getLong( const BW::string & path, long defaultValue ) const
	{
		return defaultValue;
	}

	/* Override from LoginChallengeConfig. */
	virtual double getDouble( const BW::string & path,
		double defaultValue ) const
	{
		return hasDoubleValue_ ? doubleValue_ : defaultValue;
	}

private:
	double doubleValue_;
	bool hasDoubleValue_;
	bool hasChild_;
};


/**
 *	This class is a trivial challenge used to exercise factory registration.
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
		data << BW::string( "test-response" );
		return true;
	}

	/* Override from LoginChallenge. */
	virtual bool readResponseFromStream( BinaryIStream & data )
	{
		BW::string payload;
		data >> payload;
		return !data.error() && (payload == "test-response");
	}
};


/**
 *	This class is a factory for TestLoginChallenge instances.
 */
class TestLoginChallengeFactory : public LoginChallengeFactory
{
public:
	/* Override from LoginChallengeFactory. */
	virtual LoginChallengePtr create()
	{
		return new TestLoginChallenge();
	}
};


/**
 *	This helper streams a challenge out and returns its prefix string (the
 *	random "XXXXXXXXXXXXXXXX:" identifier the cuckoo cycle challenge keys its
 *	proof-of-work graph on).
 */
BW::string challengePrefix( LoginChallenge & challenge )
{
	MemoryOStream stream;

	if (!challenge.writeChallengeToStream( stream ))
	{
		return BW::string();
	}

	MemoryIStream reader( stream.data(), stream.size() );
	int length = reader.readPackedInt();
	const char * pPrefix = static_cast< const char * >(
		reader.retrieve( length ) );

	return (pPrefix != NULL) ? BW::string( pPrefix, length ) : BW::string();
}


/**
 *	This class exposes the protected pWatcher() override for testing.
 */
class WatcherExposingCuckooCycleFactory :
	public CuckooCycleLoginChallengeFactory
{
public:
	WatcherPtr exposedPWatcher() { return this->pWatcher(); }
};


} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: LoginChallengeFactories
// -----------------------------------------------------------------------------

// A fresh collection registers the default challenge types, createChallenge
// hands out instances of the registered type, and deregistered types are no
// longer reachable until the defaults are restored.
TEST( LoginChallenge_factoriesRegistry )
{
	LoginChallengeFactories factories;

	CHECK( factories.getFactory( "delay" ).hasObject() );
	CHECK( factories.getFactory( "fail" ).hasObject() );
	CHECK( factories.getFactory( "cuckoo_cycle" ).hasObject() );
	CHECK( !factories.getFactory( "nonexistent" ).hasObject() );

	LoginChallengePtr pChallenge = factories.createChallenge( "fail" );
	CHECK( pChallenge.hasObject() );
	CHECK( !factories.createChallenge( "nonexistent" ).hasObject() );

	factories.deregisterFactory( "fail" );
	CHECK( !factories.getFactory( "fail" ).hasObject() );
	CHECK( !factories.createChallenge( "fail" ).hasObject() );

	// The default set can be restored after deregistration.
	factories.registerDefaultFactories();
	CHECK( factories.getFactory( "fail" ).hasObject() );
	CHECK( factories.createChallenge( "fail" ).hasObject() );
}


// A factory registered under a custom name creates its own challenge type.
TEST( LoginChallenge_customFactory )
{
	LoginChallengeFactories factories;

	factories.registerFactory( "test_challenge",
		new TestLoginChallengeFactory() );
	CHECK( factories.getFactory( "test_challenge" ).hasObject() );

	LoginChallengePtr pChallenge = factories.createChallenge(
		"test_challenge" );
	CHECK( pChallenge.hasObject() );

	// The created challenge round trips its own payload format.
	MemoryOStream stream;
	CHECK( pChallenge->writeChallengeToStream( stream ) );

	MemoryIStream reader( stream.data(), stream.size() );
	CHECK( pChallenge->readChallengeFromStream( reader ) );
	CHECK_EQUAL( 0, int( reader.remainingLength() ) );
}


// The delay challenge streams its duration to the client, who echoes it back
// after sleeping, and the server accepts a matching (and only a matching)
// duration.
TEST( LoginChallenge_delayRoundTrip )
{
	LoginChallengeFactories factories;

	// Configure a short duration so that the response's client-side sleep
	// stays negligible.
	TestLoginChallengeConfig config( 0.05, true );
	CHECK( factories.configureFactories( config ) );

	LoginChallengePtr pServer = factories.createChallenge( "delay" );
	CHECK( pServer.hasObject() );

	MemoryOStream challengeData;
	CHECK( pServer->writeChallengeToStream( challengeData ) );
	CHECK_EQUAL( 4, challengeData.size() ); // one float

	LoginChallengePtr pClient = factories.createChallenge( "delay" );
	MemoryIStream challengeReader( challengeData.data(), challengeData.size() );
	CHECK( pClient->readChallengeFromStream( challengeReader ) );

	MemoryOStream responseData;
	CHECK( pClient->writeResponseToStream( responseData ) );
	CHECK_EQUAL( 4, responseData.size() );

	MemoryIStream responseReader( responseData.data(), responseData.size() );
	CHECK( pServer->readResponseFromStream( responseReader ) );

	// A tampered duration is rejected.
	MemoryOStream fakeData;
	float fakeDuration = 0.5f;
	fakeData << fakeDuration;

	MemoryIStream fakeReader( fakeData.data(), fakeData.size() );
	CHECK( !pServer->readResponseFromStream( fakeReader ) );
}


// The "fail" challenge always succeeds at streaming but always fails
// verification, without touching the streams.
TEST( LoginChallenge_failChallenge )
{
	LoginChallengeFactories factories;

	LoginChallengePtr pChallenge = factories.createChallenge( "fail" );
	CHECK( pChallenge.hasObject() );

	MemoryOStream stream;
	CHECK( pChallenge->writeChallengeToStream( stream ) );
	CHECK_EQUAL( 0, stream.size() );

	MemoryIStream emptyReader( "", 0 );
	CHECK( pChallenge->readChallengeFromStream( emptyReader ) );

	MemoryOStream response;
	CHECK( pChallenge->writeResponseToStream( response ) );
	CHECK_EQUAL( 0, response.size() );

	MemoryIStream responseReader( response.data(), response.size() );
	CHECK( !pChallenge->readResponseFromStream( responseReader ) );
}


// configureFactories() configures each registered factory from the child
// config of the same name; factories that reject their configuration are
// deregistered and the failure is reported.
TEST( LoginChallenge_configureFactories )
{
	// The cuckoo cycle factory rejects easiness outside (0, 100].
	{
		LoginChallengeFactories factories;
		TestLoginChallengeConfig config( 200.0, true );
		CHECK( !factories.configureFactories( config ) );
		CHECK( !factories.getFactory( "cuckoo_cycle" ).hasObject() );

		// The other factories configured successfully and stay registered.
		CHECK( factories.getFactory( "delay" ).hasObject() );
		CHECK( factories.getFactory( "fail" ).hasObject() );
	}

	// Zero easiness is rejected too.
	{
		LoginChallengeFactories factories;
		TestLoginChallengeConfig config( 0.0, true );
		CHECK( !factories.configureFactories( config ) );
		CHECK( !factories.getFactory( "cuckoo_cycle" ).hasObject() );
	}

	// With no child configs provided, nothing is configured and all
	// factories keep their defaults.
	{
		LoginChallengeFactories factories;
		TestLoginChallengeConfig config( 0.0, false, /* hasChild */ false );
		CHECK( factories.configureFactories( config ) );
		CHECK( factories.getFactory( "cuckoo_cycle" ).hasObject() );
	}
}


// -----------------------------------------------------------------------------
// Section: CuckooCycleLoginChallengeFactory
// -----------------------------------------------------------------------------

// The factory defaults to 50% easiness and clamps manually assigned values
// into [0, 100].
TEST( CuckooCycleFactory_defaultsAndClamping )
{
	CuckooCycleLoginChallengeFactory factory;
	CHECK_CLOSE( 50.0, factory.easiness(), 0.0001 );

	factory.easiness( 75.0 );
	CHECK_CLOSE( 75.0, factory.easiness(), 0.0001 );

	factory.easiness( 150.0 );
	CHECK_CLOSE( 100.0, factory.easiness(), 0.0001 );

	factory.easiness( -20.0 );
	CHECK_CLOSE( 0.0, factory.easiness(), 0.0001 );
}


// configure() reads the "easiness" double, keeps the current value when the
// entry is absent and rejects out-of-range values. The factory also exposes
// a watcher for its easiness.
TEST( CuckooCycleFactory_configure )
{
	CuckooCycleLoginChallengeFactory factory;
	factory.easiness( 60.0 );

	// No "easiness" entry: the current value is kept.
	TestLoginChallengeConfig emptyConfig( 0.0, /* hasDoubleValue */ false );
	CHECK( factory.configure( emptyConfig ) );
	CHECK_CLOSE( 60.0, factory.easiness(), 0.0001 );

	TestLoginChallengeConfig config( 80.0, true );
	CHECK( factory.configure( config ) );
	CHECK_CLOSE( 80.0, factory.easiness(), 0.0001 );

	TestLoginChallengeConfig tooBig( 100.001, true );
	CHECK( !factory.configure( tooBig ) );

	TestLoginChallengeConfig zero( 0.0, true );
	CHECK( !factory.configure( zero ) );

	TestLoginChallengeConfig negative( -5.0, true );
	CHECK( !factory.configure( negative ) );

#if ENABLE_WATCHERS
	WatcherExposingCuckooCycleFactory watcherFactory;
	WatcherPtr pWatcher = watcherFactory.exposedPWatcher();
	CHECK( pWatcher.hasObject() );
#endif
}


// A challenge streams its prefix and maximum nonce, a client-side challenge
// adopts them, and re-serialising reproduces the original bytes.
TEST( CuckooCycle_challengeStreamRoundTrip )
{
	CuckooCycleLoginChallengeFactory factory;
	factory.easiness( 25.0 );

	LoginChallengePtr pServer = factory.create();
	CHECK( pServer.hasObject() );

	MemoryOStream challengeData;
	CHECK( pServer->writeChallengeToStream( challengeData ) );

	// packed-int length + prefix + 8 byte maximum nonce. The prefix is the
	// random challenge key rendered "%.16llx:" — up to 16 hex digits, zero
	// padded to at least two characters, plus a trailing colon.
	MemoryIStream probe( challengeData.data(), challengeData.size() );
	const int prefixLength = probe.readPackedInt();
	CHECK( (2 <= prefixLength) && (prefixLength <= 17) );
	CHECK_EQUAL( 1 + prefixLength + int( sizeof( uint64 ) ),
		challengeData.size() );

	LoginChallengePtr pClient = factory.create();
	MemoryIStream reader( challengeData.data(), challengeData.size() );
	CHECK( pClient->readChallengeFromStream( reader ) );
	CHECK_EQUAL( 0, int( reader.remainingLength() ) );

	MemoryOStream replay;
	CHECK( pClient->writeChallengeToStream( replay ) );
	CHECK_EQUAL( challengeData.size(), replay.size() );
	CHECK( memcmp( challengeData.data(), replay.data(),
		challengeData.size() ) == 0 );
}


// readResponseFromStream() rejects a truncated stream, a foreign key, a
// wrong-length proof and a proof that is not a valid cycle.
TEST( CuckooCycle_readResponseFailurePaths )
{
	CuckooCycleLoginChallengeFactory factory;
	factory.easiness( 25.0 );

	LoginChallengePtr pServer = factory.create();
	CHECK( pServer.hasObject() );

	BW::string prefix = challengePrefix( *pServer );
	// The prefix is the random challenge key rendered "%.16llx:" — up to 16
	// hex digits, zero padded to at least two characters, plus a colon.
	CHECK( (2 <= int( prefix.length() )) && (int( prefix.length() ) <= 17) );
	CHECK_EQUAL( ':', prefix[ prefix.length() - 1 ] );

	const int PROOF_BYTES = 42 * sizeof( uint32 );

	// No key at all.
	{
		MemoryIStream emptyReader( "", 0 );
		CHECK( !pServer->readResponseFromStream( emptyReader ) );
	}

	// A key belonging to a different challenge.
	{
		MemoryOStream response;
		BW::string foreignKey( "other-challenge:" );
		response << foreignKey;

		char proof[ PROOF_BYTES ];
		memset( proof, 0, sizeof( proof ) );
		response.addBlob( proof, PROOF_BYTES );

		MemoryIStream reader( response.data(), response.size() );
		CHECK( !pServer->readResponseFromStream( reader ) );
		// The response is rejected before its proof is read; consume the
		// tail so the stream does not warn on destruction.
		reader.finish();
	}

	// A valid key followed by the wrong amount of proof data.
	{
		MemoryOStream response;
		response << prefix;

		char shortProof[ 4 ];
		memset( shortProof, 0, sizeof( shortProof ) );
		response.addBlob( shortProof, sizeof( shortProof ) );

		MemoryIStream reader( response.data(), response.size() );
		CHECK( !pServer->readResponseFromStream( reader ) );
		reader.finish();
	}

	// A valid key and the right length, but a proof that fails verification
	// (all-zero nonces are not ascending).
	{
		MemoryOStream response;
		response << prefix;

		char proof[ PROOF_BYTES ];
		memset( proof, 0, sizeof( proof ) );
		response.addBlob( proof, PROOF_BYTES );

		MemoryIStream reader( response.data(), response.size() );
		CHECK( !pServer->readResponseFromStream( reader ) );
	}
}


// The full proof-of-work round trip: the client mines a 42-cycle for the
// server's challenge and the server verifies the mined response. 50% is the
// tuned operating point of the miner (the shipped default): with more edges
// the search paths overflow before a 42-cycle is found and each attempt is
// abandoned, so do not raise the easiness here to "make it faster".
TEST( CuckooCycle_responseRoundTrip )
{
	CuckooCycleLoginChallengeFactory factory;
	factory.easiness( 50.0 );

	LoginChallengePtr pServer = factory.create();
	CHECK( pServer.hasObject() );

	MemoryOStream challengeData;
	CHECK( pServer->writeChallengeToStream( challengeData ) );

	LoginChallengePtr pClient = factory.create();
	MemoryIStream challengeReader( challengeData.data(), challengeData.size() );
	CHECK( pClient->readChallengeFromStream( challengeReader ) );

	MemoryOStream responseData;
	CHECK( pClient->writeResponseToStream( responseData ) );

	MemoryIStream responseReader( responseData.data(), responseData.size() );
	CHECK( pServer->readResponseFromStream( responseReader ) );
	CHECK_EQUAL( 0, int( responseReader.remainingLength() ) );
}


BW_END_NAMESPACE

// test_login_challenge.cpp
