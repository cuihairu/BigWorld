#include "pch.hpp"

#include "cstdmf/memory_stream.hpp"

#include "connection/cuckoo_cycle_login_challenge_factory.hpp"
#include "connection/login_challenge.hpp"

#include "test_login_challenge_interfaces.hpp"

#include <ctype.h>
#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	The proof-of-work parameters the challenge bakes into a response: 42
 *	nonces of 4 bytes each.
 */
const size_t PROOF_NONCES = 42;
const int PROOF_NONCE_BYTES = 4;


/**
 *	Copies a stream's bytes, so that more than one input stream can be built
 *	over the same data - MemoryIStream only borrows its buffer, and it
 *	consumes what it reads.
 */
void copyOf( MemoryOStream & target, MemoryOStream & source )
{
	target.addBlob( source.data(), source.size() );
}


/**
 *	The random prefix of a challenge, as it appears on the wire: a packed
 *	length byte, 16 zero-padded hex digits, and a colon.
 */
const int PREFIX_WIRE_LENGTH = 17;


/**
 *	Whether two regions of n bytes hold the same data. Wrapped up like this
 *	so that the comma inside memcmp()'s argument list never reaches a test
 *	macro, which would take it for a separator.
 */
bool sameBytes( const char * lhs, const void * rhs, size_t n )
{
	return memcmp( lhs, rhs, n ) == 0;
}

} // end namespace (anonymous)


// The factory's easiness is clamped into 0..100 by its setter, and
// configure() accepts the same range while refusing everything else.
TEST( CuckooCycleLoginChallengeFactory_easiness )
{
	CuckooCycleLoginChallengeFactory factory;

	// The out-of-the-box value is half of the nonce space.
	CHECK_EQUAL( 50.0, factory.easiness() );

	factory.easiness( 33.5 );
	CHECK_EQUAL( 33.5, factory.easiness() );

	// Out of range values are clamped rather than rejected.
	factory.easiness( 200.0 );
	CHECK_EQUAL( 100.0, factory.easiness() );
	factory.easiness( -5.0 );
	CHECK_EQUAL( 0.0, factory.easiness() );

	// configure() takes the value as-is...
	TestChallengeConfig config;
	config.setDouble( "easiness", 75.0 );
	CHECK( factory.configure( config ) );
	CHECK_EQUAL( 75.0, factory.easiness() );

	// ... leaves it alone when the configuration has no such path...
	TestChallengeConfig empty;
	CHECK( factory.configure( empty ) );
	CHECK_EQUAL( 75.0, factory.easiness() );

	// ... and refuses a value outside (0, 100].
	TestChallengeConfig zero;
	zero.setDouble( "easiness", 0.0 );
	CHECK( !factory.configure( zero ) );
	CHECK_EQUAL( 75.0, factory.easiness() );

	TestChallengeConfig tooHard;
	tooHard.setDouble( "easiness", 100.5 );
	CHECK( !factory.configure( tooHard ) );
	CHECK_EQUAL( 75.0, factory.easiness() );
}


// A challenge is 17 bytes of random hex prefix plus the 64-bit nonce
// limit, and the peer picks both up by reading that stream.
TEST( CuckooCycleLoginChallenge_challengeStream )
{
	CuckooCycleLoginChallengeFactory factory;

	LoginChallengePtr pServer = factory.create();
	CHECK( pServer.hasObject() );

	MemoryOStream challenge;
	CHECK( pServer->writeChallengeToStream( challenge ) );
	// 1 length byte + 16 hex digits + ':' + 8 bytes of uint64.
	CHECK_EQUAL( 1 + PREFIX_WIRE_LENGTH + 8, challenge.size() );

	// The prefix is 16 zero-padded hex digits followed by a colon.
	const char * pBytes = static_cast< const char * >( challenge.data() );
	CHECK_EQUAL( PREFIX_WIRE_LENGTH, int( pBytes[ 0 ] ) );
	CHECK_EQUAL( ':', pBytes[ PREFIX_WIRE_LENGTH ] );
	for (int i = 1; i <= 16; ++i)
	{
		CHECK( isxdigit( pBytes[ i ] ) );
	}

	// A second challenge adopts both values by reading the stream, losing
	// the prefix it generated for itself.
	LoginChallengePtr pClient = factory.create();
	CHECK( pClient.hasObject() );

	MemoryOStream challengeBytesCopy;
	copyOf( challengeBytesCopy, challenge );
	MemoryIStream challengeIn( challengeBytesCopy.data(),
		challengeBytesCopy.size() );
	CHECK( pClient->readChallengeFromStream( challengeIn ) );
	CHECK_EQUAL( 0, challengeIn.remainingLength() );

	MemoryOStream echo;
	CHECK( pClient->writeChallengeToStream( echo ) );
	CHECK_EQUAL( challenge.size(), echo.size() );
	CHECK( sameBytes( static_cast< const char * >( challenge.data() ),
		echo.data(), challenge.size() ) );

	// A truncated challenge cannot be exercised here: reading past the
	// end of a MemoryIStream is a dev assertion that aborts the process
	// (memory_stream.ipp retrieve()), and the stream's error flag - the
	// only thing readChallengeFromStream() reports on - is set after
	// that assert in release builds. Malformed-input coverage therefore
	// stops at full-length streams; see the batch notes in
	// docs/python-313-migration.md.
}


// The proof-of-work response carries a key - the challenge prefix plus the
// attempt number - followed by 42 nonces. Only the challenge that issued
// the work, and was told about it, can verify the result.
TEST( CuckooCycleLoginChallenge_responseVerifies )
{
	CuckooCycleLoginChallengeFactory factory;

	LoginChallengePtr pServer = factory.create();
	LoginChallengePtr pClient = factory.create();
	CHECK( pServer.hasObject() );
	CHECK( pClient.hasObject() );

	// The server tells the client what to do...
	MemoryOStream challenge;
	CHECK( pServer->writeChallengeToStream( challenge ) );
	MemoryOStream challengeBytesCopy;
	copyOf( challengeBytesCopy, challenge );
	MemoryIStream challengeIn( challengeBytesCopy.data(),
		challengeBytesCopy.size() );
	CHECK( pClient->readChallengeFromStream( challengeIn ) );

	// ... and the client does it, which is the expensive part.
	MemoryOStream response;
	CHECK( pClient->writeResponseToStream( response ) );

	// The key is the challenge prefix plus the attempt number, so it is at
	// least as long as the prefix and is followed by the 42 nonces.
	const char * pBytes = static_cast< const char * >( response.data() );
	const int keyLength = int( pBytes[ 0 ] );
	CHECK( keyLength >= 1 + PREFIX_WIRE_LENGTH );
	CHECK_EQUAL( 1 + keyLength + int( PROOF_NONCES * PROOF_NONCE_BYTES ),
		int( response.size() ) );
	CHECK( sameBytes( static_cast< const char * >( challenge.data() ) + 1,
		pBytes + 1, PREFIX_WIRE_LENGTH ) );

	// A challenge that was given different work cannot verify this.
	MemoryOStream responseBytesCopy;
	copyOf( responseBytesCopy, response );
	LoginChallengePtr pOther = factory.create();
	MemoryIStream otherIn( responseBytesCopy.data(),
		responseBytesCopy.size() );
	CHECK( !pOther->readResponseFromStream( otherIn ) );

	// The issuing challenge can.
	MemoryOStream responseBytes2Copy;
	copyOf( responseBytes2Copy, response );
	MemoryIStream serverIn( responseBytes2Copy.data(),
		responseBytes2Copy.size() );
	CHECK( pServer->readResponseFromStream( serverIn ) );
	CHECK_EQUAL( 0, serverIn.remainingLength() );

	// A response with one nonce altered does not verify.
	MemoryOStream tampered;
	tampered.addBlob( response.data(), response.size() - 1 );
	MemoryOStream tail;
	int lastNonce = 0;
	tail << lastNonce;
	tampered.addBlob( tail.data(), tail.size() );

	MemoryOStream tamperedBytesCopy;
	copyOf( tamperedBytesCopy, tampered );
	MemoryIStream tamperedIn( tamperedBytesCopy.data(),
		tamperedBytesCopy.size() );
	CHECK( !pServer->readResponseFromStream( tamperedIn ) );
}

BW_END_NAMESPACE

// test_cuckoo_cycle_login_challenge.cpp
